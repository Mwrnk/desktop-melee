/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The Linux overlay window on X11 (overlay_x11.h). */
#include "overlay_x11.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdlib.h>

#define LAYER_NAME "VK_LAYER_DESKTOP_MELEE_overlay_alpha"
#define LAYER_MANIFEST "desktop_melee_overlay_alpha.json"

/* Appends `value` to a list in the environment, separated by `separator`. */
static void append_env(const char* name, const char* value, char separator) {
    const char* old = SDL_getenv(name);
    if (old == NULL || old[0] == '\0') {
        SDL_setenv_unsafe(name, value, 1);
        return;
    }
    char joined[4096];
    SDL_snprintf(joined, sizeof joined, "%s%c%s", old, separator, value);
    SDL_setenv_unsafe(name, joined, 1);
}

bool desktop_melee_x11_overlay_prepare(void) {
    const char* driver = SDL_getenv("SDL_VIDEO_DRIVER");
    if (driver != NULL && driver[0] != '\0' && SDL_strcasecmp(driver, "x11") != 0) {
        SDL_Log("desktop-melee: overlay: SDL_VIDEO_DRIVER=%s, so a normal window", driver);
        return false;
    }
    Display* display = XOpenDisplay(NULL);
    if (display == NULL) {
        SDL_Log("desktop-melee: overlay: no X server (XWayland), so a normal window");
        return false;
    }
    const int screen = DefaultScreen(display);

    /* Transparency shows only with a compositor: the owner of
     * _NET_WM_CM_S<screen>. Wayland desktops are always composited. */
    char selection[32];
    SDL_snprintf(selection, sizeof selection, "_NET_WM_CM_S%d", screen);
    const bool composited =
        XGetSelectionOwner(display, XInternAtom(display, selection, False)) != None;
    XVisualInfo visual;
    const bool argb = XMatchVisualInfo(display, screen, 32, TrueColor, &visual) != 0;
    XCloseDisplay(display);
    if (!composited || !argb) {
        SDL_Log("desktop-melee: overlay: %s, so a normal window",
            !composited ? "no compositor running" : "no 32-bit visual");
        return false;
    }

    /* The layer sits next to the game (desktop-melee.cmake). */
    const char* base = SDL_GetBasePath();
    char manifest[1024];
    SDL_snprintf(manifest, sizeof manifest, "%s%s", base != NULL ? base : "", LAYER_MANIFEST);
    if (base == NULL || !SDL_GetPathInfo(manifest, NULL)) {
        SDL_Log("desktop-melee: overlay: no %s, so a normal window", manifest);
        return false;
    }

    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "x11");
    char id[32];
    SDL_snprintf(id, sizeof id, "0x%lx", (unsigned long)visual.visualid);
    SDL_SetHint(SDL_HINT_VIDEO_X11_WINDOW_VISUALID, id);
    append_env("VK_ADD_LAYER_PATH", base, ':');
    append_env("VK_INSTANCE_LAYERS", LAYER_NAME, ':');
    SDL_Log("desktop-melee: overlay: X11 window, visual %s", id);
    return true;
}

/* Adds _NET_WM_STATE flags the way a client asks the window manager to. */
static void add_state(Display* display, Window window, const char* first, const char* second) {
    XEvent event;
    SDL_zero(event);
    event.xclient.type = ClientMessage;
    event.xclient.window = window;
    event.xclient.message_type = XInternAtom(display, "_NET_WM_STATE", False);
    event.xclient.format = 32;
    event.xclient.data.l[0] = 1; /* _NET_WM_STATE_ADD */
    event.xclient.data.l[1] = (long)XInternAtom(display, first, False);
    event.xclient.data.l[2] = second != NULL ? (long)XInternAtom(display, second, False) : 0;
    event.xclient.data.l[3] = 1; /* from a normal application */
    XSendEvent(display, DefaultRootWindow(display), False,
        SubstructureRedirectMask | SubstructureNotifyMask, &event);
}

void desktop_melee_x11_overlay_setup(SDL_Window* window) {
    /* Click-through: SDL turns a fully transparent shape into an empty input
     * shape (ShapeInput only; drawing isn't clipped). */
    int w = 0;
    int h = 0;
    SDL_GetWindowSize(window, &w, &h);
    SDL_Surface* shape = SDL_CreateSurface(w > 0 ? w : 1, h > 0 ? h : 1, SDL_PIXELFORMAT_ARGB8888);
    if (shape != NULL) {
        SDL_ClearSurface(shape, 0.f, 0.f, 0.f, 0.f);
        if (!SDL_SetWindowShape(window, shape)) {
            SDL_Log("desktop-melee: overlay: no click-through (%s)", SDL_GetError());
        }
        SDL_DestroySurface(shape);
    }

    SDL_PropertiesID props = SDL_GetWindowProperties(window);
    Display* display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, NULL);
    const Window xwindow =
        (Window)SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
    if (display == NULL || xwindow == 0) {
        return;
    }
    /* Out of the taskbar, the pager and Alt-Tab, and on every desktop, as the
     * macOS overlay is on every Space. */
    add_state(display, xwindow, "_NET_WM_STATE_SKIP_TASKBAR", "_NET_WM_STATE_SKIP_PAGER");
    add_state(display, xwindow, "_NET_WM_STATE_STICKY", NULL);
    XFlush(display);
}

void desktop_melee_x11_overlay_set_faded(SDL_Window* window, bool faded) {
    /* _NET_WM_WINDOW_OPACITY, which the compositor honours. */
    SDL_SetWindowOpacity(window, faded ? 0.f : 1.f);
}
