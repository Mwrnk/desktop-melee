#!/bin/bash
# Checks that Desktop Melee.app carries Melee's font (Contents/Resources/
# melee-font.bin, written by pet/ui/GameAssets.swift and copied in by
# pet/package.sh): the format is right and every ASCII letter and digit has
# a glyph with ink in it.
#
# Usage: pet/tests/font_bundle_test.sh [path/to/Desktop Melee.app]
set -uo pipefail
APP="${1:-/Applications/Desktop Melee.app}"
FONT="${APP}/Contents/Resources/melee-font.bin"
if [[ ! -f "${FONT}" ]]; then
    echo "FAIL: no ${FONT}"
    exit 1
fi
python3 -I - "${FONT}" <<'PY'
import struct, sys, unicodedata
data = open(sys.argv[1], "rb").read()
if data[:8] != b"DMFONT1\0":
    sys.exit("FAIL: bad magic %r" % data[:8])
count, = struct.unpack_from("<I", data, 8)
entry = 4 + 32 * 32
if len(data) != 12 + count * entry:
    sys.exit("FAIL: %d entries need %d bytes, file has %d" % (count, 12 + count * entry, len(data)))
inked = set()
for i in range(count):
    off = 12 + i * entry
    hi, lo = data[off], data[off + 1]
    try:
        ch = unicodedata.normalize("NFKC", bytes([hi, lo]).decode("shift_jis"))
    except UnicodeDecodeError:
        continue
    if len(ch) == 1 and any(data[off + 4:off + entry]):
        inked.add(ch)
want = set("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789")
missing = sorted(want - inked)
if missing:
    sys.exit("FAIL: no inked glyph for " + "".join(missing))
print("ok: %d glyphs, every letter and digit inked" % count)
PY
