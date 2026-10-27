#!/bin/bash
# Build Desktop Melee.app and install it to /Applications.
#
# Runs upstream's tools/package_macos.sh (build, bundled dylibs, Melee.app),
# then renames the app, swaps in our Info.plist and signs it with your Apple
# Development certificate. A stable signature keeps the Accessibility
# permission across rebuilds; an ad-hoc one loses it on every build.
#
# DESKTOP_MELEE_SIGN: signing identity (default: the first "Apple Development"
# one in the keychain, else ad hoc). DESKTOP_MELEE_INSTALL=0: leave the app in
# build/macos.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$(pwd)"
BUILD_DIR="${ROOT}/build/macos"
APP="${BUILD_DIR}/Desktop Melee.app"
DEST="/Applications/Desktop Melee.app"
# Homebrew's gcc bottle can point at an SDK that isn't installed (see build.sh).
export SDKROOT="${SDKROOT:-$(xcrun --show-sdk-path)}"

BUILD_DIR="${BUILD_DIR}" tools/package_macos.sh

echo "=== Making ${APP} ==="
rm -rf "${APP}"
mv "${BUILD_DIR}/Melee.app" "${APP}"
VERSION="$(cat VERSION)"
VERSION="${VERSION#v}"
VERSION="${VERSION%%-*}"
sed "s/@MELEE_VERSION@/${VERSION}/g" pet/macos/Info.plist > "${APP}/Contents/Info.plist"

# Melee's font, saved from the disc on the first boot (pet/ui/GameAssets.swift),
# so the welcome window has it before any disc is in.
FONT="${HOME}/Library/Application Support/Desktop Melee/melee-font.bin"
if [[ -f "${FONT}" ]]; then
    cp "${FONT}" "${APP}/Contents/Resources/melee-font.bin"
else
    echo "warning: no ${FONT} yet; boot the game once and package again to bundle Melee's font" >&2
fi

# App icon: a beige Mac in Light Mode, an indigo one in Dark Mode. actool
# writes Assets.car for macOS 26+ and AppIcon.icns for older systems.
echo "=== Compiling the app icon ==="
ICON_PLIST="$(mktemp)"
xcrun actool pet/macos/AppIcon.icon --compile "${APP}/Contents/Resources" \
    --platform macosx --minimum-deployment-target 13.0 --app-icon AppIcon \
    --output-partial-info-plist "${ICON_PLIST}" > /dev/null
rm -f "${ICON_PLIST}"
if [[ ! -f "${APP}/Contents/Resources/Assets.car" || ! -f "${APP}/Contents/Resources/AppIcon.icns" ]]; then
    echo "error: actool did not write the app icon" >&2
    exit 1
fi

IDENTITY="${DESKTOP_MELEE_SIGN:-$(security find-identity -v -p codesigning |
    awk -F'"' '/"Apple Development/ { print $2; exit }')}"
if [[ -z "${IDENTITY}" ]]; then
    echo "warning: no Apple Development certificate; signing ad hoc, so Accessibility" \
        "must be granted again after every build" >&2
    IDENTITY="-"
fi
echo "=== Signing with ${IDENTITY} ==="
codesign --force --deep --sign "${IDENTITY}" --timestamp=none "${APP}"
codesign --verify --deep --strict "${APP}"

if [[ "${DESKTOP_MELEE_INSTALL:-1}" != "0" ]]; then
    if pgrep -f "${DEST}/Contents/MacOS/melee" > /dev/null; then
        echo "error: Desktop Melee is running; quit it from the menu bar first" >&2
        exit 1
    fi
    echo "=== Installing ${DEST} ==="
    rm -rf "${DEST}"
    ditto "${APP}" "${DEST}"
fi
