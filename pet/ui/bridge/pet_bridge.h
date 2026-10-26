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
