/* softgl_amiga.h - the Amiga back ends of softgl. The chipset back end
 * has Blitter clears, chunky-to-planar conversion with Akiko or the CPU,
 * and an optional Blitter fill of flat spans. The Warp3D back end draws
 * with a 3D card. See docs/amiga.md.
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
#include "softgl_c2p.h"

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
 * example with LoadRGB32. It is sgl_c2p_palette of softgl_c2p.h. */
#define sgl_amiga_palette sgl_c2p_palette

/* The flags of sgl_amiga_warp3d. */
#define SGL_AMIGA_W3D_ALLOW_CPU 1u /* also use the CPU driver of Warp3D (slower than softgl on a 68k) */

/* The state of the Warp3D back end. The program sets the first fields;
 * the other fields are for the back end. */
typedef struct sgl_amiga_warp3d {
  struct BitMap *bitmap;           /* the bitmap of an RTG screen to draw in */
  struct Screen *screen;           /* with buffers: the screen to flip */
  struct ScreenBuffer *buffers[2]; /* two buffers of screen for double buffering, or NULL */
  unsigned flags;                  /* SGL_AMIGA_W3D_... */

  void *state;
  int back;                        /* the buffer that the card draws in */
  int opened;                      /* the back end opened Warp3D.library */
} sgl_amiga_warp3d;

/* Opens Warp3D.library (version 2 or later), makes a Warp3D context on
 * the bitmap (or on buffers[1]) and gives ctx the Warp3D back end. The
 * size of the color buffer of ctx (sgl_set_buffer) must be the size of
 * the bitmap. Gives 1 on success, and 0 when there is no 3D card, no
 * Warp3D or a pixel format that the back end does not know: ctx then
 * stays in software. */
int sgl_amiga_warp3d_attach(sgl_amiga_warp3d *w, sgl_context *ctx);

#ifdef __cplusplus
}
#endif

#endif
