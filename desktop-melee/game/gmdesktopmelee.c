/* SPDX-License-Identifier: GPL-3.0-or-later */
/* GM_PET (see gmpetmode.h). Built with the game code (melee_game). */
#include "gmpetmode.h"

#include <dolphin/os.h>
#include <melee/gm/forward.h>
#include <melee/gm/gm_1601.h>
#include <melee/gm/gm_1A36.h>
#include <melee/gm/gm_1A3F.h>
#include <melee/gm/gmscene.h>
#include <melee/gm/gmvs.h>
#include <melee/gm/gmvsmelee.h>
#include <melee/gm/types.h>
#include <melee/lb/forward.h>
#include <melee/mn/types.h>
#include <sysdolphin/baselib/controller.h>

#include "pet_game.h"
#include "petassets.h"

#include <stdlib.h>

enum {
    state_pet_idle = 0,
    state_pet_vs = 1,
};

static void onEnterVs(GameModeState* state);
static void onExitVs(GameModeState* state);

GameModeState gm_Mode_Pet_States[] = {
    {
        state_pet_idle,
        lbDvdPreload_2,
        0,
        NULL,
        NULL,
        {
            GS_PET_IDLE,
            NULL,
            NULL,
        },
    },
    {
        state_pet_vs,
        lbDvdPreload_2,
        0,
        onEnterVs,
        onExitVs,
        {
            GS_VS,
            &gmVsMelee_StartData,
            &gmVsMelee_VsExitInfo,
        },
    },
    { GM_GAMEMODESTATE_TERMINATE },
};

/* Pause and hide pauses as a chosen port; otherwise Melee's own check. */
static int pet_pauser_override(void)
{
    int port = pet_take_pause_request();
    return port >= 0 ? port : gm_DefaultVSGetPauser();
}

bool pet_game_paused(void)
{
    return gm_GetDbPauseFlag(1) || gm_GetDbPauseFlag(2);
}

/* The match as the menu bar set it up: Final Destination (stretched to the
 * Dock in overlay mode), items off. */
static void onEnterVs(GameModeState* state)
{
    StartMeleeData* start = gm_GetGameModeStateEnterData(state);
    PetMatchSetup setup;
    ssize_t i;

    pet_match_setup(&setup);
    gm_SetupRulesDefaults(&start->rules);
    start->rules.stkind = St_Kind_Last;
    start->rules.item_freq = -1;
    start->rules.sd_penalty = -1;
    start->rules.match_kind = MatchKind_Stock;
    start->rules.is_stock = true;
    start->rules.timer_enabled = setup.time_seconds > 0;
    start->rules.time_limit = setup.time_seconds;
    start->rules.disable_pausing = 0;
    start->rules.check_for_pauser_override = pet_pauser_override;

    for (i = 0; i < Gm_Player_NumMax; i++) {
        gm_SetupPlayerDefaults(&start->players[i]);
        start->players[i].slot_type = Gm_PKind_NA;
        start->players[i].rumble_enabled = false;
    }
    for (i = 0; i < 2; i++) {
        start->players[i].ckind = (CharacterKind) setup.players[i].ckind;
        start->players[i].color = setup.players[i].costume;
        start->players[i].stocks = setup.stocks;
        start->players[i].slot_type =
            setup.players[i].cpu ? Gm_PKind_Cpu : Gm_PKind_Human;
        start->players[i].cpu_level = setup.players[i].cpu_level;
        /* The controller port that plays a human (0 = the slot's own). */
        if (!setup.players[i].cpu && setup.players[i].port >= 0 &&
            setup.players[i].port < 4)
        {
            start->players[i].slot = (u8) (setup.players[i].port + 1);
        }
        OSReport("pet: match P%d ckind %d costume %d %s level %d port %d\n", (int) i + 1,
                 setup.players[i].ckind, setup.players[i].costume,
                 setup.players[i].cpu ? "cpu" : "human", setup.players[i].cpu_level,
                 setup.players[i].port + 1);
    }
    /* Pause and hide pauses as the first human player's port (Start on it
     * resumes), or port 1 when both are CPUs (the keyboard). */
    {
        int pause_port = 0;
        for (i = 1; i >= 0; i--) {
            if (!setup.players[i].cpu) {
                pause_port = setup.players[i].port >= 0 && setup.players[i].port < 4
                                 ? setup.players[i].port
                                 : (int) i;
            }
        }
        pet_set_pause_port(pause_port);
    }
    gm_LoadAnnouncer();
    pet_match_began();
}

/* Straight back to idle: no results screen on the desktop. */
static void onExitVs(GameModeState* state)
{
    gmVsMelee_ExitVs(state, state_pet_idle, state_pet_idle);
    pet_match_ended();
}

void gm_Scene_PetIdle_OnEnter(void* unused)
{
    static bool loaded;
    pet_assets_load_menu_sounds();
    if (!loaded) {
        loaded = true;
        /* Character icons and portraits for the menu bar, decoded once. */
        pet_assets_load_roster();
        pet_assets_load_named();
        pet_assets_ready();
        /* MELEE_PET_DUMP=<dir>: write every asset found to PNGs. */
        if (getenv("MELEE_PET_DUMP") != NULL) {
            pet_assets_scan_css();
            pet_assets_scan_title();
        }
    }
}

void gm_Scene_PetIdle_OnExit(void* unused) {}

/* Nothing is drawn (the overlay stays transparent) until a match is asked
 * for, from the menu bar or with Start on any controller. */
void gm_Scene_PetIdle_OnFrame(void)
{
    if ((gm_GetButtonsTriggered(PAD_MAX_CONTROLLERS) & HSD_PAD_START) != 0 &&
        pet_start_starts_match())
    {
        pet_request_match();
    }
    if (pet_match_wanted()) {
        gm_801A4B60();
    }
}

/* The buttons held on controller port 0..3 (HSD_PAD_* bits), for the welcome
 * window's "press any button". */
u32 pet_pad_buttons(int port)
{
    if (port < 0 || port >= PAD_MAX_CONTROLLERS) {
        return 0;
    }
    return HSD_PadMasterStatus[port].button;
}
