#!/usr/bin/env bash
# Builds the self-test firmware, flashes it to the board on /dev/ttyUSB0, reads the hash of every scene from the serial
# port and compares it with the golden hashes of the PC (firmware/pc/golden/scene_*.hash).
#
#   tools/board_hashes.sh [--no-flash] [PORT]     exit 0 only if every scene hash is the PC's
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

flash=1
[ "${1:-}" = "--no-flash" ] && { flash=0; shift; }
port="${1:-/dev/ttyUSB0}"
log="$(mktemp -t board_hashes.XXXXXX)"
trap 'rm -f "$log"' EXIT

# shellcheck disable=SC1091
source third_party/cydui/tools/env.sh
build="build-selftest"

echo "==> building the self-test firmware"
idf.py -B "$build" -D "SDKCONFIG=$build/sdkconfig" -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.selftest" build >/dev/null
if [ "$flash" = 1 ]; then
    echo "==> flashing $port"
    idf.py -B "$build" -D "SDKCONFIG=$build/sdkconfig" -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.selftest" -p "$port" flash >/dev/null
fi

echo "==> reading the hashes (up to 4 minutes)"
python3 - "$port" "$log" <<'PY'
import sys, time, serial
port, out = sys.argv[1], sys.argv[2]
with serial.Serial(port, 115200, timeout=0.5) as ser, open(out, "wb") as f:
    ser.dtr = False
    ser.rts = True
    time.sleep(0.1)
    ser.rts = False
    end = time.monotonic() + 240
    while time.monotonic() < end:
        data = ser.read(4096)
        f.write(data)
        f.flush()
        if b"TODO_SELFTEST_DONE" in open(out, "rb").read():
            break
PY

python3 - "$log" pc/golden <<'PY'
import sys, re, os
log, golden = sys.argv[1], sys.argv[2]
text = open(log, "rb").read().decode("utf-8", "replace")
got = {}
for m in re.finditer(r"SCENE (\S+) (\S+) ([0-9a-f]{16}) errors=0x([0-9a-f]+)", text):
    got["scene_%s_%s" % (m.group(1), m.group(2))] = (m.group(3), m.group(4))
if "TODO_SELFTEST_DONE" not in text:
    print("board_hashes.sh: the self-test did not finish; the log ends with:\n" + "\n".join(text.splitlines()[-15:]))
    sys.exit(1)
bad = 0
want = sorted(f[:-5] for f in os.listdir(golden) if f.startswith("scene_") and f.endswith(".hash"))
for name in want:
    exp = open(os.path.join(golden, name + ".hash")).read().strip()
    if name not in got:
        print("MISSING %s" % name); bad += 1
    elif got[name][0] != exp:
        print("DIFFERENT %s: board %s, pc %s" % (name, got[name][0], exp)); bad += 1
    elif int(got[name][1], 16) & ~0x10:  # 0x10 is "text was cut", which is informative
        print("ERRORS %s: 0x%s" % (name, got[name][1])); bad += 1
for line in text.splitlines():
    if "TODO_HEAP" in line or "TODO_SIZES" in line:
        print(line.strip())
print("board_hashes.sh: %d scenes compared, %d wrong" % (len(want), bad))
sys.exit(1 if bad else 0)
PY
