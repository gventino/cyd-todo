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

## P1 - Setup mode on the board (access point, DNS, tiny HTTP server) - built in A4, with the evidence
| STRIDE | Threat | What was built | Evidence |
|---|---|---|---|
| S | Someone joins the setup access point first and submits their own server address. | WPA2 with a **12-character random password (60 bits, an alphabet without look-alikes) that exists only in the QR on the board's own screen**: it is never printed as text, never logged (the log says "setup open", not what it is); one station at a time; a 10-minute window, after which the access point and the server are off and a late post is refused; the form carries a per-setup random field that a post must return. Someone who can see the QR is already in the room. | `test_prov_misc.c` (alphabet, uniformity: 256 bytes onto 32 characters, every byte feeds the password), `test_prov_form.c` (session field), `bootflow` table (a late post is not honored); the serial logs of the self-test, soak and acceptance builds contain no password |
| T | A hostile form (long fields, odd bytes, a huge POST) corrupts memory or hangs the board. | `prov/form.h`: a body cap of **512 bytes** (checked against `Content-Length` before anything is read), exact field names (no repeats, no strangers), percent-decoding that refuses `%00` and bad escapes, a length and character rule for every field; a refused form changes nothing. HTTP server timeouts of 5 s per read and write, at most 4 sockets, the least recently used is dropped, request headers up to 2048 bytes (512 was too little for a real phone's browser). | 250,000 mutated forms and 60,000 random bodies under the sanitizers (`test_prov_fuzz.c`), the edge of every limit, 20 mutants of the validators killed. **Not exercised against the real access point with an attack tool** (the owner chose not to take the notebook off its Wi-Fi): on the board only the owner's phone talked to it (A4.4: the form, a normal post, no crash, 82 KB of heap at the lowest) |
| T | A scanned network name (it comes from the air) breaks the HTML. | Every echoed value is escaped (`& < > " '`) and the pages have **no script**; the portal's CSP is `default-src 'none'; form-action 'self'; base-uri 'none'; frame-ancestors 'none'`. | `test_prov_form.c` (hostile SSIDs and reasons), 4,000 random pages in `test_prov_fuzz.c` |
| T | A DNS packet that is malformed or hostile. | `prov/dnsans.h`: one question, no compression, labels of at most 63 bytes, 254 bytes of name, 512 bytes of packet; anything else is dropped; the answer never exceeds the buffer. | byte-exact answers and 300,000 random and mutated packets under the sanitizers |
| I | Eavesdropping on the setup page (it is HTTP): the Wi-Fi password and the token cross the air once. | The access point is WPA2, so the link is encrypted for everyone who does not know the QR; accepted for a 10-minute window. A7 does not change this (a captive portal cannot have a valid certificate): the answer is the short window and the random password. | by design |
| I | The credentials leak through a log or the display. | The log carries lengths and states only; the form's password and token fields are `type=password`; after a post the buffers are wiped; what is read back from the flash is validated like input. | `ci.sh` greps the firmware sources for a log line that prints a secret and for a development token in a tracked config |
| D | Holding the setup mode open, or a loop of wrong submissions, keeps the board from working. | The window closes by itself; a failed join goes back to the setup page with its reason; setup mode is entered at boot without working credentials or from Settings behind a confirmation. | `bootflow` table (every transition, 200,000 random events keep the invariants) |
| E | A reflash over USB (physical access) reads the NVS. | Out of scope without secure boot and flash encryption; A7 puts the token and the password in an encrypted NVS partition for a release build (CT-18). | by design |

## P2 - Device API (board to server)
| STRIDE | Threat | What the design does |
|---|---|---|
| S | Someone on the LAN calls the API, or answers the board in the server's place. | Bearer token on every call (constant-time compare, at least 32 characters, never in a log or URL). **Plain HTTP means the token and every answer are readable and forgeable on the LAN: dev-only gap, closed by A7 (HTTPS with the server certificate pinned in the firmware).** |
| T | A tampered answer (too long, truncated, bad JSON, strings over the limit) reaches the model. | **Built (A5):** a response with no `Content-Length`, over 16 KB, longer or shorter than it promised, or not JSON of the contract's shape, is refused **whole** (nothing of it reaches the cache); a streaming, non-allocating JSON reader (depth 6, key 24, string 256 bytes, 16 KB in all, strict UTF-8 and escapes); every field is range-checked and copied with its limit (CT-02); lists are parsed into a staging block that is swapped in only when complete. | `test_jsonr.c` (strict-JSON matrix, the same events for any cut of the input, 300,000 mutated documents under the sanitizers), `test_netrepo.c` (60 broken-contract cases, every limit at its edge, 22 mutants killed), `test_netrepo_server.c` (the real server and **all eight injected faults**), and the board run against each fault for 75 s: no crash, heap flat |
| R | No audit trail. | Accepted; a home device. The server logs method, path and status (no bodies, no token). |
| I | A quest title in a log line, a token in a URL. | Logs carry no user text (the library's CT-20 rule, applied here). |
| D | A slow or silent server blocks the UI; a flood of polls. | **Built (A5):** one request at a time in the network task, 15 s for the whole exchange (8 s per wait), after a failure the next requests are answered at once and the network is tried again after 1, 2, 4, 8, then 15 s; the UI task never waits for the network; the server limits bodies (4 KB) and links (6 active). | `test_netrepo.c` (back-off, doubling, cap, clock wrap), the `slow` fault on the PC and on the board |
| E | A bug in the server's parser. | Rust, no `unsafe`, `sqlx` with bound parameters only (no string-built SQL). |

## P3 - Phone pages (the server's web face) - built in A2, with the evidence
| STRIDE | Threat | What was built | Evidence |
|---|---|---|---|
| S | Guessing, stealing or reusing a one-time link. | 128 random bits from the OS, stored only as a SHA-256; single use by one atomic `UPDATE ... RETURNING`; 10 minutes; at most 6 alive; every failure is the same 404 page after the same 200 ms; the code never reaches the log (`/m/:code`) or any page. **HTTP: anyone sniffing the LAN while the link is used can take the session; dev-only gap, A7.** | `tests/links.rs` (lifecycle, identical failures, expiry to the second, the cap, log redaction, only hashes in the database) |
| S | A session that outlives its use. | 30 minutes from creation (not sliding), cookie `HttpOnly; SameSite=Lax; Path=/` (+ `Secure` with https), ended by *Leave*; a phone session is not a device token and the reverse. | `tests/links.rs` **`Lax`, not `Strict` (found with a real phone, A6.0):** a link opened from a QR scanner is a cross-site navigation, and a browser withholds a `Strict` cookie from the redirect that follows the response that set it, so the first page answered 401 and the phone said "link expired". `Lax` is sent only on top-level GETs, which never write, and every POST still needs the CSRF token and the Origin. |
| T | Cross-site request forgery. | Every state change is a POST with the session's CSRF token (constant time) **and** an `Origin` that must be ours when present, on top of `SameSite=Strict`; a GET never writes; delete is two steps. | `tests/links.rs`, `pages_quests.rs`, `pages_events.rs` (every POST of every page, with a wrong token, a foreign Origin and no session) |
| T | **Stored cross-site scripting.** | Markup is built only through `html::Html`: text and attribute values are escaped by construction, literals are `'static`; the audit forbids markup in `format!`, raw pushes and `Html(..)` in `pages/`. **No script exists on any page** and the CSP allows none (`default-src 'none'`, the one stylesheet by hash), so a missed escape still cannot run code. | `src/html.rs` tests (5000 random strings), `tests/pages_security.rs`: 22 hostile strings in every field and context (device API and forms, accepted and refused), **every page that can show them**, checked against an allowlist of tags, attributes and link targets (the checker is itself tested on bad pages); mutations that drop the escaping fail 2 to 3 tests |
| I | Leaking data to other sites or into history. | `Referrer-Policy: no-referrer`, no third-party resources, no cookies but the session, `Cache-Control: no-store`; nothing from the URL or the headers is echoed into a page. | `tests/pages_security.rs` (reflection test) |
| D | Huge forms, floods, a table that fills. | Form body at most 4096 bytes, at most 6 links alive and expired rows swept, the same 48-item caps; one validation function for both faces. | `tests/links.rs`, `pages_*.rs` |
| E | A page that can do more than the API. | It cannot: the same validators and database functions. | the tests above |

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
- **A typed network is saved before it is tried.** If it does not work the board returns to the setup page and the old network is gone from the flash; the user types it again. Keeping the old set until the new one joins needs a second NVS slot; judged not worth it for a one-person device (a failed join is told with its reason).
Physical access to the board (flash readable without flash encryption); anyone on the LAN while the transport is plain HTTP (until A7); look-alike letters in titles (the library only filters control and bidi characters, and the fonts here are Latin-1); the ESP-IDF Wi-Fi stack.
