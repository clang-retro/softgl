/* gl12.c - the tests of the OpenGL 1.2 features of softgl: packed
 * pixels, the separate specular color, the wrap modes, the level of
 * detail parameters and glDrawRangeElements.
 *
 *     gl12 [prefix]
 *
 * With a prefix, each scene is also written as prefix-name.ppm.
 *
 * Part of clang-retro softgl, a software implementation of OpenGL.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include <GL/glext.h>

#include "harness.h"

/* Draws a quad over the buffer with s from s0 at the left to s1 at the
 * right, and t = 0.5. */
static void quad_s(GLfloat s0, GLfloat s1) {
  glBegin(GL_QUADS);
  glTexCoord2f(s0, 0.5f);
  glVertex2f(-1, -1);
  glTexCoord2f(s1, 0.5f);
  glVertex2f(1, -1);
  glTexCoord2f(s1, 0.5f);
  glVertex2f(1, 1);
  glTexCoord2f(s0, 0.5f);
  glVertex2f(-1, 1);
  glEnd();
}

/* Makes a 2 x 1 texture: blue at the left, green at the right, with
 * the nearest filter. */
static GLuint pair_texture(void) {
  static const GLubyte pair[8] = {0, 0, 255, 255, 0, 255, 0, 255};
  GLuint t;
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pair);
  glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
  return t;
}

/* The packed pixel types in glDrawPixels and glReadPixels. */
static void test_packed(void) {
  const GLushort red565 = 0xF800;
  const GLuint green_bgra = 0xFF00FF00u; /* GL_BGRA, 8_8_8_8_REV: B in the low byte */
  const GLubyte blue332 = 0x03;
  GLushort back = 0;
  GLuint back32 = 0;
  reset();
  glRasterPos2f(-1 + 2.0f * 10 / W, -1 + 2.0f * 10 / H);
  glDrawPixels(1, 1, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, &red565);
  glRasterPos2f(-1 + 2.0f * 20 / W, -1 + 2.0f * 10 / H);
  glDrawPixels(1, 1, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, &green_bgra);
  glRasterPos2f(-1 + 2.0f * 30 / W, -1 + 2.0f * 10 / H);
  glDrawPixels(1, 1, GL_RGB, GL_UNSIGNED_BYTE_3_3_2, &blue332);
  expect("5_6_5 red", 10, 10, 0xFF0000, 0);
  expect("8_8_8_8_REV green", 20, 10, 0x00FF00, 0);
  expect("3_3_2 blue", 30, 10, 0x0000FF, 0);
  glReadPixels(10, 10, 1, 1, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, &back);
  check("read 5_6_5", back == 0xF800);
  glReadPixels(20, 10, 1, 1, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, &back32);
  check("read 8_8_8_8_REV", back32 == 0xFF00FF00u);
  while (glGetError() != GL_NO_ERROR) {
  }
  glDrawPixels(1, 1, GL_RGBA, GL_UNSIGNED_SHORT_5_6_5, &red565);
  check("5_6_5 with GL_RGBA is GL_INVALID_OPERATION", glGetError() == GL_INVALID_OPERATION);
}

/* The separate specular color is added after the texture. */
static void test_separate_specular(void) {
  static const GLfloat zero[4] = {0, 0, 0, 1}, one[4] = {1, 1, 1, 1}, dir[4] = {0, 0, 1, 0};
  static const GLubyte black[4] = {0, 0, 0, 255};
  GLuint t;
  GLint v = 0;
  int pass;
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, black);
  for (pass = 0; pass < 2; pass++) {
    reset();
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glLightfv(GL_LIGHT0, GL_POSITION, dir);
    glLightfv(GL_LIGHT0, GL_AMBIENT, zero);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, zero);
    glLightfv(GL_LIGHT0, GL_SPECULAR, one);
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, zero);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, one);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 0);
    glLightModeli(GL_LIGHT_MODEL_COLOR_CONTROL, pass ? GL_SEPARATE_SPECULAR_COLOR : GL_SINGLE_COLOR);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glNormal3f(0, 0, 1);
    quad_s(0, 1);
    if (pass) dump("separate-specular");
    expect(pass ? "separate specular" : "single color", 32, 32, pass ? 0xFFFFFF : 0x000000, 2);
  }
  glGetIntegerv(GL_LIGHT_MODEL_COLOR_CONTROL, &v);
  check("GL_LIGHT_MODEL_COLOR_CONTROL", v == GL_SEPARATE_SPECULAR_COLOR);
  glLightModeli(GL_LIGHT_MODEL_COLOR_CONTROL, GL_SINGLE_COLOR);
  glDisable(GL_LIGHTING);
  glDisable(GL_TEXTURE_2D);
  glDeleteTextures(1, &t);
}

/* GL_CLAMP_TO_BORDER and GL_MIRRORED_REPEAT. */
static void test_wrap(void) {
  static const GLfloat red[4] = {1, 0, 0, 1};
  GLuint t;
  reset();
  t = pair_texture();
  glEnable(GL_TEXTURE_2D);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
  glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, red);
  /* s from -1 to 2: the left and right thirds are the border. */
  quad_s(-1, 2);
  dump("clamp-to-border");
  expect("border, left", 8, 32, 0xFF0000, 0);
  expect("border, middle left", 26, 32, 0x0000FF, 0);
  expect("border, middle right", 38, 32, 0x00FF00, 0);
  expect("border, right", 56, 32, 0xFF0000, 0);
  reset();
  glEnable(GL_TEXTURE_2D);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
  /* s from 0 to 2: blue, green, green (the mirror), blue. */
  quad_s(0, 2);
  dump("mirrored-repeat");
  expect("mirror 1", 4, 32, 0x0000FF, 0);
  expect("mirror 2", 20, 32, 0x00FF00, 0);
  expect("mirror 3", 40, 32, 0x00FF00, 0);
  expect("mirror 4", 60, 32, 0x0000FF, 0);
  glDisable(GL_TEXTURE_2D);
  glDeleteTextures(1, &t);
}

/* GL_TEXTURE_BASE_LEVEL and GL_TEXTURE_MAX_LEVEL. */
static void test_lod(void) {
  static GLubyte level0[4 * 4 * 4], level1[2 * 2 * 4], level2[4];
  GLuint t;
  GLint v = 0;
  int i;
  for (i = 0; i < 16; i++) {
    level0[i * 4] = 255;
    level0[i * 4 + 3] = 255;
  }
  for (i = 0; i < 4; i++) {
    level1[i * 4 + 1] = 255;
    level1[i * 4 + 3] = 255;
  }
  level2[2] = 255;
  level2[3] = 255;
  reset();
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, level0);
  glTexImage2D(GL_TEXTURE_2D, 1, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, level1);
  glTexImage2D(GL_TEXTURE_2D, 2, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, level2);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
  glEnable(GL_TEXTURE_2D);
  /* Magnified: the base level. */
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 1);
  quad_s(0, 1);
  expect("base level 1, magnified", 32, 32, 0x00FF00, 0);
  glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, &v);
  check("GL_TEXTURE_BASE_LEVEL", v == 1);
  /* Minified to one pixel: level 2, but GL_TEXTURE_MAX_LEVEL is 1. */
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  glBegin(GL_QUADS);
  glTexCoord2f(0, 0);
  glVertex2f(-1, -1);
  glTexCoord2f(1, 0);
  glVertex2f(-1 + 2.0f / W, -1);
  glTexCoord2f(1, 1);
  glVertex2f(-1 + 2.0f / W, -1 + 2.0f / H);
  glTexCoord2f(0, 1);
  glVertex2f(-1, -1 + 2.0f / H);
  glEnd();
  expect("max level 1, minified", 0, 0, 0x00FF00, 0);
  glDisable(GL_TEXTURE_2D);
  glDeleteTextures(1, &t);
}

/* glDrawRangeElements and GL_RESCALE_NORMAL. */
static void test_range_elements(void) {
  static const GLfloat xy[8] = {-1, -1, 1, -1, 1, 1, -1, 1};
  static const GLubyte idx[4] = {0, 1, 2, 3};
  GLint max = 0;
  reset();
  glColor3f(1, 1, 0);
  glEnableClientState(GL_VERTEX_ARRAY);
  glVertexPointer(2, GL_FLOAT, 0, xy);
  glDrawRangeElements(GL_QUADS, 0, 3, 4, GL_UNSIGNED_BYTE, idx);
  expect("glDrawRangeElements", 32, 32, 0xFFFF00, 0);
  glDrawRangeElements(GL_QUADS, 3, 0, 4, GL_UNSIGNED_BYTE, idx);
  check("end < start is GL_INVALID_VALUE", glGetError() == GL_INVALID_VALUE);
  glDisableClientState(GL_VERTEX_ARRAY);
  glGetIntegerv(GL_MAX_ELEMENTS_VERTICES, &max);
  check("GL_MAX_ELEMENTS_VERTICES", max > 0);
  glEnable(GL_RESCALE_NORMAL);
  check("GL_RESCALE_NORMAL is on", glIsEnabled(GL_RESCALE_NORMAL));
  glDisable(GL_RESCALE_NORMAL);
}

/* Runs the tests. */
int main(int argc, char **argv) {
  sgl_context *ctx;
  if (argc > 1) prefix = argv[1];
  ctx = make_context(0);
  test_packed();
  test_separate_specular();
  test_wrap();
  test_lod();
  test_range_elements();
  {
    GLenum e = glGetError();
    if (e != GL_NO_ERROR) {
      printf("FAIL: error %04x was recorded\n", e);
      failures++;
    }
  }
  sgl_destroy_context(ctx);
  if (failures) return 1;
  printf("PASS softgl gl12\n");
  return 0;
}
