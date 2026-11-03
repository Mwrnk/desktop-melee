/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The player's windows on Linux (window_list_linux.h).
 *
 * On KDE, the overlay (an X11 window) can't see native Wayland windows, so a
 * KWin script does: it runs inside KWin and, whenever a window moves, opens,
 * closes or the desktop changes, calls our D-Bus object with the stacking
 * order as text, "x,y,w,h|x,y,w,h,id;..." from bottom to top: first the
 * overlay's own frame, then the windows, all in KWin's logical coordinates.
 * The overlay's frame converts them to the overlay's, whatever the display
 * scale. Elsewhere X11's own list, _NET_CLIENT_LIST_STACKING, is read
 * directly, again only after the window manager says something changed. */
#include "window_list_linux.h"

#include <SDL3/SDL.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <stdlib.h>
#include <unistd.h>

#ifdef DESKTOP_MELEE_HOTKEY_PORTAL
#include <dbus/dbus.h>
#endif

#define MAX_LIST 64

static DesktopMeleeWindow s_list[MAX_LIST];
static int s_count = 0;

/* ---- KWin ---------------------------------------------------------------- */

#ifdef DESKTOP_MELEE_HOTKEY_PORTAL

#define SERVICE "io.github.mwrnk.DesktopMelee.Windows"
#define OBJECT "/windows"
#define SCRIPT_NAME "desktop-melee-windows"

/* KWin 6's scripting: the stacking order, minus what never makes a platform
 * (Desktop Melee's own windows, by its pid). %d is our pid. */
static const char kScript[] =
    "function send() {\n"
    "    var out = [];\n"
    "    var own = null;\n"
    "    var list = workspace.stackingOrder;\n"
    "    for (var i = 0; i < list.length; i++) {\n"
    "        var w = list[i];\n"
    "        if (w.pid == %d) {\n"
    "            var f = w.frameGeometry;\n"
    "            if (own == null || f.width * f.height > own.width * own.height) own = f;\n"
    "            continue;\n"
    "        }\n"
    "        if (!w.normalWindow || w.minimized || w.fullScreen) continue;\n"
    "        if (!w.onAllDesktops && w.desktops.indexOf(workspace.currentDesktop) < 0) continue;\n"
    "        var g = w.frameGeometry;\n"
    "        out.push([g.x, g.y, g.width, g.height, w.internalId.toString()].join(','));\n"
    "    }\n"
    "    var head = own == null ? '' : [own.x, own.y, own.width, own.height].join(',');\n"
    "    callDBus('" SERVICE "', '" OBJECT "', '" SERVICE "', 'Update', head + '|' + "
    "out.join(';'));\n"
    "}\n"
    "function watch(w) {\n"
    "    w.frameGeometryChanged.connect(send);\n"
    "    w.minimizedChanged.connect(send);\n"
    "    w.fullScreenChanged.connect(send);\n"
    "    w.desktopsChanged.connect(send);\n"
    "}\n"
    "workspace.windowList().forEach(watch);\n"
    "workspace.windowAdded.connect(function (w) { watch(w); send(); });\n"
    "workspace.windowRemoved.connect(send);\n"
    "workspace.windowActivated.connect(send);\n"
    "workspace.currentDesktopChanged.connect(send);\n"
    "send();\n";

static DBusConnection* s_bus = NULL;
static bool s_kwin_tried = false;
static bool s_kwin = false;
static char s_script_path[512] = "";

static bool call_kwin(const char* method, const char* arg1, const char* arg2) {
    DBusMessage* m = dbus_message_new_method_call(
        "org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting", method);
    if (arg1 != NULL) {
        dbus_message_append_args(m, DBUS_TYPE_STRING, &arg1, DBUS_TYPE_INVALID);
    }
    if (arg2 != NULL) {
        dbus_message_append_args(m, DBUS_TYPE_STRING, &arg2, DBUS_TYPE_INVALID);
    }
    DBusMessage* reply = dbus_connection_send_with_reply_and_block(s_bus, m, 2000, NULL);
    dbus_message_unref(m);
    if (reply == NULL) {
        return false;
    }
    dbus_message_unref(reply);
    return true;
}

/* Loads the script into KWin, once; false when this isn't KDE. */
static bool start_kwin(void) {
    if (s_kwin_tried) {
        return s_kwin;
    }
    s_kwin_tried = true;
    s_bus = dbus_bus_get_private(DBUS_BUS_SESSION, NULL);
    if (s_bus == NULL) {
        return false;
    }
    dbus_connection_set_exit_on_disconnect(s_bus, false);
    if (!dbus_bus_name_has_owner(s_bus, "org.kde.KWin", NULL) ||
        dbus_bus_request_name(s_bus, SERVICE, DBUS_NAME_FLAG_DO_NOT_QUEUE, NULL) !=
            DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER)
    {
        return false;
    }
    const char* runtime = SDL_getenv("XDG_RUNTIME_DIR");
    SDL_snprintf(s_script_path, sizeof s_script_path, "%s/desktop-melee-windows.js",
        runtime != NULL ? runtime : "/tmp");
    char script[sizeof kScript + 16];
    SDL_snprintf(script, sizeof script, kScript, (int)getpid());
    if (!SDL_SaveFile(s_script_path, script, SDL_strlen(script))) {
        return false;
    }
    call_kwin("unloadScript", SCRIPT_NAME, NULL); /* a copy from an earlier run */
    s_kwin = call_kwin("loadScript", s_script_path, SCRIPT_NAME) && call_kwin("start", NULL, NULL);
    SDL_Log(
        "desktop-melee: window platforms: %s", s_kwin ? "KWin's window list" : "no KWin script");
    return s_kwin;
}

/* The overlay's frame in KWin's coordinates, from the script's last list:
 * KWin's windows are mapped from it onto the overlay. w == 0 when unknown. */
static DesktopMeleeWindow s_kwin_overlay;

/* "ox,oy,ow,oh|x,y,w,h,id;..." into s_kwin_overlay and s_list. */
static void parse(const char* text) {
    s_count = 0;
    SDL_zero(s_kwin_overlay);
    const char* bar = SDL_strchr(text, '|');
    if (bar == NULL) {
        return;
    }
    if (bar != text) {
        char* end = NULL;
        DesktopMeleeWindow o;
        SDL_zero(o);
        o.x = (float)SDL_strtod(text, &end);
        o.y = (float)SDL_strtod(end + 1, &end);
        o.w = (float)SDL_strtod(end + 1, &end);
        o.h = (float)SDL_strtod(end + 1, &end);
        if (end == bar && o.w > 0.0f && o.h > 0.0f) {
            s_kwin_overlay = o;
        }
    }
    const char* p = bar + 1;
    while (*p != '\0' && s_count < MAX_LIST) {
        char* end = NULL;
        DesktopMeleeWindow w;
        w.x = (float)SDL_strtod(p, &end);
        if (end == p || *end != ',') {
            break;
        }
        w.y = (float)SDL_strtod(end + 1, &end);
        w.w = (float)SDL_strtod(end + 1, &end);
        w.h = (float)SDL_strtod(end + 1, &end);
        /* The id: a hash of KWin's uuid text. */
        unsigned long long hash = 1469598103934665603ULL;
        p = end + 1;
        while (*p != '\0' && *p != ';') {
            hash = (hash ^ (unsigned char)*p++) * 1099511628211ULL;
        }
        w.id = hash;
        s_list[s_count++] = w;
        if (*p == ';') {
            p++;
        }
    }
}

/* The latest list the script sent, if any came. */
static void read_kwin(void) {
    dbus_connection_read_write(s_bus, 0);
    DBusMessage* m;
    while ((m = dbus_connection_pop_message(s_bus)) != NULL) {
        const char* text = NULL;
        if (dbus_message_is_method_call(m, SERVICE, "Update") &&
            dbus_message_get_args(m, NULL, DBUS_TYPE_STRING, &text, DBUS_TYPE_INVALID))
        {
            parse(text);
            DBusMessage* reply = dbus_message_new_method_return(m);
            dbus_connection_send(s_bus, reply, NULL);
            dbus_message_unref(reply);
        }
        dbus_message_unref(m);
    }
}

#endif

/* ---- X11 ------------------------------------------------------------------ */

/* Re-read at least this often, in case a window manager moves a window
 * without telling its client (ICCCM asks it to, with a ConfigureNotify). */
#define X11_REREAD_MS 250

static Display* s_x = NULL;
static bool s_x_dirty = true;
static Uint64 s_x_read = 0;
static bool s_x_error = false;

/* A window can close between the list and the questions about it: its
 * BadWindow would end the game under Xlib's default handler. */
static int ignore_error(Display* display, XErrorEvent* error) {
    (void)display;
    (void)error;
    s_x_error = true;
    return 0;
}

static unsigned long* get_property(Window w, const char* name, Atom type, unsigned long* count) {
    Atom actual;
    int format;
    unsigned long after;
    unsigned char* data = NULL;
    if (XGetWindowProperty(s_x, w, XInternAtom(s_x, name, False), 0, 1024, False, type, &actual,
            &format, count, &after, &data) != Success ||
        data == NULL || *count == 0)
    {
        if (data != NULL) {
            XFree(data);
        }
        *count = 0;
        return NULL;
    }
    return (unsigned long*)data; /* format 32 comes back as longs */
}

static bool has_atom(const unsigned long* atoms, unsigned long count, const char* name) {
    const Atom wanted = XInternAtom(s_x, name, False);
    for (unsigned long i = 0; i < count; i++) {
        if (atoms[i] == wanted) {
            return true;
        }
    }
    return false;
}

/* Four CARDINALs (left, right, top, bottom), or zeros. */
static void get_extents(Window w, const char* name, long extents[4]) {
    unsigned long c = 0;
    unsigned long* v = get_property(w, name, XA_CARDINAL, &c);
    for (int i = 0; i < 4; i++) {
        extents[i] = v != NULL && c >= 4 ? (long)v[i] : 0;
    }
    if (v != NULL) {
        XFree(v);
    }
}

/* Whether the window manager said anything changed since the last read:
 * the stacking list or the current desktop (on the root), a window's
 * state, desktop or frame, or its size, place or mapping. Other properties
 * (titles, user times) change often and don't matter. Reading the queue
 * costs no round trip to the X server. */
static bool x11_changed(void) {
    static Atom watched[6];
    if (watched[0] == None) {
        const char* names[6] = {"_NET_CLIENT_LIST_STACKING", "_NET_CURRENT_DESKTOP",
            "_NET_WM_STATE", "_NET_WM_DESKTOP", "_NET_FRAME_EXTENTS", "_GTK_FRAME_EXTENTS"};
        for (int i = 0; i < 6; i++) {
            watched[i] = XInternAtom(s_x, names[i], False);
        }
    }
    bool changed = false;
    while (XPending(s_x) > 0) {
        XEvent event;
        XNextEvent(s_x, &event);
        if (event.type == PropertyNotify) {
            for (int i = 0; i < 6; i++) {
                changed = changed || event.xproperty.atom == watched[i];
            }
        } else if (event.type == ConfigureNotify || event.type == MapNotify ||
                   event.type == UnmapNotify || event.type == DestroyNotify)
        {
            changed = true;
        }
    }
    return changed;
}

static void read_x11(void) {
    s_count = 0;
    const Window root = DefaultRootWindow(s_x);
    s_x_error = false;
    int (*old_handler)(Display*, XErrorEvent*) = XSetErrorHandler(ignore_error);
    unsigned long n = 0;
    unsigned long* list = get_property(root, "_NET_CLIENT_LIST_STACKING", XA_WINDOW, &n);
    unsigned long dn = 0;
    unsigned long* desktop = get_property(root, "_NET_CURRENT_DESKTOP", XA_CARDINAL, &dn);
    const unsigned long current = desktop != NULL ? desktop[0] : 0;
    if (desktop != NULL) {
        XFree(desktop);
    }
    for (unsigned long i = 0; list != NULL && i < n && s_count < MAX_LIST; i++) {
        const Window w = (Window)list[i];
        /* Hear about this window's changes from now on (repeating it is
         * harmless). */
        XSelectInput(s_x, w, PropertyChangeMask | StructureNotifyMask);
        unsigned long c = 0;
        unsigned long* v = get_property(w, "_NET_WM_PID", XA_CARDINAL, &c);
        const bool ours = v != NULL && v[0] == (unsigned long)getpid();
        if (v != NULL) {
            XFree(v);
        }
        v = get_property(w, "_NET_WM_WINDOW_TYPE", XA_ATOM, &c);
        const bool normal = v == NULL || has_atom(v, c, "_NET_WM_WINDOW_TYPE_NORMAL");
        if (v != NULL) {
            XFree(v);
        }
        v = get_property(w, "_NET_WM_STATE", XA_ATOM, &c);
        const bool hidden = v != NULL && (has_atom(v, c, "_NET_WM_STATE_HIDDEN") ||
                                             has_atom(v, c, "_NET_WM_STATE_FULLSCREEN"));
        if (v != NULL) {
            XFree(v);
        }
        v = get_property(w, "_NET_WM_DESKTOP", XA_CARDINAL, &c);
        const bool elsewhere = v != NULL && v[0] != current && v[0] != 0xFFFFFFFFUL;
        if (v != NULL) {
            XFree(v);
        }
        XWindowAttributes attributes;
        int rx = 0;
        int ry = 0;
        Window child;
        if (ours || !normal || hidden || elsewhere || !XGetWindowAttributes(s_x, w, &attributes) ||
            attributes.map_state != IsViewable ||
            !XTranslateCoordinates(s_x, w, root, 0, 0, &rx, &ry, &child))
        {
            continue;
        }
        /* The window manager's frame adds a title bar around the window
         * (_NET_FRAME_EXTENTS); an app that draws its own (GTK) has an
         * invisible shadow inside it instead (_GTK_FRAME_EXTENTS). The top
         * is where the visible window starts. Left, right, top, bottom. */
        long frame[4];
        long shadow[4];
        get_extents(w, "_NET_FRAME_EXTENTS", frame);
        get_extents(w, "_GTK_FRAME_EXTENTS", shadow);
        const long left = frame[0] - shadow[0];
        const long right = frame[1] - shadow[1];
        const long top = frame[2] - shadow[2];
        const long bottom = frame[3] - shadow[3];
        s_list[s_count++] = (DesktopMeleeWindow){(float)(rx - left), (float)(ry - top),
            (float)(attributes.width + left + right), (float)(attributes.height + top + bottom),
            (unsigned long long)w};
    }
    if (list != NULL) {
        XFree(list);
    }
    XSync(s_x, False); /* any error of this read arrives before the swap back */
    XSetErrorHandler(old_handler);
    if (s_x_error) {
        s_x_dirty = true; /* a window went away mid-read: read again next time */
    }
}

/* The X11 list, read again only when something changed. */
static void poll_x11(void) {
    if (s_x == NULL) {
        if ((s_x = XOpenDisplay(NULL)) == NULL) {
            s_count = 0;
            return;
        }
        XSelectInput(s_x, DefaultRootWindow(s_x), PropertyChangeMask);
        s_x_dirty = true;
    }
    const Uint64 now = SDL_GetTicks();
    if (x11_changed() || s_x_dirty || now - s_x_read >= X11_REREAD_MS) {
        s_x_dirty = false;
        s_x_read = now;
        read_x11();
    }
}

/* ---- the interface ------------------------------------------------------- */

int desktop_melee_linux_windows(
    DesktopMeleeWindow* out, int max, const DesktopMeleeWindow* overlay) {
    /* X11's coordinates: the overlay's own origin, one to one. */
    DesktopMeleeWindow from = *overlay;
#ifdef DESKTOP_MELEE_HOTKEY_PORTAL
    if (start_kwin()) {
        read_kwin();
        if (s_kwin_overlay.w > 0.0f) {
            from = s_kwin_overlay; /* KWin's logical coordinates */
        }
    } else {
        poll_x11();
    }
#else
    poll_x11();
#endif
    const float sx = from.w > 0.0f ? overlay->w / from.w : 1.0f;
    const float sy = from.h > 0.0f ? overlay->h / from.h : 1.0f;
    const int n = s_count < max ? s_count : max;
    for (int i = 0; i < n; i++) {
        out[i] = s_list[i];
        out[i].x = (s_list[i].x - from.x) * sx;
        out[i].y = (s_list[i].y - from.y) * sy;
        out[i].w = s_list[i].w * sx;
        out[i].h = s_list[i].h * sy;
    }
    return n;
}

void desktop_melee_linux_windows_stop(void) {
#ifdef DESKTOP_MELEE_HOTKEY_PORTAL
    if (s_kwin) {
        call_kwin("unloadScript", SCRIPT_NAME, NULL);
        SDL_RemovePath(s_script_path);
        s_kwin = false;
    }
#endif
    if (s_x != NULL) {
        XCloseDisplay(s_x);
        s_x = NULL;
    }
}
