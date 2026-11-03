/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Desktop Melee's .desktop file (desktop_entry.h). */
#include "desktop_entry.h"

#include <SDL3/SDL.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>

/* Quotes a path for a .desktop Exec line: in double quotes, with " ` $ and \
 * escaped. */
static void quote(const char* path, char* out, size_t size) {
    size_t n = 0;
    out[n++] = '"';
    for (const char* c = path; *c != '\0' && n + 3 < size; c++) {
        if (*c == '"' || *c == '`' || *c == '$' || *c == '\\') {
            out[n++] = '\\';
        }
        out[n++] = *c;
    }
    out[n++] = '"';
    out[n] = '\0';
}

/* The template with @EXEC@ and @ICON@ filled in, or NULL. SDL_free it. */
static char* fill(const char* template_text, const char* exec, const char* icon) {
    const size_t size = strlen(template_text) + strlen(exec) + strlen(icon) + 1;
    char* out = SDL_malloc(size);
    if (out == NULL) {
        return NULL;
    }
    size_t n = 0;
    for (const char* p = template_text; *p != '\0';) {
        const char* value = NULL;
        size_t skip = 0;
        if (strncmp(p, "@EXEC@", 6) == 0) {
            value = exec;
            skip = 6;
        } else if (strncmp(p, "@ICON@", 6) == 0) {
            value = icon;
            skip = 6;
        }
        if (value != NULL) {
            const size_t len = strlen(value);
            memcpy(out + n, value, len);
            n += len;
            p += skip;
        } else {
            out[n++] = *p++;
        }
    }
    out[n] = '\0';
    return out;
}

/* Writes `size` bytes to path, in a folder made if needed, unless the file
 * already holds exactly them. */
static void save_if_changed(const char* folder, const char* path, const void* data, size_t size) {
    size_t old_size = 0;
    void* old = SDL_LoadFile(path, &old_size);
    const bool same = old != NULL && old_size == size && memcmp(old, data, size) == 0;
    SDL_free(old);
    if (!same) {
        SDL_CreateDirectory(folder);
        if (SDL_SaveFile(path, data, size)) {
            SDL_Log("desktop-melee: wrote %s", path);
        }
    }
}

void desktop_melee_desktop_entry_install(void) {
    /* The program to start: an AppImage runs from a mount that moves every
     * launch, so the entry names the AppImage file itself ($APPIMAGE). */
    char exe[PATH_MAX];
    const char* appimage = SDL_getenv("APPIMAGE");
    if (appimage != NULL && appimage[0] == '/') {
        SDL_strlcpy(exe, appimage, sizeof exe);
    } else {
        const ssize_t length = readlink("/proc/self/exe", exe, sizeof exe - 1);
        if (length <= 0) {
            return;
        }
        exe[length] = '\0';
    }
    const char* base = SDL_GetBasePath();
    const char* home = SDL_getenv("HOME");
    if (base == NULL || home == NULL) {
        return;
    }
    /* $XDG_DATA_HOME, ~/.local/share by default. */
    const char* xdg = SDL_getenv("XDG_DATA_HOME");
    char data[PATH_MAX];
    if (xdg != NULL && xdg[0] == '/') {
        SDL_strlcpy(data, xdg, sizeof data);
    } else {
        SDL_snprintf(data, sizeof data, "%s/.local/share", home);
    }

    /* The icon, copied out of the game's folder for the same reason, and
     * named by the app id in the icon theme. */
    char folder[PATH_MAX];
    char path[PATH_MAX];
    SDL_snprintf(path, sizeof path, "%sresources/desktop-melee/app-icon.png", base);
    size_t icon_size = 0;
    void* icon = SDL_LoadFile(path, &icon_size);
    if (icon != NULL) {
        SDL_snprintf(folder, sizeof folder, "%s/icons/hicolor/128x128/apps", data);
        SDL_snprintf(path, sizeof path, "%s/" DESKTOP_MELEE_APP_ID ".png", folder);
        save_if_changed(folder, path, icon, icon_size);
        SDL_free(icon);
    }

    SDL_snprintf(
        path, sizeof path, "%sresources/desktop-melee/" DESKTOP_MELEE_APP_ID ".desktop.in", base);
    char* template_text = SDL_LoadFile(path, NULL);
    if (template_text == NULL) {
        return;
    }
    char exec[PATH_MAX + 64];
    quote(exe, exec, sizeof exec);
    char* entry = fill(template_text, exec, DESKTOP_MELEE_APP_ID);
    SDL_free(template_text);
    if (entry == NULL) {
        return;
    }
    SDL_snprintf(folder, sizeof folder, "%s/applications", data);
    SDL_snprintf(path, sizeof path, "%s/" DESKTOP_MELEE_APP_ID ".desktop", folder);
    save_if_changed(folder, path, entry, strlen(entry));
    SDL_free(entry);
}
