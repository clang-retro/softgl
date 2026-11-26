/* chipset.c - the Amiga chipset back end of softgl (OCS, ECS and AGA):
 * the Blitter clears the buffers and, when the program asks for it,
 * fills the long rows of flat triangles. sgl_present changes the frame
 * to bit planes with Akiko (CD32) or with the CPU, and can flip two
 * screen buffers. See docs/amiga.md.
 *
 * Part of clang-retro softgl, a software implementation of OpenGL.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include <exec/execbase.h>
#include <exec/memory.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <hardware/custom.h>
#include <intuition/screens.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

#include <GL/gl.h>

#include "softgl_amiga.h"
#include "../../src/sgl.h"

/* The custom chips. */
#define CHIPS ((volatile struct Custom *)0xDFF000)

/* The bits of BLTCON0. */
#define USEC 0x0200u
#define USED 0x0100u
#define MINTERM_A 0xF0u        /* D = A */
#define MINTERM_COOKIE 0xCAu   /* D = A B + (not A) C: B where the mask A is set */

/* The rows shorter than this go to the CPU: a blit costs more to set up. */
#define MIN_BLIT_PIXELS 16

/* Gives 1 when bytes bytes from p are all in Chip RAM, where the
 * Blitter can get to them. */
static int in_chip(const void *p, size_t bytes) {
  return (IPTR)p + bytes <= (IPTR)SysBase->MaxLocMem;
}

/* Makes the Blitter ours and waits until it is free. */
static void take_blitter(sgl_amiga_chipset *a) {
  if (!a->owned) {
    OwnBlitter();
    a->owned = 1;
  }
  WaitBlit();
}

/* Sets words x rows words from dst to value, with mod bytes from the end
 * of a row to the next row. fwm and lwm mask the first and the last word
 * of each row: the bits that are 0 keep the old value. The blit can run
 * after the return. */
static void blit_fill(sgl_amiga_chipset *a, uint8_t *dst, int words, int rows, int mod, uint16_t value,
                      uint16_t fwm, uint16_t lwm) {
  int max_w = a->big_blits ? 2048 : 64, max_h = a->big_blits ? 32768 : 1024;
  /* A wide rectangle is cut in stripes; a tall one in bands. */
  while (words > 0) {
    int w = words > max_w ? max_w : words, y = 0;
    uint16_t f = fwm, l = w == words ? lwm : 0xFFFFu;
    int stripe_mod = mod + (words - w) * 2;
    while (y < rows) {
      int h = rows - y > max_h ? max_h : rows - y;
      int masked = f != 0xFFFFu || l != 0xFFFFu;
      take_blitter(a);
      CHIPS->bltcon0 = (uint16_t)(masked ? (USEC | USED | MINTERM_COOKIE) : (USED | MINTERM_A));
      CHIPS->bltcon1 = 0;
      CHIPS->bltafwm = f;
      CHIPS->bltalwm = l;
      CHIPS->bltcmod = (uint16_t)stripe_mod;
      CHIPS->bltdmod = (uint16_t)stripe_mod;
      CHIPS->bltcpt = dst + (long)y * (w * 2 + stripe_mod);
      CHIPS->bltdpt = dst + (long)y * (w * 2 + stripe_mod);
      /* Without masks, D = A; with masks, A is the mask and B the value. */
      CHIPS->bltadat = masked ? 0xFFFFu : value;
      CHIPS->bltbdat = value;
      if (a->big_blits) {
        CHIPS->bltsizv = (uint16_t)h;
        CHIPS->bltsizh = (uint16_t)w; /* starts the blit */
      } else {
        CHIPS->bltsize = (uint16_t)((h << 6) | (w & 63)); /* starts the blit */
      }
      y += h;
    }
    dst += w * 2;
    words -= w;
    fwm = 0xFFFFu;
  }
}

/* Sets n 32-bit values from dst (word aligned) to value with the
 * Blitter. */
static void fill32(sgl_amiga_chipset *a, uint32_t *dst, long n, uint32_t value) {
  uint16_t hi = (uint16_t)(value >> 16), lo = (uint16_t)value;
  if (hi == lo) {
    /* One value for each word: rows of 64 words. */
    long words = n * 2;
    long rows = words / 64;
    if (rows) blit_fill(a, (uint8_t *)dst, 64, (int)rows, 0, hi, 0xFFFFu, 0xFFFFu);
    if (words % 64) blit_fill(a, (uint8_t *)dst + rows * 128, (int)(words % 64), 1, 0, hi, 0xFFFFu, 0xFFFFu);
    return;
  }
  /* A column of 1 word with a modulo of 2 bytes writes every second
   * word: one blit for the high words and one for the low words. */
  blit_fill(a, (uint8_t *)dst, 1, (int)n, 2, hi, 0xFFFFu, 0xFFFFu);
  blit_fill(a, (uint8_t *)dst + 2, 1, (int)n, 2, lo, 0xFFFFu, 0xFFFFu);
}

/* The clear entry: clears the buffers in Chip RAM with the Blitter. */
static unsigned chip_clear(void *user, const sgl_clear_rect *r) {
  sgl_amiga_chipset *a = (sgl_amiga_chipset *)user;
  unsigned done = 0;
  int y;
  if (r->mask & GL_COLOR_BUFFER_BIT) {
    long bytes = ((long)(r->height - 1) * r->color_pitch + r->width) * 4;
    if (in_chip(r->color, (size_t)bytes)) {
      if (r->color_pitch == r->width)
        fill32(a, r->color, (long)r->width * r->height, r->color_value);
      else
        for (y = 0; y < r->height; y++) fill32(a, r->color + y * r->color_pitch, r->width, r->color_value);
      done |= GL_COLOR_BUFFER_BIT;
    }
  }
  if (r->mask & GL_DEPTH_BUFFER_BIT) {
    long bytes = ((long)(r->height - 1) * r->depth_pitch + r->width) * r->depth_bytes;
    if (in_chip(r->depth, (size_t)bytes)) {
      if (r->depth_bytes == 2) {
        blit_fill(a, (uint8_t *)r->depth, r->width, r->height, (int)(r->depth_pitch - r->width) * 2,
                  (uint16_t)r->depth_value, 0xFFFFu, 0xFFFFu);
      } else if (r->depth_pitch == r->width) {
        fill32(a, (uint32_t *)r->depth, (long)r->width * r->height, r->depth_value);
      } else {
        for (y = 0; y < r->height; y++)
          fill32(a, (uint32_t *)r->depth + y * r->depth_pitch, r->width, r->depth_value);
      }
      done |= GL_DEPTH_BUFFER_BIT;
    }
  }
  /* The stencil buffer has bytes: the masks keep the bytes outside the
   * rectangle in the first and the last word. An odd pitch changes the
   * alignment from row to row, so softgl clears it then. */
  if ((r->mask & GL_STENCIL_BUFFER_BIT) && !(r->stencil_pitch & 1)) {
    long bytes = (long)(r->height - 1) * r->stencil_pitch + r->width;
    if (in_chip(r->stencil, (size_t)bytes)) {
      uint8_t *start = (uint8_t *)((IPTR)r->stencil & ~(IPTR)1);
      uint8_t *end = r->stencil + r->width;
      int words = (int)((end - start + 1) / 2);
      uint16_t fwm = ((IPTR)r->stencil & 1) ? 0x00FFu : 0xFFFFu;
      uint16_t lwm = ((IPTR)end & 1) ? 0xFF00u : 0xFFFFu;
      uint16_t v = (uint16_t)(r->stencil_value * 0x0101u);
      if (words == 1) fwm &= lwm, lwm = fwm;
      blit_fill(a, start, words, r->height, (int)(r->stencil_pitch - words * 2), v, fwm, lwm);
      done |= GL_STENCIL_BUFFER_BIT;
    }
  }
  return done;
}

/* The fill_span entry: a long row in Chip RAM goes to the Blitter, a
 * short row to the CPU. */
static void chip_fill_span(void *user, uint32_t *dst, int count, uint32_t value) {
  sgl_amiga_chipset *a = (sgl_amiga_chipset *)user;
  if (count >= MIN_BLIT_PIXELS && in_chip(dst, (size_t)count * 4)) {
    fill32(a, dst, count, value);
    return;
  }
  while (count--) *dst++ = value;
}

/* The sync entry: waits for the Blitter and gives it back to the
 * system. */
static void chip_sync(void *user) {
  sgl_amiga_chipset *a = (sgl_amiga_chipset *)user;
  if (!a->owned) return;
  WaitBlit();
  DisownBlitter();
  a->owned = 0;
}

/* The alloc entry: the depth and stencil buffers go in Chip RAM when
 * there is sufficient Chip RAM, else in any memory. The size is kept in
 * front of the buffer for FreeMem. */
static void *chip_alloc(void *user, size_t bytes) {
  ULONG *p;
  (void)user;
  p = (ULONG *)AllocMem(bytes + 8, MEMF_CHIP);
  if (!p) p = (ULONG *)AllocMem(bytes + 8, MEMF_ANY);
  if (!p) return NULL;
  p[0] = (ULONG)(bytes + 8);
  return p + 2;
}

/* The release entry: frees a buffer of chip_alloc. */
static void chip_release(void *user, void *mem) {
  ULONG *p = (ULONG *)mem - 2;
  (void)user;
  FreeMem(p, p[0]);
}

/* Allocates a color buffer in Chip RAM (see softgl_amiga.h). */
uint32_t *sgl_amiga_alloc_color(int width, int height) {
  ULONG *p;
  size_t bytes;
  if (width <= 0 || height <= 0) return NULL;
  bytes = (size_t)width * (size_t)height * 4;
  p = (ULONG *)AllocMem(bytes + 8, MEMF_CHIP | MEMF_CLEAR);
  if (!p) return NULL;
  p[0] = (ULONG)(bytes + 8);
  return (uint32_t *)(p + 2);
}

/* Frees a buffer of sgl_amiga_alloc_color. */
void sgl_amiga_free_color(uint32_t *mem) {
  if (mem) chip_release(NULL, mem);
}

/* Writes the 32 pens from pens to 32 bits of depth planes with Akiko:
 * 8 longwords of pens go in, 8 longwords of planes come out. */
static void akiko_32(volatile uint32_t *akiko, const uint8_t *pens, uint8_t *const *planes, int x,
                     int depth) {
  const uint32_t *in = (const uint32_t *)pens;
  uint32_t out[8];
  int p;
  for (p = 0; p < 8; p++) *akiko = in[p];
  for (p = 0; p < 8; p++) out[p] = *akiko;
  for (p = 0; p < depth; p++) *(uint32_t *)(planes[p] + (x >> 3)) = out[p];
}

/* The present entry: changes the frame to pens and the pens to the bit
 * planes of the bitmap, and flips the screen buffers. */
static void chip_present(void *user, sgl_context *c, const uint32_t *pixels, int width, int height,
                         long rowpixels) {
  sgl_amiga_chipset *a = (sgl_amiga_chipset *)user;
  struct ScreenBuffer *sb = a->buffers[0] && a->buffers[1] ? a->buffers[a->back] : NULL;
  struct BitMap *bm = sb ? sb->sb_BitMap : a->bitmap;
  int n = (width + 7) & ~7, y, p, x;
  uint8_t *planes[8];
  chip_sync(a);
  if (!bm) return;
  if (n > (int)bm->BytesPerRow * 8) n = (int)bm->BytesPerRow * 8;
  if (height > (int)bm->Rows) height = (int)bm->Rows;
  if (a->chunky_size < n + 32) {
    if (a->chunky) FreeMem(a->chunky, (ULONG)a->chunky_size);
    a->chunky_size = n + 32;
    a->chunky = (uint8_t *)AllocMem((ULONG)a->chunky_size, MEMF_ANY | MEMF_CLEAR);
    if (!a->chunky) {
      a->chunky_size = 0;
      return;
    }
  }
  for (y = 0; y < height; y++) {
    int pixels_in_row = width < n ? width : n;
    sgl_c2p_pens(&a->tables, pixels + (long)y * rowpixels, a->chunky, pixels_in_row, c->index_mode);
    for (p = 0; p < a->depth; p++) planes[p] = (uint8_t *)bm->Planes[p] + (long)y * bm->BytesPerRow;
    x = 0;
    if (a->akiko)
      for (; x + 32 <= n; x += 32) akiko_32(a->akiko, a->chunky + x, planes, x, a->depth);
    if (x < n) {
      uint8_t *rest[8];
      for (p = 0; p < a->depth; p++) rest[p] = planes[p] + (x >> 3);
      sgl_c2p_planes(a->chunky + x, n - x, rest, a->depth);
    }
  }
  if (sb && a->screen) {
    ChangeScreenBuffer(a->screen, sb);
    a->back ^= 1;
  }
}

/* The detach entry: gives back the Blitter and frees the row of pens. */
static void chip_detach(void *user, sgl_context *c) {
  sgl_amiga_chipset *a = (sgl_amiga_chipset *)user;
  (void)c;
  chip_sync(a);
  if (a->chunky) FreeMem(a->chunky, (ULONG)a->chunky_size);
  a->chunky = NULL;
  a->chunky_size = 0;
}

/* Fills the entries of the back end (see softgl_amiga.h). */
int sgl_amiga_chipset_init(sgl_amiga_chipset *a, sgl_backend *be) {
  if (!a || !be || a->depth < 1 || a->depth > 8) return 0;
  if (!a->bitmap && !(a->buffers[0] && a->buffers[1])) return 0;
  memset(be, 0, sizeof *be);
  sgl_c2p_init(&a->tables, a->depth);
  a->chunky = NULL;
  a->chunky_size = 0;
  a->back = 0;
  a->owned = 0;
  a->big_blits = (GfxBase->ChipRevBits0 & GFXF_BIG_BLITS) != 0;
  /* Akiko is there when graphics.library (V40) found it. */
  a->akiko = NULL;
  if (!(a->flags & SGL_AMIGA_NO_AKIKO) && GfxBase->LibNode.lib_Version >= 40)
    a->akiko = (volatile uint32_t *)GfxBase->ChunkyToPlanarPtr;
  if (a->flags & SGL_AMIGA_BLITTER_CLEAR) {
    be->alloc = chip_alloc;
    be->release = chip_release;
    be->clear = chip_clear;
  }
  if (a->flags & SGL_AMIGA_BLITTER_FILL) be->fill_span = chip_fill_span;
  be->sync = chip_sync;
  be->present = chip_present;
  be->detach = chip_detach;
  return 1;
}
