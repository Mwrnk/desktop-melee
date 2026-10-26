/* SPDX-License-Identifier: GPL-3.0-or-later */
/* pet_disc_check <path>: checks a disc image the way Desktop Melee does and
 * prints "<state>: <message>". Exit 0 verified, 1 rejected, 2 modified,
 * 3 error. Used by pet/tests/disc_check_test.sh. */
#include <stdio.h>
#include <time.h>

#include "disc_check.h"

/* The game's log (src/pc), which disc_open.c writes to; unused here. */
void pc_log_line(const char* fmt, ...) { (void)fmt; }

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: pet_disc_check <disc image>\n");
        return 3;
    }
    PetDiscCheck* check = pet_disc_check_start(argv[1]);
    char message[512];
    int percent = 0;
    PetDiscState state;
    const struct timespec tick = {0, 100 * 1000 * 1000};
    while ((state = pet_disc_check_poll(check, &percent, message, sizeof message)) ==
           PET_DISC_CHECKING) {
        nanosleep(&tick, NULL);
    }
    pet_disc_check_free(check);
    static const char* names[] = {"checking", "verified", "rejected", "modified", "error"};
    printf("%s: %s\n", names[state], message);
    return state == PET_DISC_VERIFIED ? 0 : state == PET_DISC_REJECTED ? 1
                                         : state == PET_DISC_MODIFIED  ? 2
                                                                       : 3;
}
