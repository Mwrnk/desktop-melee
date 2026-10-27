/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Game asset scanning for the menu bar app (see desktop_melee_assets.c). */
#pragma once

/* Every texture in the character select screen's archive (MnSlChr). */
void desktop_melee_assets_scan_css(void);

/* Loads the character roster (face icons, portraits) and hands each image
 * to the Desktop Melee code, which keeps decoded copies (desktop_melee_asset_roster/portrait). */
void desktop_melee_assets_load_roster(void);

/* Loads the menu sound and announcer name banks (select screen's). */
void desktop_melee_assets_load_menu_sounds(void);

/* Named graphics for the menu bar (Smash emblem, "Melee" wordmark). */
void desktop_melee_assets_load_named(void);

/* Every texture of the title screen's logo and background (dumps). */
void desktop_melee_assets_scan_title(void);
