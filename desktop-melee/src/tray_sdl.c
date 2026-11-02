/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Desktop Melee's tray icon and menu (tray_sdl.h). */
#include "tray_sdl.h"

#include <SDL3/SDL.h>
#include <math.h>
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

/* Drawn from scratch, never from the disc: a floor with two fighters on it,
 * in the player colours (P1 #E83B38, P2 #3D75F5). */
enum { ICON_SIZE = 64 };

/* Coverage of a pixel by a rounded rectangle, as a smooth edge. */
static float rounded_rect(float x, float y, float cx, float cy, float hw, float hh, float r) {
    const float qx = fabsf(x - cx) - (hw - r);
    const float qy = fabsf(y - cy) - (hh - r);
    const float ox = qx > 0.0f ? qx : 0.0f;
    const float oy = qy > 0.0f ? qy : 0.0f;
    const float inside = qx > qy ? (qx < 0.0f ? qx : 0.0f) : (qy < 0.0f ? qy : 0.0f);
    const float distance = sqrtf(ox * ox + oy * oy) + inside - r;
    const float coverage = 0.5f - distance;
    return coverage < 0.0f ? 0.0f : coverage > 1.0f ? 1.0f : coverage;
}

static void blend(Uint8* px, float coverage, Uint8 r, Uint8 g, Uint8 b) {
    if (coverage <= 0.0f) {
        return;
    }
    /* Straight alpha: "over" onto what is there. */
    const float under = px[3] / 255.0f;
    const float out = coverage + under * (1.0f - coverage);
    const Uint8 color[3] = {r, g, b};
    for (int i = 0; i < 3; i++) {
        const float mixed =
            (color[i] * coverage + px[i] * under * (1.0f - coverage)) / (out > 0.0f ? out : 1.0f);
        px[i] = (Uint8)(mixed + 0.5f);
    }
    px[3] = (Uint8)(out * 255.0f + 0.5f);
}

static SDL_Surface* make_icon(void) {
    SDL_Surface* icon = SDL_CreateSurface(ICON_SIZE, ICON_SIZE, SDL_PIXELFORMAT_RGBA32);
    if (icon == NULL) {
        return NULL;
    }
    SDL_memset(icon->pixels, 0, (size_t)icon->pitch * ICON_SIZE);
    for (int y = 0; y < ICON_SIZE; y++) {
        Uint8* row = (Uint8*)icon->pixels + (size_t)y * icon->pitch;
        for (int x = 0; x < ICON_SIZE; x++) {
            Uint8* px = row + x * 4;
            const float fx = x + 0.5f;
            const float fy = y + 0.5f;
            /* The floor: a wide bar near the bottom. */
            blend(px, rounded_rect(fx, fy, 32.0f, 51.0f, 29.0f, 6.0f, 4.0f), 0xEE, 0xEE, 0xF2);
            /* P1 and P2: rounded bodies standing on it. */
            blend(px, rounded_rect(fx, fy, 19.0f, 31.0f, 8.0f, 14.0f, 7.0f), 0xE8, 0x3B, 0x38);
            blend(px, rounded_rect(fx, fy, 45.0f, 31.0f, 8.0f, 14.0f, 7.0f), 0x3D, 0x75, 0xF5);
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
}

void desktop_melee_tray_remove(void) {
    if (s_tray != NULL) {
        SDL_DestroyTray(s_tray);
        s_tray = NULL;
    }
}
