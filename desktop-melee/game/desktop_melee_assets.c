/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Finds textures inside the game's own archives for the menu bar app (built
 * with the game code, so the disc structures read correctly). Each texture
 * is handed to pet_asset_found() as a GcTexture pointing into game memory. */
#include "petassets.h"

#include <stdio.h>
#include <stdlib.h>
#include <dolphin/os.h>
#include <string.h>

#include <melee/lb/lbarchive.h>
#include <melee/gm/gm_1601.h>
#include <melee/lb/lbaudio_ax.h>
#include <melee/mn/forward.h>
#include <melee/mn/mncharsel.h>
#include <melee/mn/types.h>
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/fobj.h>
#include <melee/sc/types.h>
#include <sysdolphin/baselib/archive.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/tobj.h>

#include "gctex.h"
#include "pet_game.h"

/* While set, emit() hands matching textures to pet_asset_named() instead. */
static const struct NamedCapture {
    const char* tag;
    int index;
    const char* name;
}* s_capture;

static void emit(const char* tag, int index, HSD_ImageDesc* img, HSD_TlutDesc* tlut)
{
    GcTexture tex;
    if (img == NULL) {
        return;
    }
    memset(&tex, 0, sizeof tex);
    tex.data = DP(const uint8_t, img->image_ptr);
    tex.width = img->width;
    tex.height = img->height;
    tex.format = (int) img->format;
    if (tlut != NULL) {
        tex.tlut = DP(const uint8_t, tlut->lut);
        tex.tlut_format = (int) tlut->fmt;
        tex.tlut_entries = tlut->n_entries;
    }
    if (s_capture != NULL) {
        for (const struct NamedCapture* c = s_capture; c->tag != NULL; c++) {
            if (strcmp(c->tag, tag) == 0 && c->index == index) {
                pet_asset_named(c->name, &tex);
            }
        }
        return;
    }
    pet_asset_found(tag, index, &tex);
}

static void walk_joint(HSD_Joint* joint, const char* tag, int* counter)
{
    for (; joint != NULL; joint = DP(HSD_Joint, joint->next)) {
        if (!(joint->flags & (JOBJ_PTCL | JOBJ_SPLINE))) {
            HSD_DObjDesc* dobj = DP(HSD_DObjDesc, joint->u.dobjdesc);
            for (; dobj != NULL; dobj = DP(HSD_DObjDesc, dobj->next)) {
                HSD_MObjDesc* mobj = DP(HSD_MObjDesc, dobj->mobjdesc);
                HSD_TObjDesc* tobj = mobj != NULL ? DP(HSD_TObjDesc, mobj->texdesc) : NULL;
                for (; tobj != NULL; tobj = DP(HSD_TObjDesc, tobj->next)) {
                    emit(tag, (*counter)++, DP(HSD_ImageDesc, tobj->imagedesc),
                         DP(HSD_TlutDesc, tobj->tlutdesc));
                }
            }
        }
        walk_joint(DP(HSD_Joint, joint->child), tag, counter);
    }
}

/* Texture animations: each TexAnim swaps between the images in its table. */
static void walk_matanim(HSD_MatAnimJoint* mj, const char* tag, int* table)
{
    char name[96];
    for (; mj != NULL; mj = DP(HSD_MatAnimJoint, mj->next)) {
        HSD_MatAnim* ma = DP(HSD_MatAnim, mj->matanim);
        for (; ma != NULL; ma = DP(HSD_MatAnim, ma->next)) {
            HSD_TexAnim* ta = DP(HSD_TexAnim, ma->texanim);
            for (; ta != NULL; ta = DP(HSD_TexAnim, ta->next)) {
                DiscU32* images = DP(DiscU32, ta->imagetbl);
                DiscU32* tluts = DP(DiscU32, ta->tluttbl);
                snprintf(name, sizeof name, "%s_tex%02d", tag, (*table)++);
                for (int i = 0; images != NULL && i < ta->n_imagetbl; i++) {
                    HSD_TlutDesc* tl = (tluts != NULL && i < ta->n_tluttbl)
                                           ? DP(HSD_TlutDesc, tluts[i].v)
                                           : NULL;
                    emit(name, i, DP(HSD_ImageDesc, images[i].v), tl);
                }
            }
        }
        walk_matanim(DP(HSD_MatAnimJoint, mj->child), tag, table);
    }
}

static void walk_model(StaticModelDesc* model, const char* tag)
{
    int counter = 0, table = 0;
    walk_joint(DP(HSD_Joint, model->joint), tag, &counter);
    walk_matanim(DP(HSD_MatAnimJoint, model->matanim_joint), tag, &table);
}

void pet_assets_scan_css(void)
{
    HSD_Archive* archive = lbArchive_LoadArchive("MnSlChr.usd");
    struct MnSelectChrDataTable* table;
    if (archive == NULL) {
        return;
    }
    table = HSD_ArchiveGetPublicAs(struct MnSelectChrDataTable, archive,
                                   "MnSelectChrDataTable");
    if (table == NULL) {
        return;
    }
    walk_model(&table->models.background, "css_background");
    walk_model(&table->models.hand, "css_hand");
    walk_model(&table->models.token, "css_token");
    walk_model(&table->models.menu, "css_menu");
    walk_model(&table->models.press_start, "css_press_start");
    walk_model(&table->models.door, "css_door");
}

/* The title screen's logo and background (the Smash emblem lives here). */
static void scan_title(void)
{
    HSD_Archive* archive = lbArchive_LoadArchive("GmTtAll.usd");
    static const char* const names[][2] = {
        { "TtlMoji_Top_joint", "TtlMoji_Top_matanim_joint" },
        { "TtlBg_Top_joint", "TtlBg_Top_matanim_joint" },
    };
    if (archive == NULL) {
        return;
    }
    for (int i = 0; i < 2; i++) {
        int counter = 0, table = 0;
        const char* tag = i == 0 ? "title_moji" : "title_bg";
        HSD_Joint* joint = HSD_ArchiveGetPublicAddress(archive, names[i][0]);
        HSD_MatAnimJoint* mat = HSD_ArchiveGetPublicAddress(archive, names[i][1]);
        walk_joint(joint, tag, &counter);
        walk_matanim(mat, tag, &table);
    }
}

/* ---- the character roster for the menu bar app -------------------------- */

/* The character select grid, in Melee's order (top, middle, bottom row):
 * character kind, the menu joint that holds its face icon, and the
 * announcer's name callout (sound id, as the select screen plays it). */
static const struct {
    u8 ckind;
    u8 joint;
    u16 name_sfx;
} roster[PET_ROSTER_SIZE] = {
    { CKind_DrMario, ICONJOINT_DRMARIO, 0xC5 },   { CKind_Mario, ICONJOINT_MARIO, 0xCD },
    { CKind_Luigi, ICONJOINT_LUIGI, 0xCC },       { CKind_Koopa, ICONJOINT_KOOPA, 0xCA },
    { CKind_Peach, ICONJOINT_PEACH, 0xD1 },       { CKind_Yoshi, ICONJOINT_YOSHI, 0xD7 },
    { CKind_Donkey, ICONJOINT_DONKEY, 0xC4 },     { CKind_Captain, ICONJOINT_CAPTAIN, 0xC2 },
    { CKind_Ganon, ICONJOINT_GANON, 0xD9 },       { CKind_Falco, ICONJOINT_FALCO, 0xC6 },
    { CKind_Fox, ICONJOINT_FOX, 0xC7 },           { CKind_Ness, ICONJOINT_NESS, 0xD0 },
    { CKind_PopoNana, ICONJOINT_POPONANA, 0xC8 }, { CKind_Kirby, ICONJOINT_KIRBY, 0xC9 },
    { CKind_Samus, ICONJOINT_SAMUS, 0xD5 },       { CKind_Zelda, ICONJOINT_ZELDA, 0xD6 },
    { CKind_Link, ICONJOINT_LINK, 0xCB },         { CKind_CLink, ICONJOINT_CLINK, 0xC3 },
    { CKind_Pichu, ICONJOINT_PICHU, 0xD2 },       { CKind_Pikachu, ICONJOINT_PIKACHU, 0xD3 },
    { CKind_Purin, ICONJOINT_PURIN, 0xD4 },       { CKind_Mewtwo, ICONJOINT_MEWTWO, 0xCF },
    { CKind_GameWatch, ICONJOINT_GAMEWATCH, 0xD8 }, { CKind_Mars, ICONJOINT_MARS, 0xCE },
    { CKind_Emblem, ICONJOINT_EMBLEM, 0xDA },
};

/* The joint at a preorder index (how lb_80011E24 numbers a model's joints). */
static HSD_Joint* joint_at(HSD_Joint* joint, int target, int* counter)
{
    for (; joint != NULL; joint = DP(HSD_Joint, joint->next)) {
        if ((*counter)++ == target) {
            return joint;
        }
        HSD_Joint* found = joint_at(DP(HSD_Joint, joint->child), target, counter);
        if (found != NULL) {
            return found;
        }
    }
    return NULL;
}

/* The face icon: the joint's 64x56 palette (CI8) texture; the icon joints
 * also carry a shared 64x56 intensity overlay, which is not the face. Searched
 * in the joint and below it. */
static bool icon_texture(HSD_Joint* joint, GcTexture* out)
{
    if (joint == NULL) {
        return false;
    }
    if (!(joint->flags & (JOBJ_PTCL | JOBJ_SPLINE))) {
    HSD_DObjDesc* dobj = DP(HSD_DObjDesc, joint->u.dobjdesc);
    for (; dobj != NULL; dobj = DP(HSD_DObjDesc, dobj->next)) {
        HSD_MObjDesc* mobj = DP(HSD_MObjDesc, dobj->mobjdesc);
        HSD_TObjDesc* tobj = mobj != NULL ? DP(HSD_TObjDesc, mobj->texdesc) : NULL;
        for (; tobj != NULL; tobj = DP(HSD_TObjDesc, tobj->next)) {
            HSD_ImageDesc* img = DP(HSD_ImageDesc, tobj->imagedesc);
            HSD_TlutDesc* tlut = DP(HSD_TlutDesc, tobj->tlutdesc);
            if (img != NULL && img->width == 64 && img->height == 56 &&
                img->format == GX_TF_C8 && tlut != NULL) {
                memset(out, 0, sizeof *out);
                out->data = DP(const uint8_t, img->image_ptr);
                out->width = img->width;
                out->height = img->height;
                out->format = (int) img->format;
                if (tlut != NULL) {
                    out->tlut = DP(const uint8_t, tlut->lut);
                    out->tlut_format = (int) tlut->fmt;
                    out->tlut_entries = tlut->n_entries;
                }
                return true;
            }
        }
    }
    }
    for (HSD_Joint* child = DP(HSD_Joint, joint->child); child != NULL;
         child = DP(HSD_Joint, child->next)) {
        if (icon_texture(child, out)) {
            return true;
        }
    }
    return false;
}

/* The portrait table: the menu's texture animation with the most images. */
static HSD_TexAnim* portrait_texanim(HSD_MatAnimJoint* mj, HSD_TexAnim* best)
{
    for (; mj != NULL; mj = DP(HSD_MatAnimJoint, mj->next)) {
        HSD_MatAnim* ma = DP(HSD_MatAnim, mj->matanim);
        for (; ma != NULL; ma = DP(HSD_MatAnim, ma->next)) {
            HSD_TexAnim* ta = DP(HSD_TexAnim, ma->texanim);
            for (; ta != NULL; ta = DP(HSD_TexAnim, ta->next)) {
                if (best == NULL || ta->n_imagetbl > best->n_imagetbl) {
                    best = ta;
                }
            }
        }
        best = portrait_texanim(DP(HSD_MatAnimJoint, mj->child), best);
    }
    return best;
}

/* Captures the image index the TIMG track yields at the requested frame. */
static void capture_timg(void* obj, enum_t type, HSD_ObjData* val)
{
    if (type == HSD_A_T_TIMG) {
        *(int*) obj = (int) val->fv;
    }
}

/* The character select screen's portrait frame: the character's HUD index
 * (its external number, Sheik excluded) plus 30 per costume. */
static int portrait_frame(int ckind, int costume)
{
    int hud = ckind < CKind_Seak ? ckind : ckind - 1;
    return hud + costume * 0x1E;
}

static void texture_of(HSD_TexAnim* ta, int index, GcTexture* out)
{
    DiscU32* images = DP(DiscU32, ta->imagetbl);
    DiscU32* tluts = DP(DiscU32, ta->tluttbl);
    HSD_ImageDesc* img = DP(HSD_ImageDesc, images[index].v);
    HSD_TlutDesc* tlut = (tluts != NULL && index < ta->n_tluttbl)
                             ? DP(HSD_TlutDesc, tluts[index].v)
                             : NULL;
    memset(out, 0, sizeof *out);
    if (img == NULL) {
        return;
    }
    out->data = DP(const uint8_t, img->image_ptr);
    out->width = img->width;
    out->height = img->height;
    out->format = (int) img->format;
    if (tlut != NULL) {
        out->tlut = DP(const uint8_t, tlut->lut);
        out->tlut_format = (int) tlut->fmt;
        out->tlut_entries = tlut->n_entries;
    }
}

void pet_assets_load_roster(void)
{
    HSD_Archive* archive = lbArchive_LoadArchive("MnSlChr.usd");
    struct MnSelectChrDataTable* table;
    StaticModelDesc* menu;
    HSD_TexAnim* portraits;
    HSD_AObj* aobj;

    if (archive == NULL) {
        return;
    }
    table = HSD_ArchiveGetPublicAs(struct MnSelectChrDataTable, archive,
                                   "MnSelectChrDataTable");
    if (table == NULL) {
        return;
    }
    menu = &table->models.menu;
    portraits = portrait_texanim(DP(HSD_MatAnimJoint, menu->matanim_joint), NULL);
    aobj = portraits != NULL ? HSD_AObjLoadDesc(DP(HSD_AObjDesc, portraits->aobjdesc))
                             : NULL;

    /* Every playable kind, including Sheik, who has no tile of their own. */
    for (int ck = 0; ck < CKind_Playable_Count; ck++) {
        pet_asset_costumes(ck, gm_GetNumCostumesForCKind((u8) ck));
    }
    for (int i = 0; i < PET_ROSTER_SIZE; i++) {
        int counter = 0;
        int ckind = roster[i].ckind;
        int costumes = gm_GetNumCostumesForCKind((u8) ckind);
        GcTexture icon;
        memset(&icon, 0, sizeof icon);
        icon_texture(joint_at(DP(HSD_Joint, menu->joint), roster[i].joint, &counter), &icon);
        pet_asset_roster(i, ckind, costumes, &icon);

        for (int c = 0; aobj != NULL && c < costumes && c < PET_MAX_COSTUMES; c++) {
            int index = -1;
            GcTexture portrait;
            HSD_AObjReqAnim(aobj, (f32) portrait_frame(ckind, c));
            HSD_AObjInterpretAnim(aobj, &index, capture_timg);
            if (index < 0 || index >= portraits->n_imagetbl) {
                continue;
            }
            texture_of(portraits, index, &portrait);
            pet_asset_portrait(ckind, c, &portrait);
        }
    }
    if (aobj != NULL) {
        HSD_AObjRemove(aobj);
    }
}

/* ---- menu sounds ------------------------------------------------------- */

/* The character select screen's sound banks: menu sounds and the
 * announcer's character names. A match loads its own banks, so the idle
 * scene loads these each time it starts. */
void pet_assets_load_menu_sounds(void)
{
    lbAudioAx_80026F2C(0x1E);
    lbAudioAx_8002702C(2, 8);
    lbAudioAx_80027168();
    lbAudioAx_80027648();
}

void pet_sound_menu(int kind)
{
    if (kind >= 0 && kind <= 3) {
        lbAudioAx_80024030(kind);
    }
}

void pet_sound_announce(int roster_index)
{
    if (roster_index >= 0 && roster_index < PET_ROSTER_SIZE) {
        int voice = lbAudioAx_80023870(roster[roster_index].name_sfx, 0x7F, 0x40,
                                       roster_index + 0x8A);
        if (getenv("MELEE_PET_DUMP") != NULL) {
            OSReport("pet: announce %d -> voice %d\n", roster_index, voice);
        }
    }
}

void pet_assets_scan_title(void)
{
    scan_title();
}

/* Named pieces of the title logo (NTSC 1.02 layout): the Smash emblem (the
 * "O" of BROS.) and the "Melee" wordmark. */
void pet_assets_load_named(void)
{
    static const struct NamedCapture captures[] = {
        { "title_moji", 31, "emblem" },
        { "title_moji", 27, "melee" },
        { NULL, 0, NULL },
    };
    s_capture = captures;
    scan_title();
    s_capture = NULL;
}
