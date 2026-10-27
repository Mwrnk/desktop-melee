#!/bin/bash
# Re-render the app icon's layers after editing AppIcon-light.svg or
# AppIcon-dark.svg. Each top-level <g id="..."> in the SVGs is one Icon
# Composer layer (base, screen, stage, dots, gloss); this writes one 1024 px
# PNG per layer and appearance to pet/macos/AppIcon.icon/Assets, as
# <appearance>-<layer>.png. Keeping the layers apart lets macOS tint the
# icon in its tinted and clear styles. Needs Google Chrome; package.sh only
# needs the committed PNGs.
set -euo pipefail
cd "$(dirname "$0")"
CHROME="${CHROME:-/Applications/Google Chrome.app/Contents/MacOS/Google Chrome}"
WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT
mkdir -p ../AppIcon.icon/Assets
LAYERS="base screen stage dots gloss"
for variant in light dark; do
    for layer in ${LAYERS}; do
        # Hide every other top-level group.
        sed "s|<defs>|<style>svg > g:not(#${layer}){display:none}</style><defs>|" \
            "AppIcon-${variant}.svg" > "${WORK}/${variant}-${layer}.svg"
        "${CHROME}" --headless=new --disable-gpu --hide-scrollbars --no-first-run \
            --user-data-dir="${WORK}/profile" --force-device-scale-factor=1 \
            --window-size=1024,1024 --default-background-color=00000000 \
            --screenshot="../AppIcon.icon/Assets/${variant}-${layer}.png" \
            "file://${WORK}/${variant}-${layer}.svg" > /dev/null 2>&1
        echo "wrote pet/macos/AppIcon.icon/Assets/${variant}-${layer}.png"
    done
done
