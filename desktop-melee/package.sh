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
# Desktop Melee's own version (desktop-melee/VERSION); the root VERSION file is
# the melee-pc release this is built on.
VERSION="$(cat desktop-melee/VERSION)"
VERSION="${VERSION#v}"
VERSION="${VERSION%%-*}"
# The oldest macOS this build opens on: whatever the compiler stamped into the
# binary, which is the macOS of the Mac that built it (no deployment target is
# set, and Homebrew's libraries are built for that macOS too).
MIN_MACOS="$(vtool -show-build "${APP}/Contents/MacOS/melee" | awk '/minos/ { print $2; exit }')"
if [[ -z "${MIN_MACOS}" ]]; then
    echo "error: cannot read the minimum macOS from ${APP}/Contents/MacOS/melee" >&2
    exit 1
fi
sed -e "s/@DESKTOP_MELEE_VERSION@/${VERSION}/g" -e "s/@DESKTOP_MELEE_MIN_MACOS@/${MIN_MACOS}/g" \
    desktop-melee/macos/Info.plist > "${APP}/Contents/Info.plist"

# Melee's font, saved from the disc on the first boot (desktop-melee/ui/GameAssets.swift),
# so the welcome window has it before any disc is in.
FONT="${HOME}/Library/Application Support/Desktop Melee/melee-font.bin"
if [[ -f "${FONT}" ]]; then
    cp "${FONT}" "${APP}/Contents/Resources/melee-font.bin"
    echo "note: the app now holds Melee's font from your disc (Nintendo's); keep this" \
        "build to yourself and never upload it" >&2
else
    echo "warning: no ${FONT} yet; boot the game once and package again to bundle Melee's font" >&2
fi

# App icon: a beige Mac in Light Mode, an indigo one in Dark Mode. actool
# writes Assets.car for macOS 26+ and AppIcon.icns for older systems.
echo "=== Compiling the app icon ==="
ICON_PLIST="$(mktemp)"
xcrun actool desktop-melee/macos/AppIcon.icon --compile "${APP}/Contents/Resources" \
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
