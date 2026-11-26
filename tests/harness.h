/* harness.h - the helpers of the softgl tests: a buffer in memory, the
 * check of a pixel and the dump of a scene as a PPM file.
 *
 * Part of clang-retro softgl, a software implementation of OpenGL.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#ifndef SGL_TEST_HARNESS_H
#define SGL_TEST_HARNESS_H

#include <GL/gl.h>
#include <GL/glu.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "softgl.h"

#define W 64
#define H 64

static uint32_t pixels[W * H];
static int failures;
static const char *prefix;

/* Gives the pixel at x, y (y up, as OpenGL counts). */
static uint32_t at(int x, int y) { return pixels[(H - 1 - y) * W + x] & 0xFFFFFF; }

/* Records a failure when the pixel at x, y is not want (within tol for
 * each component). */
static void expect(const char *what, int x, int y, uint32_t want, int tol) {
  uint32_t got = at(x, y);
  int i;
  for (i = 0; i < 24; i += 8) {
    int a = (int)((got >> i) & 0xFF), b = (int)((want >> i) & 0xFF);
    if (a - b > tol || b - a > tol) {
      printf("FAIL %s: pixel %d,%d is %06x, not %06x\n", what, x, y, (unsigned)got, (unsigned)want);
      failures++;
      return;
    }
  }
}

/* Writes the buffer as a PPM file named prefix-name.ppm. */
static void dump(const char *name) {
  char path[256];
  FILE *f;
  int i;
  if (!prefix) return;
  snprintf(path, sizeof path, "%s-%s.ppm", prefix, name);
  f = fopen(path, "wb");
  if (!f) return;
  fprintf(f, "P6\n%d %d\n255\n", W, H);
  for (i = 0; i < W * H; i++) {
    unsigned char rgb[3] = {(unsigned char)(pixels[i] >> 16), (unsigned char)(pixels[i] >> 8),
                            (unsigned char)pixels[i]};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}

/* Sets the state that each scene starts from. */
static void reset(void) {
  glViewport(0, 0, W, H);
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_LIGHTING);
  glDisable(GL_TEXTURE_2D);
  glDisable(GL_BLEND);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_CULL_FACE);
  glShadeModel(GL_SMOOTH);
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

/* Records a failure named what when cond is 0. */
static inline void check(const char *what, int cond) {
  if (!cond) {
    printf("FAIL %s\n", what);
    failures++;
  }
}

/* Makes a context of size W x H on the buffer pixels and makes it
 * current. index_bits is 0 for RGBA mode, or the bits of an index. */
static inline sgl_context *make_context(int index_bits) {
  sgl_context *ctx = index_bits ? sgl_create_index_context(NULL, index_bits) : sgl_create_context(NULL);
  if (!ctx || !sgl_set_buffer(ctx, W, H, pixels, W) || !sgl_make_current(ctx)) {
    printf("FAIL: no context\n");
    exit(1);
  }
  return ctx;
}

#endif
