/* glidecube.c - a lit, textured, fogged cube that turns on a Voodoo
 * card, with the Glide back end of softgl: the card draws the
 * triangles. Press Esc to stop (or it stops after 30 seconds, or after
 * the number of frames that is given).
 *
 *     glidecube [frames]
 *
 * On Windows 95 and 98 it needs a Voodoo card and its glide2x.dll. On
 * 32-bit DOS (clang-retro i386-pc-msdos) it needs a Voodoo Graphics, an
 * S3 ViRGE, a Matrox or an ATI Rage Pro card, and uses the built-in
 * drivers of softgl (sgl_glide_dos). There, a second argument can name
 * the driver:
 *
 *     glidecube [frames [voodoo | virge | matrox | ati]]
 *
 * Part of clang-retro softgl, a software implementation of OpenGL.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include <stdio.h>
#include <stdlib.h>

#ifdef __MSDOS__
#include <conio.h>

#include "softgl_dos.h"
#else
#include <windows.h>

/* softgl uses the C calling convention for its GL functions; windows.h
 * sets APIENTRY to __stdcall. */
#undef APIENTRY
#define APIENTRY
#endif
#include <GL/gl.h>

#include "softgl_glide.h"

#ifdef __MSDOS__
/* Gives the time in milliseconds. */
static uint32_t ms(void) { return sgl_dos_ms(); }

/* Gives 1 when the user pushed Esc. */
static int escape(void) { return kbhit() && getch() == 27; }

/* The driver that the command line names, or NULL for the first card. */
static const char *driver_name;

/* Fills api with the built-in driver of the card. */
static int load(sgl_glide_api *api) { return sgl_glide_dos(api, driver_name) != NULL; }

/* The built-in driver has no DLL to free. */
static void unload(sgl_glide_api *api) { (void)api; }
#else
/* Gives the time in milliseconds. */
static uint32_t ms(void) { return GetTickCount(); }

/* Gives 1 when the user pushes Esc. */
static int escape(void) { return (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0; }

/* Fills api from glide2x.dll. */
static int load(sgl_glide_api *api) { return sgl_glide_load(api); }

/* Frees glide2x.dll. */
static void unload(sgl_glide_api *api) { sgl_glide_unload(api); }
#endif

#define WIDTH 640
#define HEIGHT 480

/* Draws the cube: six faces with normals and texture coordinates. */
static void cube(void) {
  static const GLfloat n[6][3] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
  static const GLfloat f[6][4][3] = {
      {{-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}},     {{1, -1, -1}, {-1, -1, -1}, {-1, 1, -1}, {1, 1, -1}},
      {{1, -1, 1}, {1, -1, -1}, {1, 1, -1}, {1, 1, 1}},     {{-1, -1, -1}, {-1, -1, 1}, {-1, 1, 1}, {-1, 1, -1}},
      {{-1, 1, 1}, {1, 1, 1}, {1, 1, -1}, {-1, 1, -1}},     {{-1, -1, -1}, {1, -1, -1}, {1, -1, 1}, {-1, -1, 1}},
  };
  static const GLfloat st[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
  int i, k;
  glBegin(GL_QUADS);
  for (i = 0; i < 6; i++) {
    glNormal3fv(n[i]);
    for (k = 0; k < 4; k++) {
      glTexCoord2fv(st[k]);
      glVertex3fv(f[i][k]);
    }
  }
  glEnd();
}

/* Makes a checkerboard texture of 64 x 64 texels with mipmaps. */
static void texture(void) {
  static uint8_t t[64 * 64 * 3];
  int x, y, level, size;
  for (level = 0, size = 64; size >= 1; level++, size /= 2) {
    for (y = 0; y < size; y++)
      for (x = 0; x < size; x++) {
        int on = size >= 8 ? ((x * 8 / size) ^ (y * 8 / size)) & 1 : 0;
        uint8_t *p = t + (y * size + x) * 3;
        p[0] = on ? 255 : 128, p[1] = on ? 255 : 128, p[2] = on ? 255 : 128;
      }
    glTexImage2D(GL_TEXTURE_2D, level, GL_RGB, size, size, 0, GL_RGB, GL_UNSIGNED_BYTE, t);
  }
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

/* Opens Glide, runs the frames and closes all. */
int main(int argc, char **argv) {
  static const GLfloat light[4] = {1, 1, 2, 0}, white[4] = {1, 1, 1, 1}, fog[4] = {0, 0, 0.3f, 1};
  static uint32_t color[WIDTH * HEIGHT];
  sgl_glide_api api;
  sgl_glide g = {0};
  sgl_context *ctx;
  uint32_t start;
  int frame = 0, frames = argc > 1 ? atoi(argv[1]) : 0;
#ifdef __MSDOS__
  driver_name = argc > 2 ? argv[2] : NULL;
#endif
  if (!load(&api)) {
    fprintf(stderr, "no Glide driver (glide2x.dll on Windows) or no 3D card\n");
    return 1;
  }
  ctx = sgl_create_context(NULL);
  if (!ctx || !sgl_set_buffer(ctx, WIDTH, HEIGHT, color, WIDTH) || !sgl_make_current(ctx)) {
    fprintf(stderr, "not sufficient memory\n");
    return 1;
  }
  g.api = &api;
  g.width = WIDTH;
  g.height = HEIGHT;
  if (!sgl_glide_attach(&g, ctx)) {
    fprintf(stderr, "no Voodoo card, or Glide does not open %d x %d\n", WIDTH, HEIGHT);
    sgl_destroy_context(ctx);
    unload(&api);
    return 1;
  }
  glViewport(0, 0, WIDTH, HEIGHT);
  glMatrixMode(GL_PROJECTION);
  glFrustum(-1.0 * WIDTH / HEIGHT, 1.0 * WIDTH / HEIGHT, -1.0, 1.0, 2.0, 20.0);
  glMatrixMode(GL_MODELVIEW);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_LIGHTING);
  glEnable(GL_LIGHT0);
  glLightfv(GL_LIGHT0, GL_POSITION, light);
  glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, white);
  glEnable(GL_TEXTURE_2D);
  texture();
  glEnable(GL_FOG);
  glFogi(GL_FOG_MODE, GL_LINEAR);
  glFogf(GL_FOG_START, 4.0f);
  glFogf(GL_FOG_END, 12.0f);
  glFogfv(GL_FOG_COLOR, fog);
  glClearColor(fog[0], fog[1], fog[2], 1);
  start = ms();
  while (!escape() && ms() - start < 30000 &&
         (!frames || frame < frames)) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();
    glTranslatef(0, 0, -5.0f - 3.0f * (float)(frame % 200) / 200.0f);
    glRotatef((GLfloat)(frame * 3), 1, 0, 0);
    glRotatef((GLfloat)(frame * 2), 0, 1, 0);
    cube();
    sgl_present(ctx);
    frame++;
  }
  sgl_make_current(NULL);
  sgl_destroy_context(ctx);
  unload(&api);
  printf("%d frames\n", frame);
  return 0;
}
