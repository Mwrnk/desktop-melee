/* SPDX-License-Identifier: GPL-3.0-or-later */
/* melee-pet hooks called from the decompiled game code (melee_game). */
#pragma once
#include <stdbool.h>

/* MELEE_PET_OVERLAY=1: run as a transparent, click-through window over the
 * whole primary display instead of a normal window. */
bool pet_overlay_enabled(void);

/* Desktop overlay: stage and background models are not drawn, so only the
 * fighters, effects and HUD appear over the desktop. */
bool pet_hide_stage(void);

/* Fixed desktop camera, replacing Melee's zooming one in overlay mode. Fills
 * the eye and interest points (Melee units) and the vertical field of view
 * (degrees) so that Melee y = 0 sits on top of the Dock, x = 0 is the middle
 * of the screen, and the screen is MELEE_PET_SCALE units wide (default 490,
 * Final Destination's blastzone width). False when not in overlay mode. */
bool pet_camera(float* eye_x, float* eye_y, float* eye_z, float* interest_x,
                float* interest_y, float* interest_z, float* fov);

/* The Dock's left and right edges in Melee units (x), for stretching the main
 * stage floor across it. False when not in overlay mode. */
bool pet_dock_span(float* left, float* right);
