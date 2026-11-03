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

void desktop_melee_desktop_entry_install(void) {
    char exe[PATH_MAX];
    const ssize_t length = readlink("/proc/self/exe", exe, sizeof exe - 1);
    const char* base = SDL_GetBasePath();
    const char* home = SDL_getenv("HOME");
    if (length <= 0 || base == NULL || home == NULL) {
        return;
    }
    exe[length] = '\0';

    char path[PATH_MAX];
    SDL_snprintf(
        path, sizeof path, "%sresources/desktop-melee/" DESKTOP_MELEE_APP_ID ".desktop.in", base);
    char* template_text = SDL_LoadFile(path, NULL);
    if (template_text == NULL) {
        return;
    }
    char exec[PATH_MAX + 64];
    quote(exe, exec, sizeof exec);
    char icon[PATH_MAX];
    SDL_snprintf(icon, sizeof icon, "%sresources/desktop-melee/app-icon.png", base);
    char* entry = fill(template_text, exec, icon);
    SDL_free(template_text);
    if (entry == NULL) {
        return;
    }

    /* $XDG_DATA_HOME/applications, ~/.local/share/applications by default. */
    const char* data = SDL_getenv("XDG_DATA_HOME");
    char folder[PATH_MAX];
    if (data != NULL && data[0] == '/') {
        SDL_snprintf(folder, sizeof folder, "%s/applications", data);
    } else {
        SDL_snprintf(folder, sizeof folder, "%s/.local/share/applications", home);
    }
    SDL_snprintf(path, sizeof path, "%s/" DESKTOP_MELEE_APP_ID ".desktop", folder);
    size_t old_size = 0;
    char* old = SDL_LoadFile(path, &old_size);
    const bool same = old != NULL && old_size == strlen(entry) && memcmp(old, entry, old_size) == 0;
    SDL_free(old);
    if (!same) {
        SDL_CreateDirectory(folder);
        if (SDL_SaveFile(path, entry, strlen(entry))) {
            SDL_Log("desktop-melee: wrote %s", path);
        }
    }
    SDL_free(entry);
}
