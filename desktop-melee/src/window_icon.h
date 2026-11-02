/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The app icon (resources/app-icon.png, from macos/icon/AppIcon-dark.svg) on
 * the game's windows on Windows and Linux, where they would otherwise show
 * the desktop's placeholder. */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Sets the icon on every open window; call again after opening one. */
void desktop_melee_set_window_icons(void);

#ifdef __cplusplus
}
#endif
