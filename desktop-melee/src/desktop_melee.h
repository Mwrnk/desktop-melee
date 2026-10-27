/* SPDX-License-Identifier: GPL-3.0-or-later */
/* desktop-melee host hooks, called from src/pc/main.c. */
#pragma once
#include <stdbool.h>

#include <SDL3/SDL_video.h>
#include <aurora/aurora.h>

#include "desktop_melee_game.h"

/* Before aurora_initialize: window and presentation settings. */
void desktop_melee_configure(AuroraConfig* config);
/* After aurora_initialize: platform window tweaks SDL has no API for. */
void desktop_melee_window_created(SDL_Window* window);
/* After the launcher applies saved preferences (pc_menu_init). */
void desktop_melee_after_menu_init(void);
/* Around the disc choice and melee-pc's launcher (its shader-cache wait):
 * the overlay stays fully transparent, so melee-pc's screen is never seen,
 * and the menu bar app shows the progress (desktop_melee_ui_loading_*). */
void desktop_melee_launcher_wait_begin(void);
void desktop_melee_launcher_wait_end(void);
#ifdef __APPLE__
/* desktop-melee/ui/Loading.swift. */
void desktop_melee_ui_loading_begin(void);
void desktop_melee_ui_loading_end(void);
#endif

#ifdef __APPLE__
void desktop_melee_overlay_mac_setup(SDL_Window* window);
/* Hide or show the overlay window (the welcome window hides it). */
void desktop_melee_mac_overlay_set_hidden(bool hidden);
/* Pause and hide: fully transparent while hidden, still drawing. */
void desktop_melee_mac_overlay_set_faded(bool faded);
/* desktop-melee/ui/MenuBar.swift: bring Desktop Melee forward with the game window
 * key, so keyboard play works. */
void desktop_melee_ui_focus_game(void);
/* The Swift menu bar app (desktop-melee/ui/MenuBar.swift); takes the game's NSWindow. */
void desktop_melee_ui_install(void* game_window);
#endif
/* Ask the game to shut down cleanly (the menu bar's Quit). */
void desktop_melee_request_quit(void);

/* Screen size and Dock top, in points, measured once at startup. */
void desktop_melee_set_screen_geometry(float width_pt, float height_pt, float floor_pt);
#ifdef __APPLE__
void desktop_melee_mac_screen_geometry(SDL_Window* window, float* width_pt, float* height_pt,
                             float* floor_pt);
/* The Dock bar on the overlay's screen, in points (y up from that screen's
 * bottom). False when the Dock is not on that screen. Exact when
 * Accessibility is granted, otherwise estimated from the Dock's settings. */
bool desktop_melee_mac_dock_frame(float screen_w, float screen_h, float* left, float* right,
                        float* top, bool* exact);
#endif

/* Notch (camera housing) of the overlay's screen, in points from its left and
 * top edges; on a screen without a notch, left == right at the centre and
 * height is the menu bar's. */
void desktop_melee_mac_notch(float* left, float* right, float* height);
/* The black island window around the notch, created hidden on first use and
 * moved by later calls. */
void desktop_melee_mac_island_place(float left, float right, float height);
/* Polled on the main thread: whether the island should be showing. */
bool desktop_melee_island_wanted(void);

/* Menu bar side of the match flow. */
void desktop_melee_set_match_setup(const DesktopMeleeMatchSetup* setup);
bool desktop_melee_match_active(void);

/* The disc to boot in overlay mode: only one that passed the disc check.
 * The command line's disc, else the one the launcher last booted, if it is
 * remembered as checked; otherwise the player picks one (desktop-melee/ui). NULL if
 * they gave up. Other modes get command_line_disc back. */
const char* desktop_melee_choose_disc(const char* command_line_disc);
#ifdef __APPLE__
/* desktop-melee/ui/DiscMemory.swift: candidate may be NULL or "". */
const char* desktop_melee_ui_choose_disc(const char* candidate);
#endif
#ifdef __APPLE__
#include <stdint.h>
void desktop_melee_mac_write_png(const char* path, const uint8_t* rgba, int width, int height);
#endif
