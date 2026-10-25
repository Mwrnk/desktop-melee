#!/bin/sh
# Build melee-pet on macOS: Apple clang for C++, Homebrew GCC for the game C
# (via tools/gcc_launcher.py). Same flags as tools/package_macos.sh.
# SDKROOT: Homebrew's gcc bottle can point at an SDK that isn't installed
# (e.g. MacOSX27.sdk), which fails with "sys/types.h: No such file".
set -eu
cd "$(dirname "$0")/.."
export SDKROOT="${SDKROOT:-$(xcrun --show-sdk-path)}"
if [ ! -f build/macos/build.ninja ]; then
    cmake -B build/macos -G Ninja \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DCMAKE_C_COMPILER=clang \
        -DCMAKE_CXX_COMPILER=clang++ \
        -DAURORA_SDL3_PROVIDER=vendor \
        -DAURORA_SDL3_LINKAGE=shared \
        -DAURORA_DAWN_PROVIDER=package \
        -DAURORA_NOD_PROVIDER=package
fi
ninja -C build/macos melee
