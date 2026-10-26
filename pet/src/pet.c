/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "pet.h"

#include <math.h>
#include <stdlib.h>

#include "pc/widescreen.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_timer.h>

static int s_overlay = -1;

/* Screen geometry in points (y up from the bottom edge). */
static float s_screen_w = 0.0f;
static float s_screen_h = 0.0f;
static float s_floor = 0.0f;
/* Notch island, in points from the screen's left/top edges. */
static float s_notch_left = 0.0f;
static float s_notch_right = 0.0f;
static float s_island_h = 0.0f;
/* HUD camera (see pet_set_hud_camera) and the last HUD frame. */
static float s_hud_distance = 0.0f;
static float s_hud_fov = 0.0f;
static volatile Uint64 s_hud_last_ms = 0;
static volatile bool s_hud_visible = false;

/* Melee's stock group layout (IfAll stock model): icon k's offset from the
 * group origin is (-5.0 + 2.4 k, 7.5) HUD units. */
#define STOCK_X0 (-5.0f)
#define STOCK_Y (7.5f)
#define STOCK_PITCH (2.4f)
/* Icons shown per side of the notch (the match is 4 stocks). */
#define ISLAND_STOCKS 4

/* Melee's damage (percent) group layout: hundreds, tens and ones digits at
 * x = -4.68, -1.53, 1.62 and the % sign at 5.04. Their animation settles the
 * digits ~2.2 units above the group origin (the model's rest pose, -8.5, is
 * only the first frame). A digit is ~2.8 units tall. The number is
 * right-aligned, so its full extent (three digits and %) is reserved. */
#define PCT_LEFT (-6.2f)
#define PCT_RIGHT (6.3f)
#define PCT_Y (2.2f)
#define PCT_DIGIT_H (2.8f)

/* Island layout, as fractions of the island height. Per side, from the
 * notch outwards: pad, stocks, gap, percent, end. */
#define ISLAND_PAD (0.40f)     /* gap between the notch and the first icon */
#define ISLAND_PITCH (0.62f)   /* stock icon spacing */
#define ISLAND_GAP (0.35f)     /* between the stocks and the percent */
#define ISLAND_DIGIT_H (0.60f) /* percent digit height */
#define ISLAND_END (0.40f)     /* black margin past the percent */
#define ISLAND_PCT_W (ISLAND_DIGIT_H * (PCT_RIGHT - PCT_LEFT) / PCT_DIGIT_H)
#define ISLAND_SIDE                                                                   \
    (ISLAND_PAD + ISLAND_PITCH * ISLAND_STOCKS + ISLAND_GAP + ISLAND_PCT_W + ISLAND_END)

/* Dock bar edges in points; equal when unknown. */
static float s_dock_left = 0.0f;
static float s_dock_right = 0.0f;

bool pet_overlay_enabled(void) {
    if (s_overlay < 0) {
        const char* env = getenv("MELEE_PET_OVERLAY");
        s_overlay = env != NULL && env[0] != '\0' && env[0] != '0';
    }
    return s_overlay != 0;
}

bool pet_hide_stage(void) {
    return pet_overlay_enabled();
}

void pet_configure(AuroraConfig* config) {
    if (!pet_overlay_enabled()) {
        return;
    }
    config->overlayWindow = true;
    /* A menu bar app: SDL must not turn the process into a regular app with a
     * Dock icon (pet_ui_install makes it an accessory app). */
    SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
    config->startFullscreen = false;
    /* The overlay never asks for focus, so losing it must not pause the game,
     * and controllers must keep working while other apps are in front. */
    config->pauseOnFocusLost = false;
    config->allowJoystickBackgroundEvents = true;
}

void pet_window_created(SDL_Window* window) {
    if (!pet_overlay_enabled() || window == NULL) {
        return;
    }
#ifdef __APPLE__
    pet_overlay_mac_setup(window);
    float w, h, floor_pt;
    pet_mac_screen_geometry(window, &w, &h, &floor_pt);
    float dock_left, dock_right, dock_top = floor_pt;
    bool exact;
    pet_mac_dock_frame(w, h, &dock_left, &dock_right, &dock_top, &exact);
    if (exact) {
        floor_pt = dock_top;
    }
    pet_set_screen_geometry(w, h, floor_pt);
    s_dock_left = dock_left;
    s_dock_right = dock_right;
    SDL_Log("pet: Dock %.0f..%.0f pt (%s)", dock_left, dock_right,
            exact ? "exact" : "estimated; grant Accessibility for the exact size");

    pet_mac_notch(&s_notch_left, &s_notch_right, &s_island_h);
    const float side = s_island_h * ISLAND_SIDE;
    pet_mac_island_create(s_notch_left - side, s_notch_right + side, s_island_h);
    SDL_Log("pet: notch %.0f..%.0f pt, island %.0f..%.0f x %.0f pt", s_notch_left,
            s_notch_right, s_notch_left - side, s_notch_right + side, s_island_h);
#endif
}

void pet_set_hud_camera(float distance, float fov_deg) {
    s_hud_distance = distance;
    s_hud_fov = fov_deg;
}

void pet_hud_frame(bool visible) {
    s_hud_last_ms = SDL_GetTicks();
    s_hud_visible = visible;
}

bool pet_island_wanted(void) {
    return pet_overlay_enabled() && s_hud_visible && SDL_GetTicks() - s_hud_last_ms < 250;
}

/* Points per HUD unit on the HUD plane. The projection is widened to the
 * window's aspect, so the scale is the same horizontally. 0 when unknown. */
static float hud_ppu(int player) {
    if (!pet_overlay_enabled() || (player != 0 && player != 1) || s_island_h <= 0.0f ||
        s_hud_distance <= 0.0f || s_hud_fov <= 0.0f || s_screen_h <= 0.0f) {
        return 0.0f;
    }
    const float half_h = s_hud_distance * tanf(0.5f * s_hud_fov * (float)M_PI / 180.0f);
    return s_screen_h / (2.0f * half_h);
}

/* Screen point (from the left/top edges) to HUD coordinates. */
static float hud_x(float x_pt, float ppu) { return (x_pt - 0.5f * s_screen_w) / ppu; }
static float hud_y(float y_pt, float ppu) { return (0.5f * s_screen_h - y_pt) / ppu; }

bool pet_hud_stock_anchor(int player, float* x, float* y, float* scale) {
    const float ppu = hud_ppu(player);
    if (ppu <= 0.0f) {
        return false;
    }
    const float h = s_island_h;
    const float pitch_pt = h * ISLAND_PITCH;
    /* Centre of the first icon: P1's stocks run leftwards from the notch, so
     * its first stock is the outermost; P2's start right next to it. */
    const float first_pt = player == 0
                               ? s_notch_left - h * ISLAND_PAD - pitch_pt * (ISLAND_STOCKS - 0.5f)
                               : s_notch_right + h * ISLAND_PAD + 0.5f * pitch_pt;
    const float s = pitch_pt / (STOCK_PITCH * ppu);
    *scale = s;
    *x = hud_x(first_pt, ppu) - STOCK_X0 * s;
    *y = hud_y(0.5f * h, ppu) - STOCK_Y * s;
    return true;
}

bool pet_hud_percent_anchor(int player, float* x, float* y, float* scale) {
    const float ppu = hud_ppu(player);
    if (ppu <= 0.0f) {
        return false;
    }
    const float h = s_island_h;
    const float inner = h * (ISLAND_PAD + ISLAND_PITCH * ISLAND_STOCKS + ISLAND_GAP);
    const float s = h * ISLAND_DIGIT_H / (PCT_DIGIT_H * ppu);
    /* The percent's reserved box sits just outside the stocks. */
    float origin_pt;
    if (player == 0) {
        const float box_right = s_notch_left - inner;
        origin_pt = box_right - PCT_RIGHT * s * ppu;
    } else {
        const float box_left = s_notch_right + inner;
        origin_pt = box_left - PCT_LEFT * s * ppu;
    }
    *scale = s;
    *x = hud_x(origin_pt, ppu);
    *y = hud_y(0.5f * h, ppu) - PCT_Y * s;
    return true;
}

bool pet_hud_in_island(int player) { return hud_ppu(player) > 0.0f; }

void pet_set_screen_geometry(float width_pt, float height_pt, float floor_pt) {
    s_screen_w = width_pt;
    s_screen_h = height_pt;
    s_floor = floor_pt;
    SDL_Log("pet: screen %.0fx%.0f pt, floor (Dock top) at %.0f pt", width_pt, height_pt,
            floor_pt);
}

/* Melee units across the whole screen width. */
static float screen_units(void) {
    static float units = 0.0f;
    if (units <= 0.0f) {
        const char* env = getenv("MELEE_PET_SCALE");
        units = env != NULL ? (float)atof(env) : 0.0f;
        if (!(units >= 50.0f && units <= 5000.0f)) {
            units = 490.0f;
        }
    }
    return units;
}

bool pet_camera(float* eye_x, float* eye_y, float* eye_z, float* interest_x,
                float* interest_y, float* interest_z, float* fov) {
    if (!pet_overlay_enabled() || s_screen_w <= 0.0f || s_screen_h <= 0.0f) {
        return false;
    }
    /* A narrow perspective looking straight down -z: the z = 0 plane (where the
     * fighters and the stage collision live) maps linearly onto the screen,
     * while depth effects such as Star KO still shrink into the distance. */
    const float fov_deg = 30.0f;
    const float units_per_pt = screen_units() / s_screen_w;
    const float view_h = s_screen_h * units_per_pt;
    const float center_y = (0.5f * s_screen_h - s_floor) * units_per_pt;
    const float distance = 0.5f * view_h / tanf(0.5f * fov_deg * (float)M_PI / 180.0f);
    *interest_x = 0.0f;
    *interest_y = center_y;
    *interest_z = 0.0f;
    *eye_x = 0.0f;
    *eye_y = center_y;
    *eye_z = distance;
    *fov = fov_deg;
    return true;
}

bool pet_dock_span(float* left, float* right) {
    if (!pet_overlay_enabled() || s_screen_w <= 0.0f || s_dock_right <= s_dock_left) {
        return false;
    }
    const float units_per_pt = screen_units() / s_screen_w;
    *left = (s_dock_left - 0.5f * s_screen_w) * units_per_pt;
    *right = (s_dock_right - 0.5f * s_screen_w) * units_per_pt;
    return true;
}

void pet_after_menu_init(void) {
    if (!pet_overlay_enabled()) {
        return;
    }
    /* "Auto (window aspect)": the match fills the whole screen, not a 4:3 box. */
    pc_widescreen_set_mode(2);
}

void pet_request_quit(void) {
    SDL_Event event;
    SDL_zero(event);
    event.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&event);
}
