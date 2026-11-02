#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Renders desktop-melee/resources/icons/*.svg to PNGs beside them, 64 pixels
# high (the device.* ones 128), with currentColor as white. The Windows and
# Linux windows load the PNGs and tint the white ones (RmlUi's image-color).
# Each icon is named after the macOS SF Symbol it stands in for. Most are
# Phosphor icons (MIT, licenses/LICENSE.md). Ours: device.* (the controller
# cards, in flat colour) and emblem.standin (after ui/Emblem.swift).
# It also renders the app icon (macos/icon/AppIcon-dark.svg), with rounded
# corners, to resources/app-icon.png: the windows' icon, and the welcome
# window's until the game has the Smash emblem, as on macOS. Needs
# rsvg-convert (librsvg). The PNGs are committed too, so building never needs
# it.
set -eu
cd "$(dirname "$0")/../resources/icons"
for svg in *.svg; do
    height=64
    case "$svg" in device.*) height=128 ;; esac # shown bigger, with detail
    sed 's/currentColor/#ffffff/g' "$svg" | rsvg-convert -h "$height" -o "${svg%.svg}.png"
done
# Rounded corners (22% of the side, like macOS's), which RmlUi can't clip.
sed -e 's#^\(<svg[^>]*>\)#\1<clipPath id="round"><rect width="1024" height="1024" rx="225"/></clipPath><g clip-path="url(\#round)">#' \
    -e 's#</svg>#</g></svg>#' ../../macos/icon/AppIcon-dark.svg | rsvg-convert -h 128 -o ../app-icon.png
