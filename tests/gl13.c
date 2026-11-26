/* gl13.c - the tests of the OpenGL 1.2 and 1.3 features of softgl:
 * multitexture, the texture environment GL_ADD, GL_CLAMP_TO_EDGE, the
 * transpose matrices, the cube maps, GL_COMBINE, the S3TC compressed
 * textures and the multisample state.
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

/* Draws a quad over the buffer with the texture coordinates s, t, r at
 * each vertex. */
static void quad_dir(GLfloat s, GLfloat t, GLfloat r) {
  glBegin(GL_QUADS);
  glTexCoord3f(s, t, r);
  glVertex2f(-1, -1);
  glVertex2f(1, -1);
  glVertex2f(1, 1);
  glVertex2f(-1, 1);
  glEnd();
}

/* The cube maps: the face of each direction, the texel in a face, a
 * cube map that has no six faces, GL_NORMAL_MAP and GL_REFLECTION_MAP. */
static void test_cube(void) {
  static const GLuint colors[6] = {0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00, 0xFF00FF, 0x00FFFF};
  static const GLfloat dirs[6][3] = {{1, 0.2f, 0.1f}, {-1, 0.1f, 0.2f}, {0.1f, 1, 0.2f},
                                     {0.2f, -1, 0.1f}, {0.1f, 0.2f, 1}, {0.2f, 0.1f, -1}};
  /* A 2 x 2 face +Z: the texels at the bottom left and the top right. */
  static const GLubyte quad4[16] = {255, 255, 255, 255, 0, 0, 0, 255, 0, 0, 0, 255, 128, 128, 128, 255};
  static const char *names[6] = {"cube +X", "cube -X", "cube +Y", "cube -Y", "cube +Z", "cube -Z"};
  GLuint t;
  GLint v;
  int f;
  reset();
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_CUBE_MAP, t);
  glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP, &v);
  check("GL_TEXTURE_BINDING_CUBE_MAP", (GLuint)v == t);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
  glEnable(GL_TEXTURE_CUBE_MAP);
  /* Five faces: the cube map is not complete, and the color is white. */
  for (f = 0; f < 5; f++) {
    GLubyte px[4] = {(GLubyte)(colors[f] >> 16), (GLubyte)(colors[f] >> 8), (GLubyte)colors[f], 255};
    glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0, GL_RGB, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
  }
  glColor3f(1, 1, 1);
  quad_dir(1, 0, 0);
  expect("cube with five faces", 32, 32, 0xFFFFFF, 0);
  {
    GLubyte px[4] = {0, 255, 255, 255};
    glTexImage2D(GL_TEXTURE_CUBE_MAP_NEGATIVE_Z, 0, GL_RGB, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
  }
  for (f = 0; f < 6; f++) {
    quad_dir(dirs[f][0], dirs[f][1], dirs[f][2]);
    expect(names[f], 32, 32, colors[f], 0);
  }
  dump("cube");
  /* The texel in a face: (-0.5, -0.5, 1) is the bottom left of +Z. */
  glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_Z, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, quad4);
  glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, quad4);
  glTexImage2D(GL_TEXTURE_CUBE_MAP_NEGATIVE_X, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, quad4);
  glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_Y, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, quad4);
  glTexImage2D(GL_TEXTURE_CUBE_MAP_NEGATIVE_Y, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, quad4);
  glTexImage2D(GL_TEXTURE_CUBE_MAP_NEGATIVE_Z, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, quad4);
  /* tc = -t: t = 0.5 is the bottom row of +Z. */
  quad_dir(-0.5f, 0.5f, 1);
  expect("cube texel bottom left", 32, 32, 0xFFFFFF, 0);
  quad_dir(0.5f, -0.5f, 1);
  expect("cube texel top right", 32, 32, 0x808080, 0);
  glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_Z, 0, GL_RGBA, 2, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, quad4);
  check("a face that is not square is GL_INVALID_VALUE", glGetError() == GL_INVALID_VALUE);
  glGetIntegerv(GL_MAX_CUBE_MAP_TEXTURE_SIZE, &v);
  check("GL_MAX_CUBE_MAP_TEXTURE_SIZE", v >= 16);
  glGetTexLevelParameteriv(GL_TEXTURE_CUBE_MAP_NEGATIVE_Y, 0, GL_TEXTURE_WIDTH, &v);
  check("the width of a face", v == 2);
  /* One color for each face again, and the generated coordinates. */
  for (f = 0; f < 6; f++) {
    GLubyte px[4] = {(GLubyte)(colors[f] >> 16), (GLubyte)(colors[f] >> 8), (GLubyte)colors[f], 255};
    glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0, GL_RGB, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
  }
  glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_NORMAL_MAP);
  glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_NORMAL_MAP);
  glTexGeni(GL_R, GL_TEXTURE_GEN_MODE, GL_NORMAL_MAP);
  glEnable(GL_TEXTURE_GEN_S);
  glEnable(GL_TEXTURE_GEN_T);
  glEnable(GL_TEXTURE_GEN_R);
  glNormal3f(0, 1, 0);
  quad_dir(0, 0, 0);
  expect("GL_NORMAL_MAP", 32, 32, 0x0000FF, 0);
  /* The eye looks at -Z: a normal of +Z reflects the view back to +Z. */
  glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_REFLECTION_MAP);
  glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_REFLECTION_MAP);
  glTexGeni(GL_R, GL_TEXTURE_GEN_MODE, GL_REFLECTION_MAP);
  glNormal3f(0, 0, 1);
  glBegin(GL_QUADS);
  glVertex3f(-0.1f, -0.1f, -0.5f);
  glVertex3f(0.1f, -0.1f, -0.5f);
  glVertex3f(0.1f, 0.1f, -0.5f);
  glVertex3f(-0.1f, 0.1f, -0.5f);
  glEnd();
  expect("GL_REFLECTION_MAP", 32, 32, 0xFF00FF, 0);
  glDisable(GL_TEXTURE_GEN_S);
  glDisable(GL_TEXTURE_GEN_T);
  glDisable(GL_TEXTURE_GEN_R);
  glDisable(GL_TEXTURE_CUBE_MAP);
  glDeleteTextures(1, &t);
  glNormal3f(0, 0, 1);
}

/* Makes a 1 x 1 texture of the color rgba (0xRRGGBBAA) on the active
 * unit, with GL_COMBINE. */
static GLuint texel_texture(GLuint rgba) {
  GLubyte px[4] = {(GLubyte)(rgba >> 24), (GLubyte)(rgba >> 16), (GLubyte)(rgba >> 8), (GLubyte)rgba};
  GLuint t;
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
  glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
  glEnable(GL_TEXTURE_2D);
  return t;
}

/* Sets the function, sources and operands of GL_COMBINE of the active
 * unit for the color. */
static void combine_rgb(GLenum fn, GLenum s0, GLenum o0, GLenum s1, GLenum o1, GLenum s2, GLenum o2) {
  glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, fn);
  glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, s0);
  glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, o0);
  glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB, s1);
  glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, o1);
  glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE2_RGB, s2);
  glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND2_RGB, o2);
}

/* GL_COMBINE: the functions, the sources, the operands, the scale,
 * GL_DOT3_RGB and a source of an other unit (the crossbar). */
static void test_combine(int units) {
  static const GLfloat half[4] = {0.25f, 0.5f, 0.75f, 0.5f};
  GLuint t0, t1 = 0;
  GLint v;
  reset();
  glActiveTexture(GL_TEXTURE0);
  t0 = texel_texture(0x0000FFFFu); /* blue */
  glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, half);
  glColor3f(1, 0, 0);
  combine_rgb(GL_REPLACE, GL_CONSTANT, GL_SRC_COLOR, GL_PREVIOUS, GL_SRC_COLOR, GL_CONSTANT, GL_SRC_ALPHA);
  quad_dir(0.5f, 0.5f, 0);
  expect("GL_COMBINE GL_REPLACE GL_CONSTANT", 32, 32, 0x4080BF, 1);
  combine_rgb(GL_INTERPOLATE, GL_TEXTURE, GL_SRC_COLOR, GL_PRIMARY_COLOR, GL_SRC_COLOR, GL_CONSTANT,
              GL_SRC_ALPHA);
  quad_dir(0.5f, 0.5f, 0);
  expect("GL_INTERPOLATE", 32, 32, 0x800080, 1);
  combine_rgb(GL_ADD_SIGNED, GL_TEXTURE, GL_SRC_COLOR, GL_PRIMARY_COLOR, GL_ONE_MINUS_SRC_COLOR,
              GL_CONSTANT, GL_SRC_ALPHA);
  /* (0, 0, 255) + (0, 255, 255) - 128 */
  quad_dir(0.5f, 0.5f, 0);
  expect("GL_ADD_SIGNED", 32, 32, 0x007FFF, 1);
  combine_rgb(GL_SUBTRACT, GL_PRIMARY_COLOR, GL_SRC_COLOR, GL_TEXTURE, GL_SRC_COLOR, GL_CONSTANT,
              GL_SRC_ALPHA);
  quad_dir(0.5f, 0.5f, 0);
  expect("GL_SUBTRACT", 32, 32, 0xFF0000, 0);
  /* GL_MODULATE of the gray color and white, then two times. */
  glColor3f(0.25f, 0.25f, 0.25f);
  combine_rgb(GL_MODULATE, GL_PRIMARY_COLOR, GL_SRC_COLOR, GL_TEXTURE, GL_SRC_ALPHA, GL_CONSTANT,
              GL_SRC_ALPHA);
  glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, 2.0f);
  quad_dir(0.5f, 0.5f, 0);
  expect("GL_RGB_SCALE 2", 32, 32, 0x808080, 2);
  glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, 3.0f);
  check("GL_RGB_SCALE 3 is GL_INVALID_VALUE", glGetError() == GL_INVALID_VALUE);
  glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, 1.0f);
  glGetTexEnviv(GL_TEXTURE_ENV, GL_SOURCE1_RGB, &v);
  check("GL_SOURCE1_RGB", v == GL_TEXTURE);
  /* GL_DOT3_RGB: the normal +X of the texture and of the color. */
  glDeleteTextures(1, &t0);
  t0 = texel_texture(0xFF8080FFu);
  glColor3ub(255, 128, 128);
  combine_rgb(GL_DOT3_RGB, GL_TEXTURE, GL_SRC_COLOR, GL_PRIMARY_COLOR, GL_SRC_COLOR, GL_CONSTANT,
              GL_SRC_ALPHA);
  quad_dir(0.5f, 0.5f, 0);
  expect("GL_DOT3_RGB of equal normals", 32, 32, 0xFFFFFF, 4);
  glColor3ub(128, 255, 128);
  quad_dir(0.5f, 0.5f, 0);
  expect("GL_DOT3_RGB of normal at right angles", 32, 32, 0x000000, 4);
  glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_DOT3_RGB);
  check("GL_DOT3_RGB for the alpha is GL_INVALID_ENUM", glGetError() == GL_INVALID_ENUM);
  if (units >= 2) {
    /* Unit 1 takes the texel of unit 0 (GL_ARB_texture_env_crossbar). */
    glColor3f(1, 1, 1);
    combine_rgb(GL_REPLACE, GL_TEXTURE, GL_SRC_COLOR, GL_PREVIOUS, GL_SRC_COLOR, GL_CONSTANT, GL_SRC_ALPHA);
    glActiveTexture(GL_TEXTURE1);
    t1 = texel_texture(0x00FF00FFu);
    combine_rgb(GL_MODULATE, GL_TEXTURE0, GL_SRC_COLOR, GL_TEXTURE, GL_SRC_COLOR, GL_CONSTANT,
                GL_SRC_ALPHA);
    quad_dir(0.5f, 0.5f, 0);
    /* (255, 128, 128) * (0, 255, 0) */
    expect("GL_TEXTURE0 as a source of unit 1", 32, 32, 0x008000, 1);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glDisable(GL_TEXTURE_2D);
    glDeleteTextures(1, &t1);
    glActiveTexture(GL_TEXTURE0);
  }
  glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
  glDisable(GL_TEXTURE_2D);
  glDeleteTextures(1, &t0);
  glColor3f(1, 1, 1);
}

/* The S3TC compressed textures: DXT1, DXT3 and DXT5 blocks, a sub
 * image, glGetCompressedTexImage and the queries. */
static void test_compressed(void) {
  /* DXT1: color 0 red, color 1 blue; pixel 0 uses color 1. */
  static const GLubyte dxt1[8] = {0x00, 0xF8, 0x1F, 0x00, 0x01, 0x00, 0x00, 0x00};
  /* DXT1 with c0 <= c1: index 3 is transparent black in RGBA_DXT1. */
  static const GLubyte dxt1a[8] = {0x1F, 0x00, 0x00, 0xF8, 0xFF, 0xFF, 0xFF, 0xFF};
  /* DXT3: alpha 0x0 for pixel 0, 0xF for pixel 1; color all green. */
  static const GLubyte dxt3[16] = {0xF0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                   0xE0, 0x07, 0xE0, 0x07, 0x00, 0x00, 0x00, 0x00};
  /* DXT5: alpha 255 and 0; pixel 0 index 1 (0), the others index 0. */
  static const GLubyte dxt5[16] = {0xFF, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
                                   0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00};
  GLubyte img[16 * 4], blocks[32], big[32];
  GLuint t;
  GLint v, formats[8];
  int i;
  reset();
  glGetIntegerv(GL_NUM_COMPRESSED_TEXTURE_FORMATS, &v);
  check("GL_NUM_COMPRESSED_TEXTURE_FORMATS", v == 4);
  glGetIntegerv(GL_COMPRESSED_TEXTURE_FORMATS, formats);
  check("GL_COMPRESSED_TEXTURE_FORMATS", formats[0] == GL_COMPRESSED_RGB_S3TC_DXT1_EXT);
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
  glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGB_S3TC_DXT1_EXT, 4, 4, 0, 8, dxt1);
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_COMPRESSED, &v);
  check("GL_TEXTURE_COMPRESSED", v == GL_TRUE);
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_COMPRESSED_IMAGE_SIZE, &v);
  check("GL_TEXTURE_COMPRESSED_IMAGE_SIZE", v == 8);
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
  glDisable(GL_TEXTURE_2D);
  dump("dxt1");
  expect("DXT1 pixel 0", 4, 4, 0x0000FF, 0);
  expect("DXT1 other pixel", 40, 40, 0xFF0000, 0);
  glGetCompressedTexImage(GL_TEXTURE_2D, 0, blocks);
  check("glGetCompressedTexImage", memcmp(blocks, dxt1, 8) == 0);
  glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, 4, 4, 0, 8, dxt1a);
  glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
  check("RGBA_DXT1 index 3 is transparent black", img[0] == 0 && img[1] == 0 && img[2] == 0 && img[3] == 0);
  glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, 4, 4, 0, 16, dxt3);
  glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
  check("DXT3 alpha", img[3] == 0 && img[7] == 255 && img[5] == 255 && img[4] == 0);
  glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, 4, 4, 0, 16, dxt5);
  glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
  check("DXT5 alpha", img[3] == 0 && img[7] == 255 && img[0] == 255);
  /* An 8 x 4 DXT1 texture, then its right block from a sub image. */
  for (i = 0; i < 16; i++) big[i] = dxt1[i % 8];
  glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGB_S3TC_DXT1_EXT, 8, 4, 0, 16, big);
  glCompressedTexSubImage2D(GL_TEXTURE_2D, 0, 4, 0, 4, 4, GL_COMPRESSED_RGB_S3TC_DXT1_EXT, 8, dxt1a);
  glGetCompressedTexImage(GL_TEXTURE_2D, 0, blocks);
  check("glCompressedTexSubImage2D blocks", memcmp(blocks, dxt1, 8) == 0 && memcmp(blocks + 8, dxt1a, 8) == 0);
  glCompressedTexSubImage2D(GL_TEXTURE_2D, 0, 2, 0, 4, 4, GL_COMPRESSED_RGB_S3TC_DXT1_EXT, 8, dxt1a);
  check("a sub image off the blocks is GL_INVALID_OPERATION", glGetError() == GL_INVALID_OPERATION);
  glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGB_S3TC_DXT1_EXT, 4, 4, 0, 9, dxt1);
  check("a wrong size is GL_INVALID_VALUE", glGetError() == GL_INVALID_VALUE);
  /* A generic compressed format is kept without compression. */
  glTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_COMPRESSED, &v);
  check("GL_COMPRESSED_RGBA is not compressed", v == GL_FALSE);
  glDeleteTextures(1, &t);
  /* Multisample: the state is there, without a multisample buffer. */
  check("GL_MULTISAMPLE is on", glIsEnabled(GL_MULTISAMPLE));
  glGetIntegerv(GL_SAMPLE_BUFFERS, &v);
  check("GL_SAMPLE_BUFFERS is 0", v == 0);
  glSampleCoverage(0.5f, GL_TRUE);
  {
    GLfloat f;
    GLboolean b;
    glGetFloatv(GL_SAMPLE_COVERAGE_VALUE, &f);
    glGetBooleanv(GL_SAMPLE_COVERAGE_INVERT, &b);
    check("glSampleCoverage", f == 0.5f && b == GL_TRUE);
  }
  glPushAttrib(GL_MULTISAMPLE_BIT);
  glDisable(GL_MULTISAMPLE);
  glSampleCoverage(1.0f, GL_FALSE);
  glPopAttrib();
  check("glPopAttrib of GL_MULTISAMPLE_BIT", glIsEnabled(GL_MULTISAMPLE));
}

/* Runs the tests. */
int main(int argc, char **argv) {
  sgl_context *ctx;
  if (argc > 1) prefix = argv[1];
  ctx = make_context(0);
  test_cube();
  {
    GLint units = 0;
    glGetIntegerv(GL_MAX_TEXTURE_UNITS, &units);
    test_combine(units);
    test_compressed();
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
