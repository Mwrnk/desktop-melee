/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Linux's desktop-melee/src/platform.h (issue #12). Plain SDL apart from the
 * overlay (overlay_x11.c) and the hotkey (hotkey_portal.c), which CMake adds
 * only where X11 and D-Bus are found, so it also builds for any other system
 * without a port of its own.
 *
 * With DESKTOP_MELEE_OVERLAY=1 and a disc (melee --dvd /path/to/melee.iso)
 * the game is a transparent, click-through X11 overlay over the desktop,
 * through XWayland on Wayland desktops, with the floor on the panel; without
 * X11, a compositor or a 32-bit visual it is a normal window, the floor a
 * little above its bottom edge. There is no notch island. Settings come from
 * settings.cfg (settings_file.h); a tray icon (tray_sdl.h) starts matches,
 * pauses, quits and opens the settings window (settings_window.h).
 *
 * docs/desktop-melee/porting.md walks through each function; every TODO
 * below names what a port would use. */
#include "platform.h"

#include "settings_file.h"
#include "settings_window.h"
#include "tray_sdl.h"
#include "desktop_entry.h"
#include "hotkey_portal.h"
#include "melee_font.h"
#ifdef DESKTOP_MELEE_X11_OVERLAY
#include "overlay_x11.h"
#include "window_list_linux.h"
#endif
#include "welcome_window.h"
#include "window_icon.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_surface.h>

#include "disc_check.h"

/* ---- the overlay window ------------------------------------------------- */

/* The overlay window (overlay_x11.h), or a normal window when it can't be. */
static bool s_overlay = false;
static SDL_Window* s_window = NULL;

void desktop_melee_platform_configure(AuroraConfig* config) {
#ifdef DESKTOP_MELEE_X11_OVERLAY
    s_overlay = desktop_melee_x11_overlay_prepare();
#endif
    config->overlayWindow = s_overlay;
#ifdef DESKTOP_MELEE_HOTKEY_PORTAL
    /* Wayland's app_id and X11's WM_CLASS: the desktop finds the .desktop
     * file, and the taskbar its icon, by it. */
    SDL_SetHint(SDL_HINT_APP_ID, DESKTOP_MELEE_APP_ID);
#endif
    if (!s_overlay) {
        SDL_Log("desktop-melee: Linux: a normal window, not an overlay "
                "(docs/desktop-melee/porting.md)");
    }
}

void desktop_melee_platform_overlay_setup(SDL_Window* window) {
    s_window = window;
#ifdef DESKTOP_MELEE_X11_OVERLAY
    if (s_overlay) {
        desktop_melee_x11_overlay_setup(window);
    }
#endif
    /* TODO, Wayland: a native overlay through wlr-layer-shell
     * (zwlr_layer_shell_v1) for KDE and wlroots, sharper at fractional
     * scales and above panels; needs SDL's custom-role window, set where
     * aurora creates it (porting.md section 6).
     * TODO: the once-a-second desktop_melee_trace_tick() the tests use, from
     * the main loop. */
}

bool desktop_melee_platform_window_covers_screen(void) {
    return s_overlay;
}

void desktop_melee_platform_overlay_set_hidden(bool hidden) {
    /* Only the macOS welcome window calls this so far. */
    if (s_overlay && s_window != NULL) {
        if (hidden) {
            SDL_HideWindow(s_window);
        } else {
            SDL_ShowWindow(s_window);
#ifdef DESKTOP_MELEE_X11_OVERLAY
            desktop_melee_x11_overlay_setup(s_window); /* a new window maps without them */
#endif
        }
    }
}

void desktop_melee_platform_overlay_set_faded(bool faded) {
    /* Pause and hide: transparent, still drawing, since a hidden window may
     * stall presentation and with it the game loop. */
#ifdef DESKTOP_MELEE_X11_OVERLAY
    if (s_overlay && s_window != NULL) {
        desktop_melee_x11_overlay_set_faded(s_window, faded);
    }
#else
    (void)faded;
#endif
}

/* ---- the screen --------------------------------------------------------- */

void desktop_melee_platform_screen_geometry(
    SDL_Window* window, float* width_pt, float* height_pt, float* floor_pt) {
    /* A normal window: it is the screen. The floor sits a little above its
     * bottom edge, because the bottom edge is the bottom blastzone: a floor
     * at 0 would KO fighters as soon as they land.
     * TODO, overlay: SDL_GetDisplayBounds of SDL_GetDisplayForWindow for the
     * size. The floor is how far SDL_GetDisplayUsableBounds stops above the
     * bottom: on X11 that comes from _NET_WORKAREA (one rectangle for all
     * monitors, so check each panel's _NET_WM_STRUT_PARTIAL for multi-monitor
     * setups); Wayland has no work area, so use the layer-shell exclusive
     * zones the compositor reports, or 0. */
    if (s_overlay) {
        /* The overlay's screen; the floor is the panel along its bottom, from
         * the work area (X11's _NET_WORKAREA, which SDL reads). */
        const SDL_DisplayID display = SDL_GetDisplayForWindow(window);
        SDL_Rect bounds;
        SDL_Rect usable;
        if (SDL_GetDisplayBounds(display, &bounds) && SDL_GetDisplayUsableBounds(display, &usable))
        {
            *width_pt = (float)bounds.w;
            *height_pt = (float)bounds.h;
            const int gap = (bounds.y + bounds.h) - (usable.y + usable.h);
            *floor_pt = gap > 0 ? (float)gap : 0.0f;
            return;
        }
    }
    int w = 0, h = 0;
    SDL_GetWindowSize(window, &w, &h);
    *width_pt = (float)w;
    *height_pt = (float)h;
    *floor_pt = 0.15f * (float)h;
}

bool desktop_melee_platform_dock_frame(
    float screen_w, float screen_h, float* left, float* right, float* top, bool* exact) {
    /* The overlay: Linux panels run the full width of the screen, so the
     * floor does too (the stage stretched edge to edge): no gaps to fall
     * through, the blastzones only up and to the sides. Its top stays where
     * screen_geometry put it (*top comes in as that floor).
     * TODO: docks narrower than the screen (GNOME's dash, Plank) would make
     * better floors: X11's _NET_WM_WINDOW_TYPE_DOCK windows along the bottom. */
    (void)screen_h;
    (void)top;
    if (!s_overlay) {
        *exact = false;
        return false;
    }
    /* A little inside the screen's sides, where the blastzones are: a fighter
     * at the very edge would already be past one. */
    const float margin = 40.0f;
    *left = margin;
    *right = screen_w - margin;
    *exact = true;
    return true;
}

void desktop_melee_platform_notch(float* left, float* right, float* height) {
    /* No island: Linux laptops with a notch are rare. A port could still put
     * one in the middle of the top edge, as macOS does without a notch. */
    *left = 0.0f;
    *right = 0.0f;
    *height = 0.0f;
}

void desktop_melee_platform_island_place(float left, float right, float height) {
    /* Never called while desktop_melee_platform_notch reports height 0.
     * TODO, with an island: a second click-through window just below the
     * overlay, black with rounded bottom corners, polling
     * desktop_melee_island_wanted() to fade in and out. */
    (void)left;
    (void)right;
    (void)height;
}

/* ---- the settings UI ---------------------------------------------------- */

void desktop_melee_platform_ui_install(SDL_Window* window) {
    (void)window;
    /* The settings file, then the tray (tray_sdl.c: a StatusNotifierItem;
     * GNOME shows it only with the AppIndicator extension), whose Settings...
     * opens the settings window (settings_window.cpp, an RmlUi document in
     * aurora's second window). The Pause and hide hotkey comes from the
     * GlobalShortcuts portal (hotkey_portal.c), which wants an installed
     * .desktop file (desktop_entry.c).
     * TODO: XGrabKey on the root window for X11 sessions without the portal
     * (porting.md section 5). */
    desktop_melee_settings_load();
    desktop_melee_set_window_icons();
#ifdef DESKTOP_MELEE_HOTKEY_PORTAL
    desktop_melee_desktop_entry_install(); /* before the portal: it asks for it */
    desktop_melee_hotkey_install();
#endif
    /* Without a tray (GNOME without the AppIndicator extension) the menu is
     * unreachable: open the settings window instead, which can start
     * matches and quit too. */
    if (!desktop_melee_tray_install()) {
        desktop_melee_settings_window_open();
    }
}

void desktop_melee_platform_ui_focus_game(void) {
    /* TODO: SDL_RaiseWindow on the game window. On Wayland a client cannot
     * take focus by itself; xdg-activation needs a token from the hotkey. */
}

void desktop_melee_platform_ui_loading_begin(void) {
    SDL_Log("desktop-melee: building graphics pipelines (the first run takes a while)");
    desktop_melee_welcome_loading_begin();
}

void desktop_melee_platform_ui_loading_end(void) {
    desktop_melee_welcome_loading_end();
}

/* The welcome window (welcome_window.cpp): the disc step, or nothing when the
 * disc passed the check before (settings.cfg's disc.verified).
 * DESKTOP_MELEE_WELCOME=1 forces the first run, =disc the disc step. */
const char* desktop_melee_platform_ui_choose_disc(const char* candidate) {
    return desktop_melee_welcome_choose_disc(candidate, SDL_getenv("DESKTOP_MELEE_WELCOME"));
}

void desktop_melee_platform_ui_assets_ready(void) {
    desktop_melee_font_assets_ready(); /* the windows' text in Melee's font */
    desktop_melee_tray_assets_ready();
    desktop_melee_welcome_assets_ready();
}

/* The RmlUi windows close while RmlUi is whole (platform.h). */
void desktop_melee_platform_ui_before_exit(void) {
    desktop_melee_welcome_close();
    desktop_melee_settings_window_close();
    desktop_melee_tray_remove();
#ifdef DESKTOP_MELEE_HOTKEY_PORTAL
    desktop_melee_hotkey_remove();
#endif
#ifdef DESKTOP_MELEE_X11_OVERLAY
    desktop_melee_linux_windows_stop(); /* unloads the KWin script */
#endif
}

/* ---- window platforms ---------------------------------------------------- */

int desktop_melee_platform_windows(DesktopMeleeWindow* out, int max) {
#ifdef DESKTOP_MELEE_X11_OVERLAY
    if (s_overlay && s_window != NULL) {
        int x = 0;
        int y = 0;
        SDL_GetWindowPosition(s_window, &x, &y);
        return desktop_melee_linux_windows(out, max, (float)x, (float)y);
    }
#endif
    (void)out;
    (void)max;
    return 0;
}

/* ---- files -------------------------------------------------------------- */

void desktop_melee_platform_write_png(
    const char* path, const uint8_t* rgba, int width, int height) {
    SDL_Surface* surface =
        SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGBA32, (void*)rgba, width * 4);
    if (surface == NULL || !SDL_SavePNG(surface, path)) {
        SDL_Log("desktop-melee: dump: %s: %s", path, SDL_GetError());
    }
    SDL_DestroySurface(surface);
}
