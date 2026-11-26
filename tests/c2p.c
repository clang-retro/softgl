/* c2p.c - the tests of the chunky-to-planar code of the Amiga back end
 * (backends/amiga/c2p.c), which has no hardware code: the pens, the
 * palette and the bit planes.
 *
 * Part of clang-retro softgl, a software implementation of OpenGL.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include <stdio.h>
#include <string.h>

#include "../backends/amiga/c2p.c"

static int failures;

/* Records a failure named what when cond is 0. */
static void check(const char *what, int cond) {
  if (!cond) {
    printf("FAIL %s\n", what);
    failures++;
  }
}

/* Gives a pseudo-random number (a linear congruential generator). */
static uint32_t rnd(void) {
  static uint32_t s = 12345;
  s = s * 1103515245u + 12345u;
  return s >> 8;
}

/* Tests the bit planes against a bit-by-bit conversion. */
static void test_planes(void) {
  enum { N = 64 };
  uint8_t pens[N], got[8][N / 8], want[8][N / 8];
  uint8_t *planes[8];
  int depth, i, p, round;
  for (p = 0; p < 8; p++) planes[p] = got[p];
  for (round = 0; round < 50; round++) {
    for (i = 0; i < N; i++) pens[i] = (uint8_t)rnd();
    for (depth = 1; depth <= 8; depth++) {
      memset(got, 0xA5, sizeof got);
      memset(want, 0xA5, sizeof want);
      for (p = 0; p < depth; p++) {
        memset(want[p], 0, N / 8);
        for (i = 0; i < N; i++)
          if ((pens[i] >> p) & 1) want[p][i >> 3] |= (uint8_t)(0x80 >> (i & 7));
      }
      sgl_c2p_planes(pens, N, planes, depth);
      if (memcmp(got, want, sizeof got) != 0) {
        printf("FAIL planes: depth %d, round %d\n", depth, round);
        failures++;
        return;
      }
    }
  }
}

/* Tests the pens and the palette: the palette color of the pen of a
 * pixel is near the pixel, and the primary colors are exact. */
static void test_pens(void) {
  sgl_c2p_tables t;
  uint8_t rgb[256][3], pen[4];
  static const uint32_t px[4] = {0xFF000000u, 0xFFFFFFFFu, 0xFFFF0000u, 0xFF0000FFu};
  int depth;
  for (depth = 1; depth <= 8; depth++) {
    char what[64];
    sgl_c2p_init(&t, depth);
    sgl_amiga_palette(depth, rgb);
    sgl_c2p_pens(&t, px, pen, 4, 0);
    snprintf(what, sizeof what, "black and white, depth %d", depth);
    check(what, pen[0] == 0 && pen[1] == (1 << depth) - 1);
    snprintf(what, sizeof what, "palette of white, depth %d", depth);
    check(what, rgb[pen[1]][0] == 255 && rgb[pen[1]][1] == 255 && rgb[pen[1]][2] == 255);
    if (depth >= 3) {
      snprintf(what, sizeof what, "red and blue, depth %d", depth);
      check(what, rgb[pen[2]][0] == 255 && rgb[pen[2]][1] == 0 && rgb[pen[2]][2] == 0 &&
                      rgb[pen[3]][0] == 0 && rgb[pen[3]][2] == 255);
    }
    {
      int i, worst = 0;
      for (i = 0; i < 2000; i++) {
        uint32_t p = rnd() & 0xFFFFFFu;
        uint8_t q;
        int c, d;
        sgl_c2p_pens(&t, &p, &q, 1, 0);
        if (depth <= 2) continue; /* gray: not near a color */
        for (c = 0; c < 3; c++) {
          d = (int)((p >> (16 - 8 * c)) & 0xFF) - rgb[q][c];
          if (d < 0) d = -d;
          if (d > worst) worst = d;
        }
      }
      snprintf(what, sizeof what, "the nearest level, depth %d", depth);
      check(what, worst <= 255);
      /* With 2 bits or more for each channel, the error is less than
       * one level of 3 bits (a palette level is 255 / 3 = 85). */
      if (depth >= 6) check(what, worst < 86);
    }
  }
  /* Color index mode: the index, masked to the planes. */
  sgl_c2p_init(&t, 5);
  {
    uint32_t idx[2] = {7, 0x123};
    sgl_c2p_pens(&t, idx, pen, 2, 1);
    check("index mode", pen[0] == 7 && pen[1] == (0x123 & 31));
  }
}

/* Runs the tests. */
int main(void) {
  test_planes();
  test_pens();
  if (failures) return 1;
  printf("PASS softgl c2p\n");
  return 0;
}
