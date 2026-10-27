/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Game asset scanning for the menu bar app (see petassets.c). */
#pragma once

/* Every texture in the character select screen's archive (MnSlChr). */
void pet_assets_scan_css(void);

/* Loads the character roster (face icons, portraits) and hands each image
 * to the pet code, which keeps decoded copies (pet_asset_roster/portrait). */
void pet_assets_load_roster(void);

/* Loads the menu sound and announcer name banks (select screen's). */
void pet_assets_load_menu_sounds(void);

/* Named graphics for the menu bar (Smash emblem, "Melee" wordmark). */
void pet_assets_load_named(void);

/* Every texture of the title screen's logo and background (dumps). */
void pet_assets_scan_title(void);
