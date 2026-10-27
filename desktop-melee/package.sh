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
#
# DESKTOP_MELEE_RELEASE=1: an app to hand out. It leaves out Melee's font (the
# one file from your disc), refuses to finish if any other game file is inside,
# signs ad hoc, doesn't install, and zips the app to dist/.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$(pwd)"
BUILD_DIR="${ROOT}/build/macos"
APP="${BUILD_DIR}/Desktop Melee.app"
DEST="/Applications/Desktop Melee.app"
RELEASE="${DESKTOP_MELEE_RELEASE:-0}"
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
if [[ "${RELEASE}" == "1" ]]; then
    echo "note: release build: Melee's font is left out; the welcome window uses the" \
        "system font until the player's disc is in" >&2
elif [[ -f "${FONT}" ]]; then
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

# The license texts: the GPL asks for them to travel with the program.
mkdir -p "${APP}/Contents/Resources/licenses"
cp licenses/COPYING licenses/LICENSE.md "${APP}/Contents/Resources/licenses/"

if [[ "${RELEASE}" == "1" ]]; then
    # Nothing from a disc may ship: Melee's font, disc images, or the game's
    # own file types (models and stages .dat/.usd, music .hps, sound .ssm/.sem,
    # movies .thp).
    echo "=== Checking for game data ==="
    GAME_DATA="$(find "${APP}" -type f \( -name 'melee-font.bin' \
        -o -iname '*.iso' -o -iname '*.gcm' -o -iname '*.ciso' -o -iname '*.rvz' \
        -o -iname '*.gcz' -o -iname '*.wia' -o -iname '*.dat' -o -iname '*.usd' \
        -o -iname '*.hps' -o -iname '*.ssm' -o -iname '*.sem' -o -iname '*.thp' \
        -o -iname '*.mth' \))"
    if [[ -n "${GAME_DATA}" ]]; then
        echo "error: game data in the release app:" >&2
        echo "${GAME_DATA}" >&2
        exit 1
    fi
    # No extended attributes from this Mac (provenance, quarantine) in the zip.
    xattr -cr "${APP}"
fi

IDENTITY="${DESKTOP_MELEE_SIGN:-$(security find-identity -v -p codesigning |
    awk -F'"' '/"Apple Development/ { print $2; exit }')}"
# A release is signed ad hoc: without Apple's paid program nothing better
# opens on other Macs anyway, and a personal certificate would carry your
# Apple ID's name into every copy.
if [[ "${RELEASE}" == "1" ]]; then
    IDENTITY="-"
elif [[ -z "${IDENTITY}" ]]; then
    echo "warning: no Apple Development certificate; signing ad hoc, so Accessibility" \
        "must be granted again after every build" >&2
    IDENTITY="-"
fi
echo "=== Signing with ${IDENTITY} ==="
codesign --force --deep --sign "${IDENTITY}" --timestamp=none "${APP}"
codesign --verify --deep --strict "${APP}"

if [[ "${RELEASE}" == "1" ]]; then
    # tools/package_macos.sh also leaves melee-pc's Melee-macOS-*.zip in dist/;
    # it is not this app, so it goes.
    rm -f dist/Melee-macOS-*.zip
    ZIP="dist/Desktop-Melee-${VERSION}-macOS-$(uname -m).zip"
    echo "=== Zipping ${ZIP} ==="
    mkdir -p dist
    rm -f "${ZIP}"
    ditto -c -k --norsrc --keepParent "${APP}" "${ZIP}"
    echo "Release: ${ZIP} (opens on macOS ${MIN_MACOS} and later, Apple silicon)"
    ls -lh "${ZIP}"
    exit 0
fi

if [[ "${DESKTOP_MELEE_INSTALL:-1}" != "0" ]]; then
    if pgrep -f "${DEST}/Contents/MacOS/melee" > /dev/null; then
        echo "error: Desktop Melee is running; quit it from the menu bar first" >&2
        exit 1
    fi
    echo "=== Installing ${DEST} ==="
    rm -rf "${DEST}"
    ditto "${APP}" "${DEST}"
fi
