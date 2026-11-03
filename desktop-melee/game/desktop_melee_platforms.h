/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Window platforms: the tops of the player's other windows become
 * drop-through platforms on the overlay's stage, like Battlefield's, moving
 * with the windows (game code, built with the game; desktop_melee.c finds
 * the windows). */
#ifndef DESKTOP_MELEE_PLATFORMS_H
#define DESKTOP_MELEE_PLATFORMS_H

#include <melee/mp/forward.h>

/* At most this many at once. Melee's collision is fixed when a stage loads,
 * so the platforms exist from the start, hidden until a window has a top
 * for them. */
#define DESKTOP_MELEE_WINDOW_PLATFORMS 8

/* mpLibLoad: the stage's collision with the platforms added, or `coll_data`
 * itself outside the overlay. `scale` is the stage's collision scale. */
MapCollData* desktop_melee_platforms_extend(MapCollData* coll_data, float scale);

/* Once a match frame: each platform to its window's top, or hidden. */
void desktop_melee_platforms_frame(void);

#endif
