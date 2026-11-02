/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The menu for the systems without the macOS menu bar app: an SDL3 tray icon
 * (a StatusNotifierItem on Linux, a notification-area icon on Windows) with
 * the actions of docs/desktop-melee/ui-guide.md section 14. Everything runs on
 * the main thread: SDL calls the menu's callbacks from SDL_PumpEvents. */
#pragma once

#include <stdbool.h>

/* Creates the icon and its menu: Start or restart match, Pause and hide,
 * Settings... (settings_window.h) and Quit. Returns false (and logs) when the
 * desktop has no tray, such as GNOME without the AppIndicator extension. */
bool desktop_melee_tray_install(void);

/* The game's assets are loaded: the menu may play Melee's menu sounds. */
void desktop_melee_tray_assets_ready(void);

void desktop_melee_tray_remove(void);
