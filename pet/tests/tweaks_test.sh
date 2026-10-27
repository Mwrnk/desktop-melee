#!/bin/bash
# Tests the Display and Pause tweaks (pet/tweaks-design.md) on the installed
# Desktop Melee.app. Each case starts a real match through MELEE_KEY_FIFO
# with MELEE_PET_TRACE=1 and checks the game's "pet: trace" lines. It takes
# over the screen for about 30 s per case. Settings are changed one key at a
# time and each is put back exactly as it was (or removed if it was absent).
#
# Usage: pet/tests/tweaks_test.sh [case...]   (after pet/package.sh)
set -uo pipefail
# A key written after the app died must fail the case, not kill the script.
trap '' PIPE
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
    [[ "${PID}" =~ ^[0-9]+$ ]] && kill -9 "${PID}"
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
# "windows [name]" saves the window list to <log>.windows[.name]. Ends by
# killing the app. run_idle is the same without starting a match.
run_match() { START_MATCH=1 run_game "$@"; }
run_idle() { START_MATCH=0 run_game "$@"; }
run_game() {
    local log="$1" action pid
    shift
    rm -f "${DIR}/keys"
    mkfifo "${DIR}/keys"
    env MELEE_KEY_FIFO="${DIR}/keys" MELEE_PET_TRACE=1 MELEE_PET_LOG="${log}" \
        pet/run-overlay.sh > /dev/null 2>&1 &
    exec 3> "${DIR}/keys"
    sleep 12
    if [[ ${START_MATCH} -eq 1 ]]; then
        echo "Return 150" >&3
        sleep 6
    fi
    for action in "$@"; do
        pid="$(pgrep -n -f "${EXE}")"
        case "${action}" in
            sleep\ *) sleep "${action#sleep }" ;;
            usr1) [[ "${pid}" =~ ^[0-9]+$ ]] && kill -USR1 "${pid}" ;;
            windows) "${DIR}/window_list" > "${log}.windows" ;;
            windows\ *) "${DIR}/window_list" > "${log}.windows.${action#windows }" ;;
            *) echo "${action}" >&3 ;;
        esac
    done
    exec 3>&-
    pid="$(pgrep -n -f "${EXE}")"
    [[ "${pid}" =~ ^[0-9]+$ ]] && kill -9 "${pid}"
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
last_tags() { traces "$1" | tail -1 | awk '{ for (i = 1; i < NF; i += 2) if ($i == "tags") print $(i + 1) }'; }

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

# A trace field's value on the last trace line.
last_field() { traces "$1" | tail -1 | awk -v f="$2" '{ for (i = 1; i < NF; i += 2) if ($i == f) print $(i + 1) }'; }

# The island: Desktop Melee's window one level below the full-screen overlay.
island_window() {
    awk -F'\t' '$1 == "Desktop Melee" { split($3, b, " "); if (b[2] == 0 && b[3] > 1000) l = $2; else w[$2] = $3 }
        END { if (l != "" && ((l - 1) in w)) print w[l - 1] }' "$1"
}

case_hud_hidden() {
    with_setting display.hud string hidden
    run_match "${DIR}/hud_hidden.log" "sleep 4" windows
    local hud island drawn win
    hud="$(last_field "${DIR}/hud_hidden.log" hud)"
    island="$(last_field "${DIR}/hud_hidden.log" island)"
    drawn="$(last_field "${DIR}/hud_hidden.log" drawn)"
    win="$(island_window "${DIR}/hud_hidden.log.windows")"
    if [[ "${hud}" == hidden && "${island}" == 0 && "${drawn}" == 0 && -z "${win}" ]]; then
        ok "hud hidden: no stocks, percents or island"
    else
        bad "hud hidden: hud ${hud:-?} island ${island:-?} drawn ${drawn:-?} island window '${win}'"
    fi
}

case_hud_bottom() {
    with_setting display.hud string bottom
    run_match "${DIR}/hud_bottom.log" "sleep 4"
    local hud island drawn
    hud="$(last_field "${DIR}/hud_bottom.log" hud)"
    island="$(last_field "${DIR}/hud_bottom.log" island)"
    drawn="$(last_field "${DIR}/hud_bottom.log" drawn)"
    if [[ "${hud}" == bottom && "${island}" == 0 && "${drawn:-0}" -gt 0 ]]; then
        ok "hud bottom: Melee's own HUD, no island"
    else
        bad "hud bottom: hud ${hud:-?} island ${island:-?} drawn ${drawn:-?}"
    fi
}

# A player who had turned the island off gets "bottom" after the upgrade.
case_hud_migrate() {
    with_setting display.hud absent
    with_setting display.island bool false
    run_match "${DIR}/hud_migrate.log" "sleep 4"
    local hud
    hud="$(last_field "${DIR}/hud_migrate.log" hud)"
    if [[ "${hud}" == bottom ]]; then ok "hud migrate: island off became bottom"; else bad "hud migrate: hud ${hud:-?} (want bottom)"; fi
}

# The full-screen overlay's alpha in a window list ("" when it isn't there).
overlay_alpha() {
    awk -F'\t' '$1 == "Desktop Melee" { split($3, b, " "); if (b[2] == 0 && b[3] > 1000) { print $4; exit } }' "$1"
}

# Line numbers of trace lines matching a pattern, in order.
trace_lines() { traces "$1" | grep -n "$2" | cut -d: -f1; }

# Pause and hide during a match: pauses and hides; again shows, still paused;
# Start (Return, the keyboard on port 1) resumes. A hidden timer stays
# hidden through it all.
case_pause() {
    with_setting display.timer string hidden
    run_match "${DIR}/pause.log" "sleep 3" usr1 "sleep 3" "windows on" usr1 "sleep 3" "windows off" \
        "Return 150" "sleep 3"
    local on off resumed a_on a_off untimed
    on="$(trace_lines "${DIR}/pause.log" "paused 1 hidden 1" | head -1)"
    off="$(trace_lines "${DIR}/pause.log" "paused 1 hidden 0" | tail -1)"
    resumed="$(traces "${DIR}/pause.log" | tail -1 | grep -c "paused 0 hidden 0")"
    a_on="$(overlay_alpha "${DIR}/pause.log.windows.on")"
    a_off="$(overlay_alpha "${DIR}/pause.log.windows.off")"
    untimed="$(traces "${DIR}/pause.log" | grep -vc "timer hidden")"
    if [[ -n "${on}" && -n "${off}" && ${off} -gt ${on} && ${resumed} -eq 1 &&
        ( -z "${a_on}" || "${a_on}" == 0 || "${a_on}" == 0.0 ) && ( "${a_off}" == 1 || "${a_off}" == 1.0 ) &&
        ${untimed} -eq 0 ]]; then
        ok "pause: hid and paused, showed still paused, Start resumed; timer stayed hidden"
    else
        bad "pause: on line ${on:-none}, off line ${off:-none}, resumed ${resumed}, overlay alpha hidden '${a_on}' shown '${a_off}', lines with the timer showing ${untimed}"
    fi
}

# With no match, Pause and hide only hides; it leaves no pause behind for
# the next match.
case_pause_idle() {
    run_idle "${DIR}/pause_idle.log" usr1 "sleep 2" usr1 "sleep 2" "Return 150" "sleep 6"
    local toggles last
    toggles="$(grep -ac "pet: pause and hide \(on\|off\) (match 0)" "${DIR}/pause_idle.log")"
    last="$(traces "${DIR}/pause_idle.log" | tail -1)"
    if [[ ${toggles} -eq 2 && "${last}" == *"paused 0 hidden 0"* ]]; then
        ok "pause idle: hid and showed with no match; the next match isn't paused"
    else
        bad "pause idle: ${toggles} toggles logged (want 2), last trace: ${last:-none}"
    fi
}

# Both players CPUs: Pause and hide pauses as port 1, so the keyboard resumes.
case_pause_cpus() {
    with_setting players.p1.cpu bool true
    with_setting players.p2.cpu bool true
    run_match "${DIR}/pause_cpus.log" usr1 "sleep 3" usr1 "sleep 2" "Return 150" "sleep 3"
    local paused last
    paused="$(trace_lines "${DIR}/pause_cpus.log" "paused 1" | head -1)"
    last="$(traces "${DIR}/pause_cpus.log" | tail -1)"
    if [[ -n "${paused}" && "${last}" == *"paused 0"* ]]; then
        ok "pause with two CPUs: paused, and the keyboard resumed"
    else
        bad "pause with two CPUs: first paused line ${paused:-none}, last trace: ${last:-none}"
    fi
}

CASES=("$@")
[[ ${#CASES[@]} -eq 0 ]] && CASES=(shake_on shake_off timer_top timer_hidden tags_on tags_off hud_hidden hud_bottom hud_migrate pause pause_idle pause_cpus)
for c in "${CASES[@]}"; do
    "case_${c}"
done
echo "${PASS} passed, ${FAIL} failed"
[[ ${FAIL} -eq 0 ]]
