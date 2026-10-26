#!/bin/bash
# Run Desktop Melee: the transparent desktop overlay with its menu bar app.
# It boots into an empty idle scene; start a match from the Smash icon in the
# menu bar (or with Start on a controller).
#
# It opens the app bundle (pet/package.sh builds and installs it) rather than
# the bare binary, so macOS gives the Accessibility permission to Desktop
# Melee and not to the terminal or tool that started it. A new instance opens
# every time; quit it from the menu bar.
#
# The disc: the path given here, else the one used last time, else a file
# picker on first run.
#
# Every MELEE_* variable set here is passed on to the game, e.g.
# MELEE_PET_SCALE: Melee units across the screen width (default 490); bigger
# means smaller fighters.
# MELEE_PET_AX_PROMPT=0: never show the Accessibility dialog (without it the
# Dock size is estimated).
# The game's log goes to this terminal, or to MELEE_PET_LOG if set (or when
# there is no terminal, ~/Library/Logs/Desktop Melee.log).
#
# Usage: pet/run-overlay.sh [path/to/melee.iso]
set -eu
cd "$(dirname "$0")/.."
APP="/Applications/Desktop Melee.app"
[[ -d "${APP}" ]] || APP="build/macos/Desktop Melee.app"
if [[ ! -d "${APP}" ]]; then
    echo "error: no Desktop Melee.app; build it with pet/package.sh" >&2
    exit 1
fi
LOG="${MELEE_PET_LOG:-}"
if [[ -z "${LOG}" ]]; then
    LOG="$(tty 2> /dev/null)" || LOG="${HOME}/Library/Logs/Desktop Melee.log"
fi
ENV_ARGS=()
while IFS= read -r var; do
    ENV_ARGS+=(--env "${var}")
done < <(env | grep '^MELEE_' || true)
ARGS=()
[[ $# -gt 0 ]] && ARGS=(--args "$@")
exec open -n -W --stdout "${LOG}" --stderr "${LOG}" ${ENV_ARGS[@]+"${ENV_ARGS[@]}"} \
    "${APP}" ${ARGS[@]+"${ARGS[@]}"}
