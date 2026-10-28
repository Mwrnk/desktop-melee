/* SPDX-License-Identifier: GPL-3.0-or-later */
/* desktop-melee host hooks, called from src/pc/main.c. */
#pragma once
#include <stdbool.h>

#include <SDL3/SDL_video.h>
#include <aurora/aurora.h>

#include "desktop_melee_game.h"
#include "platform.h"

/* Before aurora_initialize: window and presentation settings. */
void desktop_melee_configure(AuroraConfig* config);
/* After aurora_initialize: platform window tweaks SDL has no API for
 * (desktop_melee_platform_overlay_setup). */
void desktop_melee_window_created(SDL_Window* window);
/* After the launcher applies saved preferences (pc_menu_init). */
void desktop_melee_after_menu_init(void);
/* Around the disc choice and melee-pc's launcher (its shader-cache wait):
 * the overlay stays fully transparent, so melee-pc's screen is never seen,
 * and the menu bar app shows the progress
 * (desktop_melee_platform_ui_loading_*). */
void desktop_melee_launcher_wait_begin(void);
void desktop_melee_launcher_wait_end(void);
/* Ask the game to shut down cleanly (the menu bar's Quit). */
void desktop_melee_request_quit(void);

/* Screen size and Dock top, in points, measured once at startup. */
void desktop_melee_set_screen_geometry(float width_pt, float height_pt, float floor_pt);
/* Polled on the main thread: whether the island should be showing
 * (desktop_melee_platform_island_place). */
bool desktop_melee_island_wanted(void);

/* Menu bar side of the match flow. */
void desktop_melee_set_match_setup(const DesktopMeleeMatchSetup* setup);
bool desktop_melee_match_active(void);

/* The disc to boot in overlay mode: only one that passed the disc check.
 * The command line's disc, else the one the launcher last booted, if it is
 * remembered as checked; otherwise the player picks one
 * (desktop_melee_platform_ui_choose_disc). NULL if they gave up. Other modes
 * get command_line_disc back. */
const char* desktop_melee_choose_disc(const char* command_line_disc);
