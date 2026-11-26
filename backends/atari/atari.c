/* atari.c - the Atari back end of softgl: the frame goes to the screen
 * in its format at sgl_present, and the Blitter (STE, Mega STE and
 * Falcon) clears the buffers in ST RAM. See docs/atari.md.
 *
 * The Blitter registers and the system variables are in supervisor
 * memory, so the back end uses them in a function that Supexec calls.
 * The Blitter runs in hog mode: the CPU waits until the blit is done,
 * so the clear is done when the entry returns.
 *
 * Part of clang-retro softgl, a software implementation of OpenGL.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include <stdlib.h>
#include <string.h>

#include <osbind.h>

#include <GL/gl.h>

#include "softgl_atari.h"
#include "../../src/sgl.h"

/* The Blitter registers (16-bit words at 0xFFFF8A00). */
#define BLT_HALFTONE 0x00  /* 16 words */
#define BLT_ENDMASK1 0x14  /* word index of 0x28 */
#define BLT_ENDMASK2 0x15
#define BLT_ENDMASK3 0x16
#define BLT_DST_XINC 0x17  /* 0x2E */
#define BLT_DST_YINC 0x18  /* 0x30 */
#define BLT_DST_ADDR 0x19  /* 0x32, a long */
#define BLT_XCOUNT 0x1B    /* 0x36 */
#define BLT_YCOUNT 0x1C    /* 0x38 */
#define BLT_HOP_OP 0x1D    /* 0x3A: HOP in the high byte, OP in the low byte */
#define BLT_CONTROL 0x1E   /* 0x3C: control in the high byte, skew in the low byte */
#define BLITTER ((volatile uint16_t *)0xFFFF8A00ul)

/* The control bits: busy (start) and hog. */
#define BLT_BUSY 0x80u
#define BLT_HOG 0x40u

/* The end of ST RAM: the system variable phystop. */
#define PHYSTOP (*(volatile unsigned long *)0x42Eul)

/* A word fill for blit_job: the arguments in static memory, because
 * Supexec calls a function without arguments. */
static struct {
  uint16_t *dst;           /* the first word */
  int words, rows;         /* the words of a row and the number of rows */
  int xinc;                /* bytes from one word to the next: 2, or 4 for each second word */
  long pitch;              /* bytes from one row to the next */
  uint16_t value;
  uint16_t first, last;    /* the masks of the first and the last word */
  unsigned long st_top;    /* the results of read_system: phystop */
  long cpu;                /* and the _CPU cookie (0 for a 68000) */
} job;

/* Fills the words of job with the Blitter (in supervisor mode): the
 * halftone pattern (HOP 1) is the value in each of its 16 lines, and
 * OP 3 writes it. */
static long blit_job(void) {
  volatile uint16_t *b = BLITTER;
  int i;
  for (i = 0; i < 16; i++) b[BLT_HALFTONE + i] = job.value;
  b[BLT_ENDMASK1] = job.words == 1 ? (uint16_t)(job.first & job.last) : job.first;
  b[BLT_ENDMASK2] = 0xFFFFu;
  b[BLT_ENDMASK3] = job.last;
  b[BLT_DST_XINC] = (uint16_t)job.xinc;
  b[BLT_DST_YINC] = (uint16_t)(int16_t)(job.pitch - (long)(job.words - 1) * job.xinc);
  *(volatile uint32_t *)&b[BLT_DST_ADDR] = (uint32_t)(unsigned long)job.dst;
  b[BLT_XCOUNT] = (uint16_t)job.words;
  b[BLT_YCOUNT] = (uint16_t)job.rows;
  b[BLT_HOP_OP] = (1u << 8) | 3u;
  b[BLT_CONTROL] = (uint16_t)((BLT_BUSY | BLT_HOG) << 8);
  /* In hog mode the CPU waits for the blit; this loop is for safety. */
  while ((b[BLT_CONTROL] >> 8) & BLT_BUSY) {
  }
  /* The Blitter wrote behind the data cache of a 68020 or 68030: clear
   * the cache (CACR bit 11). The words are movec cacr,d0, ori.w #$800,d0
   * and movec d0,cacr, which the 68000 assembler does not take. */
  if (job.cpu == 20 || job.cpu == 30)
    __asm__ volatile(".short 0x4e7a,0x0002,0x0040,0x0800,0x4e7b,0x0002" ::: "d0", "memory");
  return 0;
}

/* Reads phystop and the _CPU cookie (in supervisor mode). The cookie
 * jar (_p_cookies, 0x5A0) has pairs of a name and a value, and ends
 * with the name 0. */
static long read_system(void) {
  const long *jar = *(long *const *)0x5A0ul;
  job.st_top = PHYSTOP;
  job.cpu = 0;
  for (; jar && jar[0]; jar += 2)
    if (jar[0] == 0x5F435055L) job.cpu = jar[1]; /* "_CPU" */
  return 0;
}

/* Gives 1 when the bytes at p are in ST RAM, where the Blitter gets to. */
static int in_st_ram(const sgl_atari *a, const void *p, unsigned long bytes) {
  return (unsigned long)p + bytes <= a->st_top;
}

/* Fills a rectangle of rows rows of bytes bytes with the 16-bit value v
 * (the Blitter). p is the first byte, pitch the bytes from row to row.
 * An odd first byte or an even last byte keeps the other byte of its
 * word (the end masks). */
static void blit_fill(uint8_t *p, long bytes, int rows, long pitch, uint16_t v) {
  unsigned long start = (unsigned long)p, end = start + (unsigned long)bytes;
  job.dst = (uint16_t *)(start & ~1ul);
  job.words = (int)(((end + 1) & ~1ul) - (start & ~1ul)) / 2;
  job.rows = rows;
  job.xinc = 2;
  job.pitch = pitch;
  job.value = v;
  job.first = (start & 1) ? 0x00FFu : 0xFFFFu;
  job.last = (end & 1) ? 0xFF00u : 0xFFFFu;
  Supexec(blit_job);
}

/* The clear entry: the Blitter clears the buffers that are in ST RAM. */
static unsigned atari_clear(void *user, const sgl_clear_rect *r) {
  sgl_atari *a = (sgl_atari *)user;
  unsigned done = 0;
  if (!a->blitter || r->width <= 0 || r->height <= 0) return 0;
  if ((r->mask & GL_COLOR_BUFFER_BIT) &&
      in_st_ram(a, r->color, (unsigned long)(r->height * r->color_pitch) * 4)) {
    uint16_t hi = (uint16_t)(r->color_value >> 16), lo = (uint16_t)r->color_value;
    if (hi == lo) {
      blit_fill((uint8_t *)r->color, (long)r->width * 4, r->height, r->color_pitch * 4, hi);
    } else {
      /* Two blits: the high words, then the low words (each second
       * word). */
      int pass;
      for (pass = 0; pass < 2; pass++) {
        job.dst = (uint16_t *)r->color + pass;
        job.words = r->width;
        job.rows = r->height;
        job.xinc = 4;
        job.pitch = r->color_pitch * 4;
        job.value = pass ? lo : hi;
        job.first = job.last = 0xFFFFu;
        Supexec(blit_job);
      }
    }
    done |= GL_COLOR_BUFFER_BIT;
  }
  if ((r->mask & GL_DEPTH_BUFFER_BIT) && r->depth_bytes == 2 &&
      in_st_ram(a, r->depth, (unsigned long)(r->height * r->depth_pitch) * 2)) {
    blit_fill((uint8_t *)r->depth, (long)r->width * 2, r->height, r->depth_pitch * 2, (uint16_t)r->depth_value);
    done |= GL_DEPTH_BUFFER_BIT;
  }
  if ((r->mask & GL_STENCIL_BUFFER_BIT) && in_st_ram(a, r->stencil, (unsigned long)(r->height * r->stencil_pitch))) {
    blit_fill(r->stencil, r->width, r->height, r->stencil_pitch,
              (uint16_t)((r->stencil_value << 8) | r->stencil_value));
    done |= GL_STENCIL_BUFFER_BIT;
  }
  return done;
}

/* The present entry: writes the frame to the back screen in its format
 * and flips the screens. */
static void atari_present(void *user, sgl_context *c, const uint32_t *pixels, int width, int height,
                          long rowpixels) {
  sgl_atari *a = (sgl_atari *)user;
  uint8_t *screen = (uint8_t *)a->screens[a->screens[1] ? a->back : 0];
  long pitch = a->pitch ? a->pitch : (long)a->width * a->depth / 8;
  int w = width < a->width ? width : a->width, h = height < a->height ? height : a->height, y;
  if (a->depth != 16) w &= ~15;
  for (y = 0; y < h; y++) {
    const uint32_t *src = pixels + (long)y * rowpixels;
    void *row = screen + (long)y * pitch;
    if (a->depth == 16) {
      sgl_c2p_pens16(&a->tables, src, (uint16_t *)row, w);
    } else {
      sgl_c2p_pens(&a->tables, src, a->pens, w, c->index_mode);
      sgl_c2p_interleaved(a->pens, w, (uint16_t *)row, a->depth);
    }
  }
  if (a->screens[1]) {
    /* The new screen shows at the next vertical blank. */
    Setscreen(-1L, (long)screen, -1);
    if (!(a->flags & SGL_ATARI_NO_VSYNC)) Vsync();
    a->back ^= 1;
  }
}

/* Fills the entries of the Atari back end (see softgl_atari.h). */
int sgl_atari_init(sgl_atari *a, sgl_backend *be) {
  if (!a || !be || !a->screens[0] || a->width <= 0 || a->height <= 0) return 0;
  if (a->depth != 1 && a->depth != 2 && a->depth != 4 && a->depth != 8 && a->depth != 16) return 0;
  if (a->depth != 16 && (a->width & 15)) return 0;
  memset(be, 0, sizeof *be);
  sgl_c2p_init(&a->tables, a->depth);
  a->pens = NULL;
  if (a->depth != 16) {
    a->pens = (uint8_t *)malloc((size_t)a->width);
    if (!a->pens) return 0;
  }
  a->back = 1;
  /* Blitmode(-1) has bit 1 set when the Blitter is there. */
  a->blitter = (a->flags & SGL_ATARI_BLITTER_CLEAR) && (Blitmode(-1) & 2);
  if (a->blitter) {
    Supexec(read_system);
    a->st_top = job.st_top;
    /* A 68040 or 68060 can have copyback lines over the buffers: there
     * the CPU clears. */
    if (job.cpu >= 40) a->blitter = 0;
  }
  if (a->blitter) be->clear = atari_clear;
  be->present = atari_present;
  return 1;
}

/* Frees the memory of sgl_atari_init (see softgl_atari.h). */
void sgl_atari_free(sgl_atari *a) {
  if (!a) return;
  free(a->pens);
  a->pens = NULL;
}
