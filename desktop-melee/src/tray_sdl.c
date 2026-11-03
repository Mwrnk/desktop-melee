/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Desktop Melee's tray icon and menu (tray_sdl.h). */
#include "tray_sdl.h"

#include <SDL3/SDL.h>
#include <stdio.h>

#include "../ui/bridge/desktop_melee_bridge.h"
#include "settings_file.h"
#include "settings_window.h"
#include "welcome_window.h"

/* desktop_melee_sound_menu kinds (ui-guide.md section 1). */
enum { SOUND_BACK = 0, SOUND_CONFIRM = 1 };

static SDL_Tray* s_tray = NULL;
static bool s_assets_ready = false;

/* ---- the icon ----------------------------------------------------------- */

/* The Smash emblem, as on macOS (ui/Emblem.swift, ui/MenuBar.swift): a
 * stand-in drawn here until the game has decoded Melee's own from the
 * player's disc, so the repository holds no game artwork. White, for a dark
 * panel. */
enum { ICON_SIZE = 64, ICON_SAMPLES = 4 };

static bool in_rounded_rect(float x, float y, float left, float top, float w, float h) {
    const float r = h / 2.0f;
    if (x < left || x > left + w || y < top || y > top + h) {
        return false;
    }
    const float cx = x < left + r ? left + r : x > left + w - r ? left + w - r : x;
    const float dx = x - cx;
    const float dy = y - (top + r);
    return dx * dx + dy * dy <= r * r;
}

/* Even-odd point in polygon. */
static bool in_polygon(float x, float y, const float (*points)[2], int count) {
    bool inside = false;
    for (int i = 0, j = count - 1; i < count; j = i++) {
        const float xi = points[i][0], yi = points[i][1];
        const float xj = points[j][0], yj = points[j][1];
        if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) {
            inside = !inside;
        }
    }
    return inside;
}

/* A small stage in a unit square: three floating platforms over a floor. */
static bool in_stand_in(float x, float y) {
    const float bar = 0.11f;
    const float platforms[3][2] = {{0.36f, 0.18f}, {0.10f, 0.40f}, {0.62f, 0.40f}};
    for (int i = 0; i < 3; i++) {
        if (in_rounded_rect(x, y, platforms[i][0], platforms[i][1], 0.28f, bar)) {
            return true;
        }
    }
    static const float floor[8][2] = {{0.04f, 0.62f}, {0.96f, 0.62f}, {0.96f, 0.73f},
        {0.82f, 0.73f}, {0.64f, 0.90f}, {0.36f, 0.90f}, {0.18f, 0.73f}, {0.04f, 0.73f}};
    return in_polygon(x, y, floor, 8);
}

static SDL_Surface* make_icon(void) {
    SDL_Surface* icon = SDL_CreateSurface(ICON_SIZE, ICON_SIZE, SDL_PIXELFORMAT_RGBA32);
    if (icon == NULL) {
        return NULL;
    }
    for (int y = 0; y < ICON_SIZE; y++) {
        Uint8* row = (Uint8*)icon->pixels + (size_t)y * icon->pitch;
        for (int x = 0; x < ICON_SIZE; x++) {
            int hits = 0;
            for (int sy = 0; sy < ICON_SAMPLES; sy++) {
                for (int sx = 0; sx < ICON_SAMPLES; sx++) {
                    const float fx = (x + (sx + 0.5f) / ICON_SAMPLES) / ICON_SIZE;
                    const float fy = (y + (sy + 0.5f) / ICON_SAMPLES) / ICON_SIZE;
                    hits += in_stand_in(fx, fy);
                }
            }
            Uint8* px = row + x * 4;
            px[0] = px[1] = px[2] = 0xff;
            px[3] = (Uint8)(hits * 255 / (ICON_SAMPLES * ICON_SAMPLES));
        }
    }
    return icon;
}

/* Melee's emblem (desktop_melee_named_alpha, an alpha mask), white and
 * centred in a square. */
static SDL_Surface* make_emblem_icon(void) {
    int w = 0;
    int h = 0;
    const Uint8* alpha = desktop_melee_named_alpha("emblem", &w, &h);
    if (alpha == NULL || w <= 0 || h <= 0) {
        return NULL;
    }
    const int side = w > h ? w : h;
    SDL_Surface* icon = SDL_CreateSurface(side, side, SDL_PIXELFORMAT_RGBA32);
    if (icon == NULL) {
        return NULL;
    }
    SDL_memset(icon->pixels, 0, (size_t)icon->pitch * side);
    const int left = (side - w) / 2;
    const int top = (side - h) / 2;
    for (int y = 0; y < h; y++) {
        Uint8* row = (Uint8*)icon->pixels + (size_t)(y + top) * icon->pitch;
        for (int x = 0; x < w; x++) {
            Uint8* px = row + (x + left) * 4;
            px[0] = px[1] = px[2] = 0xff;
            px[3] = alpha[(size_t)y * w + x];
        }
    }
    return icon;
}

/* ---- the actions -------------------------------------------------------- */

static void play_sound(int kind) {
    if (s_assets_ready && desktop_melee_settings()->menu_sounds) {
        desktop_melee_sound_menu(kind);
    }
}

static void on_match(void* userdata, SDL_TrayEntry* entry) {
    (void)userdata;
    (void)entry;
    play_sound(SOUND_CONFIRM);
    desktop_melee_request_match();
}

static void on_pause(void* userdata, SDL_TrayEntry* entry) {
    (void)userdata;
    (void)entry;
    play_sound(SOUND_CONFIRM);
    desktop_melee_toggle_pause_and_hide();
}

static void on_settings(void* userdata, SDL_TrayEntry* entry) {
    (void)userdata;
    (void)entry;
    play_sound(SOUND_CONFIRM);
    desktop_melee_settings_window_open();
}

static void on_how_to_play(void* userdata, SDL_TrayEntry* entry) {
    (void)userdata;
    (void)entry;
    play_sound(SOUND_CONFIRM);
    desktop_melee_welcome_how_to_play();
}

static void on_quit(void* userdata, SDL_TrayEntry* entry) {
    (void)userdata;
    (void)entry;
    play_sound(SOUND_BACK);
    desktop_melee_request_quit();
}

/* ---- the menu ----------------------------------------------------------- */

static void add_entry(SDL_TrayMenu* menu, const char* label, SDL_TrayCallback callback) {
    SDL_TrayEntry* entry = SDL_InsertTrayEntryAt(menu, -1, label, SDL_TRAYENTRY_BUTTON);
    if (entry != NULL && callback != NULL) {
        SDL_SetTrayEntryCallback(entry, callback, NULL);
    }
}

bool desktop_melee_tray_install(void) {
    if (s_tray != NULL) {
        return true;
    }
    SDL_Surface* icon = make_icon();
    s_tray = SDL_CreateTray(icon, "Desktop Melee");
    SDL_DestroySurface(icon);
    if (s_tray == NULL) {
        SDL_Log("desktop-melee: tray: none on this desktop (%s)", SDL_GetError());
        return false;
    }
    SDL_TrayMenu* menu = SDL_CreateTrayMenu(s_tray);
    /* The macOS menu's labels (ui-guide.md section 14). The game does not
     * call the UI back, so the match entry can't switch between "Start" and
     * "Restart"; desktop_melee_request_match does both. */
    add_entry(menu, "Start or restart match", on_match);
    add_entry(menu, "Pause and hide", on_pause);
    SDL_InsertTrayEntryAt(menu, -1, NULL, 0); /* separator */
    add_entry(menu, "Settings...", on_settings);
    add_entry(menu, "How to play", on_how_to_play);
    SDL_InsertTrayEntryAt(menu, -1, NULL, 0);
    add_entry(menu, "Quit Desktop Melee", on_quit);
    SDL_Log("desktop-melee: tray: installed");
    return true;
}

void desktop_melee_tray_assets_ready(void) {
    s_assets_ready = true;
    /* Swap the stand-in for Melee's own emblem, as macOS does. */
    SDL_Surface* emblem = s_tray != NULL ? make_emblem_icon() : NULL;
    if (emblem != NULL) {
        SDL_SetTrayIcon(s_tray, emblem);
        SDL_DestroySurface(emblem);
    }
}

void desktop_melee_tray_remove(void) {
    if (s_tray != NULL) {
        SDL_DestroyTray(s_tray);
        s_tray = NULL;
    }
}
