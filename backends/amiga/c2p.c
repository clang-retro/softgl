/* c2p.c - the chunky-to-planar conversion of the Amiga back end: 32-bit
 * pixels to pens, and pens to bit planes. This file has no hardware
 * code, so the tests use it on any system.
 *
 * Part of clang-retro softgl, a software implementation of OpenGL.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include "softgl_amiga.h"

/* The bits of red, green and blue in a pen of 3 to 8 planes. With 1 or
 * 2 planes, the pen is a gray level. */
static const unsigned char split[9][3] = {
    {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {1, 1, 1}, {1, 2, 1}, {2, 2, 1}, {2, 2, 2}, {2, 3, 2}, {3, 3, 2},
};

/* Fills the tables for depth planes (see softgl_amiga.h). */
void sgl_c2p_init(sgl_c2p_tables *t, int depth) {
  int i;
  if (depth < 1) depth = 1;
  if (depth > 8) depth = 8;
  t->index_mask = (1u << depth) - 1u;
  if (depth <= 2) {
    /* The luminance in 8.8 fixed point: 77 + 151 + 28 = 256. */
    for (i = 0; i < 256; i++) {
      t->r[i] = (uint16_t)(i * 77);
      t->g[i] = (uint16_t)(i * 151);
      t->b[i] = (uint16_t)(i * 28);
    }
    t->shift = 8 + (8 - depth);
    return;
  }
  {
    int rb = split[depth][0], gb = split[depth][1], bb = split[depth][2];
    for (i = 0; i < 256; i++) {
      t->r[i] = (uint16_t)((i >> (8 - rb)) << (gb + bb));
      t->g[i] = (uint16_t)((i >> (8 - gb)) << bb);
      t->b[i] = (uint16_t)(i >> (8 - bb));
    }
    t->shift = 0;
  }
}

/* Gives the 8-bit value of level v of a channel of bits bits. */
static uint8_t expand(int v, int bits) { return (uint8_t)(v * 255 / ((1 << bits) - 1)); }

/* Gives the palette of depth planes (see softgl_amiga.h). */
void sgl_amiga_palette(int depth, uint8_t (*rgb)[3]) {
  int i, n;
  if (depth < 1) depth = 1;
  if (depth > 8) depth = 8;
  n = 1 << depth;
  for (i = 0; i < n; i++) {
    if (depth <= 2) {
      rgb[i][0] = rgb[i][1] = rgb[i][2] = expand(i, depth);
    } else {
      int rb = split[depth][0], gb = split[depth][1], bb = split[depth][2];
      rgb[i][0] = expand((i >> (gb + bb)) & ((1 << rb) - 1), rb);
      rgb[i][1] = expand((i >> bb) & ((1 << gb) - 1), gb);
      rgb[i][2] = expand(i & ((1 << bb) - 1), bb);
    }
  }
}

/* Changes pixels to pens (see softgl_amiga.h). */
void sgl_c2p_pens(const sgl_c2p_tables *t, const uint32_t *src, uint8_t *dst, int n, int index_mode) {
  int i, s = t->shift;
  if (index_mode) {
    for (i = 0; i < n; i++) dst[i] = (uint8_t)(src[i] & t->index_mask);
    return;
  }
  for (i = 0; i < n; i++) {
    uint32_t p = src[i];
    dst[i] = (uint8_t)((t->r[(p >> 16) & 0xFF] + t->g[(p >> 8) & 0xFF] + t->b[p & 0xFF]) >> s);
  }
}

/* Writes pens to bit planes (see softgl_amiga.h). Each group of 8 pens
 * is an 8 x 8 matrix of bits; its transpose gives one byte of each
 * plane. The transpose uses 32-bit operations only (Hacker's Delight,
 * 7-3), which suit the 68000. */
void sgl_c2p_planes(const uint8_t *pens, int n, uint8_t *const *planes, int depth) {
  int i, p;
  for (i = 0; i < n; i += 8) {
    const uint8_t *a = pens + i;
    uint32_t x = ((uint32_t)a[0] << 24) | ((uint32_t)a[1] << 16) | ((uint32_t)a[2] << 8) | a[3];
    uint32_t y = ((uint32_t)a[4] << 24) | ((uint32_t)a[5] << 16) | ((uint32_t)a[6] << 8) | a[7];
    uint32_t t;
    uint8_t out[8];
    t = (x ^ (x >> 7)) & 0x00AA00AAu;
    x = x ^ t ^ (t << 7);
    t = (y ^ (y >> 7)) & 0x00AA00AAu;
    y = y ^ t ^ (t << 7);
    t = (x ^ (x >> 14)) & 0x0000CCCCu;
    x = x ^ t ^ (t << 14);
    t = (y ^ (y >> 14)) & 0x0000CCCCu;
    y = y ^ t ^ (t << 14);
    t = (x & 0xF0F0F0F0u) | ((y >> 4) & 0x0F0F0F0Fu);
    y = ((x << 4) & 0xF0F0F0F0u) | (y & 0x0F0F0F0Fu);
    x = t;
    /* out[k] has bit 7 - k of each pen: out[0] is plane 7. */
    out[0] = (uint8_t)(x >> 24);
    out[1] = (uint8_t)(x >> 16);
    out[2] = (uint8_t)(x >> 8);
    out[3] = (uint8_t)x;
    out[4] = (uint8_t)(y >> 24);
    out[5] = (uint8_t)(y >> 16);
    out[6] = (uint8_t)(y >> 8);
    out[7] = (uint8_t)y;
    for (p = 0; p < depth; p++) planes[p][i >> 3] = out[7 - p];
  }
}
