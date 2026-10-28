#!/bin/sh
# Milestone 1 dev boot: Fox vs Fox on Final Destination, 4 stocks, no menus.
# Press Start (Enter) at the title to jump straight into the match.
# Usage: desktop-melee/run-fox-ditto.sh path/to/melee.iso
set -eu
if [ $# -lt 1 ]; then
    echo "usage: $0 path/to/melee.iso" >&2
    exit 2
fi
# Absolute, since the game runs from the repository root.
case "$1" in
    /*) ISO="$1" ;;
    *) ISO="$(pwd)/$1" ;;
esac
cd "$(dirname "$0")/.."
MELEE_DEBUG_VS=1 \
MELEE_DEBUG_VS_CKIND=2 \
MELEE_DEBUG_VS_STOCKS=4 \
MELEE_NO_ATTRACT=1 \
    exec build/macos/melee "$ISO"
