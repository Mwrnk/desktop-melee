/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The player's windows on Linux, for window platforms
 * (desktop_melee_platform_windows in platform.h). */
#pragma once

#include "platform.h"

/* The windows bottom to top, relative to (origin_x, origin_y), the overlay's
 * top left on the desktop. KDE: KWin's list, which a small KWin script sends
 * over D-Bus (it alone sees native Wayland windows). Elsewhere: X11's
 * _NET_CLIENT_LIST_STACKING. */
int desktop_melee_linux_windows(
    DesktopMeleeWindow* out, int max, float origin_x, float origin_y);

/* Unloads the KWin script (desktop_melee_platform_ui_before_exit). */
void desktop_melee_linux_windows_stop(void);
