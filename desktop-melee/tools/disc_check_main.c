/* SPDX-License-Identifier: GPL-3.0-or-later */
/* pet_disc_check <path>: checks a disc image the way Desktop Melee does and
 * prints "<state>: <message>". Exit 0 verified, 1 rejected, 2 modified,
 * 3 error. With --cancel-after-ms N it frees the check N ms in, the way the
 * welcome window does when it closes, and exits 4 ("canceled").
 * Used by pet/tests/disc_check_test.sh. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "disc_check.h"

/* The game's log (src/pc), which disc_open.c writes to; unused here. */
void pc_log_line(const char* fmt, ...) { (void)fmt; }

int main(int argc, char** argv) {
    long cancel_ms = -1;
    if (argc == 4 && strcmp(argv[1], "--cancel-after-ms") == 0) {
        cancel_ms = strtol(argv[2], NULL, 10);
        argv += 2;
        argc -= 2;
    }
    if (argc != 2) {
        fprintf(stderr, "usage: pet_disc_check [--cancel-after-ms N] <disc image>\n");
        return 3;
    }
    PetDiscCheck* check = pet_disc_check_start(argv[1]);
    if (cancel_ms >= 0) {
        const struct timespec wait = {cancel_ms / 1000, (cancel_ms % 1000) * 1000 * 1000};
        nanosleep(&wait, NULL);
        const PetDiscState before = pet_disc_check_poll(check, NULL, NULL, 0);
        pet_disc_check_free(check);
        printf("canceled: freed while %s\n", before == PET_DISC_CHECKING ? "checking" : "finished");
        return 4;
    }
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
