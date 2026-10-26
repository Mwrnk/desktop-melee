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
    PET_DISC_CHECKING = 0,
    PET_DISC_VERIFIED = 1,
    PET_DISC_REJECTED = 2, /* the header check failed (PAL included) */
    PET_DISC_MODIFIED = 3, /* the size or hash does not match the clean disc */
    PET_DISC_ERROR = 4     /* the image could not be read */
} PetDiscState;

typedef struct PetDiscCheck PetDiscCheck;

/* Starts checking the image at path. */
PetDiscCheck* pet_disc_check_start(const char* path);
/* The state so far, the hash progress (0..100) and, once finished, a message
 * for the player. Never blocks. */
PetDiscState pet_disc_check_poll(PetDiscCheck* check, int* percent, char* message,
                                 int message_size);
/* Cancels the check if it is still running, waits for it, and frees it. */
void pet_disc_check_free(PetDiscCheck* check);

#ifdef __cplusplus
}
#endif
