/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The player's windows on Linux (window_list_linux.h).
 *
 * On KDE, the overlay (an X11 window) can't see native Wayland windows, so a
 * KWin script does: it runs inside KWin and, whenever a window moves, opens,
 * closes or the desktop changes, calls our D-Bus object with the stacking
 * order as text, "x,y,w,h,id;..." from bottom to top. Elsewhere X11's own
 * list, _NET_CLIENT_LIST_STACKING, is read directly. */
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
    "    var list = workspace.stackingOrder;\n"
    "    for (var i = 0; i < list.length; i++) {\n"
    "        var w = list[i];\n"
    "        if (!w.normalWindow || w.minimized || w.fullScreen || w.pid == %d) continue;\n"
    "        if (!w.onAllDesktops && w.desktops.indexOf(workspace.currentDesktop) < 0) continue;\n"
    "        var g = w.frameGeometry;\n"
    "        out.push([g.x, g.y, g.width, g.height, w.internalId.toString()].join(','));\n"
    "    }\n"
    "    callDBus('" SERVICE "', '" OBJECT "', '" SERVICE "', 'Update', out.join(';'));\n"
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
    DBusMessage* m =
        dbus_message_new_method_call("org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting", method);
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
            DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
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
    SDL_Log("desktop-melee: window platforms: %s", s_kwin ? "KWin's window list" : "no KWin script");
    return s_kwin;
}

/* "x,y,w,h,id;..." into s_list. */
static void parse(const char* text) {
    s_count = 0;
    const char* p = text;
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
            dbus_message_get_args(m, NULL, DBUS_TYPE_STRING, &text, DBUS_TYPE_INVALID)) {
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

static Display* s_x = NULL;

static unsigned long* get_property(Window w, const char* name, Atom type, unsigned long* count) {
    Atom actual;
    int format;
    unsigned long after;
    unsigned char* data = NULL;
    if (XGetWindowProperty(s_x, w, XInternAtom(s_x, name, False), 0, 1024, False, type, &actual,
            &format, count, &after, &data) != Success ||
        data == NULL || *count == 0) {
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

static void read_x11(void) {
    s_count = 0;
    if (s_x == NULL && (s_x = XOpenDisplay(NULL)) == NULL) {
        return;
    }
    const Window root = DefaultRootWindow(s_x);
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
        if (ours || !normal || hidden || elsewhere || !XGetWindowAttributes(s_x, w, &attributes) ||
            attributes.map_state != IsViewable) {
            continue;
        }
        int rx = 0;
        int ry = 0;
        Window child;
        XTranslateCoordinates(s_x, w, root, 0, 0, &rx, &ry, &child);
        /* The frame's edges, so the top is the title bar's. */
        long left = 0, right = 0, top = 0, bottom = 0;
        v = get_property(w, "_NET_FRAME_EXTENTS", XA_CARDINAL, &c);
        if (v != NULL && c >= 4) {
            left = (long)v[0], right = (long)v[1], top = (long)v[2], bottom = (long)v[3];
        }
        if (v != NULL) {
            XFree(v);
        }
        s_list[s_count++] = (DesktopMeleeWindow){(float)(rx - left), (float)(ry - top),
            (float)(attributes.width + left + right), (float)(attributes.height + top + bottom),
            (unsigned long long)w};
    }
    if (list != NULL) {
        XFree(list);
    }
}

/* ---- the interface ------------------------------------------------------- */

int desktop_melee_linux_windows(
    DesktopMeleeWindow* out, int max, float origin_x, float origin_y) {
#ifdef DESKTOP_MELEE_HOTKEY_PORTAL
    if (start_kwin()) {
        read_kwin();
    } else {
        read_x11();
    }
#else
    read_x11();
#endif
    const int n = s_count < max ? s_count : max;
    for (int i = 0; i < n; i++) {
        out[i] = s_list[i];
        out[i].x -= origin_x;
        out[i].y -= origin_y;
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
