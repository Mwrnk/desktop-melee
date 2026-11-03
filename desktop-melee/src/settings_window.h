/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Desktop Melee's settings window on Windows and Linux: an RmlUi document
 * (desktop-melee/resources/settings.rml) in aurora's second window
 * (aurora::rmlui::open_window), with the tabs of
 * docs/desktop-melee/ui-guide.md sections 3 to 8. Main thread only. */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opens the window, or raises it when it is already open. */
bool desktop_melee_settings_window_open(void);
void desktop_melee_settings_window_close(void);

#ifdef __cplusplus
}
#endif
