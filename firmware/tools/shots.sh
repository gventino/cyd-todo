#!/usr/bin/env bash
# The screenshots of docs/img/, rendered by the PC program with the demo data (the same pictures the board must show).
#
#   tools/shots.sh            write docs/img/<screen>-<day|night>.png
#   tools/shots.sh --check    render into a temp dir and fail if it differs from docs/img/
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/../.."

build="firmware/pc/build-shots"
cmake -S firmware/pc -B "$build" -G Ninja >/dev/null
cmake --build "$build" --target todo_pc >/dev/null

# <file name>:<scene>
# the pictures are compared by their pixels, not their bytes: the compressor may change between versions
pixels() { magick "$1" -depth 8 rgb:- | sha256sum; }

SHOTS="quests:quests calendar:calendar agenda:agenda detail:detail-quest settings:settings qr-quest:qr-quest setup:setup"

mode="write"
[ "${1:-}" = "--check" ] && mode="check"
out="docs/img"
if [ "$mode" = "check" ]; then
    out="$(mktemp -d)"
fi
mkdir -p "$out"

raw="$(mktemp -d)"
trap 'rm -rf "$raw" "$out_tmp"' EXIT
out_tmp=""
[ "$mode" = "check" ] && out_tmp="$out"

for shot in $SHOTS; do
    name="${shot%%:*}"
    scene="${shot##*:}"
    for p in day night; do
        "$build/todo_pc" --scene "$scene" --palette "$p" --png "$raw/$name-$p.png"
        magick "$raw/$name-$p.png" -strip -define png:compression-level=9 "$out/$name-$p.png" # the program's PNG is stored, not compressed
    done
done

if [ "$mode" = "check" ]; then
    for shot in $SHOTS; do
        name="${shot%%:*}"
        for p in day night; do
            [ "$(pixels "$out/$name-$p.png")" = "$(pixels "docs/img/$name-$p.png")" ] || { echo "shots.sh: docs/img/$name-$p.png differs from what the program renders" >&2; exit 1; }
        done
    done
    echo "shots.sh: the screenshots match the program"
fi
