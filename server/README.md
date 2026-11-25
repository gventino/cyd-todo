# cyd-todo server

The server of cyd-todo: the **device API** the board talks to and (from step A2) the **phone pages**. Rust, [`poem`](https://github.com/poem-web/poem), [`sqlx`](https://github.com/launchbadge/sqlx) with SQLite. The contract is [`../docs/api.md`](../docs/api.md); this page is how to run it.

> **Development only: plain HTTP.** Do not put real data in it and do not expose it outside your LAN until TLS (step A7) is done. See [`../docs/threat.md`](../docs/threat.md).

## Build and run
```sh
cargo build --release
export CYD_TOKEN="$(head -c 32 /dev/urandom | base32 | tr -d =)"   # at least 32 characters; keep it out of git
export PUBLIC_URL=http://192.168.1.50:8080                         # what a phone must use to reach this server
./target/release/cyd-todo-server serve                              # or just: cyd-todo-server
./target/release/cyd-todo-server seed                               # demo quests and events (refuses a database with rows; --force to add anyway)
```

| Variable | Meaning | Default |
|---|---|---|
| `CYD_TOKEN` | the device token, at least 32 characters, no spaces | required |
| `PUBLIC_URL` | `http://host:port` without a trailing slash, at most 32 bytes (it ends up in a QR code) | required |
| `BIND` | listen address | `0.0.0.0:8080` |
| `DATABASE_URL` | SQLite file | `sqlite://cyd-todo.db` |
| `TZ_OFFSET_MIN` | zone for the phone forms (A2), minutes east of UTC | `0` |
| `FAULT`, `ALLOW_FAULTS` | fault injection for firmware tests (A5); `FAULT` is refused without `ALLOW_FAULTS=1` | off |

A wrong configuration stops the server at start with a message that names the variable and never prints the token. The migrations run at start. Ctrl-C and SIGTERM stop it cleanly.

## Check it
```sh
cargo fmt --check && cargo clippy --all-targets -- -D warnings && cargo test   # the unit and integration tests
./scripts/audit.sh                                                              # source audits (no unsafe, no built SQL, what may be logged)
CYD_TOKEN=... BASE=http://127.0.0.1:8080 ./scripts/smoke.sh                     # every call of the API against a running server
```
The tests run on an in-memory database and cover every limit of the contract on both sides of its boundary, the error shapes (and that no answer contains what the client sent), the access log (no token, query or body), the worst-case list bodies, and `Content-Length` on a real socket.

## On the homelab
- **systemd:** `cyd-todo.service` (hardened: no new privileges, read-only system, one writable directory). The variables go in `/etc/cyd-todo.env` (mode 0600), for example:
  ```
  CYD_TOKEN=...
  PUBLIC_URL=http://192.168.1.50:8080
  DATABASE_URL=sqlite:///var/lib/cyd-todo/cyd-todo.db
  ```
- **Docker:** `Dockerfile` (a multi-stage build, runs as an unprivileged user, the database in the `/data` volume).
- **Backup:** the whole state is one SQLite file (plus its `-wal` while running). Copy it with the server stopped, or run `sqlite3 cyd-todo.db ".backup backup.db"` while it runs.
- Logs go to stdout/journald: one line per request (`GET /quests 200 3ms`), the start and stop lines, and database errors. Never a token, a query string, a header or a body.
