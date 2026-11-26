/* softgl_amiga.h - the Amiga chipset back end of softgl: Blitter clears,
 * chunky-to-planar conversion with Akiko or the CPU, and an optional
 * Blitter fill of flat spans. See docs/amiga.md.
 *
 * The program opens graphics.library (GfxBase) and, for double
 * buffering, intuition.library (IntuitionBase) before it uses these
 * functions.
 *
 * Part of clang-retro softgl, a software implementation of OpenGL.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#ifndef SOFTGL_AMIGA_H
#define SOFTGL_AMIGA_H

#include "softgl.h"

#ifdef __cplusplus
extern "C" {
#endif

struct BitMap;
struct Screen;
struct ScreenBuffer;

/* The flags of sgl_amiga_chipset. */
#define SGL_AMIGA_BLITTER_CLEAR 1u /* clear the buffers with the Blitter (buffers in Chip RAM) */
#define SGL_AMIGA_BLITTER_FILL 2u  /* fill the long rows of flat triangles with the Blitter */
#define SGL_AMIGA_NO_AKIKO 4u      /* do not use Akiko (CD32) for chunky-to-planar */

/* The tables that change a 32-bit pixel to a pen of depth planes. */
typedef struct sgl_c2p_tables {
  uint16_t r[256], g[256], b[256]; /* the pen is (r[R] + g[G] + b[B]) >> shift */
  int shift;
  uint32_t index_mask;             /* color index mode: the pen is index & index_mask */
} sgl_c2p_tables;

/* The state of the back end. The program sets the first fields; the
 * other fields are for the back end. */
typedef struct sgl_amiga_chipset {
  struct BitMap *bitmap;           /* the planar bitmap that sgl_present writes */
  struct Screen *screen;           /* with buffers: the screen to flip */
  struct ScreenBuffer *buffers[2]; /* two buffers of screen for double buffering, or NULL */
  int depth;                       /* the planes to write, 1 to 8 */
  unsigned flags;                  /* SGL_AMIGA_... */

  sgl_c2p_tables tables;
  uint8_t *chunky;                 /* one row of pens */
  int chunky_size;
  int back;                        /* the buffer that the next frame goes to */
  int owned;                       /* the back end owns the Blitter */
  int big_blits;                   /* ECS or AGA: large blits */
  volatile uint32_t *akiko;        /* the chunky-to-planar register of Akiko, or NULL */
} sgl_amiga_chipset;

/* Fills be with the entries of the chipset back end for the settings
 * in a. Gives 1 on success, 0 when depth is not 1 to 8 or there is no
 * bitmap. Then call sgl_set_backend(ctx, be, a). */
int sgl_amiga_chipset_init(sgl_amiga_chipset *a, sgl_backend *be);

/* Allocates a color buffer of width x height pixels in Chip RAM, so
 * that the Blitter can clear and fill it. Gives NULL on failure. */
uint32_t *sgl_amiga_alloc_color(int width, int height);

/* Frees a buffer of sgl_amiga_alloc_color. */
void sgl_amiga_free_color(uint32_t *p);

/* Gives the palette that the back end uses for depth planes: 1 << depth
 * colors of 3 bytes (red, green, blue). Load it into the screen, for
 * example with LoadRGB32. */
void sgl_amiga_palette(int depth, uint8_t (*rgb)[3]);

/* The parts without the hardware, which the tests use on any system. */

/* Fills the tables for depth planes. */
void sgl_c2p_init(sgl_c2p_tables *t, int depth);

/* Changes n pixels of 32 bits to pens. index_mode is 1 for a context
 * in color index mode. */
void sgl_c2p_pens(const sgl_c2p_tables *t, const uint32_t *src, uint8_t *dst, int n, int index_mode);

/* Writes n pens (n a multiple of 8) to depth planes. planes[p] points
 * to the first byte of the row in plane p. */
void sgl_c2p_planes(const uint8_t *pens, int n, uint8_t *const *planes, int depth);

#ifdef __cplusplus
}
#endif

#endif
