# cyd-todo

A quest log and a calendar for the ESP32 "Cheap Yellow Display" (320x240, resistive touch), with a small server at home. Tasks are **quests** (finish one, get XP, level up); the calendar holds **events** with a start, an end and a description. Built on [cydui](https://github.com/gventino/cydui), the immediate-mode UI library for that screen, as the real application that proves its API before 1.0.0.

Nothing is typed on the display:
- **Wi-Fi** is set up by scanning a QR code: the board opens its own access point, the phone joins it and fills a page (network, password, server, token). If the board is already connected when it restarts, no QR appears.
- **Quests and events** are written on the phone: a button on the display shows a QR code with a one-time link to a page of the server, and the board shows the new item seconds after you save.

> **Status: development only.** The board talks to the server over **plain HTTP**. Do not put real data in it and do not expose the server outside your LAN until the TLS step (A7 in the plan) is done. See [`docs/threat.md`](docs/threat.md).

## Layout
| Path | What |
|---|---|
| `server/` | Rust: `poem`, `sqlx`, SQLite. The device API and the phone pages. |
| `firmware/` | ESP-IDF 5.5 application, and a PC build (SDL) for development and tests. `third_party/cydui` is the library, pinned to a release tag. |
| `docs/api.md` | The contract between the board, the phone and the server, with its limits. |
| `docs/design.md` | Screens, data, setup flow, sync, memory. |
| `docs/threat.md` | A short threat model and the state of each control. |
| `docs/findings.md` | What the library got wrong or made hard (feeds `cydui` 1.0.0). |

## Getting the code
```sh
git clone --recurse-submodules https://github.com/gventino/cyd-todo.git
# or, in an existing clone:  git submodule update --init
```

## Status of the plan
A0 repository and contract (done) - A1 server device API (done, see `server/README.md`) - A2 server phone pages (done) - A3 model and UI on the PC - A4 Wi-Fi setup by QR - A5 network and sync - A6 polish and hardening - A7 TLS.

## License
MIT, see `LICENSE`. The serif font that will be bundled is under the SIL OFL (its license will be in `firmware/fonts/`).
