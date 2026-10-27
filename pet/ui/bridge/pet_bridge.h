/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The C functions the Swift menu bar app calls into the game. */
#pragma once
#include <stdbool.h>

/* Sound settings (src/pc/launcher.cpp): volumes are 0..1. Set applies them
 * immediately; save writes launcher.cfg. */
void pc_prefs_get_audio(float* volume, float* music, float* sfx, bool* mute, bool* reverb);
void pc_prefs_set_audio(float volume, float music, float sfx, bool mute, bool reverb);
void pc_prefs_save(void);

/* The disc check (pet/src/disc_check.cpp). */
#include "../../src/disc_check.h"

/* Hide or show the game overlay (pet/src/overlay_mac.m). Hidden while the
 * welcome window waits for a disc: nothing is drawn yet, and a full-screen
 * window there only gets in the way. */
void pet_mac_overlay_set_hidden(bool hidden);

/* The welcome window (pet/ui/Welcome.swift): buttons held on a controller
 * port (HSD_PAD_* bits; pet/game/gmpetmode.c) and a keyboard binding's key
 * name (pet.c). pet_set_start_starts_match is in pet_game.h. */
#include <stdint.h>
uint32_t pet_pad_buttons(int port);
const char* pet_key_name(int slot);

/* Accessibility, for the exact Dock size: granted?, ask (system dialog),
 * re-measure the Dock now (or after the running match), and whether the
 * last measure was exact (pet/src/overlay_mac.m, pet.c). */
bool pet_ax_trusted(void);
void pet_ax_request(void);
void pet_relayout(void);
bool pet_dock_exact(void);

/* Pause and hide (pet_game.h: pet_toggle_pause_and_hide, pet_hidden). */

/* Ask the game to shut down cleanly. */
void pet_request_quit(void);

/* Match setup and control (GM_PET): PetMatchSetup and friends. */
#include "../../src/pet_game.h"
void pet_set_match_setup(const PetMatchSetup* setup);
bool pet_match_active(void);

/* Per-player controls (pet/src/pet.c): tap jump, and each Melee button's
 * PET_BTN_* target (PET_BTN_NONE unbinds it). Apply immediately. */
void pet_set_tap_jump(int player, bool on);
void pet_set_button_map(int player, int button, int target);
/* What is plugged into controller port 0..3 ("" when nothing is). */
const char* pet_port_device(int port);

/* Display (pet/src/pet.c). Fighter size in percent (100 = 490 Melee units
 * across the screen) and the notch island apply at the next match; size is
 * locked when MELEE_PET_SCALE is set. The screen is chosen by name (as
 * NSScreen.localizedName reports it; "" = the primary) and moves the overlay
 * right away, or when the running match ends. */
void pet_set_fighter_size(float percent);
bool pet_fighter_size_locked(void);
void pet_set_screen(const char* name);
/* Effects (pet/src/pet.c): screen shake on or off, immediately; the timer
 * mode (PET_TIMER_*, pet_game.h) too. */
void pet_set_shake(bool on);

/* UCF (src/pc/launcher.cpp): Universal Controller Fix. Applies immediately;
 * locked is true when MELEE_UCF in the environment decides it. */
void pc_prefs_get_ucf(bool* on, bool* locked);
void pc_prefs_set_ucf(bool on);

/* The character roster decoded from the game (pet/src/assets.c): Melee's
 * select grid order. Images are straight RGBA8, owned by the game side. */
#include <stdint.h>
int pet_roster_count(void);
bool pet_roster_entry(int i, int* ckind, int* costumes);
const uint8_t* pet_roster_icon(int i, int* w, int* h);
const uint8_t* pet_portrait(int ckind, int costume, int* w, int* h);
/* Costumes a character kind has; 0 until the roster is loaded. */
int pet_costume_count(int ckind);

/* Melee's text font: character entries (Shift-JIS, atlas glyph, margins)
 * and the atlas glyphs' 32x32 coverage, one byte per pixel. */
int pet_font_glyph_count(void);
bool pet_font_glyph_info(int i, uint8_t* sjis_hi, uint8_t* sjis_lo, int* atlas, int* left,
                         int* right);
const uint8_t* pet_font_glyph_alpha(int i);

/* Named graphics from the game ("emblem", "melee"): coverage, one byte per
 * pixel, owned by the game side. */
const uint8_t* pet_named_alpha(const char* name, int* w, int* h);
