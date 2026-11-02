/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Desktop Melee's welcome window on Windows and Linux: an RmlUi document
 * (desktop-melee/resources/welcome.rml) in aurora's second window, with the
 * steps of docs/desktop-melee/ui-guide.md sections 9 to 11. Main thread
 * only. It shares aurora's one second window with the settings window: only
 * one of them is open at a time. */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* desktop_melee_platform_ui_choose_disc: the disc to boot, or NULL to quit.
 * Returns the candidate straight away when it is the disc that passed the
 * check before; otherwise runs the welcome window's first steps (or just the
 * disc step) until the player continues or closes it. Blocks, running its
 * own event and frame loop: the game hasn't started yet. force is NULL, or
 * "disc" to open on the disc step, or anything else to force the first run
 * (DESKTOP_MELEE_WELCOME, for testing). */
const char* desktop_melee_welcome_choose_disc(const char* candidate, const char* force);

/* Around melee-pc's launcher, which builds the graphics pipelines. On the
 * first run the welcome window shows the progress; later, a small loading
 * window appears when there is a lot left to build. */
void desktop_melee_welcome_loading_begin(void);
void desktop_melee_welcome_loading_end(void);

/* The game's assets are loaded: the first run moves on to the controller
 * step. */
void desktop_melee_welcome_assets_ready(void);

/* "How to play": the welcome window from the controller step. */
void desktop_melee_welcome_how_to_play(void);

/* Open, and in a step that can't give the window up (the disc or the
 * loading progress). */
bool desktop_melee_welcome_busy(void);
bool desktop_melee_welcome_is_open(void);
void desktop_melee_welcome_close(void);

#ifdef __cplusplus
}
#endif
