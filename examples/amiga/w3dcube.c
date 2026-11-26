/* w3dcube.c - a lit, depth-tested cube that turns on an RTG screen, with
 * the Warp3D back end of softgl: the 3D card draws the triangles. Click
 * the left mouse button to stop.
 *
 *     w3dcube
 *
 * It needs Kickstart 3.0 (V39) or later, an RTG screen like the
 * Workbench (15 to 32 bits) and Warp3D with a driver for the card.
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

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;

/* Gives 1 while the left mouse button is up (CIA-A, port 0 fire). */
static int mouse_up(void) { return (*(volatile UBYTE *)0xBFE001 & 0x40) != 0; }

/* Draws the cube: six faces with normals. */
static void cube(void) {
  static const GLfloat n[6][3] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
  static const GLfloat f[6][4][3] = {
      {{-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}},     {{1, -1, -1}, {-1, -1, -1}, {-1, 1, -1}, {1, 1, -1}},
      {{1, -1, 1}, {1, -1, -1}, {1, 1, -1}, {1, 1, 1}},     {{-1, -1, -1}, {-1, -1, 1}, {-1, 1, 1}, {-1, 1, -1}},
      {{-1, 1, 1}, {1, 1, 1}, {1, 1, -1}, {-1, 1, -1}},     {{-1, -1, -1}, {1, -1, -1}, {1, -1, 1}, {-1, -1, 1}},
  };
  int i, k;
  glBegin(GL_QUADS);
  for (i = 0; i < 6; i++) {
    glNormal3fv(n[i]);
    for (k = 0; k < 4; k++) glVertex3fv(f[i][k]);
  }
  glEnd();
}

/* Opens the screen, runs the frames and closes all. */
int main(void) {
  static const GLfloat light[4] = {1, 1, 2, 0}, red[4] = {0.9f, 0.2f, 0.1f, 1};
  struct Screen *screen;
  sgl_amiga_warp3d w3d = {0};
  sgl_context *ctx;
  uint32_t *color;
  int frame = 0, width, height;
  IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR) "intuition.library", 39);
  GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR) "graphics.library", 39);
  if (!IntuitionBase || !GfxBase) {
    fprintf(stderr, "needs Kickstart 3.0 or later\n");
    return 20;
  }
  screen = OpenScreenTags(NULL, SA_LikeWorkbench, TRUE, SA_Quiet, TRUE, SA_ShowTitle, FALSE,
                          SA_Type, CUSTOMSCREEN, TAG_DONE);
  if (!screen) {
    fprintf(stderr, "could not open the screen\n");
    return 20;
  }
  width = screen->Width;
  height = screen->Height;
  w3d.screen = screen;
  w3d.buffers[0] = AllocScreenBuffer(screen, NULL, SB_SCREEN_BITMAP);
  w3d.buffers[1] = AllocScreenBuffer(screen, NULL, 0);
  color = (uint32_t *)malloc((size_t)width * height * 4);
  ctx = sgl_create_context(NULL);
  if (!w3d.buffers[0] || !w3d.buffers[1] || !color || !ctx || !sgl_set_buffer(ctx, width, height, color, width) ||
      !sgl_make_current(ctx)) {
    fprintf(stderr, "not sufficient memory\n");
    return 20;
  }
  if (!sgl_amiga_warp3d_attach(&w3d, ctx)) {
    fprintf(stderr, "no Warp3D driver for a 3D card on this screen\n");
    sgl_destroy_context(ctx);
    FreeScreenBuffer(screen, w3d.buffers[1]);
    FreeScreenBuffer(screen, w3d.buffers[0]);
    CloseScreen(screen);
    return 10;
  }
  glViewport(0, 0, width, height);
  glMatrixMode(GL_PROJECTION);
  glFrustum(-1.0 * width / height, 1.0 * width / height, -1.0, 1.0, 2.0, 10.0);
  glMatrixMode(GL_MODELVIEW);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_LIGHTING);
  glEnable(GL_LIGHT0);
  glLightfv(GL_LIGHT0, GL_POSITION, light);
  glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, red);
  glClearColor(0, 0, 0.3f, 1);
  while (mouse_up()) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();
    glTranslatef(0, 0, -5);
    glRotatef((GLfloat)(frame * 3), 1, 0, 0);
    glRotatef((GLfloat)(frame * 2), 0, 1, 0);
    cube();
    sgl_present(ctx);
    frame++;
  }
  sgl_make_current(NULL);
  sgl_destroy_context(ctx);
  free(color);
  FreeScreenBuffer(screen, w3d.buffers[1]);
  FreeScreenBuffer(screen, w3d.buffers[0]);
  CloseScreen(screen);
  CloseLibrary((struct Library *)GfxBase);
  CloseLibrary((struct Library *)IntuitionBase);
  printf("%d frames\n", frame);
  return 0;
}
