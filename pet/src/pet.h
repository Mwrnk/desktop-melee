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
/* Hide or show the overlay window (the welcome window hides it). */
void pet_mac_overlay_set_hidden(bool hidden);
/* The Swift menu bar app (pet/ui/MenuBar.swift); takes the game's NSWindow. */
void pet_ui_install(void* game_window);
#endif
/* Ask the game to shut down cleanly (the menu bar's Quit). */
void pet_request_quit(void);

/* Screen size and Dock top, in points, measured once at startup. */
void pet_set_screen_geometry(float width_pt, float height_pt, float floor_pt);
#ifdef __APPLE__
void pet_mac_screen_geometry(SDL_Window* window, float* width_pt, float* height_pt,
                             float* floor_pt);
/* The Dock bar on the overlay's screen, in points (y up from that screen's
 * bottom). False when the Dock is not on that screen. Exact when
 * Accessibility is granted, otherwise estimated from the Dock's settings. */
bool pet_mac_dock_frame(float screen_w, float screen_h, float* left, float* right,
                        float* top, bool* exact);
#endif

/* Notch (camera housing) of the overlay's screen, in points from its left and
 * top edges; on a screen without a notch, left == right at the centre and
 * height is the menu bar's. */
void pet_mac_notch(float* left, float* right, float* height);
/* The black island window around the notch, created hidden on first use and
 * moved by later calls. */
void pet_mac_island_place(float left, float right, float height);
/* Polled on the main thread: whether the island should be showing. */
bool pet_island_wanted(void);

/* Menu bar side of the match flow. */
void pet_set_match_setup(const PetMatchSetup* setup);
bool pet_match_active(void);

/* The disc to boot in overlay mode: only one that passed the disc check.
 * The command line's disc, else the one the launcher last booted, if it is
 * remembered as checked; otherwise the player picks one (pet/ui). NULL if
 * they gave up. Other modes get command_line_disc back. */
const char* pet_choose_disc(const char* command_line_disc);
#ifdef __APPLE__
/* pet/ui/DiscMemory.swift: candidate may be NULL or "". */
const char* pet_ui_choose_disc(const char* candidate);
#endif
#ifdef __APPLE__
#include <stdint.h>
void pet_mac_write_png(const char* path, const uint8_t* rgba, int width, int height);
#endif
