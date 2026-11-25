# Threat model (short STRIDE)

The setting is `cydui`'s own (docs 04 to 06 of the library): a home device on a LAN that guests can join, on a desk other people can touch. This page covers what this application adds: the board's setup access point, HTTP to the server, the server, and the phone pages. The library's own threats (memory safety, UTF-8, input floods) are in the library's docs and tests.

> **Status: development only.** The transport is plain HTTP on purpose, to get the application working first. **Do not put real data in it and do not expose the server outside the LAN until step A7 (TLS) is done.** This is a known, accepted gap of the development phase, not a decision about the final product.

## Assets
A1 the quests and events (private life); A2 the device token and the Wi-Fi password; A3 the integrity of what the screen shows (a forged quest or event); A4 the board and the server staying up.

## Boundaries
```
 [phone] --Wi-Fi AP--> (P1 board: setup page) --NVS--> [flash]
 [board] --LAN HTTP--> (P2 server: device API) --> [SQLite]
 [phone] --LAN HTTP--> (P3 server: phone pages) --> [SQLite]
 [anyone on the LAN] -----> P2, P3 (they can be reached, so they must refuse)
```

## P1 - Setup mode on the board (access point, DNS, tiny HTTP server)
| STRIDE | Threat | What the design does |
|---|---|---|
| S | Someone joins the setup access point first and submits their own server address. | The access point has a random WPA2 password that is only in the QR on the board's own screen; one client at a time; the window is 10 minutes; the board shows the address it was given on screen before it is used for the first call. Someone who can see the QR is already in the room. |
| T | A hostile form (long fields, odd bytes, a huge POST) corrupts memory or hangs the board. | Body cap (1 KB), every field has a length and a character rule, parsed by code that is fuzzed on the PC (A4); anything else is a 400. The HTTP server runs only in setup mode. |
| I | Eavesdropping on the setup page (it is HTTP): the Wi-Fi password and the token cross the air once. | The access point is WPA2, so the link is encrypted for everyone who does not know the QR; accepted for a 10-minute window. A7 does not change this (a captive portal cannot have a valid certificate), the answer is the short window and the random password. |
| D | Holding the setup mode open, or a loop of wrong submissions, keeps the board from working. | The window closes by itself; the previous credentials are kept until a new set joins successfully; setup mode is only entered at boot without working credentials or from Settings with a confirmation. |
| E | A reflash over USB (physical access) reads the NVS. | Out of scope without secure boot and flash encryption; A7 puts the token and the password in an encrypted NVS partition for a release build (CT-18). |

## P2 - Device API (board to server)
| STRIDE | Threat | What the design does |
|---|---|---|
| S | Someone on the LAN calls the API, or answers the board in the server's place. | Bearer token on every call (constant-time compare, at least 32 characters, never in a log or URL). **Plain HTTP means the token and every answer are readable and forgeable on the LAN: dev-only gap, closed by A7 (HTTPS with the server certificate pinned in the firmware).** |
| T | A tampered answer (too long, truncated, bad JSON, strings over the limit) reaches the model. | The firmware reads at most 16 KB and gives up the connection on more; a non-allocating JSON reader with a depth limit and per-string caps; every field is copied with its limit (CT-02); the reader is fuzzed (A5). The fault-injection mode of the server proves it. |
| R | No audit trail. | Accepted; a home device. The server logs method, path and status (no bodies, no token). |
| I | A quest title in a log line, a token in a URL. | Logs carry no user text (the library's CT-20 rule, applied here). |
| D | A slow or silent server blocks the UI; a flood of polls. | One request at a time in the network task, a 15 s timeout, backoff on failure, the UI task never waits for the network; the server limits bodies (4 KB) and links (6 active). |
| E | A bug in the server's parser. | Rust, no `unsafe`, `sqlx` with bound parameters only (no string-built SQL). |

## P3 - Phone pages (the server's web face)
| STRIDE | Threat | What the design does |
|---|---|---|
| S | Guessing or stealing a one-time link; reusing it. | 128 random bits, stored only as a hash, single use, 10 minutes, at most 6 alive; a used or expired link is a plain 404 that looks like any other; the link travels only in the QR and the phone's address bar (**HTTP: anyone sniffing the LAN while it is used can take the session; dev-only gap, A7**). |
| T | Cross-site request forgery makes the phone delete quests. | `SameSite=Strict` session cookie, a CSRF token bound to the session in every form, state changes only by POST. |
| T | **Stored cross-site scripting**: a quest title like `<script>` shown on a page, or in the device JSON. | Everything is HTML-escaped on output (one escaping function, tested with hostile strings in every field and context), `Content-Security-Policy` that allows no script except one nonce-ed inline script of four lines, `X-Content-Type-Options`, `frame-ancestors 'none'`. The device receives JSON strings, never HTML, and draws them as text. |
| I | The page leaking data to other sites. | `Referrer-Policy: no-referrer`, no third-party resources, no cookies beyond the session. |
| D | Huge forms, floods. | Same body limit (4 KB) and validation as the device API; one function validates for both faces. |
| E | A page that can do more than the API. | It cannot: it calls the same functions with the same rules. |

## P4 - Server and database
Runs as an unprivileged user, a single SQLite file, migrations in code, backups are the owner's job (copy the file). The token and `PUBLIC_URL` come from the environment, never from a file in the repository (`.env` is ignored by git).

## Controls of the library's list (CT-16 to CT-22), as they stand
| Control | State |
|---|---|
| CT-16 HTTPS with a pinned certificate, never a token over plain HTTP | **Dev-only gap.** HTTP now; A7 closes it. |
| CT-17 Response limits, timeouts, a JSON parser with a depth limit, validation, copy with limits | **Server side done (A1):** body, string, list and 14 KB response caps built while serializing, rules in one place, tests on both sides of every limit, worst-case bodies, `Content-Length` on a real socket. Device side at A5 (fuzzed). |
| CT-18 Secrets in an encrypted NVS | **Later** (A7): the token and Wi-Fi password are in a plain NVS during development. |
| CT-19 Secure Boot | Optional, release only; not planned for v0.1. |
| CT-20 Logs never print user text or tokens | **Server side done (A1):** the access log has method, a printable path, status and time only (tested with markers), errors never echo the client, `server/scripts/audit.sh` checks what may be logged. Firmware side at A5. |
| CT-21 Backlight timeout and a privacy mode | A6. |
| CT-22 Destructive actions need a confirmation | Done in the design: delete and Change Wi-Fi ask again (A3). |

## Accepted residual risks
Physical access to the board (flash readable without flash encryption); anyone on the LAN while the transport is plain HTTP (until A7); look-alike letters in titles (the library only filters control and bidi characters, and the fonts here are Latin-1); the ESP-IDF Wi-Fi stack.
