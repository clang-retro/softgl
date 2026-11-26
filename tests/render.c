/* render.c - tests of the software OpenGL without a window system.
 *
 * The test draws scenes into a buffer in memory and examines pixels:
 * a clear, a flat and a smooth triangle, the depth test, a lit sphere
 * (GLU quadrics), a texture, a display list, vertex arrays, an
 * evaluator mesh, blending and the scissor box. With an argument, it
 * writes each scene as a PPM file with that prefix.
 *
 * Part of clang-retro softgl, a software implementation of OpenGL.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include "harness.h"

/* A clear and a flat triangle. */
static void test_flat(void) {
  reset();
  glClearColor(0, 0, 1, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  glColor3f(1, 0, 0);
  glBegin(GL_TRIANGLES);
  glVertex2f(-1, -1);
  glVertex2f(1, -1);
  glVertex2f(0, 1);
  glEnd();
  dump("flat");
  expect("flat inside", 32, 10, 0xFF0000, 0);
  expect("flat outside", 2, 60, 0x0000FF, 0);
  expect("flat edge", 32, 62, 0xFF0000, 0);
}

/* A smooth triangle: the colors of the corners blend. */
static void test_smooth(void) {
  reset();
  glBegin(GL_TRIANGLES);
  glColor3f(1, 0, 0);
  glVertex2f(-1, -1);
  glColor3f(0, 1, 0);
  glVertex2f(1, -1);
  glColor3f(0, 0, 1);
  glVertex2f(-1, 1);
  glEnd();
  dump("smooth");
  expect("smooth red corner", 0, 0, 0xFC0201, 6);
  expect("smooth middle", 16, 16, 0x7F3F3F, 8);
}

/* Two quads that cross in depth. */
static void test_depth(void) {
  reset();
  glEnable(GL_DEPTH_TEST);
  glColor3f(1, 0, 0);
  glBegin(GL_QUADS);
  glVertex3f(-1, -1, -0.5f);
  glVertex3f(1, -1, 0.5f);
  glVertex3f(1, 1, 0.5f);
  glVertex3f(-1, 1, -0.5f);
  glEnd();
  glColor3f(0, 1, 0);
  glBegin(GL_QUADS);
  glVertex3f(-1, -1, 0.5f);
  glVertex3f(1, -1, -0.5f);
  glVertex3f(1, 1, -0.5f);
  glVertex3f(-1, 1, 0.5f);
  glEnd();
  dump("depth");
  expect("depth left", 8, 32, 0xFF0000, 0);
  expect("depth right", 56, 32, 0x00FF00, 0);
}

/* A lit sphere in perspective, from a display list. */
static void test_light(void) {
  GLfloat pos[4] = {1, 1, 1, 0}, white[4] = {1, 1, 1, 1}, red[4] = {0.8f, 0.1f, 0.1f, 1};
  GLUquadric *q = gluNewQuadric();
  GLuint list;
  reset();
  glMatrixMode(GL_PROJECTION);
  gluPerspective(45, 1, 1, 10);
  glMatrixMode(GL_MODELVIEW);
  gluLookAt(0, 0, 4, 0, 0, 0, 0, 1, 0);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_LIGHTING);
  glEnable(GL_LIGHT0);
  glLightfv(GL_LIGHT0, GL_POSITION, pos);
  glMaterialfv(GL_FRONT, GL_DIFFUSE, red);
  glMaterialfv(GL_FRONT, GL_SPECULAR, white);
  glMaterialf(GL_FRONT, GL_SHININESS, 30);
  list = glGenLists(1);
  glNewList(list, GL_COMPILE);
  gluSphere(q, 1.2, 24, 16);
  glEndList();
  glCallList(list);
  gluDeleteQuadric(q);
  dump("light");
  expect("light background", 2, 2, 0x000000, 0);
  if ((at(32, 32) >> 16) < 0x80) {
    printf("FAIL light: the sphere is not lit (%06x)\n", (unsigned)at(32, 32));
    failures++;
  }
  if (at(41, 41) <= at(24, 24)) {
    printf("FAIL light: the light side is not brighter\n");
    failures++;
  }
}

/* A checker texture on a quad in perspective. */
static void test_texture(void) {
  GLubyte tex[8 * 8 * 3];
  GLuint name;
  int i;
  for (i = 0; i < 64; i++) {
    int on = ((i % 8) + (i / 8)) & 1;
    tex[i * 3] = on ? 255 : 0;
    tex[i * 3 + 1] = on ? 255 : 0;
    tex[i * 3 + 2] = on ? 0 : 255;
  }
  reset();
  glGenTextures(1, &name);
  glBindTexture(GL_TEXTURE_2D, name);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, 3, 8, 8, 0, GL_RGB, GL_UNSIGNED_BYTE, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
  glEnable(GL_TEXTURE_2D);
  glBegin(GL_QUADS);
  glTexCoord2f(0, 0);
  glVertex2f(-1, -1);
  glTexCoord2f(1, 0);
  glVertex2f(1, -1);
  glTexCoord2f(1, 1);
  glVertex2f(1, 1);
  glTexCoord2f(0, 1);
  glVertex2f(-1, 1);
  glEnd();
  dump("texture");
  expect("texture texel 0,0", 4, 4, 0x0000FF, 0);
  expect("texture texel 1,0", 12, 4, 0xFFFF00, 0);
  expect("texture texel 7,7", 60, 60, 0x0000FF, 0);
  glMatrixMode(GL_PROJECTION);
  glFrustum(-1, 1, -1, 1, 1, 10);
  glMatrixMode(GL_MODELVIEW);
  glTranslatef(0, 0, -2);
  glRotatef(-60, 1, 0, 0);
  glClear(GL_COLOR_BUFFER_BIT);
  glBegin(GL_QUADS);
  glTexCoord2f(0, 0);
  glVertex2f(-1, -1);
  glTexCoord2f(1, 0);
  glVertex2f(1, -1);
  glTexCoord2f(1, 1);
  glVertex2f(1, 1);
  glTexCoord2f(0, 1);
  glVertex2f(-1, 1);
  glEnd();
  dump("texture-persp");
  glDeleteTextures(1, &name);
}

/* Vertex arrays and glDrawElements. */
static void test_arrays(void) {
  static const GLfloat v[] = {-1, -1, 1, -1, 1, 1, -1, 1};
  static const GLubyte col[] = {255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};
  static const GLubyte idx[] = {0, 1, 2, 0, 2, 3};
  reset();
  glEnableClientState(GL_VERTEX_ARRAY);
  glEnableClientState(GL_COLOR_ARRAY);
  glVertexPointer(2, GL_FLOAT, 0, v);
  glColorPointer(3, GL_UNSIGNED_BYTE, 0, col);
  glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, idx);
  glDisableClientState(GL_VERTEX_ARRAY);
  glDisableClientState(GL_COLOR_ARRAY);
  dump("arrays");
  expect("arrays", 10, 50, 0xFFFFFF, 0);
}

/* An evaluator patch, blending and the scissor box. */
static void test_eval_blend(void) {
  GLfloat pts[2][2][3] = {{{-1, -1, 0}, {-1, 1, 0}}, {{1, -1, 0}, {1, 1, 0}}};
  reset();
  glMap2f(GL_MAP2_VERTEX_3, 0, 1, 3, 2, 0, 1, 6, 2, &pts[0][0][0]);
  glEnable(GL_MAP2_VERTEX_3);
  glMapGrid2f(4, 0, 1, 4, 0, 1);
  glColor3f(0, 1, 0);
  glEvalMesh2(GL_FILL, 0, 4, 0, 4);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_SCISSOR_TEST);
  glScissor(0, 0, 32, 64);
  glColor4f(1, 0, 0, 0.5f);
  glRectf(-1, -1, 1, 1);
  dump("eval-blend");
  expect("eval", 48, 32, 0x00FF00, 0);
  expect("blend", 16, 32, 0x807F00, 2);
}

/* Runs the tests. */
int main(int argc, char **argv) {
  sgl_context *ctx = sgl_create_context(NULL);
  if (argc > 1) prefix = argv[1];
  if (!ctx || !sgl_set_buffer(ctx, W, H, pixels, W) || !sgl_make_current(ctx)) {
    printf("FAIL: no context\n");
    return 1;
  }
  if (!strstr((const char *)glGetString(GL_VERSION), "1.1")) {
    printf("FAIL: the version is not 1.1\n");
    failures++;
  }
  if (!sgl_get_proc("glAddSwapHintRectWIN") || !sgl_get_proc("glColorTableEXT")) {
    printf("FAIL: the extensions are missing\n");
    failures++;
  }
  test_flat();
  test_smooth();
  test_depth();
  test_light();
  test_texture();
  test_arrays();
  test_eval_blend();
  if (glGetError() != GL_NO_ERROR) {
    printf("FAIL: an error was recorded\n");
    failures++;
  }
  sgl_destroy_context(ctx);
  if (failures) return 1;
  printf("PASS softgl render\n");
  return 0;
}
