#!/usr/bin/env bash
# Reads every golden QR picture with a real decoder (zbar) and compares what it finds with what was encoded.
# usage: decode_qr.sh <dir with quest_qr_*.png and scene_*.png, and their .txt sidecars>      exit 77 = skipped (no decoder)
# A picture with a sidecar must decode to it; a scene_* picture without one has no code on it.
set -u
dir="$1"
if ! command -v zbarimg >/dev/null; then
    echo "decode_qr.sh: zbarimg is not installed, the QR pictures were NOT decoded" >&2
    exit 77
fi
n=0
for png in "$dir"/quest_qr_*.png "$dir"/scene_*.png; do
    [ -e "$png" ] || { echo "decode_qr.sh: no pictures in $dir" >&2; exit 1; }
    [ -e "${png%.png}.txt" ] || continue
    want="$(cat "${png%.png}.txt")"
    got="$(zbarimg --quiet --raw -Sdisable -Sqr.enable "$png" 2>/dev/null)"
    if [ "$got" != "$want" ]; then
        echo "decode_qr.sh: $(basename "$png"): decoded '$got', expected '$want'" >&2
        exit 1
    fi
    n=$((n + 1))
done
echo "decode_qr.sh: $n pictures decoded and matched"
