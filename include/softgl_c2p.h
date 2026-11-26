/* softgl_c2p.h - the conversion of the 32-bit frame of softgl to the
 * screen formats of the Amiga and the Atari: pens, bit planes (the
 * Amiga), interleaved bit planes (the Atari) and 16-bit pixels (the
 * Falcon). This code has no hardware code, so the tests use it on any
 * system. See docs/amiga.md and docs/atari.md.
 *
 * Part of clang-retro softgl, a software implementation of OpenGL.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#ifndef SOFTGL_C2P_H
#define SOFTGL_C2P_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The tables that change a 32-bit pixel to a pen of depth planes (1 to
 * 8), or to a 16-bit RGB 565 pixel (depth 16). */
typedef struct sgl_c2p_tables {
  uint16_t r[256], g[256], b[256]; /* the pen is (r[R] + g[G] + b[B]) >> shift */
  int shift;
  uint32_t index_mask;             /* color index mode: the pen is index & index_mask */
} sgl_c2p_tables;

/* Fills the tables for depth planes (1 to 8), or for RGB 565 (16). */
void sgl_c2p_init(sgl_c2p_tables *t, int depth);

/* Gives the palette of depth planes: 1 << depth colors of 3 bytes (red,
 * green, blue). Load it into the screen. */
void sgl_c2p_palette(int depth, uint8_t (*rgb)[3]);

/* Changes n pixels of 32 bits to pens. index_mode is 1 for a context
 * in color index mode. */
void sgl_c2p_pens(const sgl_c2p_tables *t, const uint32_t *src, uint8_t *dst, int n, int index_mode);

/* Changes n pixels of 32 bits to 16-bit pixels with the tables of depth
 * 16 (RGB 565). */
void sgl_c2p_pens16(const sgl_c2p_tables *t, const uint32_t *src, uint16_t *dst, int n);

/* Writes n pens (n a multiple of 8) to depth planes. planes[p] points
 * to the first byte of the row in plane p. */
void sgl_c2p_planes(const uint8_t *pens, int n, uint8_t *const *planes, int depth);

/* Writes n pens (n a multiple of 16) to interleaved planes, as the
 * Atari shows them: for each group of 16 pixels, one 16-bit word of
 * plane 0, then of plane 1, to plane depth - 1. */
void sgl_c2p_interleaved(const uint8_t *pens, int n, uint16_t *dst, int depth);

#ifdef __cplusplus
}
#endif

#endif
