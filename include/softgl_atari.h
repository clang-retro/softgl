/* softgl_atari.h - the Atari back end of softgl (ST, STE, TT and
 * Falcon): sgl_present writes the frame to the screen in its format
 * (interleaved bit planes, or the 16-bit true color of the Falcon Videl)
 * and flips two screens; the Blitter of the STE, Mega STE and Falcon
 * clears the buffers. See docs/atari.md.
 *
 * Part of clang-retro softgl, a software implementation of OpenGL.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#ifndef SOFTGL_ATARI_H
#define SOFTGL_ATARI_H

#include "softgl.h"
#include "softgl_c2p.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The flags of sgl_atari. */
#define SGL_ATARI_BLITTER_CLEAR 1u /* clear the buffers in ST RAM with the Blitter */
#define SGL_ATARI_NO_VSYNC 2u      /* the flip does not wait for the vertical blank */

/* The state of the back end. The program sets the first fields; the
 * other fields are for the back end. */
typedef struct sgl_atari {
  void *screens[2];        /* the screen memory: screens[1] NULL for one screen */
  int depth;               /* 1, 2, 4 or 8 interleaved planes, or 16 (Falcon true color) */
  int width, height;       /* the size of the screen in pixels */
  long pitch;              /* bytes from one row to the next; 0 for width * depth / 8 */
  unsigned flags;          /* SGL_ATARI_... */

  sgl_c2p_tables tables;
  int back;                /* the screen that sgl_present writes */
  int blitter;             /* the Blitter is there and the flag is set */
  unsigned long st_top;    /* the end of ST RAM (phystop) */
  uint8_t *pens;           /* one row of pens */
} sgl_atari;

/* Fills be with the entries of the Atari back end for a. Call
 * sgl_set_backend(ctx, be, a) after it. Gives 0 when the depth or the
 * size is not valid (with planes, the width must be a multiple of 16),
 * or when there is not sufficient memory. */
int sgl_atari_init(sgl_atari *a, sgl_backend *be);

/* Frees the memory of sgl_atari_init. Call it after the context no
 * longer uses the back end. */
void sgl_atari_free(sgl_atari *a);

#ifdef __cplusplus
}
#endif

#endif
