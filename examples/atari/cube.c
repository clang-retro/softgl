/* cube.c - a lit, depth-tested cube that turns on an Atari screen, with
 * the Atari back end of softgl. Press a key to stop.
 *
 *     cube.tos [frames]
 *
 * On a Falcon, it uses 320 x 240 (VGA) or 320 x 200 (RGB and TV) in
 * true color. On an ST, STE or TT, it uses the ST low resolution (320 x
 * 200, 16 colors). With a Blitter (STE, Mega STE, Falcon), the Blitter
 * clears the buffers.
 *
 * Part of clang-retro softgl, a software implementation of OpenGL.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include <stdio.h>
#include <stdlib.h>

#include <osbind.h>

#include <GL/gl.h>

#include "softgl_atari.h"

/* The Falcon video modes (VsetMode). */
#define BPS16 4
#define COL40 0
#define VGA 0x10
#define PAL 0x20
#define VERTFLAG 0x100

static long video;         /* the _VDO cookie: 0x00030000 on a Falcon */

/* Reads the _VDO cookie (in supervisor mode). */
static long read_vdo(void) {
  const long *jar = *(long *const *)0x5A0ul;
  for (; jar && jar[0]; jar += 2)
    if (jar[0] == 0x5F56444FL) video = jar[1]; /* "_VDO" */
  return 0;
}

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

/* Gives screen memory of bytes bytes in ST RAM, on a boundary of 256
 * bytes (the ST needs it), or NULL. */
static void *screen_memory(long bytes) {
  /* The C library keeps the memory of the program for malloc, so try
   * Mxalloc first and malloc after. The program loads in ST RAM unless
   * its header allows TT RAM. */
  char *p = (char *)Mxalloc(bytes + 256, MX_STRAM);
  if ((long)p <= 0) p = (char *)malloc((size_t)bytes + 256);
  if (!p || (long)p < 0) return NULL;
  return (void *)(((unsigned long)p + 255) & ~255ul);
}

/* Loads the 16 colors of the ST palette of the back end. */
static void st_palette(void) {
  uint8_t rgb[16][3];
  int i;
  sgl_c2p_palette(4, rgb);
  for (i = 0; i < 16; i++) Setcolor(i, ((rgb[i][0] >> 5) << 8) | ((rgb[i][1] >> 5) << 4) | (rgb[i][2] >> 5));
}

/* Opens the screen, runs the frames and restores the screen. */
int main(int argc, char **argv) {
  static const GLfloat light[4] = {1, 1, 2, 0}, red[4] = {0.9f, 0.2f, 0.1f, 1};
  sgl_atari at = {0};
  sgl_backend be;
  sgl_context *ctx;
  uint32_t *color;
  void *old_phys = Physbase(), *old_log = Logbase();
  short old_mode = 0, old_rez = Getrez(), old_colors[16];
  int frame = 0, frames = argc > 1 ? atoi(argv[1]) : 0, i, falcon;
  long size;
  Supexec(read_vdo);
  falcon = (video >> 16) == 3;
  for (i = 0; i < 16; i++) old_colors[i] = Setcolor(i, -1);
  if (falcon) {
    short mode;
    old_mode = VsetMode(-1);
    /* VGA: 320 x 240 with line doubling. RGB and TV: 320 x 200. */
    mode = VgetMonitor() == 2 ? BPS16 | COL40 | VGA | VERTFLAG : BPS16 | COL40 | (old_mode & PAL);
    at.depth = 16;
    at.width = 320;
    at.height = VgetMonitor() == 2 ? 240 : 200;
    size = VgetSize(mode);
    at.screens[0] = screen_memory(size);
    at.screens[1] = screen_memory(size);
    if (!at.screens[0] || !at.screens[1]) return 1;
    Setscreen((long)at.screens[0], (long)at.screens[0], -1);
    VsetMode(mode);
  } else {
    at.depth = 4;
    at.width = 320;
    at.height = 200;
    at.screens[0] = screen_memory(32000);
    at.screens[1] = screen_memory(32000);
    if (!at.screens[0] || !at.screens[1]) return 1;
    Setscreen((long)at.screens[0], (long)at.screens[0], 0);
    st_palette();
  }
  at.flags = SGL_ATARI_BLITTER_CLEAR;
  /* The Blitter gets to ST RAM only. */
  color = (uint32_t *)screen_memory((long)at.width * at.height * 4);
  ctx = sgl_create_context(NULL);
  if (!color || !ctx || !sgl_atari_init(&at, &be) || !sgl_set_backend(ctx, &be, &at) ||
      !sgl_set_buffer(ctx, at.width, at.height, color, at.width) || !sgl_make_current(ctx))
    return 1;
  glViewport(0, 0, at.width, at.height);
  glMatrixMode(GL_PROJECTION);
  glFrustum(-1.0 * at.width / at.height, 1.0 * at.width / at.height, -1.0, 1.0, 2.0, 10.0);
  glMatrixMode(GL_MODELVIEW);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_LIGHTING);
  glEnable(GL_LIGHT0);
  glLightfv(GL_LIGHT0, GL_POSITION, light);
  glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, red);
  glClearColor(0, 0, 0.3f, 1);
  while (!Cconis() && (!frames || frame < frames)) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();
    glTranslatef(0, 0, -5);
    glRotatef((GLfloat)(frame * 3), 1, 0, 0);
    glRotatef((GLfloat)(frame * 2), 0, 1, 0);
    cube();
    sgl_present(ctx);
    frame++;
  }
  if (Cconis()) Cnecin();
  sgl_make_current(NULL);
  sgl_destroy_context(ctx);
  sgl_atari_free(&at);
  if (falcon) VsetMode(old_mode);
  Setscreen((long)old_log, (long)old_phys, falcon ? -1 : old_rez);
  for (i = 0; i < 16; i++) Setcolor(i, old_colors[i]);
  printf("%d frames\n", frame);
  return 0;
}
