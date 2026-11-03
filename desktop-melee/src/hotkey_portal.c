/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The Pause and hide shortcut through the GlobalShortcuts portal
 * (hotkey_portal.h), over libdbus on a thread of its own:
 *
 *   Register                our app id (host.portal.Registry): the portal
 *                           refuses apps without one
 *   CreateSession           a session for our shortcuts
 *   BindShortcuts           "pause-and-hide", preferred Ctrl+Alt+M; the
 *                           desktop asks the player the first time
 *   Activated (signal)      pressed: the toggle runs on the main thread
 *
 * Each portal call answers with a Request object whose Response signal
 * carries the result, so the thread listens for every Response and keeps the
 * one for its request. */
#include "hotkey_portal.h"

#include <SDL3/SDL.h>
#include <dbus/dbus.h>
#include <string.h>

#include "desktop_entry.h"
#include "desktop_melee_game.h"

#define PORTAL_NAME "org.freedesktop.portal.Desktop"
#define PORTAL_PATH "/org/freedesktop/portal/desktop"
#define SHORTCUTS "org.freedesktop.portal.GlobalShortcuts"
#define REQUEST "org.freedesktop.portal.Request"
#define SHORTCUT_ID "pause-and-hide"
/* Unsandboxed apps name themselves to the portal before calling it, by an
 * app id with an installed .desktop file (desktop_entry.h). */
#define APP_ID DESKTOP_MELEE_APP_ID

static DBusConnection* s_bus = NULL;
static SDL_Thread* s_thread = NULL;
static SDL_AtomicInt s_stop;
static SDL_Mutex* s_mutex = NULL;
static char s_session[256] = "";
static char s_label[64] = "";
static int s_token = 0;
static void (*s_unavailable)(void) = NULL;

/* ---- messages ------------------------------------------------------------ */

/* Appends {key: <string>} to an open a{sv}. */
static void append_string_entry(DBusMessageIter* dict, const char* key, const char* value) {
    DBusMessageIter entry;
    DBusMessageIter variant;
    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &value);
    dbus_message_iter_close_container(&entry, &variant);
    dbus_message_iter_close_container(dict, &entry);
}

/* A new handle_token, and the Request path the portal will answer on. */
static void next_request(char* token, size_t token_size, char* path, size_t path_size) {
    SDL_snprintf(token, token_size, "desktop_melee_%d", ++s_token);
    /* ":1.42" becomes "1_42". */
    char sender[128];
    SDL_strlcpy(sender, dbus_bus_get_unique_name(s_bus) + 1, sizeof sender);
    for (char* c = sender; *c != '\0'; c++) {
        if (*c == '.') {
            *c = '_';
        }
    }
    SDL_snprintf(path, path_size, PORTAL_PATH "/request/%s/%s", sender, token);
}

/* Sends a call and waits for its reply (not for the Response signal). */
static bool call(DBusMessage* message) {
    DBusError error;
    dbus_error_init(&error);
    DBusMessage* reply = dbus_connection_send_with_reply_and_block(s_bus, message, 10000, &error);
    dbus_message_unref(message);
    if (reply == NULL) {
        SDL_Log("desktop-melee: hotkey: %s", error.message != NULL ? error.message : "no reply");
        dbus_error_free(&error);
        return false;
    }
    dbus_message_unref(reply);
    return true;
}

/* In an a{sv} at iter, the variant of `key`, opened into `value`. */
static bool find_entry(DBusMessageIter* dict, const char* key, DBusMessageIter* value) {
    if (dbus_message_iter_get_arg_type(dict) != DBUS_TYPE_ARRAY) {
        return false;
    }
    DBusMessageIter entries;
    dbus_message_iter_recurse(dict, &entries);
    while (dbus_message_iter_get_arg_type(&entries) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter entry;
        dbus_message_iter_recurse(&entries, &entry);
        const char* name = NULL;
        dbus_message_iter_get_basic(&entry, &name);
        if (name != NULL && SDL_strcmp(name, key) == 0 && dbus_message_iter_next(&entry) &&
            dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_VARIANT)
        {
            dbus_message_iter_recurse(&entry, value);
            return true;
        }
        dbus_message_iter_next(&entries);
    }
    return false;
}

static bool string_entry(DBusMessageIter* dict, const char* key, char* out, size_t size) {
    DBusMessageIter value;
    if (!find_entry(dict, key, &value) ||
        dbus_message_iter_get_arg_type(&value) != DBUS_TYPE_STRING)
    {
        return false;
    }
    const char* s = NULL;
    dbus_message_iter_get_basic(&value, &s);
    SDL_strlcpy(out, s != NULL ? s : "", size);
    return true;
}

/* ---- the shortcut ------------------------------------------------------- */

static void SDLCALL toggle_on_main_thread(void* userdata) {
    (void)userdata;
    desktop_melee_toggle_pause_and_hide();
}

/* The trigger the desktop bound, from BindShortcuts' (or ShortcutsChanged's)
 * a(sa{sv}). */
static void read_bound(DBusMessageIter* shortcuts) {
    if (dbus_message_iter_get_arg_type(shortcuts) != DBUS_TYPE_ARRAY) {
        return;
    }
    DBusMessageIter items;
    dbus_message_iter_recurse(shortcuts, &items);
    while (dbus_message_iter_get_arg_type(&items) == DBUS_TYPE_STRUCT) {
        DBusMessageIter item;
        dbus_message_iter_recurse(&items, &item);
        const char* id = NULL;
        dbus_message_iter_get_basic(&item, &id);
        char label[64] = "";
        if (id != NULL && SDL_strcmp(id, SHORTCUT_ID) == 0 && dbus_message_iter_next(&item) &&
            string_entry(&item, "trigger_description", label, sizeof label))
        {
            SDL_LockMutex(s_mutex);
            SDL_strlcpy(s_label, label, sizeof s_label);
            SDL_UnlockMutex(s_mutex);
            SDL_Log("desktop-melee: hotkey: Pause and hide is %s", label[0] ? label : "unbound");
        }
        dbus_message_iter_next(&items);
    }
}

/* Handles one incoming message. With `request`, its Response's results are
 * handed to `on_response` and true is returned. */
static bool handle(
    DBusMessage* message, const char* request, void (*on_response)(DBusMessageIter* results)) {
    if (dbus_message_is_signal(message, SHORTCUTS, "Activated")) {
        DBusMessageIter args;
        const char* id = NULL;
        if (dbus_message_iter_init(message, &args) && dbus_message_iter_next(&args)) {
            dbus_message_iter_get_basic(&args, &id);
        }
        if (id != NULL && SDL_strcmp(id, SHORTCUT_ID) == 0) {
            SDL_RunOnMainThread(toggle_on_main_thread, NULL, false);
        }
        return false;
    }
    if (dbus_message_is_signal(message, SHORTCUTS, "ShortcutsChanged")) {
        DBusMessageIter args;
        if (dbus_message_iter_init(message, &args) && dbus_message_iter_next(&args)) {
            read_bound(&args);
        }
        return false;
    }
    if (request == NULL || !dbus_message_is_signal(message, REQUEST, "Response") ||
        SDL_strcmp(dbus_message_get_path(message), request) != 0)
    {
        return false;
    }
    DBusMessageIter args;
    dbus_uint32_t code = 2;
    if (dbus_message_iter_init(message, &args)) {
        dbus_message_iter_get_basic(&args, &code);
        if (code == 0 && dbus_message_iter_next(&args) && on_response != NULL) {
            on_response(&args);
        }
    }
    if (code != 0) {
        SDL_Log("desktop-melee: hotkey: the portal said no (%u)", (unsigned)code);
    }
    return true;
}

/* Reads messages until the Response for `request` came (true), or stopping
 * (false). */
static bool wait_for(const char* request, void (*on_response)(DBusMessageIter* results)) {
    while (!SDL_GetAtomicInt(&s_stop)) {
        if (!dbus_connection_read_write(s_bus, 200)) {
            return false; /* the bus went away */
        }
        DBusMessage* message;
        while ((message = dbus_connection_pop_message(s_bus)) != NULL) {
            const bool answered = handle(message, request, on_response);
            dbus_message_unref(message);
            if (answered) {
                return true;
            }
        }
    }
    return false;
}

static void on_session(DBusMessageIter* results) {
    string_entry(results, "session_handle", s_session, sizeof s_session);
}

static void on_bound(DBusMessageIter* results) {
    DBusMessageIter shortcuts;
    if (find_entry(results, "shortcuts", &shortcuts)) {
        read_bound(&shortcuts);
    }
}

/* Names the app (xdg-desktop-portal 1.19 and later). Older portals lack the
 * Registry and take the app as it is. */
static void register_app(void) {
    DBusMessage* m = dbus_message_new_method_call(
        PORTAL_NAME, PORTAL_PATH, "org.freedesktop.host.portal.Registry", "Register");
    DBusMessageIter args;
    DBusMessageIter options;
    const char* app_id = APP_ID;
    dbus_message_iter_init_append(m, &args);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &app_id);
    dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &options);
    dbus_message_iter_close_container(&args, &options);
    call(m);
}

static bool create_session(void) {
    char token[64];
    char path[256];
    next_request(token, sizeof token, path, sizeof path);
    DBusMessage* m =
        dbus_message_new_method_call(PORTAL_NAME, PORTAL_PATH, SHORTCUTS, "CreateSession");
    DBusMessageIter args;
    DBusMessageIter options;
    dbus_message_iter_init_append(m, &args);
    dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &options);
    append_string_entry(&options, "handle_token", token);
    append_string_entry(&options, "session_handle_token", "desktop_melee");
    dbus_message_iter_close_container(&args, &options);
    return call(m) && wait_for(path, on_session) && s_session[0] != '\0';
}

static bool bind_shortcut(void) {
    char token[64];
    char path[256];
    next_request(token, sizeof token, path, sizeof path);
    DBusMessage* m =
        dbus_message_new_method_call(PORTAL_NAME, PORTAL_PATH, SHORTCUTS, "BindShortcuts");
    DBusMessageIter args;
    DBusMessageIter list;
    DBusMessageIter item;
    DBusMessageIter props;
    DBusMessageIter options;
    const char* session = s_session;
    const char* id = SHORTCUT_ID;
    const char* parent = "";
    dbus_message_iter_init_append(m, &args);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_OBJECT_PATH, &session);
    dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "(sa{sv})", &list);
    dbus_message_iter_open_container(&list, DBUS_TYPE_STRUCT, NULL, &item);
    dbus_message_iter_append_basic(&item, DBUS_TYPE_STRING, &id);
    dbus_message_iter_open_container(&item, DBUS_TYPE_ARRAY, "{sv}", &props);
    append_string_entry(&props, "description", "Pause and hide Desktop Melee");
    append_string_entry(&props, "preferred_trigger", "CTRL+ALT+M");
    dbus_message_iter_close_container(&item, &props);
    dbus_message_iter_close_container(&list, &item);
    dbus_message_iter_close_container(&args, &list);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &parent);
    dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &options);
    append_string_entry(&options, "handle_token", token);
    dbus_message_iter_close_container(&args, &options);
    return call(m) && wait_for(path, on_bound);
}

static void SDLCALL fallback_on_main_thread(void* userdata) {
    (void)userdata;
    s_unavailable();
}

static int SDLCALL run(void* userdata) {
    (void)userdata;
    register_app();
    if (!create_session()) {
        SDL_Log("desktop-melee: hotkey: no GlobalShortcuts session");
        if (s_unavailable != NULL && !SDL_GetAtomicInt(&s_stop)) {
            SDL_RunOnMainThread(fallback_on_main_thread, NULL, false);
        }
        return 0;
    }
    if (!bind_shortcut()) {
        SDL_Log("desktop-melee: hotkey: the shortcut wasn't bound");
    }
    /* Pressed from now on: Activated signals. */
    wait_for(NULL, NULL);
    return 0;
}

/* ---- the interface ------------------------------------------------------ */

bool desktop_melee_hotkey_install(void (*unavailable)(void)) {
    if (s_thread != NULL) {
        return true;
    }
    s_unavailable = unavailable;
    dbus_threads_init_default(); /* ConfigureShortcuts calls from the main thread */
    DBusError error;
    dbus_error_init(&error);
    s_bus = dbus_bus_get_private(DBUS_BUS_SESSION, &error);
    if (s_bus == NULL) {
        SDL_Log("desktop-melee: hotkey: no session bus (%s)", error.message);
        dbus_error_free(&error);
        return false;
    }
    dbus_connection_set_exit_on_disconnect(s_bus, false);
    dbus_bus_add_match(s_bus, "type='signal',interface='" REQUEST "',member='Response'", NULL);
    dbus_bus_add_match(s_bus, "type='signal',interface='" SHORTCUTS "'", NULL);
    s_mutex = SDL_CreateMutex();
    SDL_SetAtomicInt(&s_stop, 0);
    s_thread = SDL_CreateThread(run, "desktop-melee hotkey", NULL);
    return s_thread != NULL;
}

const char* desktop_melee_hotkey_label(void) {
    static char label[64];
    if (s_mutex == NULL) {
        return "";
    }
    SDL_LockMutex(s_mutex);
    SDL_strlcpy(label, s_label, sizeof label);
    SDL_UnlockMutex(s_mutex);
    return label;
}

bool desktop_melee_hotkey_configure(void) {
    if (s_bus == NULL || s_session[0] == '\0') {
        return false;
    }
    DBusMessage* m =
        dbus_message_new_method_call(PORTAL_NAME, PORTAL_PATH, SHORTCUTS, "ConfigureShortcuts");
    DBusMessageIter args;
    DBusMessageIter options;
    const char* session = s_session;
    const char* parent = "";
    dbus_message_iter_init_append(m, &args);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_OBJECT_PATH, &session);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &parent);
    dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &options);
    dbus_message_iter_close_container(&args, &options);
    return call(m); /* the new trigger comes as ShortcutsChanged */
}

void desktop_melee_hotkey_remove(void) {
    if (s_thread == NULL) {
        return;
    }
    SDL_SetAtomicInt(&s_stop, 1);
    SDL_WaitThread(s_thread, NULL);
    s_thread = NULL;
    dbus_connection_close(s_bus);
    dbus_connection_unref(s_bus);
    s_bus = NULL;
}
