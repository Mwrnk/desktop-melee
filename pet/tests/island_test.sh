#!/bin/bash
# Starts a match on the installed Desktop Melee.app (it takes over the screen
# for about 25 s) and checks that the notch island sits in the menu bar, flush
# with the top of the screen, not pushed down below it. Needs the disc from
# launcher.cfg; your melee-pet settings are saved and restored around the run.
#
# Usage: pet/tests/island_test.sh   (after pet/package.sh)
set -uo pipefail
cd "$(dirname "$0")/../.."
EXE="/Applications/Desktop Melee.app/Contents/MacOS/melee"
if pgrep -f "${EXE}" > /dev/null; then
    echo "FAIL: Desktop Melee is already running; quit it first"
    exit 1
fi
CFG="${HOME}/Library/Application Support/melee-pc/launcher.cfg"
DISC="$(sed -n 's/^disc "\(.*\)"$/\1/p' "${CFG}" 2> /dev/null | head -1)"
if [[ ! -f "${DISC}" ]]; then
    echo "skip: no disc in launcher.cfg"
    exit 0
fi
DIR="$(mktemp -d)"
defaults export melee-pet "${DIR}/settings.plist"
restore() {
    exec 3>&- 2> /dev/null
    defaults delete melee-pet 2> /dev/null
    defaults import melee-pet "${DIR}/settings.plist"
    rm -rf "${DIR}"
}
trap restore EXIT
swiftc -O -o "${DIR}/window_list" pet/tools/window_list.swift 2> /dev/null || { echo "FAIL: build window_list"; exit 1; }
# Boot straight in: the disc remembered as checked, the welcome done.
cat > "${DIR}/remember.swift" <<'SWIFT'
import Foundation
let path = CommandLine.arguments[1]
let a = try! FileManager.default.attributesOfItem(atPath: path)
UserDefaults(suiteName: "melee-pet")!.set(
    ["path": path, "size": (a[.size] as! NSNumber).intValue,
     "mtime": (a[.modificationDate] as! Date).timeIntervalSince1970], forKey: "disc.verified")
SWIFT
swift "${DIR}/remember.swift" "${DISC}" 2> /dev/null
defaults write melee-pet onboarding.done -bool true
defaults write melee-pet display.island -bool true

mkfifo "${DIR}/keys"
env MELEE_KEY_FIFO="${DIR}/keys" MELEE_PET_LOG="${DIR}/log" pet/run-overlay.sh > /dev/null 2>&1 &
exec 3> "${DIR}/keys"
sleep 12
echo "Return 150" >&3 # Start: a match, with the island
sleep 10
LIST="$("${DIR}/window_list")"
PID="$(pgrep -n -f "${EXE}")"
[[ -n "${PID}" ]] && kill -9 "${PID}"

# The island: Desktop Melee's window just below the overlay's level.
OVERLAY_LAYER="$(printf '%s\n' "${LIST}" | awk -F'\t' '$1 == "Desktop Melee" { split($3, b, " "); if (b[2] == 0 && b[3] > 1000) { print $2; exit } }')"
ISLAND="$(printf '%s\n' "${LIST}" | awk -F'\t' -v l="${OVERLAY_LAYER}" '$1 == "Desktop Melee" && $2 == l - 1 { print $3; exit }')"
if [[ -z "${ISLAND}" ]]; then
    echo "FAIL: no island window on screen"
    exit 1
fi
Y="$(echo "${ISLAND}" | awk '{ print $2 }')"
if [[ "${Y}" != "0" ]]; then
    echo "FAIL: the island is at y ${Y} (x y w h: ${ISLAND}); it must be flush with the top (0)"
    exit 1
fi
echo "ok: the island is flush with the top of the screen (${ISLAND})"
