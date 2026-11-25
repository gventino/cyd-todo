#!/usr/bin/env bash
# Regenerates the serif fonts and the star of the quest theme from Liberation Serif.
#
#   tools/gen_fonts.sh            write components/theme_quest/fonts/*.{c,h} and src/star.c
#   tools/gen_fonts.sh --check    regenerate into a temp dir and fail if it differs
#
# Needs the venv (python3 -m venv tools/.venv && tools/.venv/bin/pip install -r third_party/cydui/tools/requirements.txt)
# and the TTFs. Override with TODO_PYTHON, TODO_SERIF_TTF and TODO_SERIF_BOLD_TTF.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

PY="${TODO_PYTHON:-tools/.venv/bin/python}"
REG="${TODO_SERIF_TTF:-/usr/share/fonts/liberation/LiberationSerif-Regular.ttf}"
BOLD="${TODO_SERIF_BOLD_TTF:-/usr/share/fonts/liberation/LiberationSerif-Bold.ttf}"
GEN=third_party/cydui/tools/fontgen.py
CHARS="U+0020-U+007E,U+00A0-U+00FF,U+2022,U+2026,U+20AC,U+2190-U+2193,U+2713"

notice() {
    printf 'Licensed under the SIL Open Font License 1.1 (fonts/LICENSE-OFL.txt), not the MIT license\nof the rest of this repository. Bitmap rendering of Liberation Serif %s:\nDigitized data copyright (c) 2010 Google Corporation with Reserved Font Arimo, Tinos and Cousine.\nCopyright (c) 2012 Red Hat, Inc. with Reserved Font Name Liberation.' "$1"
}

for f in "$PY" "$REG" "$BOLD" "$GEN"; do
    [ -e "$f" ] || { echo "gen_fonts.sh: missing $f" >&2; exit 2; }
done

mode="write"
[ "${1:-}" = "--check" ] && mode="check"
dir="components/theme_quest"
out="$dir"
if [ "$mode" = "check" ]; then
    out="$(mktemp -d)"
    mkdir -p "$out/fonts" "$out/src"
    trap 'rm -rf "$out"' EXIT
fi

gen() { # name ttf size style
    "$PY" "$GEN" --ttf "$2" --size "$3" --name "$1" --chars "$CHARS" --notice "$(notice "$4")" --out "$out/fonts/$1"
}
gen todo_serif_12 "$REG" 12 Regular
gen todo_serif_16 "$REG" 16 Regular
gen todo_serif_bold_16 "$BOLD" 16 Bold
gen todo_serif_bold_24 "$BOLD" 24 Bold
"$PY" tools/gen_star.py > "$out/src/star.c"

if [ "$mode" = "check" ]; then
    for n in todo_serif_12 todo_serif_16 todo_serif_bold_16 todo_serif_bold_24; do
        for e in c h; do
            cmp -s "$out/fonts/$n.$e" "$dir/fonts/$n.$e" || { echo "gen_fonts.sh: fonts/$n.$e differs from the generator output" >&2; exit 1; }
        done
    done
    cmp -s "$out/src/star.c" "$dir/src/star.c" || { echo "gen_fonts.sh: src/star.c differs from the generator output" >&2; exit 1; }
    echo "gen_fonts.sh: fonts and star match the generator"
fi
