/* SPDX-License-Identifier: GPL-3.0-or-later */
/* GameCube texture decoding to straight (non-premultiplied) RGBA8, for the
 * menu bar app's game assets. Texture data is laid out in tiles exactly as on
 * the console; 16-bit values are big-endian. */
#include "gctex.h"

#include <stdlib.h>
#include <string.h>

static inline uint16_t be16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }

static inline uint8_t x4(unsigned v) { return (uint8_t)(v << 4 | v); }
static inline uint8_t x5(unsigned v) { return (uint8_t)(v << 3 | v >> 2); }
static inline uint8_t x6(unsigned v) { return (uint8_t)(v << 2 | v >> 4); }
static inline uint8_t x3(unsigned v) { return (uint8_t)(v << 5 | v << 2 | v >> 1); }

static void put(uint8_t* out, int w, int h, int x, int y, uint8_t r, uint8_t g, uint8_t b,
                uint8_t a) {
    if (x >= w || y >= h) {
        return;
    }
    uint8_t* p = out + ((size_t)y * w + x) * 4;
    p[0] = r;
    p[1] = g;
    p[2] = b;
    p[3] = a;
}

static void rgb565(uint16_t c, uint8_t* rgba) {
    rgba[0] = x5(c >> 11 & 31);
    rgba[1] = x6(c >> 5 & 63);
    rgba[2] = x5(c & 31);
    rgba[3] = 255;
}

static void rgb5a3(uint16_t c, uint8_t* rgba) {
    if (c & 0x8000) {
        rgba[0] = x5(c >> 10 & 31);
        rgba[1] = x5(c >> 5 & 31);
        rgba[2] = x5(c & 31);
        rgba[3] = 255;
    } else {
        rgba[0] = x4(c >> 8 & 15);
        rgba[1] = x4(c >> 4 & 15);
        rgba[2] = x4(c & 15);
        rgba[3] = x3(c >> 12 & 7);
    }
}

static void tlut_color(const uint8_t* tlut, int tlut_fmt, unsigned index, uint8_t* rgba) {
    const uint16_t c = be16(tlut + index * 2);
    switch (tlut_fmt) {
    case GCTEX_TL_IA8:
        rgba[0] = rgba[1] = rgba[2] = (uint8_t)(c & 0xFF);
        rgba[3] = (uint8_t)(c >> 8);
        break;
    case GCTEX_TL_RGB565:
        rgb565(c, rgba);
        break;
    default:
        rgb5a3(c, rgba);
        break;
    }
}

/* CMPR: 8x8 tiles of four 4x4 DXT1 blocks (big-endian colours, reversed bits). */
static void decode_cmpr(const uint8_t* src, int w, int h, uint8_t* out) {
    for (int ty = 0; ty < h; ty += 8) {
        for (int tx = 0; tx < w; tx += 8) {
            for (int sub = 0; sub < 4; sub++, src += 8) {
                const int bx = tx + (sub & 1) * 4;
                const int by = ty + (sub >> 1) * 4;
                const uint16_t c0 = be16(src), c1 = be16(src + 2);
                uint8_t pal[4][4];
                rgb565(c0, pal[0]);
                rgb565(c1, pal[1]);
                if (c0 > c1) {
                    for (int k = 0; k < 3; k++) {
                        pal[2][k] = (uint8_t)((2 * pal[0][k] + pal[1][k]) / 3);
                        pal[3][k] = (uint8_t)((pal[0][k] + 2 * pal[1][k]) / 3);
                    }
                    pal[2][3] = pal[3][3] = 255;
                } else {
                    for (int k = 0; k < 3; k++) {
                        pal[2][k] = (uint8_t)((pal[0][k] + pal[1][k]) / 2);
                        pal[3][k] = 0;
                    }
                    pal[2][3] = 255;
                    pal[3][3] = 0;
                }
                for (int row = 0; row < 4; row++) {
                    const uint8_t bits = src[4 + row];
                    for (int col = 0; col < 4; col++) {
                        const uint8_t* c = pal[(bits >> (6 - col * 2)) & 3];
                        put(out, w, h, bx + col, by + row, c[0], c[1], c[2], c[3]);
                    }
                }
            }
        }
    }
}

uint8_t* gctex_decode(const GcTexture* tex) {
    const int w = tex->width, h = tex->height;
    if (w <= 0 || h <= 0 || tex->data == NULL) {
        return NULL;
    }
    uint8_t* out = calloc((size_t)w * h, 4);
    if (out == NULL) {
        return NULL;
    }
    const uint8_t* src = tex->data;
    uint8_t c[4];

    switch (tex->format) {
    case GCTEX_I4: /* 8x8 tiles, 4 bits */
        for (int ty = 0; ty < h; ty += 8)
            for (int tx = 0; tx < w; tx += 8)
                for (int y = 0; y < 8; y++)
                    for (int x = 0; x < 8; x += 2, src++) {
                        const uint8_t a = x4(*src >> 4), b = x4(*src & 15);
                        put(out, w, h, tx + x, ty + y, a, a, a, 255);
                        put(out, w, h, tx + x + 1, ty + y, b, b, b, 255);
                    }
        break;
    case GCTEX_I8: /* 8x4 tiles */
        for (int ty = 0; ty < h; ty += 4)
            for (int tx = 0; tx < w; tx += 8)
                for (int y = 0; y < 4; y++)
                    for (int x = 0; x < 8; x++, src++)
                        put(out, w, h, tx + x, ty + y, *src, *src, *src, 255);
        break;
    case GCTEX_IA4: /* 8x4 tiles, alpha in the high nibble */
        for (int ty = 0; ty < h; ty += 4)
            for (int tx = 0; tx < w; tx += 8)
                for (int y = 0; y < 4; y++)
                    for (int x = 0; x < 8; x++, src++) {
                        const uint8_t i = x4(*src & 15);
                        put(out, w, h, tx + x, ty + y, i, i, i, x4(*src >> 4));
                    }
        break;
    case GCTEX_IA8: /* 4x4 tiles, alpha then intensity */
        for (int ty = 0; ty < h; ty += 4)
            for (int tx = 0; tx < w; tx += 4)
                for (int y = 0; y < 4; y++)
                    for (int x = 0; x < 4; x++, src += 2)
                        put(out, w, h, tx + x, ty + y, src[1], src[1], src[1], src[0]);
        break;
    case GCTEX_RGB565:
    case GCTEX_RGB5A3: /* 4x4 tiles of 16-bit colours */
        for (int ty = 0; ty < h; ty += 4)
            for (int tx = 0; tx < w; tx += 4)
                for (int y = 0; y < 4; y++)
                    for (int x = 0; x < 4; x++, src += 2) {
                        if (tex->format == GCTEX_RGB565) {
                            rgb565(be16(src), c);
                        } else {
                            rgb5a3(be16(src), c);
                        }
                        put(out, w, h, tx + x, ty + y, c[0], c[1], c[2], c[3]);
                    }
        break;
    case GCTEX_RGBA8: /* 4x4 tiles: 32 bytes of AR, then 32 bytes of GB */
        for (int ty = 0; ty < h; ty += 4)
            for (int tx = 0; tx < w; tx += 4, src += 64)
                for (int i = 0; i < 16; i++)
                    put(out, w, h, tx + i % 4, ty + i / 4, src[i * 2 + 1], src[32 + i * 2],
                        src[32 + i * 2 + 1], src[i * 2]);
        break;
    case GCTEX_CI4: /* 8x8 tiles of 4-bit palette indices */
        if (tex->tlut == NULL) break;
        for (int ty = 0; ty < h; ty += 8)
            for (int tx = 0; tx < w; tx += 8)
                for (int y = 0; y < 8; y++)
                    for (int x = 0; x < 8; x += 2, src++) {
                        tlut_color(tex->tlut, tex->tlut_format, *src >> 4, c);
                        put(out, w, h, tx + x, ty + y, c[0], c[1], c[2], c[3]);
                        tlut_color(tex->tlut, tex->tlut_format, *src & 15, c);
                        put(out, w, h, tx + x + 1, ty + y, c[0], c[1], c[2], c[3]);
                    }
        break;
    case GCTEX_CI8: /* 8x4 tiles of 8-bit palette indices */
        if (tex->tlut == NULL) break;
        for (int ty = 0; ty < h; ty += 4)
            for (int tx = 0; tx < w; tx += 8)
                for (int y = 0; y < 4; y++)
                    for (int x = 0; x < 8; x++, src++) {
                        tlut_color(tex->tlut, tex->tlut_format, *src, c);
                        put(out, w, h, tx + x, ty + y, c[0], c[1], c[2], c[3]);
                    }
        break;
    case GCTEX_CMPR:
        decode_cmpr(src, w, h, out);
        break;
    default:
        free(out);
        return NULL;
    }
    return out;
}
