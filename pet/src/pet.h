/* SPDX-License-Identifier: GPL-3.0-or-later */
/* melee-pet host hooks, called from src/pc/main.c. */
#pragma once
#include <stdbool.h>

#include <SDL3/SDL_video.h>
#include <aurora/aurora.h>

#include "pet_game.h"

/* Before aurora_initialize: window and presentation settings. */
void pet_configure(AuroraConfig* config);
/* After aurora_initialize: platform window tweaks SDL has no API for. */
void pet_window_created(SDL_Window* window);
/* After the launcher applies saved preferences (pc_menu_init). */
void pet_after_menu_init(void);

#ifdef __APPLE__
void pet_overlay_mac_setup(SDL_Window* window);
#endif

/* Screen size and Dock top, in points, measured once at startup. */
void pet_set_screen_geometry(float width_pt, float height_pt, float floor_pt);
#ifdef __APPLE__
void pet_mac_screen_geometry(SDL_Window* window, float* width_pt, float* height_pt,
                             float* floor_pt);
#endif
