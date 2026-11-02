/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Linux skeleton of desktop-melee/src/platform.h (issue #12). Nothing here is
 * Linux-specific yet: it is plain SDL, so CMake also builds it for any other
 * system without a port of its own.
 *
 * What runs today, with DESKTOP_MELEE_OVERLAY=1 and a disc:
 *   melee --dvd /path/to/melee.iso
 * The match plays in a normal, resizable window (not an overlay), the floor
 * sits a little above the window's bottom edge, there is no notch island,
 * and Pause and hide only pauses. Settings come from settings.cfg
 * (settings_file.h); a tray icon (tray_sdl.h) starts matches, pauses, quits
 * and opens the settings window (settings_window.h).
 *
 * docs/desktop-melee/porting.md walks through each function; every TODO
 * below names what a port would use. X11 and Wayland differ for most of it:
 * check SDL_GetCurrentVideoDriver() ("x11" or "wayland") at runtime, since
 * one build runs on both. */
#include "platform.h"

#include "settings_file.h"
#include "settings_window.h"
#include "tray_sdl.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_surface.h>

#include "disc_check.h"

/* ---- the overlay window ------------------------------------------------- */

void desktop_melee_platform_configure(AuroraConfig* config) {
    /* TODO: keep overlayWindow on once desktop_melee_platform_overlay_setup
     * makes the window click-through. Until then, a borderless window that
     * covers the screen and takes every click locks the player out. */
    config->overlayWindow = false;
    SDL_Log("desktop-melee: Linux skeleton: a normal window, not an overlay "
            "(docs/desktop-melee/porting.md)");
}

void desktop_melee_platform_overlay_setup(SDL_Window* window) {
    (void)window;
    /* TODO, X11:
     *   - transparency: SDL picks an ARGB (32-bit) visual for
     *     SDL_WINDOW_TRANSPARENT; it only shows through with a compositor
     *     running (an owner for the _NET_WM_CM_S<screen> selection). Without
     *     one, say so and stay a normal window.
     *   - click-through: an empty input shape, XShapeCombineRectangles(dpy,
     *     win, ShapeInput, 0, 0, NULL, 0, ShapeSet, Unsorted) (libXext), or
     *     XFixesSetWindowShapeRegion with an empty region.
     *   - stacking: _NET_WM_STATE_ABOVE, _NET_WM_STATE_STICKY (every desktop),
     *     _NET_WM_STATE_SKIP_TASKBAR and _NET_WM_STATE_SKIP_PAGER, and
     *     _NET_WM_WINDOW_TYPE_DOCK or _NOTIFICATION so it never takes focus.
     *   Get the Display* and Window from SDL_PROP_WINDOW_X11_DISPLAY_POINTER
     *   and SDL_PROP_WINDOW_X11_WINDOW_NUMBER.
     * TODO, Wayland:
     *   - a wlr-layer-shell surface (zwlr_layer_shell_v1) on the OVERLAY
     *     layer, anchored to all four edges, keyboard interactivity none;
     *     KDE Plasma and wlroots compositors (Sway, Hyprland) have it, GNOME
     *     does not, so GNOME stays a normal window.
     *   - click-through: wl_surface_set_input_region with an empty
     *     wl_region (SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER).
     *   SDL creates its own xdg_toplevel, so the layer surface likely needs
     *   SDL_PROP_WINDOW_CREATE_WAYLAND_SURFACE_ROLE_CUSTOM_BOOLEAN, set
     *   before aurora creates the window.
     * TODO: the once-a-second desktop_melee_trace_tick() the tests use, from
     * the main loop. */
}

bool desktop_melee_platform_window_covers_screen(void) {
    /* TODO: true once the window is an overlay. */
    return false;
}

void desktop_melee_platform_overlay_set_hidden(bool hidden) {
    /* TODO: SDL_HideWindow / SDL_ShowWindow, then click-through again.
     * Only the welcome window calls this, and there is none yet. */
    (void)hidden;
}

void desktop_melee_platform_overlay_set_faded(bool faded) {
    /* TODO: Pause and hide. SDL_SetWindowOpacity(window, 0 or 1)
     * (_NET_WM_WINDOW_OPACITY, needs the compositor) keeps the game
     * drawing; hiding or minimising the window may stall presentation. On
     * Wayland, unmap the layer surface's buffer or draw nothing instead. */
    (void)faded;
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
    int w = 0, h = 0;
    SDL_GetWindowSize(window, &w, &h);
    *width_pt = (float)w;
    *height_pt = (float)h;
    *floor_pt = 0.15f * (float)h;
}

bool desktop_melee_platform_dock_frame(
    float screen_w, float screen_h, float* left, float* right, float* top, bool* exact) {
    /* TODO: the bottom panel's frame. X11: find the window with
     * _NET_WM_WINDOW_TYPE_DOCK along the bottom edge (XQueryTree on the
     * root, then XGetWindowAttributes) and use its geometry, exact. A
     * full-width panel makes the whole bottom edge a floor, which is fine.
     * Wayland: other clients' surfaces are not visible to us; the most a
     * port can do is assume a full-width floor at the usable bounds. */
    (void)screen_w;
    (void)screen_h;
    (void)left;
    (void)right;
    (void)top;
    *exact = false;
    return false;
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
     * aurora's second window).
     * TODO (docs/desktop-melee/ui-guide.md):
     *   - the welcome window and "How to play".
     *   - the Pause and hide hotkey, from any app: XGrabKey on the root window
     *     (X11), or the org.freedesktop.portal.GlobalShortcuts portal
     *     (Wayland; KDE has it, GNOME only in recent releases). It calls
     *     desktop_melee_toggle_pause_and_hide(). */
    desktop_melee_settings_load();
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
    /* TODO: show progress while desktop_melee_pipelines_pending() > 0. */
    SDL_Log("desktop-melee: building graphics pipelines (the first run takes a while)");
}

void desktop_melee_platform_ui_loading_end(void) {}

const char* desktop_melee_platform_ui_choose_disc(const char* candidate) {
    /* TODO: the welcome window's disc step, with SDL_ShowOpenFileDialog
     * (xdg-desktop-portal), and remembering a disc that passed the check so
     * the next launch skips the hash. */
    if (candidate == NULL || candidate[0] == '\0') {
        SDL_Log("desktop-melee: no disc: start the game with --dvd /path/to/melee.iso");
        return NULL;
    }
    SDL_Log("desktop-melee: checking %s (a few seconds)", candidate);
    char message[256];
    const DesktopMeleeDiscState state =
        desktop_melee_disc_check_wait(candidate, message, (int)sizeof message);
    if (state != DESKTOP_MELEE_DISC_VERIFIED) {
        SDL_Log("desktop-melee: disc refused: %s", message);
        return NULL;
    }
    return candidate;
}

void desktop_melee_platform_ui_assets_ready(void) {
    /* TODO: the settings window may draw portraits and the font now. */
    desktop_melee_tray_assets_ready();
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
