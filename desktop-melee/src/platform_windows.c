/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Windows's desktop-melee/src/platform.h (issue #11).
 *
 * With DESKTOP_MELEE_OVERLAY=1 the game is a transparent, click-through,
 * topmost window over the desktop (overlay_windows.h), with the floor on the
 * taskbar, end to end, and the tops of the player's windows as drop-through
 * platforms. Settings come from settings.cfg (settings_file.h); a
 * notification area icon (tray_sdl.h) starts matches, pauses, quits and
 * opens the settings window (settings_window.h). Pause and hide is
 * Ctrl+Alt+M. When the overlay can't be made transparent, the game falls
 * back to a normal window with the floor a little above its bottom edge.
 * There is no notch island.
 *
 * docs/desktop-melee/porting.md section 4 explains each part. SDL's window
 * and display coordinates on Windows are pixels, like the Win32 rectangles
 * below. */
#include "platform.h"

#include <windows.h>

#include <dwmapi.h>
#include <shellapi.h>
#include <wchar.h>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_surface.h>

#include "desktop_melee_game.h"
#include "melee_font.h"
#include "overlay_windows.h"
#include "settings_file.h"
#include "settings_window.h"
#include "tray_sdl.h"
#include "welcome_window.h"
#include "window_icon.h"

/* ---- the overlay window ------------------------------------------------- */

/* The overlay window (overlay_windows.h), or a normal window when it can't be. */
static bool s_overlay = false;
static SDL_Window* s_window = NULL;

void desktop_melee_platform_configure(AuroraConfig* config) {
    s_overlay = desktop_melee_win_overlay_prepare();
    config->overlayWindow = s_overlay;
    if (!s_overlay) {
        SDL_Log("desktop-melee: Windows: a normal window, not an overlay "
                "(docs/desktop-melee/porting.md)");
    }
}

/* The overlay's swap chain turned out opaque: a full-screen black window,
 * even a click-through one, would hide the desktop. Make it a normal one. */
static void become_normal_window(SDL_Window* window) {
    s_overlay = false;
    SDL_Log("desktop-melee: Windows: the overlay can't be transparent here, so a normal window");
    SDL_SetWindowAlwaysOnTop(window, false);
    SDL_SetWindowBordered(window, true);
    SDL_SetWindowResizable(window, true);
    SDL_SetWindowSize(window, 1280, 720);
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
}

/* Once a second, on the main thread (SDL dispatches WM_TIMER): the trace
 * lines the tests read (DESKTOP_MELEE_TRACE=1). */
static void CALLBACK trace_timer(HWND hwnd, UINT message, UINT_PTR id, DWORD time) {
    (void)hwnd;
    (void)message;
    (void)id;
    (void)time;
    desktop_melee_trace_tick();
}

void desktop_melee_platform_overlay_setup(SDL_Window* window) {
    s_window = window;
    if (s_overlay) {
        if (desktop_melee_win_overlay_transparent()) {
            desktop_melee_win_overlay_setup(window);
        } else {
            become_normal_window(window);
        }
    }
    if (desktop_melee_trace_enabled()) {
        SetTimer(NULL, 0, 1000, trace_timer);
    }
}

bool desktop_melee_platform_window_covers_screen(void) {
    return s_overlay;
}

void desktop_melee_platform_overlay_set_hidden(bool hidden) {
    /* The welcome window hides the overlay while it waits for a disc. */
    if (s_overlay && s_window != NULL) {
        if (hidden) {
            SDL_HideWindow(s_window);
        } else {
            SDL_ShowWindow(s_window);
            desktop_melee_win_overlay_setup(s_window);
        }
    }
}

void desktop_melee_platform_overlay_set_faded(bool faded) {
    /* Pause and hide: transparent, still drawing, since a hidden window may
     * stall presentation and with it the game loop. */
    if (s_overlay && s_window != NULL) {
        desktop_melee_win_overlay_set_faded(s_window, faded);
    }
}

/* ---- the screen --------------------------------------------------------- */

void desktop_melee_platform_screen_geometry(
    SDL_Window* window, float* width_pt, float* height_pt, float* floor_pt) {
    /* The overlay: its monitor (rcMonitor), with the floor where the work
     * area (rcWork) stops above the bottom edge, the taskbar's height when
     * it sits there. A normal window: it is the screen, and the floor sits a
     * little above its bottom edge, which is the bottom blastzone. */
    if (s_overlay) {
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

/* The taskbar's frame on the overlay's monitor, in that monitor's frame with
 * y up from its bottom. Only a bottom taskbar that stays shown counts:
 * ABM_GETTASKBARPOS is the primary monitor's taskbar, which is where the
 * overlay goes unless the player picks another screen. */
static bool taskbar_frame(const SDL_Rect* monitor, float* left, float* right, float* top) {
    APPBARDATA data = {.cbSize = sizeof data};
    if (!SHAppBarMessage(ABM_GETTASKBARPOS, &data) || data.uEdge != ABE_BOTTOM) {
        return false;
    }
    APPBARDATA state = {.cbSize = sizeof state};
    if ((SHAppBarMessage(ABM_GETSTATE, &state) & ABS_AUTOHIDE) != 0) {
        return false;
    }
    const RECT rc = data.rc;
    const int monitor_right = monitor->x + monitor->w;
    const int monitor_bottom = monitor->y + monitor->h;
    if (rc.left < monitor->x || rc.right > monitor_right || rc.top < monitor->y ||
        rc.bottom > monitor_bottom + 8 || rc.bottom < monitor_bottom - 8)
    {
        return false; /* another monitor's */
    }
    *left = (float)(rc.left - monitor->x);
    *right = (float)(rc.right - monitor->x);
    *top = (float)(monitor_bottom - rc.top);
    return *top > 0.0f && *right > *left;
}

bool desktop_melee_platform_dock_frame(
    float screen_w, float screen_h, float* left, float* right, float* top, bool* exact) {
    /* The whole taskbar is ground. The Windows 11 taskbar spans the screen,
     * so the floor runs on past both edges, out of sight: the side
     * blastzones sit at the screen's edges, and a floor ending there would
     * let a fighter drop down its side, below the taskbar, to the bottom
     * blastzone. Past the edges, the side blastzones are the only way off.
     * Without a measured taskbar (it hides, sits on a side, or is on
     * another monitor) the floor runs the same way, at the height
     * screen_geometry gave (*top comes in as it). */
    (void)screen_h;
    if (!s_overlay || s_window == NULL) {
        *exact = false;
        return false;
    }
    const float past_edge = 0.25f * screen_w;
    SDL_Rect monitor;
    float bar_left = 0.0f;
    float bar_right = 0.0f;
    float bar_top = 0.0f;
    if (SDL_GetDisplayBounds(SDL_GetDisplayForWindow(s_window), &monitor) &&
        taskbar_frame(&monitor, &bar_left, &bar_right, &bar_top))
    {
        const bool full_width = bar_right - bar_left >= 0.9f * screen_w;
        *left = full_width ? -past_edge : bar_left;
        *right = full_width ? screen_w + past_edge : bar_right;
        *top = bar_top;
        *exact = true;
        return true;
    }
    *left = -past_edge;
    *right = screen_w + past_edge;
    *exact = true;
    return true;
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
     * below the overlay, black with rounded bottom corners, polling
     * desktop_melee_island_wanted() to fade. */
    (void)left;
    (void)right;
    (void)height;
}

/* ---- the settings UI ---------------------------------------------------- */

void desktop_melee_platform_ui_install(SDL_Window* window) {
    (void)window;
    /* The settings file, the Pause and hide hotkey, then the notification
     * area icon (tray_sdl.c), whose Settings... opens the settings window
     * (settings_window.cpp, an RmlUi document in aurora's second window). */
    desktop_melee_settings_load();
    desktop_melee_set_window_icons();
    desktop_melee_win_hotkey_install();
    if (!desktop_melee_tray_install()) {
        desktop_melee_settings_window_open();
    }
}

void desktop_melee_platform_ui_focus_game(void) {
    /* Windows lets a process take the foreground right after input it
     * received, which the hotkey's WM_HOTKEY is. */
    if (s_window == NULL) {
        return;
    }
    HWND hwnd = (HWND)SDL_GetPointerProperty(
        SDL_GetWindowProperties(s_window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    if (hwnd != NULL) {
        SetForegroundWindow(hwnd);
    }
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
    desktop_melee_win_hotkey_remove();
}

/* ---- window platforms ---------------------------------------------------- */

typedef struct {
    DesktopMeleeWindow* out;
    int max;
    int count;
    SDL_Rect monitor; /* the overlay's, on the desktop */
    DWORD pid;        /* ours */
} WindowList;

/* A window the player would call one: shown, not minimized, on the current
 * virtual desktop (not cloaked), an app's own (no owner, or one that asks for
 * a taskbar button) and titled; not ours, and not the desktop or taskbar. */
static bool is_app_window(HWND hwnd, DWORD own_pid) {
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) {
        return false;
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == own_pid) {
        return false;
    }
    const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    const bool app = (ex & WS_EX_APPWINDOW) != 0;
    if (!app && ((ex & WS_EX_TOOLWINDOW) != 0 || GetWindow(hwnd, GW_OWNER) != NULL)) {
        return false;
    }
    if ((ex & WS_EX_NOACTIVATE) != 0 && !app) {
        return false;
    }
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof cloaked)) &&
        cloaked != 0)
    {
        return false;
    }
    if (GetWindowTextLengthW(hwnd) == 0) {
        return false;
    }
    wchar_t name[32];
    if (GetClassNameW(hwnd, name, 32) > 0 &&
        (wcscmp(name, L"Progman") == 0 || wcscmp(name, L"WorkerW") == 0 ||
            wcscmp(name, L"Shell_TrayWnd") == 0 || wcscmp(name, L"Shell_SecondaryTrayWnd") == 0))
    {
        return false;
    }
    return true;
}

static BOOL CALLBACK add_window(HWND hwnd, LPARAM data) {
    WindowList* list = (WindowList*)data;
    if (list->count >= list->max) {
        return FALSE;
    }
    if (!is_app_window(hwnd, list->pid)) {
        return TRUE;
    }
    /* The visible frame: Windows 10 and 11 windows have an invisible
     * resize border around it, several pixels wide, that GetWindowRect
     * counts. */
    RECT r;
    if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof r)) &&
        !GetWindowRect(hwnd, &r))
    {
        return TRUE;
    }
    if (r.right <= r.left || r.bottom <= r.top) {
        return TRUE;
    }
    list->out[list->count++] = (DesktopMeleeWindow){
        .x = (float)(r.left - list->monitor.x),
        .y = (float)(r.top - list->monitor.y),
        .w = (float)(r.right - r.left),
        .h = (float)(r.bottom - r.top),
        .id = (unsigned long long)(uintptr_t)hwnd,
    };
    return TRUE;
}

int desktop_melee_platform_windows(DesktopMeleeWindow* out, int max) {
    /* EnumWindows goes top to bottom; platform.h wants bottom to top. */
    if (!s_overlay || s_window == NULL || max <= 0) {
        return 0;
    }
    WindowList list = {.out = out, .max = max, .pid = GetCurrentProcessId()};
    if (!SDL_GetDisplayBounds(SDL_GetDisplayForWindow(s_window), &list.monitor)) {
        return 0;
    }
    EnumWindows(add_window, (LPARAM)&list);
    for (int i = 0, j = list.count - 1; i < j; i++, j--) {
        const DesktopMeleeWindow t = out[i];
        out[i] = out[j];
        out[j] = t;
    }
    return list.count;
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
