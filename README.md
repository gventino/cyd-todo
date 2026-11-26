# cyd-todo

A quest log and a calendar for the ESP32 "Cheap Yellow Display" (320x240, resistive touch), with a small server at home. Tasks are **quests** (finish one, get XP, level up); the calendar holds **events** with a start, an end and a description. Built on [cydui](https://github.com/gventino/cydui), the immediate-mode UI library for that screen, as the real application that proves its API before 1.0.0.

Nothing is typed on the display:
- **Wi-Fi** is set up by scanning a QR code: the board opens its own access point, the phone joins it and fills a page (network, password, server, token). If the board is already connected when it restarts, no QR appears.
- **Quests and events** are written on the phone: a button on the display shows a QR code with a one-time link to a page of the server, and the board shows the new item seconds after you save.

> **Status: development only.** The board talks to the server over **plain HTTP**. Do not put real data in it and do not expose the server outside your LAN until the TLS step (A7 in the plan) is done. See [`docs/threat.md`](docs/threat.md).

## Screens
The real 320 x 240 screens, rendered by the PC program with the demo data (`firmware/tools/shots.sh`). Day palette, then night (candlelight).

| Quests | Calendar | Agenda of a day | Settings |
|---|---|---|---|
| ![](docs/img/quests-day.png) | ![](docs/img/calendar-day.png) | ![](docs/img/agenda-day.png) | ![](docs/img/settings-day.png) |

| Quest details | New quest (QR) | Wi-Fi setup (QR) | Quests, night |
|---|---|---|---|
| ![](docs/img/detail-night.png) | ![](docs/img/qr-quest-night.png) | ![](docs/img/setup-night.png) | ![](docs/img/quests-night.png) |

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

## Run it
Development setup: a server on a computer of your LAN, the board on its USB cable.

1. **Server** (details in `server/README.md`):
   ```sh
   cd server && cargo build --release
   export CYD_TOKEN="$(head -c 32 /dev/urandom | base32 | tr -d =)"   # at least 32 characters
   export PUBLIC_URL=http://192.168.1.50:8080                         # the address a phone and the board can reach
   export TZ_OFFSET_MIN=-180                                          # the zone of the phone forms
   ./target/release/cyd-todo-server
   ```
2. **Firewall.** If the computer has one, the board must be allowed to reach port 8080, for example `sudo ufw allow from 192.168.1.0/24 to any port 8080 proto tcp`; take the rule away when you are done (`sudo ufw delete allow from 192.168.1.0/24 to any port 8080 proto tcp`).
3. **Board** (ESP-IDF 5.5, `source firmware/third_party/cydui/tools/env.sh`): to skip the Wi-Fi page while developing, put the server and the token in `firmware/sdkconfig.local` (git-ignored, never committed):
   ```
   CONFIG_TODO_DEV_SERVER_ADDRESS="http://192.168.1.50:8080"
   CONFIG_TODO_DEV_TOKEN="the same CYD_TOKEN"
   ```
   then `cd firmware && rm -f sdkconfig && idf.py -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.local" build flash`. Without that file the board shows a Wi-Fi QR on first boot: scan it with a phone and fill the page.
4. **Use it.** The *New quest* and *New event* buttons show a QR with a one-time link; scan it, write, save, and the board shows the item seconds later. `cyd-todo-server link quest` prints such a link by hand.
5. **Check it.** `./ci.sh` (a local script, not in git: there is no hosted CI) builds and tests everything; `CYD_TOKEN=... firmware/tools/accept_db.py snapshot FILE`, then the session, then `... diff FILE`, judge an acceptance session by the server's database and log, not by anyone's description.

## Status of the plan
A0 repository and contract (done) - A1 server device API (done, see `server/README.md`) - A2 server phone pages (done) - A3 model and UI on the PC (done: model, repository, theme, QR, every screen, scripted runs and about 90 golden pictures) - A4 Wi-Fi setup by QR (done and accepted on the board with a phone: the same UI as the PC, 37 of 37 scene hashes, access point, captive portal, no QR once connected) - A5 network and sync (the board syncs with the server: SNTP time, lists, polling, every injected fault survived; accepted with a phone, judged by the server's database) - A6 polish and hardening (done: the settings survive a reboot, the backlight dims to 10 % after its timeout and a touch wakes it, cards and toasts and level-ups move gently; **A6.4, the whole-app fuzz and a long soak, was skipped on purpose**, see `docs/design.md`) - A7 TLS.

## License
MIT, see `LICENSE`. Third party: the QR encoder of Project Nayuki (MIT, `firmware/components/qr/vendor/`) and Liberation Serif (SIL OFL). The serif font that will be bundled is under the SIL OFL (its license is in `firmware/components/theme_quest/fonts/`).

## Firmware, PC build
Fonts are generated, and checked by `ci.sh`, with a venv: `python3 -m venv firmware/tools/.venv && firmware/tools/.venv/bin/pip install -r firmware/third_party/cydui/tools/requirements.txt`, then `firmware/tools/gen_fonts.sh [--check]`.

The PC program renders any screen without a window (`firmware/pc/build/todo_pc --list`, `--scene agenda --palette night --png out.png`); built with `-DTODO_SDL=ON` it opens a window where the mouse is the touch screen.
