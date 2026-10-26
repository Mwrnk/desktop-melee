/* SPDX-License-Identifier: GPL-3.0-or-later */
/* melee-pet hooks called from the decompiled game code (melee_game). */
#pragma once
#include <stdbool.h>

/* MELEE_PET_OVERLAY=1: run as a transparent, click-through window over the
 * whole primary display instead of a normal window. */
bool pet_overlay_enabled(void);

/* Desktop overlay: stage and background models are not drawn, so only the
 * fighters, effects and HUD appear over the desktop. */
bool pet_hide_stage(void);

/* Fixed desktop camera, replacing Melee's zooming one in overlay mode. Fills
 * the eye and interest points (Melee units) and the vertical field of view
 * (degrees) so that Melee y = 0 sits on top of the Dock, x = 0 is the middle
 * of the screen, and the screen is MELEE_PET_SCALE units wide (default 490,
 * Final Destination's blastzone width). False when not in overlay mode. */
bool pet_camera(float* eye_x, float* eye_y, float* eye_z, float* interest_x,
                float* interest_y, float* interest_z, float* fov);

/* The Dock's left and right edges in Melee units (x), for stretching the main
 * stage floor across it. False when not in overlay mode. */
bool pet_dock_span(float* left, float* right);

/* The blastzones (Melee units): the overlay's screen edges, at the size the
 * current match uses. False when not in overlay mode. */
bool pet_blastzones(float* left, float* right, float* top, float* bottom);

/* HUD camera, reported once when the HUD scene loads: distance from the eye
 * to the HUD plane (Melee units) and vertical field of view (degrees). */
void pet_set_hud_camera(float distance, float fov_deg);
/* Called every frame the HUD camera renders; the notch island is shown while
 * these keep coming and the HUD is visible. */
void pet_hud_frame(bool visible);
/* Stock icons inside the notch island: the translation and uniform scale (HUD
 * units) for a player's stock group, P1 left of the notch and P2 right of it.
 * False keeps Melee's own placement. */
bool pet_hud_stock_anchor(int player, float* x, float* y, float* scale);
/* The same for a player's percent (damage) group. */
bool pet_hud_percent_anchor(int player, float* x, float* y, float* scale);
/* True when this player's HUD lives in the notch island (its series emblem
 * is then not drawn). */
bool pet_hud_in_island(int player);

/* ---- GM_PET match flow (pet/game/gmpetmode.c) ---------------------------- */

typedef struct PetPlayerSetup {
    int ckind;     /* CharacterKind (2 = Fox) */
    int costume;   /* costume/color index */
    int cpu;       /* 0 human, 1 CPU */
    int cpu_level; /* 1..9 */
    int port;      /* controller port 0..3 that plays this player (humans) */
} PetPlayerSetup;

typedef struct PetMatchSetup {
    int stocks;       /* 1..99 */
    int time_seconds; /* 0 = no time limit */
    PetPlayerSetup players[2];
} PetMatchSetup;

/* The next match's setup, as the menu bar left it. */
void pet_match_setup(PetMatchSetup* out);
/* Ask for a match: starts one from idle, restarts a running one. */
void pet_request_match(void);
/* Idle scene: a match has been asked for. */
bool pet_match_wanted(void);
/* VS scene: a new match was asked for while this one runs. */
bool pet_match_restart_wanted(void);
/* The pet VS state entered / left. */
void pet_match_began(void);
void pet_match_ended(void);

/* ---- per-player controls (pet/src/pet.c) ---------------------------------- */
/* Set from the menu bar's Controls tab; read every frame, so changes apply
 * mid-match. Players are fighter slots (P1 = 0). */

/* The Melee buttons a player's button map covers. */
enum {
    PET_BTN_A,
    PET_BTN_B,
    PET_BTN_X,
    PET_BTN_Y,
    PET_BTN_Z,
    PET_BTN_L,
    PET_BTN_R,
    PET_BTN_COUNT,
    PET_BTN_NONE = PET_BTN_COUNT,
};

/* Tap jump: stick up jumps (Melee's own behavior, on by default). Called
 * where a stick-up jump would start; false blocks it. */
bool pet_tap_jump(int player);
/* A human player's held buttons (HSD_PAD_* bits) after their button map.
 * Also says which analog triggers still shield: bit 0 the physical L, bit 1
 * the physical R (a trigger mapped to another button no longer does). */
unsigned pet_map_buttons(int player, unsigned held, int* analog_mask);

/* ---- game assets (pet/game/petassets.c) ---------------------------------- */

struct GcTexture;
/* A texture found in a game archive: tag names where it came from (model and
 * texture-animation table), index is its position there. */
void pet_asset_found(const char* tag, int index, const struct GcTexture* tex);

/* The character select grid: 25 characters, Melee's order. */
#define PET_ROSTER_SIZE 25
#define PET_MAX_COSTUMES 6
/* Roster entry i: character kind, costume count and face icon. */
void pet_asset_roster(int i, int ckind, int costumes, const struct GcTexture* icon);
/* How many costumes a character kind has (every playable kind, Sheik too). */
void pet_asset_costumes(int ckind, int costumes);
/* A character's portrait for one costume. */
void pet_asset_portrait(int ckind, int costume, const struct GcTexture* portrait);
/* The roster and font are loaded: tells the menu bar app. */
void pet_assets_ready(void);

/* Melee's menu sounds, for the menu bar: 0 back, 1 confirm, 2 cursor move,
 * 3 not allowed. */
void pet_sound_menu(int kind);
/* The announcer says the name of roster entry i. */
void pet_sound_announce(int roster_index);
/* A named graphic for the menu bar ("emblem", "melee"). */
void pet_asset_named(const char* name, const struct GcTexture* tex);
