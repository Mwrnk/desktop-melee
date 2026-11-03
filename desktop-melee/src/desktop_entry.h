/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Desktop Melee's .desktop file on Linux. The desktop matches windows to it
 * by app id (their icon in the taskbar), lists it among the apps, and the
 * portals only serve apps that have one (hotkey_portal.h). */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define DESKTOP_MELEE_APP_ID "io.github.mwrnk.DesktopMelee"

/* Writes ~/.local/share/applications/io.github.mwrnk.DesktopMelee.desktop
 * from resources/desktop-melee/io.github.mwrnk.DesktopMelee.desktop.in,
 * pointing at this game, when it is missing or points elsewhere. */
void desktop_melee_desktop_entry_install(void);

#ifdef __cplusplus
}
#endif
