#!/bin/sh
# Run melee-pet: the transparent desktop overlay with its menu bar app.
# It boots into an empty idle scene; start a match from the Smash icon in the
# menu bar (or with Start on a controller).
#
# The disc: the path given here, else the one used last time, else a file
# picker on first run.
#
# MELEE_PET_SCALE: Melee units across the screen width (default 490); bigger
# means smaller fighters.
# Accessibility (asked once) gives the exact Dock size; MELEE_PET_AX_PROMPT=0
# skips the dialog.
#
# Usage: pet/run-overlay.sh [path/to/melee.iso]
cd "$(dirname "$0")/.."
MELEE_PET_OVERLAY=1 exec build/macos/melee "$@"
