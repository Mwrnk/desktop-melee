#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# The style check pull requests run: clang-format and whitespace on Desktop Melee's
# C/C++ and on melee-pc's src/pc (where our hooks live). Objective-C is left out:
# .clang-format has no section for it. Pass --fix to reformat in place.
#
# Needs clang-format 18, the version CI pins; other versions format differently:
#   python3 -m pip install clang-format==18.1.8
set -eu
cd "$(dirname "$0")/../.."

if ! clang-format --version 2>/dev/null | grep -q 'version 18\.'; then
    echo "check_style: needs clang-format 18 (python3 -m pip install clang-format==18.1.8)" >&2
    exit 2
fi

status=0
python3 tools/check_style.py "$@" || status=1
# shellcheck disable=SC2046 # one argument per file
python3 tools/check_style.py "$@" \
    --files $(git ls-files -- 'desktop-melee/*.c' 'desktop-melee/*.cpp' 'desktop-melee/*.h') ||
    status=1
exit $status
