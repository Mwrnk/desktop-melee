#!/bin/bash
# Re-render the app icon's artwork after editing AppIcon-light.svg or
# AppIcon-dark.svg. Writes the 1024 px PNGs that pet/macos/AppIcon.icon uses
# (light = the default appearance, dark = Dark Mode). Needs Google Chrome;
# package.sh only needs the committed PNGs.
set -euo pipefail
cd "$(dirname "$0")"
CHROME="${CHROME:-/Applications/Google Chrome.app/Contents/MacOS/Google Chrome}"
PROFILE="$(mktemp -d)"
trap 'rm -rf "${PROFILE}"' EXIT
for variant in light dark; do
    "${CHROME}" --headless=new --disable-gpu --hide-scrollbars --no-first-run \
        --user-data-dir="${PROFILE}" --force-device-scale-factor=1 \
        --window-size=1024,1024 --default-background-color=00000000 \
        --screenshot="../AppIcon.icon/Assets/${variant}.png" \
        "file://$(pwd)/AppIcon-${variant}.svg" > /dev/null 2>&1
    echo "wrote pet/macos/AppIcon.icon/Assets/${variant}.png"
done
