#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds Desktop Melee for Windows as a zip: a "Desktop Melee" folder with
# Desktop Melee.exe (the launcher, windows/launcher.c), the licences, and a
# "game" folder with melee.exe, every DLL a clean Windows 10 or 11 lacks, and
# the resources, so the launcher is the only program a player sees. Run it in
# MSYS2's UCRT64 shell, with the packages from docs/desktop-melee/porting.md
# section 2:
#
#   desktop-melee/package_windows.sh    ->  dist/Desktop-Melee-<version>-windows-x86_64.zip
#
# Like package_linux.sh, it refuses to finish if any game data is inside:
# everything from the disc is read at run time, and Melee's font is cached in
# the player's own settings folder.
set -euo pipefail

if [[ "${MSYSTEM:-}" != "UCRT64" ]]; then
    echo "error: run this in MSYS2's UCRT64 shell (docs/desktop-melee/porting.md)" >&2
    exit 1
fi

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT}"
BUILD="${BUILD_DIR:-${ROOT}/build/win-release}"
DIST="${ROOT}/dist"
VERSION="$(cat desktop-melee/VERSION)"
ARCH="x86_64"
STAGE="${BUILD}/stage/Desktop Melee"
GAME="${STAGE}/game"

echo "=== Building Desktop Melee ${VERSION} (Windows ${ARCH}) ==="
# The same providers as the CI build, and the Windows 10 defines MSYS2's
# headers need (porting.md section 2). Source paths (__FILE__ in asserts)
# relative to the checkout in every target, the fetched SDL too, so the zip
# doesn't name the builder's home folder.
WIN10='-DNTDDI_VERSION=0x0A000000 -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00'
PREFIX_MAP="-ffile-prefix-map=$(cygpath -m "${ROOT}")/="
cmake -B "${BUILD}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_FLAGS="${CFLAGS:-} ${WIN10} ${PREFIX_MAP}" \
    -DCMAKE_CXX_FLAGS="${CXXFLAGS:-} ${WIN10} ${PREFIX_MAP}" \
    -DAURORA_SDL3_PROVIDER=vendor \
    -DAURORA_SDL3_LINKAGE=shared \
    -DAURORA_DAWN_PROVIDER=package \
    -DAURORA_NOD_PROVIDER=package
ninja -C "${BUILD}" melee

echo "=== Building the launcher ==="
# The icon is the windows' icon (resources/app-icon.png) inside an .ico,
# which Windows Vista and later read as a PNG.
LAUNCHER="${BUILD}/launcher"
rm -rf "${LAUNCHER}"
mkdir -p "${LAUNCHER}"
python3 -I - "$(cygpath -m desktop-melee/resources/app-icon.png)" \
    "$(cygpath -m "${LAUNCHER}/app-icon.ico")" << 'PY'
import struct, sys

png = open(sys.argv[1], 'rb').read()
width, height = struct.unpack('>II', png[16:24])
entry = struct.pack('<BBBBHHII', width % 256, height % 256, 0, 0, 1, 32, len(png), 6 + 16)
open(sys.argv[2], 'wb').write(struct.pack('<HHH', 0, 1, 1) + entry + png)
PY
IFS=. read -r V1 V2 V3 <<< "${VERSION}"
cat > "${LAUNCHER}/launcher.rc" << EOF
#include <windows.h>

1 ICON "app-icon.ico"

VS_VERSION_INFO VERSIONINFO
FILEVERSION ${V1},${V2},${V3},0
PRODUCTVERSION ${V1},${V2},${V3},0
FILEOS VOS__WINDOWS32
FILETYPE VFT_APP
BEGIN
    BLOCK "StringFileInfo"
    BEGIN
        BLOCK "040904b0"
        BEGIN
            VALUE "FileDescription", "Desktop Melee"
            VALUE "FileVersion", "${VERSION}"
            VALUE "OriginalFilename", "Desktop Melee.exe"
            VALUE "ProductName", "Desktop Melee"
            VALUE "ProductVersion", "${VERSION}"
        END
    END
    BLOCK "VarFileInfo"
    BEGIN
        VALUE "Translation", 0x0409, 1200
    END
END
EOF
windres "${LAUNCHER}/launcher.rc" -O coff -o "${LAUNCHER}/launcher.res"
gcc -O2 -Wall -Wextra ${WIN10} ${PREFIX_MAP} -municode -mwindows -static-libgcc \
    desktop-melee/windows/launcher.c "${LAUNCHER}/launcher.res" \
    -o "${LAUNCHER}/Desktop Melee.exe"

echo "=== Staging ==="
rm -rf "${BUILD}/stage"
mkdir -p "${GAME}" "${STAGE}/licenses"
cp "${LAUNCHER}/Desktop Melee.exe" "${STAGE}/"
cp "${BUILD}/melee.exe" "${GAME}/"
# Dawn loads the DirectX shader compiler at run time, so no import table
# names it.
cp "${BUILD}/dxcompiler.dll" "${BUILD}/dxil.dll" "${GAME}/"
# Beside the game, where it looks for them (SDL_GetBasePath): the resources
# (melee-pc's and Desktop Melee's) and the shader cache melee-pc starts from.
cp -r "${BUILD}/resources" "${GAME}/resources"
gzip -dc tools/initial_pipeline_cache.db.gz > "${GAME}/initial_pipeline_cache.db"
cp licenses/COPYING licenses/LICENSE.md desktop-melee/resources/icons/PHOSPHOR-LICENSE.txt \
    "${STAGE}/licenses/"

echo "=== Adding the DLLs a clean Windows lacks ==="
# Follows every import from the binaries above, and copies each DLL Windows
# doesn't have: from the build (Dawn, SDL, nod, libpng), from MSYS2 (zlib,
# bzip2, SQLite, zstd, libusb, winpthreads), and the Visual C++ runtime that
# Dawn, dxcompiler and nod are built against, which only machines with the
# Visual C++ Redistributable have. That one comes from this machine's
# System32, so the Redistributable must be installed here.
python3 -I - "$(cygpath -m "${GAME}")" "$(cygpath -m "${BUILD}")" \
    "$(cygpath -m /ucrt64/bin)" "$(cygpath -m "${SYSTEMROOT:-C:/Windows}/System32")" << 'PY' > "${BUILD}/msys2-dlls.txt"
import pathlib, re, shutil, subprocess, sys

stage, build, msys2, system32 = (pathlib.Path(a) for a in sys.argv[1:5])
# On every Windows 10 and 11 (melee-pc's list, tools/package_windows.sh).
OS_DLLS = {
    'kernel32.dll', 'user32.dll', 'gdi32.dll', 'advapi32.dll', 'shell32.dll',
    'ole32.dll', 'oleaut32.dll', 'ntdll.dll', 'winmm.dll', 'version.dll',
    'imm32.dll', 'setupapi.dll', 'bcrypt.dll', 'bcryptprimitives.dll',
    'dxgi.dll', 'd3d11.dll', 'd3d12.dll', 'ucrtbase.dll', 'ws2_32.dll',
    'crypt32.dll', 'shlwapi.dll', 'msvcrt.dll', 'rpcrt4.dll', 'userenv.dll',
    'cfgmgr32.dll', 'dwmapi.dll', 'uxtheme.dll', 'powrprof.dll', 'dbghelp.dll',
    'winhttp.dll', 'iphlpapi.dll', 'hid.dll',
}
VC_RUNTIME = {'msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll', 'msvcp140_atomic_wait.dll',
              'vcruntime140.dll', 'vcruntime140_1.dll'}

def is_os(name):
    n = name.lower()
    return n in OS_DLLS or n.startswith(('api-ms-win-', 'ext-ms-'))

def imports(binary):
    dump = subprocess.run(['objdump', '-p', str(binary)],
                          capture_output=True, text=True, check=True).stdout
    return set(re.findall(r'DLL Name:\s*(\S+)', dump))

def find(name):
    if name.lower() in VC_RUNTIME:
        places = [system32]
    else:
        places = [build, msys2]
    for place in places:
        for candidate in place.iterdir():
            if candidate.name.lower() == name.lower():
                return candidate
    return None

shipped = {p.name.lower() for p in stage.iterdir()}
todo = [p for p in stage.iterdir() if p.suffix.lower() in ('.exe', '.dll')]
missing = {}
while todo:
    binary = todo.pop()
    for dep in sorted(imports(binary)):
        if is_os(dep) or dep.lower() in shipped:
            continue
        source = find(dep)
        if source is None:
            missing.setdefault(dep, []).append(binary.name)
            continue
        shutil.copy(source, stage / source.name)
        shipped.add(dep.lower())
        todo.append(stage / source.name)
        print(f'  {source.name} <- {source.parent}', file=sys.stderr)
        if source.parent == msys2:
            print(source.name)

if missing:
    for dep, users in sorted(missing.items()):
        print(f'  MISSING {dep} <- {", ".join(users)}', file=sys.stderr)
    sys.exit('error: the package depends on DLLs it can\'t find')
PY
# The licences of the MSYS2 libraries shipped, from their packages.
while read -r dll; do
    dll="${dll%$'\r'}" # Windows's Python ends its lines with \r\n
    package="$(pacman -Qqo "/ucrt64/bin/${dll}")"
    name="${package#mingw-w64-ucrt-x86_64-}"
    if [[ -d "/ucrt64/share/licenses/${name}" ]]; then
        cp -r "/ucrt64/share/licenses/${name}" "${STAGE}/licenses/${name}"
    else
        echo "note: no licence folder for ${package} (${dll})"
    fi
done < "${BUILD}/msys2-dlls.txt"

echo "=== Refusing a determinism test build ==="
# A build configured with -DMELEE_FP_PERTURB=<fn> desyncs against every
# other build (tools/package_windows.sh says more).
if grep -qa "FP PERTURB" "${GAME}/melee.exe"; then
    echo "error: melee.exe was built with MELEE_FP_PERTURB; it desyncs by design" >&2
    exit 1
fi

echo "=== Checking for game data ==="
# Nothing from a disc may ship: Melee's font, disc images, or the game's own
# file types (models and stages .dat/.usd, music .hps, sound .ssm/.sem,
# movies .thp).
GAME_DATA="$(find "${STAGE}" -type f \( -name 'melee-font.bin' \
    -o -iname '*.iso' -o -iname '*.gcm' -o -iname '*.ciso' -o -iname '*.rvz' \
    -o -iname '*.gcz' -o -iname '*.wia' -o -iname '*.dat' -o -iname '*.usd' \
    -o -iname '*.hps' -o -iname '*.ssm' -o -iname '*.sem' -o -iname '*.thp' \
    -o -iname '*.mth' \))"
if [[ -n "${GAME_DATA}" ]]; then
    echo "error: game data in the package:" >&2
    echo "${GAME_DATA}" >&2
    exit 1
fi
# Nor paths of the machine that built it: this checkout, and on a personal
# machine anything under the builder's home folder, in both slash styles.
LEAKS=("$(cygpath -m "${ROOT}")/" "$(cygpath -w "${ROOT}")\\")
if [[ -z "${CI:-}" ]]; then
    LEAKS+=("$(cygpath -m "${USERPROFILE}")/" "$(cygpath -w "${USERPROFILE}")\\")
fi
for leak in "${LEAKS[@]}"; do
    if grep -rlqiF -- "${leak}" "${STAGE}"; then
        echo "error: the package names ${leak}:" >&2
        grep -rliF -- "${leak}" "${STAGE}" >&2
        exit 1
    fi
done

echo "=== Making the zip ==="
mkdir -p "${DIST}"
OUT="${DIST}/Desktop-Melee-${VERSION}-windows-${ARCH}.zip"
rm -f "${OUT}"
python3 -I - "$(cygpath -m "${BUILD}/stage")" "$(cygpath -m "${OUT}")" << 'PY'
import pathlib, sys, zipfile

stage = pathlib.Path(sys.argv[1])
with zipfile.ZipFile(sys.argv[2], 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for path in sorted(stage.rglob('*')):
        z.write(path, path.relative_to(stage).as_posix())
PY
echo "zip: ${OUT} ($(du -h "${OUT}" | cut -f1))"
