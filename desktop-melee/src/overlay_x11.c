/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The Linux overlay window on X11 (overlay_x11.h). */
#include "overlay_x11.h"

#include <X11/XKBlib.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <poll.h>
#include <stdlib.h>

#include "desktop_melee_game.h"

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

/* ---- the panel ---------------------------------------------------------- */

/* Whether `window`'s _NET_WM_WINDOW_TYPE lists _NET_WM_WINDOW_TYPE_DOCK. */
static bool is_dock(Display* display, Window window, Atom type_atom, Atom dock_atom) {
    Atom actual = None;
    int format = 0;
    unsigned long count = 0;
    unsigned long after = 0;
    unsigned char* data = NULL;
    if (XGetWindowProperty(display, window, type_atom, 0, 16, False, XA_ATOM, &actual, &format,
            &count, &after, &data) != Success ||
        data == NULL)
    {
        return false;
    }
    bool dock = false;
    if (actual == XA_ATOM && format == 32) {
        const Atom* types = (const Atom*)data;
        for (unsigned long i = 0; i < count; i++) {
            dock = dock || types[i] == dock_atom;
        }
    }
    XFree(data);
    return dock;
}

typedef struct DockSearch {
    Display* display;
    Window root;
    Atom type_atom;
    Atom dock_atom;
    SDL_Rect screen;
    SDL_Rect best;
} DockSearch;

/* Keeps `window` as the panel if it is a visible dock along the bottom of the
 * screen, wider than tall, and wider than the panel kept so far. */
static void consider(DockSearch* search, Window window) {
    if (!is_dock(search->display, window, search->type_atom, search->dock_atom)) {
        return;
    }
    XWindowAttributes attributes;
    int x = 0;
    int y = 0;
    Window child = None;
    if (!XGetWindowAttributes(search->display, window, &attributes) ||
        attributes.map_state != IsViewable ||
        !XTranslateCoordinates(search->display, window, search->root, 0, 0, &x, &y, &child))
    {
        return;
    }
    const SDL_Rect* s = &search->screen;
    const int bottom = y + attributes.height;
    const int left = SDL_max(x, s->x);
    const int right = SDL_min(x + attributes.width, s->x + s->w);
    /* A few pixels of slack: docks like Plank float just above the edge.
     * Under 16 pixels tall it is an auto-hidden panel's sliver, and a floor
     * that low is the bottom blastzone. */
    const bool along_bottom = SDL_abs(bottom - (s->y + s->h)) <= 8 && y > s->y + s->h / 2;
    if (!along_bottom || attributes.width <= attributes.height || attributes.height < 16 ||
        right - left <= search->best.w)
    {
        return;
    }
    search->best.x = left;
    search->best.y = y;
    search->best.w = right - left;
    search->best.h = attributes.height;
}

bool desktop_melee_x11_dock_frame(SDL_Window* window, float* left, float* right, float* top) {
    SDL_PropertiesID props = SDL_GetWindowProperties(window);
    Display* display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, NULL);
    DockSearch search;
    SDL_zero(search);
    if (display == NULL || !SDL_GetDisplayBounds(SDL_GetDisplayForWindow(window), &search.screen)) {
        return false;
    }
    search.display = display;
    search.root = DefaultRootWindow(display);
    search.type_atom = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
    search.dock_atom = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DOCK", False);

    /* Panels are usually children of the root; a reparenting window manager
     * puts some inside a frame, so look one level down too. */
    Window parent = None;
    Window* children = NULL;
    unsigned int count = 0;
    if (!XQueryTree(display, search.root, &search.root, &parent, &children, &count)) {
        return false;
    }
    for (unsigned int i = 0; i < count; i++) {
        consider(&search, children[i]);
        Window* inner = NULL;
        unsigned int inner_count = 0;
        Window inner_root = None;
        if (XQueryTree(display, children[i], &inner_root, &parent, &inner, &inner_count)) {
            for (unsigned int j = 0; j < inner_count; j++) {
                consider(&search, inner[j]);
            }
            if (inner != NULL) {
                XFree(inner);
            }
        }
    }
    if (children != NULL) {
        XFree(children);
    }
    if (search.best.w <= 0) {
        return false;
    }
    *left = (float)(search.best.x - search.screen.x);
    *right = (float)(search.best.x + search.best.w - search.screen.x);
    *top = (float)(search.screen.y + search.screen.h - search.best.y);
    return true;
}

/* ---- the hotkey --------------------------------------------------------- */

static Display* s_hotkey_display = NULL;
static SDL_Thread* s_hotkey_thread = NULL;
static SDL_AtomicInt s_hotkey_stop;
static KeyCode s_hotkey_code = 0;
static bool s_grab_failed = false;

/* Num Lock (Mod2) and Caps Lock count as modifiers on X11: grab the key with
 * each of them on and off. */
static const unsigned int kLockMasks[] = {0, LockMask, Mod2Mask, LockMask | Mod2Mask};
#define HOTKEY_MODIFIERS (ControlMask | Mod1Mask)

static int on_grab_error(Display* display, XErrorEvent* error) {
    (void)display;
    if (error->error_code == BadAccess) {
        s_grab_failed = true;
    }
    return 0;
}

static void SDLCALL toggle_on_main_thread(void* userdata) {
    (void)userdata;
    desktop_melee_toggle_pause_and_hide();
}

static int SDLCALL hotkey_run(void* userdata) {
    (void)userdata;
    struct pollfd fd = {ConnectionNumber(s_hotkey_display), POLLIN, 0};
    bool held = false;
    while (!SDL_GetAtomicInt(&s_hotkey_stop)) {
        if (XPending(s_hotkey_display) == 0) {
            poll(&fd, 1, 200);
            continue;
        }
        XEvent event;
        XNextEvent(s_hotkey_display, &event);
        /* A held key repeats its presses; with detectable auto-repeat there
         * are no releases in between, so one toggle per press. */
        if (event.type == KeyRelease && event.xkey.keycode == s_hotkey_code) {
            held = false;
        } else if (event.type == KeyPress && event.xkey.keycode == s_hotkey_code &&
                   (event.xkey.state & HOTKEY_MODIFIERS) == HOTKEY_MODIFIERS && !held)
        {
            held = true;
            SDL_RunOnMainThread(toggle_on_main_thread, NULL, false);
        }
    }
    return 0;
}

static bool wayland_session(void) {
    const char* type = SDL_getenv("XDG_SESSION_TYPE");
    const char* wayland = SDL_getenv("WAYLAND_DISPLAY");
    return (type != NULL && SDL_strcasecmp(type, "wayland") == 0) ||
           (wayland != NULL && wayland[0] != '\0');
}

bool desktop_melee_x11_hotkey_install(void) {
    if (s_hotkey_thread != NULL) {
        return true;
    }
    if (wayland_session()) {
        SDL_Log("desktop-melee: hotkey: a Wayland session without the GlobalShortcuts "
                "portal: no Pause and hide shortcut");
        return false;
    }
    Display* display = XOpenDisplay(NULL);
    if (display == NULL) {
        return false;
    }
    const Window root = DefaultRootWindow(display);
    s_hotkey_code = XKeysymToKeycode(display, XK_m);
    if (s_hotkey_code == 0) {
        XCloseDisplay(display);
        return false;
    }
    XkbSetDetectableAutoRepeat(display, True, NULL);

    /* Another app holding the key answers BadAccess, which Xlib's default
     * handler would exit on. Error handlers are process-wide, so swap it in
     * only around the grab, on the main thread. */
    s_grab_failed = false;
    int (*old_handler)(Display*, XErrorEvent*) = XSetErrorHandler(on_grab_error);
    for (size_t i = 0; i < SDL_arraysize(kLockMasks); i++) {
        XGrabKey(display, s_hotkey_code, HOTKEY_MODIFIERS | kLockMasks[i], root, True,
            GrabModeAsync, GrabModeAsync);
    }
    XSync(display, False);
    XSetErrorHandler(old_handler);
    if (s_grab_failed) {
        SDL_Log("desktop-melee: hotkey: another app holds Ctrl+Alt+M: no Pause and hide shortcut");
        XCloseDisplay(display); /* closing releases any grabs that did take */
        return false;
    }
    s_hotkey_display = display;
    SDL_SetAtomicInt(&s_hotkey_stop, 0);
    s_hotkey_thread = SDL_CreateThread(hotkey_run, "desktop-melee X11 hotkey", NULL);
    if (s_hotkey_thread == NULL) {
        XCloseDisplay(display);
        s_hotkey_display = NULL;
        return false;
    }
    SDL_Log("desktop-melee: hotkey: Pause and hide is Ctrl+Alt+M (X11 key grab)");
    return true;
}

const char* desktop_melee_x11_hotkey_label(void) {
    return s_hotkey_thread != NULL ? "Ctrl+Alt+M" : "";
}

void desktop_melee_x11_hotkey_remove(void) {
    if (s_hotkey_thread == NULL) {
        return;
    }
    SDL_SetAtomicInt(&s_hotkey_stop, 1);
    SDL_WaitThread(s_hotkey_thread, NULL);
    s_hotkey_thread = NULL;
    XCloseDisplay(s_hotkey_display);
    s_hotkey_display = NULL;
}
