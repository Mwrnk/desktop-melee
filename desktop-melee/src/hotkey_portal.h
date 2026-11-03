/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The Pause and hide shortcut on Linux (docs/desktop-melee/ui-guide.md
 * section 12), through the org.freedesktop.portal.GlobalShortcuts portal
 * (KDE Plasma, GNOME 48 and later, some wlroots desktops): it works whichever
 * app is in front, on Wayland and on X11. The desktop asks the player to
 * confirm the shortcut once, and may let them pick another key, so the
 * settings window shows what was bound rather than what was asked for. */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Asks the portal for the shortcut (Control-Alt-M by default, as on macOS)
 * on a thread of its own; when it is pressed,
 * desktop_melee_toggle_pause_and_hide() runs on the main thread. False when
 * there is no session bus. If the desktop turns out to have no
 * GlobalShortcuts portal, `unavailable` (when not NULL) runs on the main
 * thread, for another way to the shortcut. */
bool desktop_melee_hotkey_install(void (*unavailable)(void));

/* What the desktop bound, for the settings window ("Ctrl+Alt+M"), or "" while
 * it hasn't answered, or if there's no portal. Main thread. */
const char* desktop_melee_hotkey_label(void);

/* Opens the desktop's own dialog to change the shortcut (the portal's
 * ConfigureShortcuts). False when the desktop has none. */
bool desktop_melee_hotkey_configure(void);

/* Stops the thread (desktop_melee_platform_ui_before_exit). */
void desktop_melee_hotkey_remove(void);

#ifdef __cplusplus
}
#endif
