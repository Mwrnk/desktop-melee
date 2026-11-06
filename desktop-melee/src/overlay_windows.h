/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The Windows overlay (docs/desktop-melee/porting.md section 4): a topmost,
 * click-through window whose frames are composited over the desktop with
 * their alpha.
 *
 * aurora's Dawn makes every D3D swap chain opaque (DXGI_ALPHA_MODE_IGNORE,
 * through CreateSwapChainForHwnd), and a swap chain made for a window can't
 * be anything else. So, as the Linux overlay's Vulkan layer does, this
 * steps in where the swap chain is made: for the overlay's window only,
 * IDXGIFactory2::CreateSwapChainForHwnd makes a premultiplied swap chain for
 * DirectComposition instead and shows it on the window through a
 * composition visual. Dawn presents to it as to any other. */
#pragma once

#include <SDL3/SDL_video.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Before aurora creates the window: steps in where DXGI makes swap chains.
 * False, with the reason logged, when that can't be done: the game then
 * stays a normal window. */
bool desktop_melee_win_overlay_prepare(void);

/* Whether the overlay's swap chain was made for composition, with alpha.
 * Known once aurora has configured the window's surface. */
bool desktop_melee_win_overlay_transparent(void);

/* After: clicks through to the windows underneath (WS_EX_LAYERED |
 * WS_EX_TRANSPARENT), never activated, out of the taskbar and Alt-Tab,
 * topmost. Run again after showing the window. */
void desktop_melee_win_overlay_setup(SDL_Window* window);

/* Pause and hide: fully transparent, still drawing. */
void desktop_melee_win_overlay_set_faded(SDL_Window* window, bool faded);

/* Pause and hide from any app: Ctrl+Alt+M (RegisterHotKey), the same default
 * as macOS. WM_HOTKEY arrives in the main thread's queue, where SDL's
 * message hook sees it, and runs desktop_melee_toggle_pause_and_hide().
 * False when another app holds the key. Main thread. */
bool desktop_melee_win_hotkey_install(void);

/* "Ctrl+Alt+M" while it is registered, else "". */
const char* desktop_melee_win_hotkey_label(void);

/* Unregisters it. */
void desktop_melee_win_hotkey_remove(void);

#ifdef __cplusplus
}
#endif
