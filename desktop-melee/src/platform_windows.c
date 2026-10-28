/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Windows skeleton of desktop-melee/src/platform.h (issue #11).
 *
 * What runs today, with DESKTOP_MELEE_OVERLAY=1 and a disc:
 *   melee.exe --dvd C:\path\to\melee.iso
 * The match plays in a normal, resizable window (not an overlay), the floor
 * is the window's bottom edge, there is no notch island, and Pause and hide
 * does nothing. Settings keep their defaults: there is no menu yet.
 *
 * docs/desktop-melee/porting.md walks through each function; every TODO
 * below names what a port would use. The HWND is
 * SDL_GetPointerProperty(SDL_GetWindowProperties(window),
 * SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL). SDL's window coordinates on
 * Windows are pixels, like the Win32 rectangles below; keep to one or the
 * other throughout. */
#include "platform.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_surface.h>

#include "disc_check.h"

/* ---- the overlay window ------------------------------------------------- */

void desktop_melee_platform_configure(AuroraConfig* config) {
    /* TODO: keep overlayWindow on once desktop_melee_platform_overlay_setup
     * makes the window click-through. Until then, a borderless topmost
     * window that covers the screen and takes every click locks the player
     * out. */
    config->overlayWindow = false;
    SDL_Log("desktop-melee: Windows skeleton: a normal window, not an overlay "
            "(docs/desktop-melee/porting.md)");
}

void desktop_melee_platform_overlay_setup(SDL_Window* window) {
    (void)window;
    /* TODO:
     *   - click-through: add WS_EX_LAYERED | WS_EX_TRANSPARENT to the
     *     extended style (GetWindowLongPtrW / SetWindowLongPtrW, GWL_EXSTYLE).
     *     With WS_EX_LAYERED, SetLayeredWindowAttributes(hwnd, 0, 255,
     *     LWA_ALPHA) keeps it visible.
     *   - out of the taskbar and Alt-Tab, and never activated by a click:
     *     WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE.
     *   - topmost: aurora's SDL_WINDOW_ALWAYS_ON_TOP is HWND_TOPMOST already.
     *   - per-pixel alpha is the hard part. aurora asks the WebGPU surface for
     *     premultiplied alpha, and a plain HWND swap chain (D3D12 or Vulkan)
     *     usually offers only opaque: aurora then logs "surface has no
     *     premultiplied alpha mode; it will be opaque". The usual way out is
     *     a DirectComposition swap chain (IDCompositionDevice, a visual on
     *     the HWND), which Dawn would have to be given; see porting.md.
     * TODO: the once-a-second desktop_melee_trace_tick() the tests use
     * (SetTimer, or from the main loop). */
}

bool desktop_melee_platform_window_covers_screen(void) {
    /* TODO: true once the window is an overlay. */
    return false;
}

void desktop_melee_platform_overlay_set_hidden(bool hidden) {
    /* TODO: SDL_HideWindow / SDL_ShowWindow (ShowWindow SW_HIDE /
     * SW_SHOWNOACTIVATE). Only the welcome window calls this, and there is
     * none yet. */
    (void)hidden;
}

void desktop_melee_platform_overlay_set_faded(bool faded) {
    /* TODO: Pause and hide. SetLayeredWindowAttributes(hwnd, 0, faded ? 0 :
     * 255, LWA_ALPHA) on the layered window (or SDL_SetWindowOpacity) keeps
     * the game drawing; a minimised window may stall presentation. */
    (void)faded;
}

/* ---- the screen --------------------------------------------------------- */

void desktop_melee_platform_screen_geometry(
    SDL_Window* window, float* width_pt, float* height_pt, float* floor_pt) {
    /* A normal window: it is the screen, and its bottom edge is the floor.
     * TODO, overlay: MonitorFromWindow + GetMonitorInfoW. rcMonitor is the
     * size; the floor is rcMonitor.bottom - rcWork.bottom, the taskbar's
     * height when it sits at the bottom (SDL_GetDisplayBounds and
     * SDL_GetDisplayUsableBounds report the same two rectangles). */
    int w = 0, h = 0;
    SDL_GetWindowSize(window, &w, &h);
    *width_pt = (float)w;
    *height_pt = (float)h;
    *floor_pt = 0.0f;
}

bool desktop_melee_platform_dock_frame(
    float screen_w, float screen_h, float* left, float* right, float* top, bool* exact) {
    /* TODO: SHAppBarMessage(ABM_GETTASKBARPOS, &data) gives the taskbar's
     * rectangle (data.rc) and edge (data.uEdge); only ABE_BOTTOM is a floor,
     * and ABM_GETSTATE says whether it auto-hides. That is the primary
     * monitor's taskbar; on other monitors, find the "Shell_SecondaryTrayWnd"
     * windows. The Windows 11 taskbar spans the whole width, so the floor
     * does too: exact = true. Convert data.rc to the monitor's own frame,
     * with y up from its bottom. */
    (void)screen_w;
    (void)screen_h;
    (void)left;
    (void)right;
    (void)top;
    *exact = false;
    return false;
}

void desktop_melee_platform_notch(float* left, float* right, float* height) {
    /* No island: Windows laptops have no notch. A port could still put one
     * in the middle of the top edge, as macOS does without a notch. */
    *left = 0.0f;
    *right = 0.0f;
    *height = 0.0f;
}

void desktop_melee_platform_island_place(float left, float right, float height) {
    /* Never called while desktop_melee_platform_notch reports height 0.
     * TODO, with an island: a second layered, click-through window just
     * below the overlay, black with rounded bottom corners (SetWindowRgn or
     * per-pixel alpha), polling desktop_melee_island_wanted() to fade. */
    (void)left;
    (void)right;
    (void)height;
}

/* ---- the settings UI ---------------------------------------------------- */

void desktop_melee_platform_ui_install(SDL_Window* window) {
    (void)window;
    /* TODO (docs/desktop-melee/ui-guide.md has the proposal):
     *   - the menu: SDL_CreateTray (a notification area icon,
     *     Shell_NotifyIconW), with Start match, Pause and hide, Settings and
     *     Quit.
     *   - settings: an RmlUi window, as melee-pc's launcher uses, saving a
     *     settings file that the game reads at startup.
     *   - the Pause and hide hotkey, from any app: RegisterHotKey(hwnd, id,
     *     MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, vk), and WM_HOTKEY through
     *     SDL_SetWindowsMessageHook. It calls
     *     desktop_melee_toggle_pause_and_hide(). */
    SDL_Log("desktop-melee: Windows skeleton: no menu yet; settings keep their defaults");
}

void desktop_melee_platform_ui_focus_game(void) {
    /* TODO: SetForegroundWindow on the game window. Windows only allows it
     * right after input the process received, which the hotkey's WM_HOTKEY
     * is; SDL_RaiseWindow does the same. */
}

void desktop_melee_platform_ui_loading_begin(void) {
    /* TODO: show progress while desktop_melee_pipelines_pending() > 0. */
    SDL_Log("desktop-melee: building graphics pipelines (the first run takes a while)");
}

void desktop_melee_platform_ui_loading_end(void) {}

const char* desktop_melee_platform_ui_choose_disc(const char* candidate) {
    /* TODO: the welcome window's disc step, with SDL_ShowOpenFileDialog
     * (IFileOpenDialog), and remembering a disc that passed the check so the
     * next launch skips the hash. */
    if (candidate == NULL || candidate[0] == '\0') {
        SDL_Log("desktop-melee: no disc: start the game with --dvd C:\\path\\to\\melee.iso");
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
