#!/bin/bash
# Tests the Display and Pause tweaks (pet/tweaks-design.md) on the installed
# Desktop Melee.app. Each case starts a real match through MELEE_KEY_FIFO
# with MELEE_PET_TRACE=1 and checks the game's "pet: trace" lines. It takes
# over the screen for about 30 s per case. Settings are changed one key at a
# time and each is put back exactly as it was (or removed if it was absent).
#
# Usage: pet/tests/tweaks_test.sh [case...]   (after pet/package.sh)
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
SAVED=()

# with_setting <key> <bool|int|string|absent> [value]: set a melee-pet key for
# this run; restore() puts the old value back.
with_setting() {
    local key="$1" type="$2" value="${3:-}" old
    if old="$(defaults read-type melee-pet "${key}" 2> /dev/null)"; then
        SAVED+=("${key}|${old#Type is }|$(defaults read melee-pet "${key}")")
    else
        SAVED+=("${key}|absent|")
    fi
    if [[ "${type}" == absent ]]; then
        defaults delete melee-pet "${key}" 2> /dev/null
    else
        defaults write melee-pet "${key}" "-${type}" "${value}"
    fi
}

ADDED_VERIFIED=0
restore() {
    local i entry key type value
    for ((i = ${#SAVED[@]} - 1; i >= 0; i--)); do
        entry="${SAVED[i]}"
        key="${entry%%|*}"
        type="${entry#*|}"
        type="${type%%|*}"
        value="${entry#*|*|}"
        case "${type}" in
            absent) defaults delete melee-pet "${key}" 2> /dev/null ;;
            boolean) [[ "${value}" == 1 ]] && value=true || value=false
                defaults write melee-pet "${key}" -bool "${value}" ;;
            integer) defaults write melee-pet "${key}" -int "${value}" ;;
            float) defaults write melee-pet "${key}" -float "${value}" ;;
            *) defaults write melee-pet "${key}" -string "${value}" ;;
        esac
    done
    [[ ${ADDED_VERIFIED} -eq 1 ]] && defaults delete melee-pet disc.verified 2> /dev/null
    PID="$(pgrep -n -f "${EXE}")"
    [[ -n "${PID}" ]] && kill -9 "${PID}"
    rm -rf "${DIR}"
}
trap restore EXIT

# Boot straight in: the disc remembered as checked, the welcome done.
if ! defaults read melee-pet disc.verified > /dev/null 2>&1; then
    cat > "${DIR}/remember.swift" << 'SWIFT'
import Foundation
let path = CommandLine.arguments[1]
let a = try! FileManager.default.attributesOfItem(atPath: path)
UserDefaults(suiteName: "melee-pet")!.set(
    ["path": path, "size": (a[.size] as! NSNumber).intValue,
     "mtime": (a[.modificationDate] as! Date).timeIntervalSince1970], forKey: "disc.verified")
SWIFT
    swift "${DIR}/remember.swift" "${DISC}" 2> /dev/null && ADDED_VERIFIED=1
fi
[[ "$(defaults read melee-pet onboarding.done 2> /dev/null)" == "1" ]] ||
    with_setting onboarding.done bool true

swiftc -O -o "${DIR}/window_list" pet/tools/window_list.swift 2> /dev/null || { echo "FAIL: build window_list"; exit 1; }

# run_match <log> [action...]: boot, press Start, then each action: a FIFO
# key line ("Right 3500"), "sleep N", or "usr1" (SIGUSR1: Pause and hide).
# "windows" saves the window list to <log>.windows. Ends by killing the app.
run_match() {
    local log="$1" action pid
    shift
    rm -f "${DIR}/keys"
    mkfifo "${DIR}/keys"
    env MELEE_KEY_FIFO="${DIR}/keys" MELEE_PET_TRACE=1 MELEE_PET_LOG="${log}" \
        pet/run-overlay.sh > /dev/null 2>&1 &
    exec 3> "${DIR}/keys"
    sleep 12
    echo "Return 150" >&3
    sleep 6
    for action in "$@"; do
        pid="$(pgrep -n -f "${EXE}")"
        case "${action}" in
            sleep\ *) sleep "${action#sleep }" ;;
            usr1) kill -USR1 "${pid}" ;;
            windows) "${DIR}/window_list" > "${log}.windows" ;;
            *) echo "${action}" >&3 ;;
        esac
    done
    exec 3>&-
    pid="$(pgrep -n -f "${EXE}")"
    [[ -n "${pid}" ]] && kill -9 "${pid}"
    sleep 1
}

# P1 runs off the Dock's right end: a KO at the bottom blast zone, which
# shakes the camera.
KO_RIGHT=("Right 5000" "sleep 6")

PASS=0
FAIL=0
ok() { echo "ok   $*"; PASS=$((PASS + 1)); }
bad() { echo "FAIL $*"; FAIL=$((FAIL + 1)); }
traces() { grep -a "pet: trace" "$1" | sed 's/.*pet: trace //'; }

# Largest "shake" value across the trace lines.
max_shake() { traces "$1" | awk '{ if ($2 > m) m = $2 } END { printf "%.2f", m + 0 }'; }

case_shake_on() {
    with_setting display.shake bool true
    run_match "${DIR}/shake_on.log" "${KO_RIGHT[@]}"
    local n m
    n="$(traces "${DIR}/shake_on.log" | wc -l | tr -d ' ')"
    m="$(max_shake "${DIR}/shake_on.log")"
    if [[ ${n} -ge 3 ]] && awk -v m="${m}" 'BEGIN { exit !(m > 0) }'; then
        ok "shake on: ${n} trace lines, largest shake ${m}"
    else
        bad "shake on: ${n} trace lines (want >= 3), largest shake ${m} (want > 0)"
    fi
}

case_shake_off() {
    with_setting display.shake bool false
    run_match "${DIR}/shake_off.log" "${KO_RIGHT[@]}"
    local n m
    n="$(traces "${DIR}/shake_off.log" | wc -l | tr -d ' ')"
    m="$(max_shake "${DIR}/shake_off.log")"
    if [[ ${n} -ge 3 && "${m}" == "0.00" ]]; then
        ok "shake off: ${n} trace lines, no shake"
    else
        bad "shake off: ${n} trace lines (want >= 3), largest shake ${m} (want 0.00)"
    fi
}

case_timer_top() {
    with_setting match.minutes int 7
    with_setting display.timer string top
    run_match "${DIR}/timer_top.log" "sleep 4"
    if traces "${DIR}/timer_top.log" | grep -q "timer top scale 0.50"; then
        ok "timer top: shown at half size"
    else
        bad "timer top: want \"timer top scale 0.50\", got: $(traces "${DIR}/timer_top.log" | head -1)"
    fi
}

case_timer_hidden() {
    with_setting match.minutes int 7
    with_setting display.timer string hidden
    run_match "${DIR}/timer_hidden.log" "sleep 4"
    local n hidden
    n="$(traces "${DIR}/timer_hidden.log" | wc -l | tr -d ' ')"
    hidden="$(traces "${DIR}/timer_hidden.log" | grep -c "timer hidden")"
    if [[ ${n} -ge 3 && ${n} -eq ${hidden} ]]; then
        ok "timer hidden: hidden in all ${n} trace lines"
    else
        bad "timer hidden: ${hidden} of ${n} trace lines say hidden"
    fi
}

# The last trace line's tag count, about 10 s into the match (after Melee's
# start-of-match flash of the tags).
last_tags() { traces "$1" | tail -1 | awk '{ for (i = 1; i < NF; i++) if ($i == "tags") print $(i + 1) }'; }

case_tags_on() {
    with_setting display.tags bool true
    run_match "${DIR}/tags_on.log" "sleep 6"
    local t
    t="$(last_tags "${DIR}/tags_on.log")"
    if [[ "${t}" == 2 ]]; then ok "tags on: 2 shown"; else bad "tags on: ${t:-none} shown (want 2)"; fi
}

case_tags_off() {
    with_setting display.tags bool false
    run_match "${DIR}/tags_off.log" "sleep 6"
    local t
    t="$(last_tags "${DIR}/tags_off.log")"
    if [[ "${t}" == 0 ]]; then ok "tags off: none shown"; else bad "tags off: ${t:-none} shown (want 0)"; fi
}

CASES=("$@")
[[ ${#CASES[@]} -eq 0 ]] && CASES=(shake_on shake_off timer_top timer_hidden tags_on tags_off)
for c in "${CASES[@]}"; do
    "case_${c}"
done
echo "${PASS} passed, ${FAIL} failed"
[[ ${FAIL} -eq 0 ]]
