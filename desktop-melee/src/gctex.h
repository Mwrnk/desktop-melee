/* SPDX-License-Identifier: GPL-3.0-or-later */
/* GameCube texture decoding (see gctex.c). Plain types only: this header is
 * shared by the game code (GCC) and the pet code (clang). */
#pragma once
#include <stdint.h>

enum {
    GCTEX_I4 = 0,
    GCTEX_I8 = 1,
    GCTEX_IA4 = 2,
    GCTEX_IA8 = 3,
    GCTEX_RGB565 = 4,
    GCTEX_RGB5A3 = 5,
    GCTEX_RGBA8 = 6,
    GCTEX_CI4 = 8,
    GCTEX_CI8 = 9,
    GCTEX_CMPR = 14,
};

enum {
    GCTEX_TL_IA8 = 0,
    GCTEX_TL_RGB565 = 1,
    GCTEX_TL_RGB5A3 = 2,
};

/* A texture as stored in a game archive (all pointers into game memory). */
typedef struct GcTexture {
    const uint8_t* data;
    int width;
    int height;
    int format;       /* GCTEX_* */
    const uint8_t* tlut; /* palette (big-endian 16-bit entries), or NULL */
    int tlut_format;  /* GCTEX_TL_* */
    int tlut_entries;
} GcTexture;

/* Straight RGBA8, width*height*4 bytes (free() it), or NULL. */
uint8_t* gctex_decode(const GcTexture* tex);
