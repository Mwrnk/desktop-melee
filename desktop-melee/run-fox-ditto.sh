#!/bin/sh
# Milestone 1 dev boot: Fox vs Fox on Final Destination, 4 stocks, no menus.
# Press Start (Enter) at the title to jump straight into the match.
# Usage: desktop-melee/run-fox-ditto.sh [path/to/melee.iso]
set -eu
cd "$(dirname "$0")/.."
ISO="${1:-$HOME/Downloads/Super Smash Bros. Melee (USA) (En,Ja) (v1.02).iso}"
MELEE_DEBUG_VS=1 \
MELEE_DEBUG_VS_CKIND=2 \
MELEE_DEBUG_VS_STOCKS=4 \
MELEE_NO_ATTRACT=1 \
    exec build/macos/melee "$ISO"
