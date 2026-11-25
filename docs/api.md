# API contract

The server has two faces: a **device API** (JSON, for the CYD) and **phone pages** (HTML, for a person). They share one SQLite database. This page is the contract; the limits in it are a contract with a device that has about 290 KB of free RAM and no PSRAM, and the server enforces every one of them (and the tests prove it).

> **Development only: plain HTTP.** Until step A7 (TLS) nothing here is meant for real data or for anything outside the LAN. See `threat.md`.

## Conventions
- Base URL: the server's `PUBLIC_URL`, for example `http://192.168.1.50:8080` (at most 32 characters: it ends up in a QR code).
- JSON in and out, UTF-8, `Content-Type: application/json`. Every response is small and has a `Content-Length` (no chunked replies: the board refuses to read what it cannot bound).
- Times are **UTC epoch seconds** (integers). Calendar dates without a time (a quest's due date) are the string `"YYYY-MM-DD"` and mean that day in the user's own zone: the server never interprets them.
- Ids are positive integers. A path id that is not one (`/quests/abc`, `0`, too big) names nothing and is a 404.
- `PATCH` tells an absent member from `null`: `"due": null` clears the due date; `null` for any other member is a 400, and a `PATCH` that changes nothing (`{}`) is a 400. A response of `204` has no body and no `Content-Length`.
- Text: valid UTF-8, no control characters except `\n` in notes and descriptions, lengths are **bytes**. The server rejects instead of truncating.
- Errors: a JSON body `{"error": "<code>", "message": "<for a human>"}` and one of the statuses below. A message never contains what the client sent.

| Status | `error` | When |
|---|---|---|
| 400 | `bad_request` | not JSON, wrong type, unknown field, a bad query |
| 401 | `unauthorized` | the Bearer token is missing or wrong (the same answer for both) |
| 404 | `not_found` | no such id, or a one-time link that does not exist, was used or expired |
| 405 | `method_not_allowed` | the path exists but not with that method |
| 413 | `too_large` | the body is over the limit |
| 422 | `invalid` | well-formed, but a rule is broken (a length, a range, `end <= start`) |
| 429 | `too_many` | too many active one-time links |
| 500 | `internal` | a bug; the body says nothing more |

## Limits (the contract with the board)
| What | Limit |
|---|---|
| Request body | 4 KB (4096 bytes) |
| Any response of the device API | 16 KB (16384 bytes); the server's own tests build the worst case and check it |
| Quest `title` / `notes` | 1..64 bytes / 0..256 bytes |
| Event `title` / `description` | 1..64 bytes / 0..256 bytes |
| Quest `difficulty` | 1..5 |
| Items in one list | 48, and the body stops growing at 14 KB: the server adds items one by one and stops at whichever limit comes first (`"truncated": true`) |
| Event window (`to - from`) | at most 62 days (5 356 800 s) |
| Event length (`end - start`) | at most 31 days |
| Epoch seconds accepted | 946684800 (2000-01-01) .. 4102444800 (2100-01-01) |
| Active one-time links | 6 |
| One-time link lifetime / phone session lifetime | 10 minutes / 30 minutes |
| `PUBLIC_URL` | at most 32 bytes |

A typical quest is 150 to 250 bytes of JSON, but the worst one (a title and notes made of characters that must be escaped, such as `\n` or `"`) is about 740, so 48 of them would not fit 16 KB. That is why the size cap is checked while the body is built, not assumed. The firmware has fixed arrays for 48 items (`firmware/components/model`) and a 16 KB response buffer, so nothing the server sends can need more.

## Authentication
`Authorization: Bearer <token>` on every device call except `GET /health`. The token is `CYD_TOKEN` in the server's environment, at least 32 characters, compared in constant time. It never appears in a URL, a log line or an error. A wrong token costs the caller a fixed delay of 200 ms (not a lockout: the device must keep working).

## Device API

### Quests
A quest: `{"id": 7, "title": "Slay the inbox", "notes": "", "difficulty": 3, "due": "2026-12-01", "state": "open", "done_at": null, "updated_at": 1795600000}`. `due` and `done_at` may be `null`. `state` is `"open"` or `"done"`.

- `GET /quests` -> `{"rev": 41, "xp": 270, "open": 5, "done": 12, "truncated": false, "quests": [ ... ]}`: at most 48 quests (fewer if the 14 KB cap comes first), **open ones first** (by due date, then by id), then done ones, most recently done first. `xp` is the all-time total (the sum of `difficulty * 10` of every done quest), `open` and `done` are all-time counts; the device derives its level from `xp` (`design.md`).
- `POST /quests` with `{"title", "notes"?, "difficulty"?, "due"?}` -> `201` and the quest. `difficulty` defaults to 1, `notes` to `""`, `due` to `null`.
- `GET /quests/{id}` -> the quest.
- `PATCH /quests/{id}` with any of `title`, `notes`, `difficulty`, `due`, `state` -> the quest. Setting `state` to `"done"` sets `done_at` to now; back to `"open"` clears it. Unknown fields are a 400.
- `DELETE /quests/{id}` -> `204`.

### Events
An event: `{"id": 3, "title": "Dentist", "description": "Bring the X-rays", "start": 1796230800, "end": 1796234400, "updated_at": 1795600000}`.

- `GET /events?from=<epoch>&to=<epoch>` -> `{"rev": 41, "truncated": false, "events": [ ... ]}`: the events that **overlap** `[from, to)`, ordered by `start` then `id`, at most 48 (fewer if the 14 KB cap comes first). If there are more, `truncated` is true and the earliest ones are returned. Both parameters are required.
- `POST /events` with `{"title", "start", "end", "description"?}` -> `201` and the event. `end` must be greater than `start`.
- `GET`, `PATCH` (any of `title`, `description`, `start`, `end`; the final pair is validated as a whole), `DELETE` on `/events/{id}` as for quests.

### Has anything changed?
`GET /changes?since=<rev>` -> `{"rev": 41, "changed": false}`. `rev` is a counter that goes up by one on every write, from either face. It is the cheap call the board makes every ~60 seconds, and every 3 seconds while a QR code is on its screen. `since` is optional (absent means "changed"). The answer is about 30 bytes.

### One-time links (for the phone pages)
`POST /link` with `{"kind": "quest"}`, `{"kind": "event"}` or `{"kind": "home"}` -> `201` and `{"url": "http://192.168.1.50:8080/m/K3H9...", "expires_in": 600}`.
- The code is 128 random bits written in Crockford base32 (26 characters), kept as a hash on the server.
- It works once, for 10 minutes. `kind` only chooses the page the phone lands on (the new-quest form, the new-event form, or the home page).
- The URL is at most 61 bytes (`PUBLIC_URL` <= 32, `/m/` and 26 characters), which fits a QR code of version 4 in byte mode with the medium error correction level (62 bytes).

### Health
`GET /health` -> `{"ok": true}`. No token. The only call that needs none.

## Phone pages (HTML)
Server-rendered, no JavaScript framework, a few lines of CSS, made for a phone in portrait. English only.

| Route | What |
|---|---|
| `GET /m/{code}` | Trades a valid one-time code for a session cookie (`HttpOnly`, `SameSite=Strict`, `Path=/`, 30 minutes) and redirects to the page of its `kind`. A code that is unknown, used or expired is a plain 404 page. |
| `GET /app` | Home: the open quests and the next events, with links. Needs a session. |
| `GET /app/quests/new`, `POST /app/quests` | The new-quest form (title, notes, difficulty, due date) and its submission. |
| `GET /app/quests/{id}/edit`, `POST /app/quests/{id}` | Edit. |
| `POST /app/quests/{id}/delete` | Delete (the page asks for a second tap). |
| `GET /app/events`, `GET /app/events/new`, `POST /app/events`, `GET /app/events/{id}/edit`, `POST /app/events/{id}`, `POST /app/events/{id}/delete` | The same for events; start and end use `<input type="datetime-local">` and are read in the **time zone the page declares** (a hidden field filled from the phone's own zone by a four-line script; without the script, the server's `TZ_OFFSET_MIN`). |

Every form is posted with a CSRF token (bound to the session) and answered with a redirect (post-redirect-get). A bad submission shows the form again with the message and what the person typed, **escaped**. Every response carries `Content-Security-Policy: default-src 'none'; style-src 'unsafe-inline'; form-action 'self'; base-uri 'none'; frame-ancestors 'none'`, plus `X-Content-Type-Options: nosniff` and `Referrer-Policy: no-referrer`. The four-line script is the one inline script and is allowed by a per-response nonce.

A phone page is a second client of the same rules: it uses the same validation and the same limits as the device API (one function, one set of tests).

## Server configuration (environment)
| Variable | Meaning |
|---|---|
| `CYD_TOKEN` | the device token, at least 32 characters (required) |
| `PUBLIC_URL` | what the phone must use to reach the server, at most 32 bytes (required) |
| `BIND` | listen address, default `0.0.0.0:8080` |
| `DATABASE_URL` | SQLite file, default `sqlite://cyd-todo.db` |
| `TZ_OFFSET_MIN` | zone for phone forms without the script, default 0 |
| `FAULT` | a fault-injection mode for tests of the firmware (`slow`, `truncate`, `huge`, `garbage`, `error500`); never set in real use, refused unless `ALLOW_FAULTS=1`. Parsed in A1, acted on in A5. |
