/* SPDX-License-Identifier: GPL-3.0-or-later */
/* GM_PET (see gmpetmode.h). Built with the game code (melee_game). */
#include "gmpetmode.h"

#include <melee/gm/forward.h>
#include <melee/gm/gm_1601.h>
#include <melee/gm/gm_1A36.h>
#include <melee/gm/gm_1A3F.h>
#include <melee/gm/gmscene.h>
#include <melee/gm/gmvsmelee.h>
#include <melee/gm/types.h>
#include <melee/lb/forward.h>
#include <melee/mn/types.h>
#include <sysdolphin/baselib/controller.h>

#include "pet_game.h"

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

void gm_Scene_PetIdle_OnEnter(void* unused) {}

void gm_Scene_PetIdle_OnExit(void* unused) {}

/* Nothing is drawn (the overlay stays transparent) until a match is asked
 * for, from the menu bar or with Start on any controller. */
void gm_Scene_PetIdle_OnFrame(void)
{
    if ((gm_GetButtonsTriggered(PAD_MAX_CONTROLLERS) & HSD_PAD_START) != 0) {
        pet_request_match();
    }
    if (pet_match_wanted()) {
        gm_801A4B60();
    }
}
