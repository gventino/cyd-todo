#!/usr/bin/env bash
# One entry point for trying cyd-todo on your own network: server, board and phone links.
#
#   scripts/dev.sh doctor      what this computer has and lacks
#   scripts/dev.sh setup       makes .env (a fresh token, your LAN address) and prints what to type on the phone form
#   scripts/dev.sh server      builds and runs the server with that .env
#   scripts/dev.sh link [kind] a one-time link for the phone (quest | event | home), without the board
#   scripts/dev.sh board       builds and flashes the firmware (ESP-IDF 5.5; add PORT=/dev/ttyUSB0 if needed)
#   scripts/dev.sh bake        writes firmware/sdkconfig.local so that a reflash keeps this server and token
#   scripts/dev.sh firewall    prints the firewall rule the board needs (it does not run it)
#
# Development only: plain HTTP, on your LAN. See docs/threat.md.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
env_file="${CYD_ENV_FILE:-$root/.env}"            # git-ignored: it holds the token
local_cfg="${CYD_SDKCONFIG_LOCAL:-$root/firmware/sdkconfig.local}"

lan_ip() {
    ip route get 1.1.1.1 2>/dev/null | awk '{for (i = 1; i <= NF; i++) if ($i == "src") { print $(i + 1); exit }}'
}

tz_minutes() {
    local z; z="$(date +%z)"   # +0200 / -0300
    local sign=1; [ "${z:0:1}" = "-" ] && sign=-1
    echo $(( sign * (10#${z:1:2} * 60 + 10#${z:3:2}) ))
}

need_env() {
    [ -f "$env_file" ] || { echo "dev.sh: no $env_file yet: run  scripts/dev.sh setup" >&2; exit 1; }
    # shellcheck disable=SC1090
    set -a; . "$env_file"; set +a
}

cmd_doctor() {
    local bad=0
    for t in cargo gcc cmake ninja; do
        if command -v "$t" >/dev/null 2>&1; then echo "  [ok]   $t"; else echo "  [MISS] $t"; bad=1; fi
    done
    if [ -f "${CYDUI_IDF_PATH:-$HOME/esp/esp-idf}/export.sh" ]; then echo "  [ok]   ESP-IDF"; else echo "  [MISS] ESP-IDF 5.5 (only to build the firmware)"; bad=1; fi
    if [ -f "$root/firmware/third_party/cydui/CMakeLists.txt" ]; then echo "  [ok]   the cydui submodule"; else echo "  [MISS] the library: git submodule update --init"; bad=1; fi
    [ -n "$(lan_ip)" ] && echo "  [ok]   LAN address $(lan_ip)" || { echo "  [MISS] a LAN address (are you on the network?)"; bad=1; }
    exit "$bad"
}

cmd_setup() {
    if [ -f "$env_file" ] && [ "${1:-}" != "--force" ]; then
        echo "dev.sh: $env_file exists (use --force to make a new token); keeping it."
    else
        local ip; ip="$(lan_ip)"
        [ -n "$ip" ] || { echo "dev.sh: could not find the LAN address; write $env_file by hand" >&2; exit 1; }
        umask 077
        {
            echo "CYD_TOKEN=$(head -c 32 /dev/urandom | base32 | tr -d =)"
            echo "PUBLIC_URL=http://$ip:8080"
            echo "TZ_OFFSET_MIN=$(tz_minutes)"
            echo "DATABASE_URL=sqlite://$root/server/cyd-todo.db?mode=rwc"
        } > "$env_file"
        echo "Wrote $env_file (mode 600, git-ignored)."
    fi
    need_env
    cat <<MSG

On the phone form (the board shows a Wi-Fi QR code at its first start; scan it, join the board's network, fill the page):
  Server: ${PUBLIC_URL#http://}
  Token:  $CYD_TOKEN

Next:  scripts/dev.sh server      (in one terminal)
       scripts/dev.sh board       (in another)
If the board cannot reach the server, your firewall is the usual reason:  scripts/dev.sh firewall
MSG
}

cmd_server() {
    need_env
    ( cd "$root/server" && cargo build --release )
    echo "Serving $PUBLIC_URL (Ctrl-C stops it)"
    cd "$root/server" && exec ./target/release/cyd-todo-server serve
}

cmd_link() {
    need_env
    ( cd "$root/server" && cargo build --release -q )
    ( cd "$root/server" && ./target/release/cyd-todo-server link "${1:-quest}" )
}

cmd_board() {
    # shellcheck disable=SC1091
    source "$root/firmware/third_party/cydui/tools/env.sh"
    cd "$root/firmware"
    rm -f sdkconfig
    local defaults="sdkconfig.defaults"
    [ -f "$local_cfg" ] && defaults="$defaults;$(basename "$local_cfg")"
    idf.py -D "SDKCONFIG_DEFAULTS=$defaults" ${PORT:+-p "$PORT"} build flash monitor
}

cmd_bake() {
    need_env
    umask 077
    {
        echo "# Local development only (git-ignored): the server and token of this computer. Never commit this file."
        echo "CONFIG_TODO_DEV_SERVER_ADDRESS=\"${PUBLIC_URL#http://}\""
        echo "CONFIG_TODO_DEV_TOKEN=\"$CYD_TOKEN\""
    } > "$local_cfg"
    echo "Wrote $local_cfg. It replaces the server and token only when the Wi-Fi was already set up on the board."
}

cmd_firewall() {
    local ip net; ip="$(lan_ip)"; net="${ip%.*}.0/24"
    cat <<MSG
If the board cannot reach the server, allow it through this computer's firewall (ufw example), and take it away when done:
  sudo ufw allow from $net to any port 8080 proto tcp
  sudo ufw delete allow from $net to any port 8080 proto tcp
MSG
}

case "${1:-}" in
    doctor) cmd_doctor ;;
    setup) shift; cmd_setup "$@" ;;
    server) cmd_server ;;
    link) shift; cmd_link "$@" ;;
    board) cmd_board ;;
    bake) cmd_bake ;;
    firewall) cmd_firewall ;;
    *) sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'; exit 2 ;;
esac
