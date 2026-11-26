/* glidetest.c - a test of the Glide drivers of softgl for
 * 32-bit DOS (backends/glide/). The test draws with OpenGL
 * through the Glide back end, reads the pixels of the card back through
 * the linear frame buffer, and compares them with the expected colors.
 * It writes "PASS" when all checks are correct.
 *
 * Run it in DOSBox-X with "[voodoo] voodoo_card=software"
 * (tools/test-glide-dosbox.sh), in 86Box with a Voodoo, a ViRGE, a
 * Matrox or an ATI Rage Pro card (tools/test-glide-86box.py), or on a
 * computer with one of the cards.
 *
 *     glidetest [voodoo | virge | matrox | ati]
 *
 * Part of clang-retro softgl, a software implementation of OpenGL.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include <stdio.h>
#include <stdlib.h>

#include <GL/gl.h>

#include "softgl_glide.h"

#define WIDTH 640
#define HEIGHT 480

/* The values of glide_api.h that the test uses. */
#define FRONT_BUFFER 0
#define LFB_READ_ONLY 0
#define LFB_ANY 0xff
#define ORIGIN_LOWER_LEFT 1

/* The driver table, and the DrawTriangle function of the driver. */
static sgl_glide_api api;
static void(SGL_GLIDE_CALL *card_triangle)(const sgl_gr_vertex *, const sgl_gr_vertex *,
                                           const sgl_gr_vertex *);
static int triangles; /* the triangles that the card drew */
static int failures;

/* Counts a triangle and gives it to the card. */
static void SGL_GLIDE_CALL count_triangle(const sgl_gr_vertex *a, const sgl_gr_vertex *b,
                                          const sgl_gr_vertex *c) {
  triangles++;
  card_triangle(a, b, c);
}

/* Reads the pixel at (x, y) of the front buffer (y = 0 is the bottom
 * row) as 8-bit red, green and blue. */
static void pixel(int x, int y, int *r, int *g, int *b) {
  sgl_gr_lfbinfo info;
  uint16_t p;
  if (!api.LfbLock(LFB_READ_ONLY, FRONT_BUFFER, LFB_ANY, ORIGIN_LOWER_LEFT, 0, &info)) {
    *r = *g = *b = -1;
    return;
  }
  p = *(volatile uint16_t *)((volatile uint8_t *)info.ptr + (unsigned long)y * info.stride + x * 2);
  api.LfbUnlock(LFB_READ_ONLY, FRONT_BUFFER);
  *r = (p >> 11) * 255 / 31;
  *g = ((p >> 5) & 63) * 255 / 63;
  *b = (p & 31) * 255 / 31;
}

/* Compares the pixel at (x, y) with the color (r, g, b). The tolerance
 * is tol for each channel, because the card dithers to 16 bits. */
static void expect(const char *name, int x, int y, int r, int g, int b, int tol) {
  int pr, pg, pb;
  pixel(x, y, &pr, &pg, &pb);
  if (abs(pr - r) > tol || abs(pg - g) > tol || abs(pb - b) > tol) {
    printf("FAIL %s: (%d, %d) is %d %d %d, not %d %d %d\n", name, x, y, pr, pg, pb, r, g, b);
    failures++;
  } else {
    printf("ok %s\n", name);
  }
}

/* Draws a quad from (x0, y0) to (x1, y1) at depth z, in window
 * coordinates. */
static void quad(float x0, float y0, float x1, float y1, float z) {
  glBegin(GL_QUADS);
  glTexCoord2f(0, 0), glVertex3f(x0, y0, z);
  glTexCoord2f(1, 0), glVertex3f(x1, y0, z);
  glTexCoord2f(1, 1), glVertex3f(x1, y1, z);
  glTexCoord2f(0, 1), glVertex3f(x0, y1, z);
  glEnd();
}

/* Makes a texture of 8 x 8 texels: red on the left half, green on the
 * right half. */
static void texture(void) {
  static uint8_t t[8 * 8 * 3];
  int x, y;
  for (y = 0; y < 8; y++)
    for (x = 0; x < 8; x++) {
      uint8_t *p = t + (y * 8 + x) * 3;
      p[0] = x < 4 ? 255 : 0, p[1] = x < 4 ? 0 : 255, p[2] = 0;
    }
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 8, 8, 0, GL_RGB, GL_UNSIGNED_BYTE, t);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

/* Opens the card, draws each test scene, reads it back and closes all.
 * The argument can name the driver (voodoo, virge, matrox or ati). */
int main(int argc, char **argv) {
  static uint32_t color[WIDTH * HEIGHT];
  sgl_glide g = {0};
  sgl_context *ctx;
  /* Write each line at once, so that a crash keeps the lines before it. */
  setvbuf(stdout, NULL, _IONBF, 0);
  const char *driver = sgl_glide_dos(&api, argc > 1 ? argv[1] : NULL);
  if (!driver) {
    printf("FAIL no 3D card\n");
    return 1;
  }
  printf("driver %s\n", driver);
  card_triangle = api.DrawTriangle;
  api.DrawTriangle = count_triangle;
  ctx = sgl_create_context(NULL);
  if (!ctx || !sgl_set_buffer(ctx, WIDTH, HEIGHT, color, WIDTH) || !sgl_make_current(ctx)) {
    printf("FAIL not sufficient memory\n");
    return 1;
  }
  g.api = &api;
  g.width = WIDTH;
  g.height = HEIGHT;
  g.flags = SGL_GLIDE_NO_VSYNC;
  if (!sgl_glide_attach(&g, ctx)) {
    printf("FAIL the card does not open\n");
    return 1;
  }
  printf("card open\n");
  glViewport(0, 0, WIDTH, HEIGHT);
  glMatrixMode(GL_PROJECTION);
  glOrtho(0, WIDTH, 0, HEIGHT, -1, 1);
  glMatrixMode(GL_MODELVIEW);

  /* 1: the clear color, and a flat quad. */
  glClearColor(0, 0, 1, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  glColor3f(1, 0, 0);
  quad(100, 100, 300, 200, 0);
  sgl_present(ctx);
  expect("clear", 10, 10, 0, 0, 255, 8);
  expect("flat quad", 200, 150, 255, 0, 0, 8);
  expect("bottom-left origin", 200, 300, 0, 0, 255, 8);

  /* 2: a smooth triangle: red, green and blue corners. */
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  glShadeModel(GL_SMOOTH);
  glBegin(GL_TRIANGLES);
  glColor3f(1, 0, 0), glVertex2f(0, 0);
  glColor3f(0, 1, 0), glVertex2f(600, 0);
  glColor3f(0, 0, 1), glVertex2f(0, 600);
  glEnd();
  sgl_present(ctx);
  expect("smooth red corner", 2, 2, 255, 0, 0, 12);
  expect("smooth green corner", 590, 2, 3, 250, 1, 12);
  expect("smooth middle", 200, 200, 85, 85, 85, 16);

  /* 3: the depth buffer. The far quad comes after the middle quad, and
   * must be hidden; the near quad comes last, and must be in front. */
  glEnable(GL_DEPTH_TEST);
  glClearDepth(1);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glColor3f(0, 1, 0);
  quad(100, 100, 300, 300, 0.5f);
  glColor3f(1, 1, 1);
  quad(200, 200, 400, 400, -0.5f);
  glColor3f(1, 0, 0);
  quad(120, 120, 180, 180, 0.9f);
  sgl_present(ctx);
  glDisable(GL_DEPTH_TEST);
  expect("depth middle", 250, 250, 0, 255, 0, 8);
  expect("depth far", 350, 350, 255, 255, 255, 8);
  expect("depth near", 150, 150, 255, 0, 0, 8);

  /* 4: a texture. */
  glClear(GL_COLOR_BUFFER_BIT);
  texture();
  glEnable(GL_TEXTURE_2D);
  glColor3f(1, 1, 1);
  quad(100, 100, 300, 300, 0);
  sgl_present(ctx);
  glDisable(GL_TEXTURE_2D);
  expect("texture left", 150, 200, 255, 0, 0, 8);
  expect("texture right", 250, 200, 0, 255, 0, 8);

  /* 5: blending: half white over black. */
  glClear(GL_COLOR_BUFFER_BIT);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glColor4f(1, 1, 1, 0.5f);
  quad(100, 100, 300, 300, 0);
  sgl_present(ctx);
  glDisable(GL_BLEND);
  expect("blend", 200, 200, 128, 128, 128, 12);

  /* 6: the alpha test: the quad with the small alpha is not drawn. */
  glClear(GL_COLOR_BUFFER_BIT);
  glEnable(GL_ALPHA_TEST);
  glAlphaFunc(GL_GREATER, 0.5f);
  glColor4f(1, 1, 0, 0.25f);
  quad(100, 100, 200, 200, 0);
  glColor4f(0, 1, 1, 0.75f);
  quad(300, 100, 400, 200, 0);
  sgl_present(ctx);
  glDisable(GL_ALPHA_TEST);
  expect("alpha test fail", 150, 150, 0, 0, 0, 8);
  expect("alpha test pass", 350, 150, 0, 255, 255, 8);

  sgl_make_current(NULL);
  sgl_destroy_context(ctx);
  printf("%d triangles on the card\n", triangles);
  if (triangles < 10) {
    printf("FAIL the card drew too few triangles\n");
    failures++;
  }
  if (failures) {
    printf("%d failures\n", failures);
    return 1;
  }
  printf("PASS softgl glide on the %s card\n", driver);
  return 0;
}
