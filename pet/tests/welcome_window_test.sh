#!/bin/bash
# Opens the installed Desktop Melee.app on the welcome window (it takes over
# the screen for a few seconds) and checks that the player can reach it:
# no Desktop Melee window that covers the screen may sit above the welcome
# window while it waits for a disc. Quits the instance it started.
#
# Usage: pet/tests/welcome_window_test.sh   (after pet/package.sh)
set -uo pipefail
cd "$(dirname "$0")/../.."
APP="/Applications/Desktop Melee.app"
EXE="${APP}/Contents/MacOS/melee"
if pgrep -f "${EXE}" > /dev/null; then
    echo "FAIL: Desktop Melee is already running; quit it first"
    exit 1
fi
DIR="$(mktemp -d)"
trap 'rm -rf "${DIR}"' EXIT
swiftc -O -o "${DIR}/window_list" pet/tools/window_list.swift 2> /dev/null || { echo "FAIL: build window_list"; exit 1; }

open -n --env MELEE_PET_WELCOME=1 "${APP}"
# Wait (up to 20 s) for the welcome window, 440 pt wide, then 1 s more.
for _ in $(seq 1 40); do
    LIST="$("${DIR}/window_list")"
    printf '%s\n' "${LIST}" | awk -F'\t' '$1 == "Desktop Melee" { split($3, b, " "); if (b[3] == 440) f = 1 } END { exit !f }' && break
    sleep 0.5
done
sleep 1
LIST="$("${DIR}/window_list")"
PID="$(pgrep -n -f "${EXE}")"
[[ -n "${PID}" ]] && kill -9 "${PID}"

# Desktop Melee's windows, front to back: the welcome window is 440 pt wide.
OURS="$(printf '%s\n' "${LIST}" | awk -F'\t' '$1 == "Desktop Melee"')"
echo "${OURS}"
WELCOME_LAYER="$(printf '%s\n' "${OURS}" | awk -F'\t' '{ split($3, b, " "); if (b[3] == 440) { print $2; exit } }')"
if [[ -z "${WELCOME_LAYER}" ]]; then
    echo "FAIL: no welcome window on screen"
    exit 1
fi
SCREEN_W="$(printf '%s\n' "${LIST}" | awk -F'\t' '$1 == "Dock" { split($3, b, " "); print b[3]; exit }')"
COVER="$(printf '%s\n' "${OURS}" | awk -F'\t' -v w="${SCREEN_W}" -v l="${WELCOME_LAYER}" \
    '{ split($3, b, " "); if (b[3] >= w && $2 > l) print }')"
if [[ -n "${COVER}" ]]; then
    echo "FAIL: a screen-sized Desktop Melee window sits above the welcome window (layer ${WELCOME_LAYER}): ${COVER}"
    exit 1
fi
echo "ok: the welcome window (layer ${WELCOME_LAYER}) is the topmost Desktop Melee window"
