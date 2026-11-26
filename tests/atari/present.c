/* present.c - the test of the Atari back end in an emulator (Hatari,
 * tools/test-atari-hatari.sh): it clears with the Blitter, draws a
 * triangle, presents, and examines the buffer of softgl and the screen
 * memory. It writes "PASS softgl atari" or the failures to the screen
 * and to RESULT.TXT in the current directory.
 *
 *     present.tos
 *
 * Part of clang-retro softgl, a software implementation of OpenGL.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include <stdio.h>
#include <stdlib.h>

#include <osbind.h>

#include <GL/gl.h>

#include "softgl_atari.h"

#define W 320
#define H 200

static FILE *out;
static int failures;
static long video;         /* the _VDO cookie: 0x00030000 on a Falcon */

/* Records a failure named what when cond is 0. */
static void check(const char *what, int cond) {
  if (cond) return;
  failures++;
  printf("FAIL %s\n", what);
  if (out) fprintf(out, "FAIL %s\n", what);
}

/* Reads the _VDO cookie (in supervisor mode). */
static long read_vdo(void) {
  const long *jar = *(long *const *)0x5A0ul;
  for (; jar && jar[0]; jar += 2)
    if (jar[0] == 0x5F56444FL) video = jar[1]; /* "_VDO" */
  return 0;
}

/* Gives memory in ST RAM on a boundary of 256 bytes. */
static void *st_memory(long bytes) {
  /* The C library keeps the memory of the program for malloc, so try
   * Mxalloc first and malloc after. The program loads in ST RAM unless
   * its header allows TT RAM. */
  char *p = (char *)Mxalloc(bytes + 256, MX_STRAM);
  if ((long)p <= 0) p = (char *)malloc((size_t)bytes + 256);
  if (!p || (long)p < 0) return NULL;
  return (void *)(((unsigned long)p + 255) & ~255ul);
}

/* Runs the test with depth (4 or 16) on a screen in memory that is not
 * shown. */
static void run(int depth) {
  sgl_atari at = {0};
  sgl_backend be;
  sgl_context *ctx;
  uint32_t *color = (uint32_t *)st_memory((long)W * H * 4);
  char what[64];
  at.depth = depth;
  at.width = W;
  at.height = H;
  at.screens[0] = st_memory((long)W * H * depth / 8);
  at.flags = SGL_ATARI_BLITTER_CLEAR;
  ctx = sgl_create_context(NULL);
  if (!color || !at.screens[0] || !ctx) {
    check("memory", 0);
    return;
  }
  check("init", sgl_atari_init(&at, &be) == 1);
  sgl_set_backend(ctx, &be, &at);
  sgl_set_buffer(ctx, W, H, color, W);
  sgl_make_current(ctx);
  /* An ST and a TT have no Blitter: then the CPU clears. */
  snprintf(what, sizeof what, "the Blitter is used when it is there (depth %d)", depth);
  check(what, at.blitter == ((Blitmode(-1) & 2) != 0));
  if (depth == 4 && out) fprintf(out, "Blitter %s\n", at.blitter ? "on" : "off");
  glViewport(0, 0, W, H);
  /* Two different halves: the clear needs the two blits of words. */
  glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
  glClearDepth(1.0);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  snprintf(what, sizeof what, "the clear in the buffer (depth %d)", depth);
  check(what, color[0] == 0xFF0000FFu && color[W * H - 1] == 0xFF0000FFu && color[W * 100 + 7] == 0xFF0000FFu);
  /* A scissor box of odd bytes: the stencil masks. */
  glEnable(GL_SCISSOR_TEST);
  glScissor(3, 5, 7, 2);
  glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glDisable(GL_SCISSOR_TEST);
  snprintf(what, sizeof what, "the clear of the scissor box (depth %d)", depth);
  check(what, color[(H - 1 - 5) * W + 3] == 0xFFFFFFFFu && color[(H - 1 - 5) * W + 2] == 0xFF0000FFu &&
                  color[(H - 1 - 5) * W + 10] == 0xFF0000FFu && color[(H - 1 - 7) * W + 3] == 0xFF0000FFu);
  glEnable(GL_DEPTH_TEST);
  glColor3f(1.0f, 0.0f, 0.0f);
  glBegin(GL_TRIANGLES);
  glVertex2f(-0.5f, -0.5f);
  glVertex2f(0.5f, -0.5f);
  glVertex2f(0.0f, 0.5f);
  glEnd();
  sgl_present(ctx);
  if (depth == 16) {
    const uint16_t *s = (const uint16_t *)at.screens[0];
    snprintf(what, sizeof what, "the screen in true color");
    check(what, s[0] == 0x001F && s[(H / 2) * W + W / 2] == 0xF800 && s[(H - 1 - 5) * W + 3] == 0xFFFF);
  } else {
    /* Red is pen 0b0100 + ..., blue pen 1: compare with the tables. */
    const uint16_t *s = (const uint16_t *)at.screens[0];
    uint8_t pen_blue, pen_red;
    uint32_t px = 0xFF0000FFu, pr = 0xFFFF0000u;
    int p, ok = 1;
    sgl_c2p_pens(&at.tables, &px, &pen_blue, 1, 0);
    sgl_c2p_pens(&at.tables, &pr, &pen_red, 1, 0);
    for (p = 0; p < 4; p++) {
      uint16_t want0 = (pen_blue >> p) & 1 ? 0xFFFF : 0;
      uint16_t wantc = (pen_red >> p) & 1 ? 0xFFFF : 0;
      if (s[p] != want0) ok = 0;
      if (s[(H / 2) * (W / 16) * 4 + (W / 2 / 16) * 4 + p] != wantc) ok = 0;
    }
    check("the screen in 4 interleaved planes", ok);
  }
  sgl_make_current(NULL);
  sgl_destroy_context(ctx);
  sgl_atari_free(&at);
}

/* Runs the tests: 16-bit true color on a Falcon, then 4 planes. */
int main(void) {
  out = fopen("RESULT.TXT", "w");
  Supexec(read_vdo);
  if ((video >> 16) == 3) run(16);
  run(4);
  if (!failures) {
    printf("PASS softgl atari\n");
    if (out) fprintf(out, "PASS softgl atari\n");
  }
  if (out) fclose(out);
  return failures != 0;
}
