/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The disc check: only a clean Melee USA 1.02 image passes. Runs melee-pc's
 * header check and full-image hash (src/pc/launcher_data.cpp) on a
 * background thread. */
#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DESKTOP_MELEE_DISC_CHECKING = 0,
    DESKTOP_MELEE_DISC_VERIFIED = 1,
    DESKTOP_MELEE_DISC_REJECTED = 2, /* the header check failed (PAL included) */
    DESKTOP_MELEE_DISC_MODIFIED = 3, /* the size or hash does not match the clean disc */
    DESKTOP_MELEE_DISC_ERROR = 4     /* the image could not be read */
} DesktopMeleeDiscState;

typedef struct DesktopMeleeDiscCheck DesktopMeleeDiscCheck;

/* Starts checking the image at path. */
DesktopMeleeDiscCheck* desktop_melee_disc_check_start(const char* path);
/* The state so far, the hash progress (0..100) and, once finished, a message
 * for the player. Never blocks. */
DesktopMeleeDiscState desktop_melee_disc_check_poll(
    DesktopMeleeDiscCheck* check, int* percent, char* message, int message_size);
/* Cancels the check if it is still running, waits for it, and frees it. */
void desktop_melee_disc_check_free(DesktopMeleeDiscCheck* check);
/* Checks the image at path and waits for the result, which can take a few
 * seconds (the full-image hash). For a UI that has nothing to show
 * meanwhile, such as the platform skeletons. */
DesktopMeleeDiscState desktop_melee_disc_check_wait(
    const char* path, char* message, int message_size);

#ifdef __cplusplus
}
#endif
