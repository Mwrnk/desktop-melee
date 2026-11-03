/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The Linux overlay: an X11 window, through XWayland on Wayland desktops, so
 * the same window works on KDE, GNOME, X11 sessions and wlroots desktops
 * (docs/desktop-melee/porting.md section 5). X11 lets a window stay above
 * the others, let clicks through and keep out of the taskbar; a plain
 * Wayland window can't stay above. A native Wayland layer-shell overlay could
 * come later, for KDE and wlroots.
 *
 * Its transparency needs three things: a compositor, a 32-bit (ARGB) visual,
 * and a swap chain that isn't opaque, which aurora's Dawn won't make on
 * Vulkan without the layer in vk_overlay_alpha.c. */
#pragma once

#include <SDL3/SDL.h>
#include <stdbool.h>

/* Before aurora creates the window: picks SDL's X11 driver and an ARGB
 * visual, and loads the Vulkan layer. False, with the reason logged, when
 * there is no X server, no compositor, no ARGB visual or no layer: then the
 * game stays a normal window. The player's own SDL_VIDEO_DRIVER=wayland also
 * keeps a normal window. */
bool desktop_melee_x11_overlay_prepare(void);

/* After: lets clicks through (an empty input shape), keeps the window out of
 * the taskbar and the pager, and on every virtual desktop. */
void desktop_melee_x11_overlay_setup(SDL_Window* window);

/* Pause and hide: fully transparent, still drawing. */
void desktop_melee_x11_overlay_set_faded(SDL_Window* window, bool faded);
