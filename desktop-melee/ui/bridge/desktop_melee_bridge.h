/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The C functions the Swift menu bar app calls into the game. */
#pragma once
#include <stdbool.h>

/* Sound settings (src/pc/launcher.cpp): volumes are 0..1. Set applies them
 * immediately; save writes launcher.cfg. */
void pc_prefs_get_audio(float* volume, float* music, float* sfx, bool* mute, bool* reverb);
void pc_prefs_set_audio(float volume, float music, float sfx, bool mute, bool reverb);
void pc_prefs_save(void);

/* The disc check (desktop-melee/src/disc_check.cpp). */
#include "../../src/disc_check.h"

/* Hide or show the game overlay (desktop-melee/src/platform.h). Hidden while the
 * welcome window waits for a disc: nothing is drawn yet, and a full-screen
 * window there only gets in the way. */
void desktop_melee_platform_overlay_set_hidden(bool hidden);

/* The welcome window (desktop-melee/ui/Welcome.swift): buttons held on a controller
 * port (HSD_PAD_* bits; desktop-melee/game/gmdesktopmelee.c) and a keyboard binding's key
 * name (desktop_melee.c). desktop_melee_set_start_starts_match is in desktop_melee_game.h. */
#include <stdint.h>
uint32_t desktop_melee_pad_buttons(int port);
const char* desktop_melee_key_name(int slot);

/* Accessibility, for the exact Dock size: granted?, ask (system dialog),
 * re-measure the Dock now (or after the running match), and whether the
 * last measure was exact (desktop-melee/src/overlay_mac.m, desktop_melee.c). */
bool desktop_melee_ax_trusted(void);
void desktop_melee_ax_request(void);
void desktop_melee_relayout(void);
bool desktop_melee_dock_exact(void);

/* Pause and hide (desktop_melee_game.h: desktop_melee_toggle_pause_and_hide,
 * desktop_melee_hidden). */

/* Ask the game to shut down cleanly. */
void desktop_melee_request_quit(void);

/* Graphics pipelines melee-pc's launcher still waits for before the game
 * starts (desktop-melee/src/desktop_melee.c): the loading progress. */
unsigned desktop_melee_pipelines_pending(void);

/* Match setup and control (GM_DESKTOP_MELEE): DesktopMeleeMatchSetup and friends. */
#include "../../src/desktop_melee_game.h"
void desktop_melee_set_match_setup(const DesktopMeleeMatchSetup* setup);
bool desktop_melee_match_active(void);

/* Per-player controls (desktop-melee/src/desktop_melee.c): tap jump, and each Melee button's
 * DESKTOP_MELEE_BTN_* target (DESKTOP_MELEE_BTN_NONE unbinds it). Apply immediately. */
void desktop_melee_set_tap_jump(int player, bool on);
void desktop_melee_set_button_map(int player, int button, int target);
/* What is plugged into controller port 0..3 ("" when nothing is). */
const char* desktop_melee_port_device(int port);

/* Display (desktop-melee/src/desktop_melee.c). Fighter size in percent (100 = 490 Melee units
 * across the screen) and the notch island apply at the next match; size is
 * locked when DESKTOP_MELEE_SCALE is set. The screen is chosen by name (as
 * NSScreen.localizedName reports it; "" = the primary) and moves the overlay
 * right away, or when the running match ends. */
void desktop_melee_set_fighter_size(float percent);
bool desktop_melee_fighter_size_locked(void);
void desktop_melee_set_screen(const char* name);
/* Effects (desktop-melee/src/desktop_melee.c): screen shake on or off, immediately; the timer
 * mode (DESKTOP_MELEE_TIMER_*, desktop_melee_game.h) too. */
void desktop_melee_set_shake(bool on);

/* UCF (src/pc/launcher.cpp): Universal Controller Fix. Applies immediately;
 * locked is true when MELEE_UCF in the environment decides it. */
void pc_prefs_get_ucf(bool* on, bool* locked);
void pc_prefs_set_ucf(bool on);

/* The character roster decoded from the game (desktop-melee/src/assets.c): Melee's
 * select grid order. Images are straight RGBA8, owned by the game side. */
#include <stdint.h>
int desktop_melee_roster_count(void);
bool desktop_melee_roster_entry(int i, int* ckind, int* costumes);
const uint8_t* desktop_melee_roster_icon(int i, int* w, int* h);
const uint8_t* desktop_melee_portrait(int ckind, int costume, int* w, int* h);
/* Costumes a character kind has; 0 until the roster is loaded. */
int desktop_melee_costume_count(int ckind);

/* Melee's text font: character entries (Shift-JIS, atlas glyph, margins)
 * and the atlas glyphs' 32x32 coverage, one byte per pixel. */
int desktop_melee_font_glyph_count(void);
bool desktop_melee_font_glyph_info(
    int i, uint8_t* sjis_hi, uint8_t* sjis_lo, int* atlas, int* left, int* right);
const uint8_t* desktop_melee_font_glyph_alpha(int i);

/* Named graphics from the game ("emblem", "melee"): coverage, one byte per
 * pixel, owned by the game side. */
const uint8_t* desktop_melee_named_alpha(const char* name, int* w, int* h);
