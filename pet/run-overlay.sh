#!/bin/sh
# Milestone 2 dev boot: the Fox ditto as a transparent, click-through overlay
# over the whole primary display, with the stage and background hidden and a
# fixed camera: Melee's floor sits on top of the Dock.
# Press Start (Enter) at the title to jump straight into the match. The
# keyboard only works while melee is the active app (Cmd-Tab to it).
#
# MELEE_PET_SCALE: how many Melee units span the screen width (default 490,
# Final Destination's blastzone width). Bigger = smaller fighters.
#
# The main floor is stretched to the Dock's width. macOS asks once for
# Accessibility permission for this terminal: granting it gives the exact
# Dock size (otherwise it is estimated from the Dock's settings). Relaunch
# after granting. MELEE_PET_AX_PROMPT=0 skips the dialog.
#
# Usage: [MELEE_PET_SCALE=600] pet/run-overlay.sh [path/to/melee.iso]
MELEE_PET_OVERLAY=1 exec "$(dirname "$0")/run-fox-ditto.sh" "$@"
