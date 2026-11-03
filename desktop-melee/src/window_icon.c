/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The app icon on every window (window_icon.h). */
#include "window_icon.h"

#include <SDL3/SDL.h>

void desktop_melee_set_window_icons(void) {
    static SDL_Surface* icon = NULL;
    static bool tried = false;
    if (!tried) {
        tried = true;
        const char* base = SDL_GetBasePath();
        char path[1024];
        SDL_snprintf(path, sizeof path, "%sresources/desktop-melee/app-icon.png",
            base != NULL ? base : "./");
        icon = SDL_LoadPNG(path);
        if (icon == NULL) {
            SDL_Log("desktop-melee: no window icon (%s)", SDL_GetError());
        }
    }
    if (icon == NULL) {
        return;
    }
    int count = 0;
    SDL_Window** windows = SDL_GetWindows(&count);
    for (int i = 0; windows != NULL && i < count; i++) {
        SDL_SetWindowIcon(windows[i], icon);
    }
    SDL_free(windows);
}
