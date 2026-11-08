/* SPDX-License-Identifier: GPL-3.0-or-later */
/* What's on the Windows taskbar: where Start, search, the apps and the
 * notification area are, so that stocks and percents can go in the space
 * left over (platform_windows.c, desktop_melee_platform_notch). */
#ifndef DESKTOP_MELEE_TASKBAR_WINDOWS_H
#define DESKTOP_MELEE_TASKBAR_WINDOWS_H

#ifdef __cplusplus
extern "C" {
#endif

/* A horizontal span, in screen pixels. */
typedef struct DesktopMeleeWinSpan {
    float left;
    float right;
} DesktopMeleeWinSpan;

/* The taskbar's buttons and its notification area, through UI Automation (the
 * Windows 11 taskbar draws its buttons itself, so they have no windows of
 * their own), unsorted. Returns how many it wrote, at most max; 0 when it
 * can't read the taskbar. Takes a few tens of milliseconds: explorer
 * answers each question. Main thread. */
int desktop_melee_win_taskbar_buttons(DesktopMeleeWinSpan* spans, int max);

#ifdef __cplusplus
}
#endif

#endif
