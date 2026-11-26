/* gl13.c - the tests of the OpenGL 1.2 and 1.3 features of softgl:
 * multitexture, the texture environment GL_ADD, GL_CLAMP_TO_EDGE and
 * the transpose matrices.
 *
 *     gl13 [prefix]
 *
 * With a prefix, each scene is also written as prefix-name.ppm.
 *
 * Part of clang-retro softgl, a software implementation of OpenGL.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include <GL/glext.h>

#include "harness.h"

/* The gray of unit 0. With the texel of unit 1 added, the left half is
 * 0x8080FF and the right half is 0x80FF80. */
#define GRAY 0x808080u

/* The texture objects: a gray 1 x 1 texture and a 2 x 1 texture. */
static GLuint tex_gray, tex_pair;

/* Makes the two textures of the tests. */
static void make_textures(void) {
  static const GLubyte gray[4] = {128, 128, 128, 255};
  static const GLubyte pair[8] = {0, 0, 255, 255, 0, 255, 0, 255};
  glGenTextures(1, &tex_gray);
  glBindTexture(GL_TEXTURE_2D, tex_gray);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, gray);
  glGenTextures(1, &tex_pair);
  glBindTexture(GL_TEXTURE_2D, tex_pair);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 2, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pair);
}

/* Sets unit 0 to the gray texture (modulate) and unit 1 to the pair
 * texture (add). */
static void setup_units(void) {
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, tex_gray);
  glEnable(GL_TEXTURE_2D);
  glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, tex_pair);
  glEnable(GL_TEXTURE_2D);
  glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_ADD);
}

/* Turns the textures of both units off. */
static void units_off(void) {
  glActiveTexture(GL_TEXTURE1);
  glDisable(GL_TEXTURE_2D);
  glMatrixMode(GL_TEXTURE);
  glLoadIdentity();
  glMatrixMode(GL_MODELVIEW);
  glActiveTexture(GL_TEXTURE0);
  glDisable(GL_TEXTURE_2D);
}

/* Draws a white quad over the buffer. Unit 1 has s from 0 at the left
 * to 1 at the right; unit 0 has the coordinates (0.5, 0.5). */
static void quad(void) {
  static const GLfloat xy[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
  static const GLfloat s1[4] = {0, 1, 1, 0};
  int i;
  glColor3f(1, 1, 1);
  glBegin(GL_QUADS);
  for (i = 0; i < 4; i++) {
    glTexCoord2f(0.5f, 0.5f);
    glMultiTexCoord2f(GL_TEXTURE1, s1[i], 0.5f);
    glVertex2fv(xy[i]);
  }
  glEnd();
}

/* Checks the result of the two units: gray plus blue at the left, gray
 * plus green at the right. swap tells that the halves change places. */
static void expect_halves(const char *what, int swap) {
  const uint32_t left = 0x8080FFu, right = 0x80FF80u;
  expect(what, 8, 32, swap ? right : left, 2);
  expect(what, 56, 32, swap ? left : right, 2);
}

/* Two texture units: the second adds its texel to the first. */
static void test_two_units(void) {
  GLint n = 0, active = 0;
  reset();
  setup_units();
  quad();
  dump("multitexture");
  expect_halves("two units", 0);
  glGetIntegerv(GL_MAX_TEXTURE_UNITS, &n);
  check("GL_MAX_TEXTURE_UNITS is 2 or more", n >= 2);
  glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
  check("GL_ACTIVE_TEXTURE", active == GL_TEXTURE1);
  check("unit 1 has GL_TEXTURE_2D on", glIsEnabled(GL_TEXTURE_2D));
  {
    GLint b = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &b);
    check("unit 1 binding", (GLuint)b == tex_pair);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &b);
    check("unit 0 binding", (GLuint)b == tex_gray);
  }
  units_off();
  /* Unit 0 alone: the gray texture. */
  reset();
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, tex_gray);
  glEnable(GL_TEXTURE_2D);
  quad();
  expect("unit 0 alone", 32, 32, GRAY, 2);
  units_off();
}

/* The texture matrix of unit 1 scales its coordinates; that of unit 0
 * does not change. */
static void test_texture_matrix(void) {
  GLint depth = 0;
  reset();
  setup_units();
  glMatrixMode(GL_TEXTURE);
  glLoadIdentity();
  glScalef(2, 1, 1);
  glGetIntegerv(GL_TEXTURE_STACK_DEPTH, &depth);
  check("texture stack depth", depth == 1);
  glMatrixMode(GL_MODELVIEW);
  /* s is now 0 to 2: the left quarter has the left texel, and the
   * other part has the right texel (clamped to the edge after 1). */
  quad();
  dump("texture-matrix");
  expect("unit 1 matrix, left", 8, 32, 0x8080FFu, 2);
  expect("unit 1 matrix, right", 56, 32, 0x80FF80u, 2);
  expect("unit 1 matrix, middle left", 24, 32, 0x80FF80u, 2);
  units_off();
}

/* The texture coordinate arrays of the two units. */
static void test_arrays(void) {
  static const GLfloat xy[8] = {-1, -1, 1, -1, 1, 1, -1, 1};
  static const GLfloat t0[8] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
  static const GLfloat t1[8] = {0, 0.5f, 1, 0.5f, 1, 0.5f, 0, 0.5f};
  GLint client = 0;
  reset();
  setup_units();
  glColor3f(1, 1, 1);
  glEnableClientState(GL_VERTEX_ARRAY);
  glVertexPointer(2, GL_FLOAT, 0, xy);
  glClientActiveTexture(GL_TEXTURE0);
  glEnableClientState(GL_TEXTURE_COORD_ARRAY);
  glTexCoordPointer(2, GL_FLOAT, 0, t0);
  glClientActiveTexture(GL_TEXTURE1);
  glEnableClientState(GL_TEXTURE_COORD_ARRAY);
  glTexCoordPointer(2, GL_FLOAT, 0, t1);
  glGetIntegerv(GL_CLIENT_ACTIVE_TEXTURE, &client);
  check("GL_CLIENT_ACTIVE_TEXTURE", client == GL_TEXTURE1);
  glDrawArrays(GL_QUADS, 0, 4);
  expect_halves("arrays", 0);
  glDisableClientState(GL_TEXTURE_COORD_ARRAY);
  glClientActiveTexture(GL_TEXTURE0);
  check("unit 0 array is on", glIsEnabled(GL_TEXTURE_COORD_ARRAY));
  glDisableClientState(GL_TEXTURE_COORD_ARRAY);
  glDisableClientState(GL_VERTEX_ARRAY);
  units_off();
}

/* glActiveTexture and glMultiTexCoord in a display list. */
static void test_list(void) {
  GLuint list = glGenLists(1);
  reset();
  glNewList(list, GL_COMPILE);
  setup_units();
  quad();
  glEndList();
  units_off();
  glCallList(list);
  expect_halves("display list", 0);
  units_off();
  glDeleteLists(list, 1);
}

/* glPushAttrib(GL_TEXTURE_BIT) keeps the state of each unit. */
static void test_attrib(void) {
  GLint mode = 0, b = 0, active = 0;
  reset();
  setup_units();
  glPushAttrib(GL_TEXTURE_BIT);
  glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
  glBindTexture(GL_TEXTURE_2D, 0);
  glDisable(GL_TEXTURE_2D);
  glActiveTexture(GL_TEXTURE0);
  glPopAttrib();
  glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
  check("pop: active texture", active == GL_TEXTURE1);
  glGetTexEnviv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, &mode);
  check("pop: unit 1 env mode", mode == GL_ADD);
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &b);
  check("pop: unit 1 binding", (GLuint)b == tex_pair);
  check("pop: unit 1 enable", glIsEnabled(GL_TEXTURE_2D));
  quad();
  expect_halves("after pop", 0);
  units_off();
}

/* The transpose matrices and the names that wglGetProcAddress gives. */
static void test_transpose_and_procs(void) {
  static const GLfloat rows[16] = {1, 0, 0, 5, 0, 1, 0, 6, 0, 0, 1, 7, 0, 0, 0, 1};
  GLfloat m[16], t[16];
  GLenum e;
  glMatrixMode(GL_MODELVIEW);
  glLoadTransposeMatrixf(rows);
  glGetFloatv(GL_MODELVIEW_MATRIX, m);
  check("transpose load: translation", m[12] == 5 && m[13] == 6 && m[14] == 7);
  glGetFloatv(GL_TRANSPOSE_MODELVIEW_MATRIX, t);
  check("transpose query", t[3] == 5 && t[7] == 6 && t[11] == 7);
  glMultTransposeMatrixf(rows);
  glGetFloatv(GL_MODELVIEW_MATRIX, m);
  check("transpose mult", m[12] == 10 && m[13] == 12 && m[14] == 14);
  glLoadIdentity();
  check("proc glActiveTextureARB", sgl_get_proc("glActiveTextureARB") == (void *)glActiveTextureARB);
  check("proc glActiveTexture", sgl_get_proc("glActiveTexture") == (void *)glActiveTexture);
  check("proc glMultiTexCoord2fvARB", sgl_get_proc("glMultiTexCoord2fvARB") != NULL);
  check("proc glLoadTransposeMatrixdARB", sgl_get_proc("glLoadTransposeMatrixdARB") != NULL);
  check("extension string",
        strstr((const char *)glGetString(GL_EXTENSIONS), "GL_ARB_multitexture") != NULL);
  while (glGetError() != GL_NO_ERROR) {
  }
  glActiveTexture(GL_TEXTURE0 + 31);
  e = glGetError();
  check("unit 31 is GL_INVALID_ENUM", e == GL_INVALID_ENUM);
  glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_LINE);
  e = glGetError();
  check("bad env mode is GL_INVALID_ENUM", e == GL_INVALID_ENUM);
}

/* Runs the tests. */
int main(int argc, char **argv) {
  sgl_context *ctx;
  if (argc > 1) prefix = argv[1];
  ctx = make_context(0);
  {
    GLint units = 0;
    glGetIntegerv(GL_MAX_TEXTURE_UNITS, &units);
    if (units < 2) {
      /* A build with SGL_MAX_TEXTURE_UNITS=1. */
      test_transpose_and_procs();
      sgl_destroy_context(ctx);
      printf("PASS softgl gl13 (one texture unit)\n");
      return failures ? 1 : 0;
    }
  }
  make_textures();
  test_two_units();
  test_texture_matrix();
  test_arrays();
  test_list();
  test_attrib();
  {
    GLenum e = glGetError();
    if (e != GL_NO_ERROR) {
      printf("FAIL: error %04x was recorded\n", e);
      failures++;
    }
  }
  test_transpose_and_procs();
  sgl_destroy_context(ctx);
  if (failures) return 1;
  printf("PASS softgl gl13\n");
  return 0;
}
