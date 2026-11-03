/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The player's windows on Linux, for window platforms
 * (desktop_melee_platform_windows in platform.h). */
#pragma once

#include "platform.h"

/* The windows bottom to top, in the overlay's coordinates (top left 0,0).
 * `overlay` is the overlay's frame on the desktop in SDL's coordinates
 * (X11's). KDE: KWin's list, which a small KWin script sends over D-Bus (it
 * alone sees native Wayland windows), mapped through the overlay's frame as
 * KWin sees it, so display scaling doesn't shift it. Elsewhere: X11's
 * _NET_CLIENT_LIST_STACKING, read again only after a change. */
int desktop_melee_linux_windows(
    DesktopMeleeWindow* out, int max, const DesktopMeleeWindow* overlay);

/* Unloads the KWin script (desktop_melee_platform_ui_before_exit). */
void desktop_melee_linux_windows_stop(void);
