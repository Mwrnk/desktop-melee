/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "pet.h"

#include <math.h>
#include <stdlib.h>

#include "pc/widescreen.h"

#include <SDL3/SDL_log.h>

static int s_overlay = -1;

/* Screen geometry in points (y up from the bottom edge). */
static float s_screen_w = 0.0f;
static float s_screen_h = 0.0f;
static float s_floor = 0.0f;
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
#endif
}

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
