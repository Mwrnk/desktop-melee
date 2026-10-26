/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The C functions the Swift menu bar app calls into the game. */
#pragma once
#include <stdbool.h>

/* Sound settings (src/pc/launcher.cpp): volumes are 0..1. Set applies them
 * immediately; save writes launcher.cfg. */
void pc_prefs_get_audio(float* volume, float* music, float* sfx, bool* mute, bool* reverb);
void pc_prefs_set_audio(float volume, float music, float sfx, bool mute, bool reverb);
void pc_prefs_save(void);

/* Ask the game to shut down cleanly. */
void pet_request_quit(void);

/* Match setup and control (GM_PET): PetMatchSetup and friends. */
#include "../../src/pet_game.h"
void pet_set_match_setup(const PetMatchSetup* setup);
bool pet_match_active(void);

/* The character roster decoded from the game (pet/src/assets.c): Melee's
 * select grid order. Images are straight RGBA8, owned by the game side. */
#include <stdint.h>
int pet_roster_count(void);
bool pet_roster_entry(int i, int* ckind, int* costumes);
const uint8_t* pet_roster_icon(int i, int* w, int* h);
const uint8_t* pet_portrait(int ckind, int costume, int* w, int* h);

/* Melee's text font: character entries (Shift-JIS, atlas glyph, margins)
 * and the atlas glyphs' 32x32 coverage, one byte per pixel. */
int pet_font_glyph_count(void);
bool pet_font_glyph_info(int i, uint8_t* sjis_hi, uint8_t* sjis_lo, int* atlas, int* left,
                         int* right);
const uint8_t* pet_font_glyph_alpha(int i);

/* Named graphics from the game ("emblem", "melee"): coverage, one byte per
 * pixel, owned by the game side. */
const uint8_t* pet_named_alpha(const char* name, int* w, int* h);
