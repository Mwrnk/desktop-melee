#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds Desktop Melee for Linux as an AppImage, one file that runs on most
# distributions (Arch, Debian, Ubuntu, Mint, Fedora...) as long as they are
# at least as new as the system it was built on. Releases are built by
# .github/workflows/linux-appimage.yml on Ubuntu 24.04, the oldest that
# aurora's prebuilt Dawn links on; built on a rolling distribution, the
# AppImage only runs on new ones.
#
#   desktop-melee/package_linux.sh      ->  dist/Desktop-Melee-<version>-linux-<arch>.AppImage
#
# Like DESKTOP_MELEE_RELEASE=1 desktop-melee/package.sh on macOS, it refuses to
# finish if any game data is inside: everything from the disc is read at run
# time, and Melee's font is cached in the player's own settings folder.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT}"
BUILD="${BUILD_DIR:-${ROOT}/build/linux-release}"
DIST="${ROOT}/dist"
APPDIR="${BUILD}/AppDir"
TOOLS="${BUILD}/tools"
VERSION="$(cat desktop-melee/VERSION)"
APP_ID="io.github.mwrnk.DesktopMelee"

ARCH="$(uname -m)"
case "${ARCH}" in
    x86_64 | aarch64) ;;
    *) echo "error: unsupported architecture ${ARCH}" >&2; exit 1 ;;
esac

echo "=== Building Desktop Melee ${VERSION} (Linux ${ARCH}) ==="
# The same dependency providers as the CI builds and melee-pc's own Linux
# packages: SDL from source (shipped beside the game), prebuilt Dawn and nod.
# Source paths (__FILE__ in asserts) relative to the checkout in every
# target, the fetched SDL too, so the AppImage doesn't name the builder's
# home folder; CMakeLists.txt does it only for the game and aurora.
PREFIX_MAP="-ffile-prefix-map=${ROOT}/="
cmake -B "${BUILD}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_FLAGS="${CFLAGS:-} ${PREFIX_MAP}" \
    -DCMAKE_CXX_FLAGS="${CXXFLAGS:-} ${PREFIX_MAP}" \
    -DAURORA_SDL3_PROVIDER=vendor \
    -DAURORA_SDL3_LINKAGE=shared \
    -DAURORA_DAWN_PROVIDER="${AURORA_DAWN_PROVIDER:-package}" \
    -DAURORA_NOD_PROVIDER="${AURORA_NOD_PROVIDER:-package}"
ninja -C "${BUILD}" melee
for f in libdesktop_melee_overlay_alpha.so desktop_melee_overlay_alpha.json; do
    if [[ ! -f "${BUILD}/${f}" ]]; then
        echo "error: no ${BUILD}/${f}: the overlay needs X11 and SDL's Vulkan headers" >&2
        exit 1
    fi
done

echo "=== Fetching the AppImage tools ==="
mkdir -p "${TOOLS}" "${DIST}"
for tool in appimagetool linuxdeploy; do
    if [[ ! -x "${TOOLS}/${tool}" ]]; then
        case "${tool}" in
            appimagetool) url="https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-${ARCH}.AppImage" ;;
            linuxdeploy) url="https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${ARCH}.AppImage" ;;
        esac
        curl -fL "${url}" -o "${TOOLS}/${tool}"
        chmod +x "${TOOLS}/${tool}"
    fi
done
# CI runners have no FUSE: the tools extract themselves instead.
if [[ ! -e /dev/fuse ]]; then
    export APPIMAGE_EXTRACT_AND_RUN=1
fi

echo "=== Staging the AppDir ==="
rm -rf "${APPDIR}"
mkdir -p "${APPDIR}"
STAGE="${BUILD}/stage"
rm -rf "${STAGE}"
mkdir -p "${STAGE}"
# The icon and the menu entry, named after the app id, as the game's own
# .desktop file is (desktop_entry.c).
cp desktop-melee/resources/app-icon.png "${STAGE}/${APP_ID}.png"
cat > "${STAGE}/${APP_ID}.desktop" << EOF
[Desktop Entry]
Type=Application
Name=Desktop Melee
Comment=Super Smash Bros. Melee on your desktop
Exec=melee
Icon=${APP_ID}
Terminal=false
Categories=Game;
StartupWMClass=${APP_ID}
EOF
# AppRun: Desktop Melee rather than plain melee-pc, unless the player says
# otherwise.
cat > "${STAGE}/AppRun" << 'EOF'
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
export DESKTOP_MELEE_OVERLAY="${DESKTOP_MELEE_OVERLAY:-1}"
exec "${HERE}/usr/bin/melee" "$@"
EOF
chmod +x "${STAGE}/AppRun"

# libusb is dlopened by SDL's HIDAPI (the GameCube adapter), so linuxdeploy
# can't see it: bundle it by hand when the system has it.
LIBUSB="$(ldconfig -p 2> /dev/null | awk '/libusb-1\.0\.so\.0 / { print $NF; exit }')"
EXTRA=()
if [[ -n "${LIBUSB}" && -f "${LIBUSB}" ]]; then
    EXTRA+=(--library "${LIBUSB}")
else
    echo "note: no libusb-1.0 here; the AppImage will use the player's"
fi
"${TOOLS}/linuxdeploy" \
    --appdir "${APPDIR}" \
    --executable "${BUILD}/melee" \
    --icon-file "${STAGE}/${APP_ID}.png" \
    --desktop-file "${STAGE}/${APP_ID}.desktop" \
    --custom-apprun "${STAGE}/AppRun" \
    "${EXTRA[@]}"

# Beside the game, where it looks for them (SDL_GetBasePath): the Vulkan
# layer that makes the overlay transparent (vk_overlay_alpha.c), the
# resources (melee-pc's and Desktop Melee's), the shader cache melee-pc
# starts from, and the licences.
cp "${BUILD}/libdesktop_melee_overlay_alpha.so" "${BUILD}/desktop_melee_overlay_alpha.json" \
    "${APPDIR}/usr/bin/"
cp -r "${BUILD}/resources" "${APPDIR}/usr/bin/resources"
gzip -dc tools/initial_pipeline_cache.db.gz > "${APPDIR}/usr/bin/initial_pipeline_cache.db"
mkdir -p "${APPDIR}/usr/share/licenses/desktop-melee"
cp licenses/COPYING licenses/LICENSE.md desktop-melee/resources/icons/PHOSPHOR-LICENSE.txt \
    "${APPDIR}/usr/share/licenses/desktop-melee/"
strip --strip-debug "${APPDIR}/usr/bin/melee" "${APPDIR}/usr/bin/libdesktop_melee_overlay_alpha.so"

echo "=== Checking for game data ==="
# Nothing from a disc may ship: Melee's font, disc images, or the game's own
# file types (models and stages .dat/.usd, music .hps, sound .ssm/.sem,
# movies .thp).
GAME_DATA="$(find "${APPDIR}" -type f \( -name 'melee-font.bin' \
    -o -iname '*.iso' -o -iname '*.gcm' -o -iname '*.ciso' -o -iname '*.rvz' \
    -o -iname '*.gcz' -o -iname '*.wia' -o -iname '*.dat' -o -iname '*.usd' \
    -o -iname '*.hps' -o -iname '*.ssm' -o -iname '*.sem' -o -iname '*.thp' \
    -o -iname '*.mth' \))"
if [[ -n "${GAME_DATA}" ]]; then
    echo "error: game data in the AppImage:" >&2
    echo "${GAME_DATA}" >&2
    exit 1
fi
# Nor paths of the machine that built it: this checkout (source paths are
# mapped away, so any left are a leak), and on a personal machine anything
# under the builder's home folder. Not the home folder on CI: it names nobody,
# and aurora's prebuilt Dawn, built on GitHub's runners too, carries
# /home/runner/... paths of its own. The trailing slash skips a bare "/root",
# which system libraries mention.
LEAKS=("${ROOT}/")
if [[ -z "${CI:-}" ]]; then
    LEAKS+=("${HOME}/")
fi
for leak in "${LEAKS[@]}"; do
    if grep -rlqF -- "${leak}" "${APPDIR}"; then
        echo "error: the AppImage names ${leak}:" >&2
        grep -rlF -- "${leak}" "${APPDIR}" >&2
        exit 1
    fi
done

echo "=== Making the AppImage ==="
OUT="${DIST}/Desktop-Melee-${VERSION}-linux-${ARCH}.AppImage"
ARCH="${ARCH}" "${TOOLS}/appimagetool" --no-appstream "${APPDIR}" "${OUT}"
echo "AppImage: ${OUT}"
