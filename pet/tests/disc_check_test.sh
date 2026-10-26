#!/bin/bash
# Tests the disc check (pet/src/disc_check.cpp) through its command-line
# tester: only a clean USA 1.02 image passes. Builds the tester, makes fake
# disc images (sparse, so they cost no disk) and checks each verdict. The
# real disc from launcher.cfg is checked too when it exists.
#
# Usage: pet/tests/disc_check_test.sh
set -uo pipefail
cd "$(dirname "$0")/../.."
export SDKROOT="${SDKROOT:-$(xcrun --show-sdk-path)}"
ninja -C build/macos pet_disc_check > /dev/null || { echo "FAIL: build pet_disc_check"; exit 1; }
BIN="build/macos/pet_disc_check"
DIR="$(mktemp -d)"
trap 'rm -rf "${DIR}"' EXIT
SIZE=1459978240

# make_disc <file> <game id> <version>: a GameCube header, zeros after it.
make_disc() {
    python3 -I - "$1" "$2" "$3" "${SIZE}" <<'PY'
import sys
path, game_id, version, size = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
header = bytearray(0x440)
header[0:6] = game_id.encode()
header[7] = version
header[0x1C:0x20] = bytes.fromhex("c2339f3d")
header[0x20:0x40] = b"SUPER SMASH BROS. MELEE".ljust(32, b"\0")
with open(path, "wb") as f:
    f.write(header)
    f.truncate(size)
PY
}

PASS=0
FAIL=0
# check <name> <allowed exit codes, e.g. "1 3"> <text the output must contain> <path>
check() {
    local name="$1" codes="$2" want="$3" path="$4"
    local out code
    out="$("${BIN}" "${path}" 2>&1)"
    code=$?
    if [[ " ${codes} " == *" ${code} "* && "${out}" == *"${want}"* ]]; then
        echo "ok   ${name}: ${out}"
        PASS=$((PASS + 1))
    else
        echo "FAIL ${name}: exit ${code} (want ${codes}), output: ${out} (want \"${want}\")"
        FAIL=$((FAIL + 1))
    fi
}

echo hello > "${DIR}/notes.txt"
make_disc "${DIR}/pal.iso" GALP01 0
make_disc "${DIR}/rev0.iso" GALE01 0
make_disc "${DIR}/fake.iso" GALE01 2
mkdir "${DIR}/folder"
: > "${DIR}/empty.iso"

check "text file" "1" "isn't a GameCube disc image" "${DIR}/notes.txt"
check "empty file" "1" "isn't a GameCube disc image" "${DIR}/empty.iso"
check "folder" "1 3" "" "${DIR}/folder"
check "PAL disc" "1" "European (PAL)" "${DIR}/pal.iso"
check "revision 0" "1" "Unsupported revision" "${DIR}/rev0.iso"
check "modified USA 1.02" "2" "modified or is a bad copy" "${DIR}/fake.iso"

# Closing the window or dropping another file frees a running check: it must
# stop promptly and cleanly.
START=$(date +%s)
out="$("${BIN}" --cancel-after-ms 300 "${DIR}/fake.iso" 2>&1)"
code=$?
ELAPSED=$(( $(date +%s) - START ))
if [[ ${code} -eq 4 && "${out}" == canceled* && ${ELAPSED} -le 2 ]]; then
    echo "ok   cancel mid-check: ${out} (${ELAPSED} s)"
    PASS=$((PASS + 1))
else
    echo "FAIL cancel mid-check: exit ${code} (want 4), output: ${out}, ${ELAPSED} s (want <= 2)"
    FAIL=$((FAIL + 1))
fi

CFG="${HOME}/Library/Application Support/melee-pc/launcher.cfg"
REAL="$(sed -n 's/^disc "\(.*\)"$/\1/p' "${CFG}" 2> /dev/null | head -1)"
if [[ -n "${REAL}" && -f "${REAL}" ]]; then
    check "real disc" "0" "verified" "${REAL}"
else
    echo "skip real disc: none in launcher.cfg"
fi

echo "${PASS} passed, ${FAIL} failed"
[[ ${FAIL} -eq 0 ]]
