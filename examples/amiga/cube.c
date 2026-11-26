/* cube.c - a flat-shaded cube that turns on a planar screen, with the
 * Amiga chipset back end of softgl: the Blitter clears the frame and
 * fills the faces, and Akiko or the CPU changes the frame to bit planes.
 * Click the left mouse button to stop.
 *
 *     cube [depth]
 *
 * depth is the number of planes, 3 to 8 (default 5; 8 needs AGA). It
 * needs Kickstart 3.0 (V39) or later for the screen buffers.
 *
 * Part of clang-retro softgl, a software implementation of OpenGL.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include <stdio.h>
#include <stdlib.h>

#include <exec/types.h>
#include <graphics/gfxbase.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

#include <GL/gl.h>

#include "softgl_amiga.h"

#define WIDTH 320
#define HEIGHT 200

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;

/* Gives 1 while the left mouse button is up (CIA-A, port 0 fire). */
static int mouse_up(void) { return (*(volatile UBYTE *)0xBFE001 & 0x40) != 0; }

/* Loads the palette of the back end into the screen. */
static void load_palette(struct Screen *s, int depth) {
  static uint8_t rgb[256][3];
  static ULONG table[256 * 3 + 2];
  int i, n = 1 << depth;
  sgl_amiga_palette(depth, rgb);
  table[0] = (ULONG)n << 16;
  for (i = 0; i < n; i++) {
    table[1 + i * 3] = rgb[i][0] * 0x01010101u;
    table[2 + i * 3] = rgb[i][1] * 0x01010101u;
    table[3 + i * 3] = rgb[i][2] * 0x01010101u;
  }
  table[1 + n * 3] = 0;
  LoadRGB32(&s->ViewPort, table);
}

/* Draws one face of the cube in color r, g, b. */
static void face(GLfloat r, GLfloat g, GLfloat b, const GLfloat (*v)[3]) {
  glColor3f(r, g, b);
  glBegin(GL_QUADS);
  glVertex3fv(v[0]);
  glVertex3fv(v[1]);
  glVertex3fv(v[2]);
  glVertex3fv(v[3]);
  glEnd();
}

/* Draws the cube: six faces of different colors. Culling removes the
 * back faces, so no depth test is necessary. */
static void cube(void) {
  static const GLfloat f[6][4][3] = {
      {{-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}},     {{1, -1, -1}, {-1, -1, -1}, {-1, 1, -1}, {1, 1, -1}},
      {{1, -1, 1}, {1, -1, -1}, {1, 1, -1}, {1, 1, 1}},     {{-1, -1, -1}, {-1, -1, 1}, {-1, 1, 1}, {-1, 1, -1}},
      {{-1, 1, 1}, {1, 1, 1}, {1, 1, -1}, {-1, 1, -1}},     {{-1, -1, -1}, {1, -1, -1}, {1, -1, 1}, {-1, -1, 1}},
  };
  face(1, 0, 0, f[0]);
  face(0, 1, 0, f[1]);
  face(0, 0, 1, f[2]);
  face(1, 1, 0, f[3]);
  face(0, 1, 1, f[4]);
  face(1, 0, 1, f[5]);
}

/* Opens the screen, runs the frames and closes all. */
int main(int argc, char **argv) {
  int depth = argc > 1 ? atoi(argv[1]) : 5;
  struct Screen *screen;
  sgl_amiga_chipset chip = {0};
  sgl_backend be;
  sgl_context *ctx;
  uint32_t *color;
  int frame = 0;
  if (depth < 3 || depth > 8) depth = 5;
  IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR) "intuition.library", 39);
  GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR) "graphics.library", 39);
  if (!IntuitionBase || !GfxBase) {
    fprintf(stderr, "needs Kickstart 3.0 or later\n");
    return 20;
  }
  screen = OpenScreenTags(NULL, SA_Width, WIDTH, SA_Height, HEIGHT, SA_Depth, depth, SA_Quiet, TRUE,
                          SA_ShowTitle, FALSE, SA_Type, CUSTOMSCREEN, TAG_DONE);
  if (!screen) {
    fprintf(stderr, "could not open the screen\n");
    return 20;
  }
  load_palette(screen, depth);
  chip.screen = screen;
  chip.buffers[0] = AllocScreenBuffer(screen, NULL, SB_SCREEN_BITMAP);
  chip.buffers[1] = AllocScreenBuffer(screen, NULL, 0);
  chip.depth = depth;
  chip.flags = SGL_AMIGA_BLITTER_CLEAR | SGL_AMIGA_BLITTER_FILL;
  color = sgl_amiga_alloc_color(WIDTH, HEIGHT);
  ctx = sgl_create_context(NULL);
  if (!chip.buffers[0] || !chip.buffers[1] || !color || !ctx || !sgl_amiga_chipset_init(&chip, &be) ||
      !sgl_set_backend(ctx, &be, &chip) || !sgl_set_buffer(ctx, WIDTH, HEIGHT, color, WIDTH) ||
      !sgl_make_current(ctx)) {
    fprintf(stderr, "not sufficient memory\n");
    return 20;
  }
  glViewport(0, 0, WIDTH, HEIGHT);
  glMatrixMode(GL_PROJECTION);
  glFrustum(-1.6, 1.6, -1.0, 1.0, 2.0, 10.0);
  glMatrixMode(GL_MODELVIEW);
  glEnable(GL_CULL_FACE);
  glShadeModel(GL_FLAT);
  glClearColor(0, 0, 0.5f, 1);
  while (mouse_up()) {
    glClear(GL_COLOR_BUFFER_BIT);
    glLoadIdentity();
    glTranslatef(0, 0, -5);
    glRotatef((GLfloat)(frame * 3), 1, 0, 0);
    glRotatef((GLfloat)(frame * 2), 0, 1, 0);
    cube();
    sgl_present(ctx);
    WaitTOF();
    frame++;
  }
  sgl_make_current(NULL);
  sgl_destroy_context(ctx);
  sgl_amiga_free_color(color);
  FreeScreenBuffer(screen, chip.buffers[1]);
  FreeScreenBuffer(screen, chip.buffers[0]);
  CloseScreen(screen);
  CloseLibrary((struct Library *)GfxBase);
  CloseLibrary((struct Library *)IntuitionBase);
  printf("%d frames\n", frame);
  return 0;
}
