/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Game assets for the menu bar app: textures from the game's archives,
 * decoded to RGBA. DESKTOP_MELEE_DUMP=<dir> writes everything found as PNGs (to
 * locate assets; nothing is ever stored in the repo). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <SDL3/SDL_log.h>

#include "gctex.h"
#include "desktop_melee.h"

#define DUMP_SEEN_MAX 4096
static const void* s_seen[DUMP_SEEN_MAX];
static int s_seen_n;

static void dump_named(const char* name, const uint8_t* rgba, int w, int h) {
    const char* dir = getenv("DESKTOP_MELEE_DUMP");
    if (dir == NULL || dir[0] == '\0' || rgba == NULL) {
        return;
    }
    char path[1024];
    mkdir(dir, 0755);
    snprintf(path, sizeof path, "%s/%s.png", dir, name);
#ifdef __APPLE__
    desktop_melee_mac_write_png(path, rgba, w, h);
#endif
}

static bool seen(const void* data) {
    for (int i = 0; i < s_seen_n; i++) {
        if (s_seen[i] == data) {
            return true;
        }
    }
    if (s_seen_n < DUMP_SEEN_MAX) {
        s_seen[s_seen_n++] = data;
    }
    return false;
}

void desktop_melee_asset_found(const char* tag, int index, const struct GcTexture* tex) {
    const char* dir = getenv("DESKTOP_MELEE_DUMP");
    if (dir == NULL || dir[0] == '\0' || tex->data == NULL || seen(tex->data)) {
        return;
    }
    uint8_t* rgba = gctex_decode(tex);
    if (rgba == NULL) {
        SDL_Log("desktop-melee: dump: %s %d: format %d not decoded", tag, index, tex->format);
        return;
    }
    char path[1024];
    mkdir(dir, 0755);
    snprintf(path, sizeof path, "%s/%s_%03d_%dx%d_f%d.png", dir, tag, index, tex->width,
             tex->height, tex->format);
#ifdef __APPLE__
    desktop_melee_mac_write_png(path, rgba, tex->width, tex->height);
#endif
    free(rgba);
}

/* ---- the character roster, decoded once at boot --------------------------- */

#define DESKTOP_MELEE_CKIND_MAX 32

static struct {
    int ckind;
    int costumes;
    uint8_t* icon;
    int icon_w, icon_h;
} s_roster[DESKTOP_MELEE_ROSTER_SIZE];
static int s_roster_n;

static struct {
    uint8_t* rgba;
    int w, h;
} s_portraits[DESKTOP_MELEE_CKIND_MAX][DESKTOP_MELEE_MAX_COSTUMES];

void desktop_melee_asset_roster(int i, int ckind, int costumes, const struct GcTexture* icon) {
    if (i < 0 || i >= DESKTOP_MELEE_ROSTER_SIZE) {
        return;
    }
    s_roster[i].ckind = ckind;
    s_roster[i].costumes = costumes;
    free(s_roster[i].icon);
    s_roster[i].icon = icon->data != NULL ? gctex_decode(icon) : NULL;
    s_roster[i].icon_w = icon->width;
    s_roster[i].icon_h = icon->height;
    char name[64];
    snprintf(name, sizeof name, "roster_%02d_ck%02d", i, ckind);
    dump_named(name, s_roster[i].icon, icon->width, icon->height);
    if (i + 1 > s_roster_n) {
        s_roster_n = i + 1;
    }
}

static int s_costumes[DESKTOP_MELEE_CKIND_MAX];

void desktop_melee_asset_costumes(int ckind, int costumes) {
    if (ckind >= 0 && ckind < DESKTOP_MELEE_CKIND_MAX) {
        s_costumes[ckind] = costumes;
    }
}

int desktop_melee_costume_count(int ckind) {
    return ckind >= 0 && ckind < DESKTOP_MELEE_CKIND_MAX ? s_costumes[ckind] : 0;
}

void desktop_melee_asset_portrait(int ckind, int costume, const struct GcTexture* portrait) {
    if (ckind < 0 || ckind >= DESKTOP_MELEE_CKIND_MAX || costume < 0 ||
        costume >= DESKTOP_MELEE_MAX_COSTUMES || portrait->data == NULL) {
        return;
    }
    free(s_portraits[ckind][costume].rgba);
    s_portraits[ckind][costume].rgba = gctex_decode(portrait);
    s_portraits[ckind][costume].w = portrait->width;
    s_portraits[ckind][costume].h = portrait->height;
    char name[64];
    snprintf(name, sizeof name, "portrait_ck%02d_c%d", ckind, costume);
    dump_named(name, s_portraits[ckind][costume].rgba, portrait->width, portrait->height);
}

int desktop_melee_roster_count(void) { return s_roster_n; }

bool desktop_melee_roster_entry(int i, int* ckind, int* costumes) {
    if (i < 0 || i >= s_roster_n) {
        return false;
    }
    *ckind = s_roster[i].ckind;
    *costumes = s_roster[i].costumes;
    return true;
}

const uint8_t* desktop_melee_roster_icon(int i, int* w, int* h) {
    if (i < 0 || i >= s_roster_n || s_roster[i].icon == NULL) {
        return NULL;
    }
    *w = s_roster[i].icon_w;
    *h = s_roster[i].icon_h;
    return s_roster[i].icon;
}

const uint8_t* desktop_melee_portrait(int ckind, int costume, int* w, int* h) {
    if (ckind < 0 || ckind >= DESKTOP_MELEE_CKIND_MAX || costume < 0 ||
        costume >= DESKTOP_MELEE_MAX_COSTUMES || s_portraits[ckind][costume].rgba == NULL) {
        return NULL;
    }
    *w = s_portraits[ckind][costume].w;
    *h = s_portraits[ckind][costume].h;
    return s_portraits[ckind][costume].rgba;
}

/* ---- Melee's text font (sislib) ------------------------------------------- */
/* The glyph sheet melee-pc lifts from the disc at boot (src/pc/discfont.c):
 * 287 glyphs of 32x32 4-bit intensity, each with its Shift-JIS character
 * and left/right margins for proportional spacing (game data tables). */

#define SIS_GLYPHS 287
#define SIS_CHARS 288
extern const unsigned char HSD_SisLib_FontAtlas[][512];
extern const unsigned char lbl_8040C8C0[][2];        /* Shift-JIS per character */
extern const unsigned char HSD_SisLib_8040C680[][2]; /* glyph code per character */
extern const unsigned char HSD_SisLib_8040CB00[][2]; /* left, right margin per glyph */

static uint8_t* s_glyph_alpha[SIS_GLYPHS];

int desktop_melee_font_glyph_count(void) { return SIS_CHARS; }

/* Character entry i: its Shift-JIS bytes, the atlas glyph that draws it
 * (glyph code - 0x2000, as sislib does) and that glyph's margins. */
bool desktop_melee_font_glyph_info(int i, uint8_t* sjis_hi, uint8_t* sjis_lo, int* atlas, int* left,
                         int* right) {
    if (i < 0 || i >= SIS_CHARS) {
        return false;
    }
    const int code = HSD_SisLib_8040C680[i][0] << 8 | HSD_SisLib_8040C680[i][1];
    const int index = code - 0x2000;
    if (code == 0 || index < 0 || index >= SIS_GLYPHS) {
        return false;
    }
    *sjis_hi = lbl_8040C8C0[i][0];
    *sjis_lo = lbl_8040C8C0[i][1];
    *atlas = index;
    *left = HSD_SisLib_8040CB00[index][0];
    *right = HSD_SisLib_8040CB00[index][1];
    return true;
}

/* Atlas glyph i: 32x32 coverage (0..255), one byte per pixel, top row first. */
const uint8_t* desktop_melee_font_glyph_alpha(int i) {
    if (i < 0 || i >= SIS_GLYPHS) {
        return NULL;
    }
    if (s_glyph_alpha[i] == NULL) {
        GcTexture tex = {
            .data = HSD_SisLib_FontAtlas[i], .width = 32, .height = 32, .format = GCTEX_I4};
        uint8_t* rgba = gctex_decode(&tex);
        if (rgba == NULL) {
            return NULL;
        }
        uint8_t* alpha = malloc(32 * 32);
        for (int p = 0; p < 32 * 32; p++) {
            alpha[p] = rgba[p * 4];
        }
        free(rgba);
        s_glyph_alpha[i] = alpha;
    }
    return s_glyph_alpha[i];
}

#ifdef __APPLE__
void desktop_melee_ui_assets_ready(void); /* desktop-melee/ui/GameAssets.swift */
#endif

void desktop_melee_assets_ready(void) {
#ifdef __APPLE__
    if (desktop_melee_overlay_enabled()) {
        desktop_melee_ui_assets_ready();
    }
#endif
}

/* ---- named graphics ------------------------------------------------------ */
/* Intensity images (white art on black) kept as coverage, like the font:
 * the menu bar tints them. */

#define NAMED_MAX 8
static struct {
    char name[16];
    uint8_t* alpha;
    int w, h;
} s_named[NAMED_MAX];

void desktop_melee_asset_named(const char* name, const struct GcTexture* tex) {
    uint8_t* rgba = gctex_decode(tex);
    if (rgba == NULL) {
        return;
    }
    for (int i = 0; i < NAMED_MAX; i++) {
        if (s_named[i].alpha == NULL || strcmp(s_named[i].name, name) == 0) {
            free(s_named[i].alpha);
            snprintf(s_named[i].name, sizeof s_named[i].name, "%s", name);
            s_named[i].alpha = malloc((size_t)tex->width * tex->height);
            for (int p = 0; p < tex->width * tex->height; p++) {
                s_named[i].alpha[p] = rgba[p * 4];
            }
            s_named[i].w = tex->width;
            s_named[i].h = tex->height;
            break;
        }
    }
    dump_named(name, rgba, tex->width, tex->height);
    free(rgba);
}

const uint8_t* desktop_melee_named_alpha(const char* name, int* w, int* h) {
    for (int i = 0; i < NAMED_MAX; i++) {
        if (s_named[i].alpha != NULL && strcmp(s_named[i].name, name) == 0) {
            *w = s_named[i].w;
            *h = s_named[i].h;
            return s_named[i].alpha;
        }
    }
    return NULL;
}
