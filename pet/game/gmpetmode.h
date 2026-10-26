/* SPDX-License-Identifier: GPL-3.0-or-later */
/* GM_PET: melee-pet's own game mode (overlay mode boots into it). An empty,
 * transparent idle scene; the menu bar (or Start on a pad) begins a VS match
 * on the Desktop stage, and the match returns to idle when it ends. */
#pragma once
#include <melee/gm/types.h>

extern GameModeState gm_Mode_Pet_States[];

void gm_Scene_PetIdle_OnFrame(void);
void gm_Scene_PetIdle_OnEnter(void* unused);
void gm_Scene_PetIdle_OnExit(void* unused);
