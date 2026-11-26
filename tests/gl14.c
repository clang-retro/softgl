/* gl14.c - the tests of the OpenGL 1.4 features of softgl: the blend
 * functions and equations, the stencil wrap, the LOD bias, the
 * generation of mipmaps, glWindowPos, glMultiDrawArrays, the point
 * parameters, the secondary color, the fog coordinate, the depth
 * textures and the shadow comparison.
 *
 *     gl14 [prefix]
 *
 * With a prefix, each scene is also written as prefix-name.ppm.
 *
 * Part of clang-retro softgl, a software implementation of OpenGL.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include <GL/glext.h>

#include "harness.h"

/* Draws a quad of color rgb over the buffer, with the texture
 * coordinates 0 to 1 and the r coordinate r. */
static void full_quad(GLfloat r, GLfloat g, GLfloat b, GLfloat a, GLfloat tr) {
  glColor4f(r, g, b, a);
  glBegin(GL_QUADS);
  glTexCoord3f(0, 0, tr);
  glVertex2f(-1, -1);
  glTexCoord3f(1, 0, tr);
  glVertex2f(1, -1);
  glTexCoord3f(1, 1, tr);
  glVertex2f(1, 1);
  glTexCoord3f(0, 1, tr);
  glVertex2f(-1, 1);
  glEnd();
}

/* Tests glBlendFuncSeparate, glBlendColor and glBlendEquation. */
static void test_blend(void) {
  reset();
  glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glEnable(GL_BLEND);
  /* The constant color as the source factor: 0.5 of red. */
  glBlendColor(0.5f, 0.5f, 0.5f, 0.5f);
  glBlendFuncSeparate(GL_CONSTANT_COLOR, GL_ZERO, GL_ONE, GL_ZERO);
  full_quad(1, 0, 0, 1, 0);
  expect("blend constant color", 32, 32, 0x800000, 2);
  /* The subtraction: destination - source. */
  glClear(GL_COLOR_BUFFER_BIT);
  glBlendFunc(GL_ONE, GL_ONE);
  glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
  full_quad(0.25f, 0, 0.75f, 1, 0);
  expect("blend reverse subtract", 32, 32, 0x408000, 2);
  /* The subtraction: source - destination, clamped at 0. */
  glClear(GL_COLOR_BUFFER_BIT);
  glBlendEquation(GL_FUNC_SUBTRACT);
  full_quad(1, 0.25f, 0.75f, 1, 0);
  expect("blend subtract", 32, 32, 0x800040, 2);
  /* The minimum and the maximum: the factors are not used. */
  glClear(GL_COLOR_BUFFER_BIT);
  glBlendFunc(GL_ZERO, GL_ZERO);
  glBlendEquation(GL_MIN);
  full_quad(1, 0.25f, 0.5f, 1, 0);
  expect("blend min", 32, 32, 0x804080, 2);
  glClear(GL_COLOR_BUFFER_BIT);
  glBlendEquation(GL_MAX);
  full_quad(1, 0.25f, 0.5f, 1, 0);
  expect("blend max", 32, 32, 0xFF8080, 2);
  dump("blend");
  /* The state. */
  {
    GLint v[4];
    GLfloat col[4];
    glBlendFuncSeparate(GL_SRC_COLOR, GL_DST_COLOR, GL_ONE, GL_SRC_ALPHA);
    glGetIntegerv(GL_BLEND_SRC_RGB, &v[0]);
    glGetIntegerv(GL_BLEND_DST_RGB, &v[1]);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &v[2]);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &v[3]);
    check("blend factors query", v[0] == GL_SRC_COLOR && v[1] == GL_DST_COLOR && v[2] == GL_ONE &&
                                     v[3] == GL_SRC_ALPHA);
    glGetIntegerv(GL_BLEND_EQUATION, &v[0]);
    check("blend equation query", v[0] == GL_MAX);
    glPushAttrib(GL_COLOR_BUFFER_BIT);
    glBlendColor(1, 1, 1, 1);
    glBlendEquation(GL_FUNC_ADD);
    glPopAttrib();
    glGetFloatv(GL_BLEND_COLOR, col);
    glGetIntegerv(GL_BLEND_EQUATION, &v[0]);
    check("blend state restored", col[0] == 0.5f && v[0] == GL_MAX);
    glBlendFuncSeparate(GL_ONE, GL_SRC_ALPHA_SATURATE, GL_ONE, GL_ZERO);
    check("saturate is not a destination factor", glGetError() == GL_INVALID_ENUM);
    glBlendEquation(GL_ONE);
    check("bad blend equation", glGetError() == GL_INVALID_ENUM);
  }
  glBlendEquation(GL_FUNC_ADD);
  glBlendFunc(GL_ONE, GL_ZERO);
  glDisable(GL_BLEND);
}

/* Tests GL_INCR_WRAP and GL_DECR_WRAP. */
static void test_stencil_wrap(void) {
  reset();
  glClearStencil(255);
  glClear(GL_STENCIL_BUFFER_BIT);
  glEnable(GL_STENCIL_TEST);
  /* 255 + 1 wraps to 0 on the left half; 255 - 1 is 254 on the
   * right half. */
  glStencilFunc(GL_ALWAYS, 0, 0xFF);
  glStencilOp(GL_KEEP, GL_KEEP, GL_INCR_WRAP);
  glColorMask(0, 0, 0, 0);
  glRectf(-1, -1, 0, 1);
  glStencilOp(GL_KEEP, GL_KEEP, GL_DECR_WRAP);
  glRectf(0, -1, 1, 1);
  glColorMask(1, 1, 1, 1);
  glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
  glStencilFunc(GL_EQUAL, 254, 0xFF);
  full_quad(0, 1, 0, 1, 0);
  expect("decr: right is 254", 48, 32, 0x00FF00, 0);
  expect("decr: left is not 254", 16, 32, 0x000000, 0);
  glStencilFunc(GL_EQUAL, 0, 0xFF);
  full_quad(0, 0, 1, 1, 0);
  expect("incr wrap: left is 0", 16, 32, 0x0000FF, 0);
  expect("incr wrap: right is not 0", 48, 32, 0x00FF00, 0);
  /* 0 - 1 wraps to 255 on the left half. */
  glStencilFunc(GL_ALWAYS, 0, 0xFF);
  glStencilOp(GL_KEEP, GL_KEEP, GL_DECR_WRAP);
  glColorMask(0, 0, 0, 0);
  glRectf(-1, -1, 0, 1);
  glColorMask(1, 1, 1, 1);
  glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
  glStencilFunc(GL_EQUAL, 255, 0xFF);
  full_quad(1, 0, 0, 1, 0);
  expect("decr wrap: left is 255", 16, 32, 0xFF0000, 0);
  dump("stencil-wrap");
  glStencilOp(GL_KEEP, GL_KEEP, GL_ONE);
  check("bad stencil op", glGetError() == GL_INVALID_ENUM);
  glDisable(GL_STENCIL_TEST);
}

/* Makes a 4 x 4 texture with the levels red, green and blue. */
static GLuint mip_texture(void) {
  static const GLuint colors[3] = {0xFF0000FFu, 0xFF00FF00u, 0xFFFF0000u};
  GLuint t, texels[16];
  int level, i;
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  for (level = 0; level < 3; level++) {
    int size = 4 >> level;
    for (i = 0; i < size * size; i++) texels[i] = colors[level];
    glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
  }
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  return t;
}

/* Tests GL_TEXTURE_LOD_BIAS of the texture and of the unit. */
static void test_lod_bias(void) {
  GLuint t;
  GLfloat v;
  reset();
  t = mip_texture();
  glEnable(GL_TEXTURE_2D);
  glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
  /* 4 texels on 64 pixels: magnified, level 0. */
  full_quad(1, 1, 1, 1, 0);
  expect("no bias: level 0", 32, 32, 0xFF0000, 0);
  /* A bias of 10 on the texture: the last level. */
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS, 10.0f);
  full_quad(1, 1, 1, 1, 0);
  expect("texture bias: level 2", 32, 32, 0x0000FF, 0);
  /* The bias of the unit is added: 10 - 10 is 0. */
  glTexEnvf(GL_TEXTURE_FILTER_CONTROL, GL_TEXTURE_LOD_BIAS, -10.0f);
  full_quad(1, 1, 1, 1, 0);
  expect("unit bias: level 0", 32, 32, 0xFF0000, 0);
  glGetTexEnvfv(GL_TEXTURE_FILTER_CONTROL, GL_TEXTURE_LOD_BIAS, &v);
  check("unit bias query", v == -10.0f);
  glGetFloatv(GL_MAX_TEXTURE_LOD_BIAS, &v);
  check("max bias", v >= 4.0f);
  glTexEnvf(GL_TEXTURE_FILTER_CONTROL, GL_TEXTURE_LOD_BIAS, 0.0f);
  dump("lod-bias");
  glDisable(GL_TEXTURE_2D);
  glDeleteTextures(1, &t);
}

/* Tests GL_GENERATE_MIPMAP. */
static void test_generate_mipmap(void) {
  GLuint t, texels[16], out[4];
  GLint w = 0, gen = 0;
  int i;
  for (i = 0; i < 16; i++) texels[i] = (i & 1) ? 0xFFFFFFFFu : 0xFF000000u;
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
  glGetTexParameteriv(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, &gen);
  check("generate mipmap query", gen == GL_TRUE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 1, GL_TEXTURE_WIDTH, &w);
  check("generated level 1 is 2 wide", w == 2);
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 2, GL_TEXTURE_WIDTH, &w);
  check("generated level 2 is 1 wide", w == 1);
  glGetTexImage(GL_TEXTURE_2D, 2, GL_RGBA, GL_UNSIGNED_BYTE, out);
  check("generated level 2 is the mean", (out[0] & 0xFF) >= 126 && (out[0] & 0xFF) <= 129);
  /* A change of the base level makes the levels again. */
  for (i = 0; i < 16; i++) texels[i] = 0xFFFFFFFFu;
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 4, 4, GL_RGBA, GL_UNSIGNED_BYTE, texels);
  glGetTexImage(GL_TEXTURE_2D, 1, GL_RGBA, GL_UNSIGNED_BYTE, out);
  check("generated again after sub image", out[0] == 0xFFFFFFFFu && out[3] == 0xFFFFFFFFu);
  glHint(GL_GENERATE_MIPMAP_HINT, GL_NICEST);
  glGetIntegerv(GL_GENERATE_MIPMAP_HINT, &gen);
  check("mipmap hint", gen == GL_NICEST);
  glDeleteTextures(1, &t);
}

/* Tests glWindowPos and glMultiDrawArrays. */
static void test_window_pos_and_multi_draw(void) {
  static const GLubyte red[4] = {255, 0, 0, 255};
  static const GLfloat xy[8][2] = {{-1, -1}, {-0.5f, -1}, {-0.5f, -0.5f}, {-1, -0.5f},
                                   {0.5f, 0.5f}, {1, 0.5f}, {1, 1}, {0.5f, 1}};
  static const GLint first[2] = {0, 4};
  static const GLsizei count[2] = {4, 4};
  GLfloat pos[4];
  reset();
  /* The matrices do not change the window position. */
  glMatrixMode(GL_MODELVIEW);
  glTranslatef(0.5f, 0.5f, 0);
  glWindowPos2i(10, 20);
  glGetFloatv(GL_CURRENT_RASTER_POSITION, pos);
  check("window pos", pos[0] == 10 && pos[1] == 20 && pos[2] == 0 && pos[3] == 1);
  glDrawPixels(1, 1, GL_RGBA, GL_UNSIGNED_BYTE, red);
  expect("window pos pixel", 10, 20, 0xFF0000, 0);
  glWindowPos3f(1, 2, 2.0f);
  glGetFloatv(GL_CURRENT_RASTER_POSITION, pos);
  check("window pos z is clamped", pos[2] == 1.0f);
  glLoadIdentity();
  glColor3f(0, 1, 0);
  glEnableClientState(GL_VERTEX_ARRAY);
  glVertexPointer(2, GL_FLOAT, 0, xy);
  glMultiDrawArrays(GL_QUADS, first, count, 2);
  glDisableClientState(GL_VERTEX_ARRAY);
  expect("multi draw 1", 4, 4, 0x00FF00, 0);
  expect("multi draw 2", 60, 60, 0x00FF00, 0);
  expect("multi draw gap", 32, 32, 0x000000, 0);
  dump("window-pos");
}

/* Tests the attenuation of the point size with the distance. */
static void test_point_parameters(void) {
  static const GLfloat atten[3] = {0, 0, 1};
  GLfloat v[3];
  reset();
  glMatrixMode(GL_PROJECTION);
  glOrtho(-1, 1, -1, 1, 0, 10);
  glMatrixMode(GL_MODELVIEW);
  glPointSize(8);
  glPointParameterfv(GL_POINT_DISTANCE_ATTENUATION, atten);
  /* At distance 2: 8 / sqrt(4) is 4 pixels, 30 to 33. */
  glColor3f(1, 1, 1);
  glBegin(GL_POINTS);
  glVertex3f(0, 0, -2);
  glEnd();
  expect("attenuated point: inside", 30, 32, 0xFFFFFF, 0);
  expect("attenuated point: inside right", 33, 32, 0xFFFFFF, 0);
  expect("attenuated point: outside", 29, 32, 0x000000, 0);
  expect("attenuated point: outside right", 34, 32, 0x000000, 0);
  /* GL_POINT_SIZE_MIN keeps it at 6 pixels, 29 to 34. */
  glPointParameterf(GL_POINT_SIZE_MIN, 6);
  glClear(GL_COLOR_BUFFER_BIT);
  glBegin(GL_POINTS);
  glVertex3f(0, 0, -2);
  glEnd();
  expect("minimum point size", 29, 32, 0xFFFFFF, 0);
  expect("minimum point size: outside", 28, 32, 0x000000, 0);
  dump("points");
  glGetFloatv(GL_POINT_DISTANCE_ATTENUATION, v);
  check("attenuation query", v[0] == 0 && v[1] == 0 && v[2] == 1);
  glPointParameterf(GL_POINT_SIZE_MIN, -1);
  check("negative minimum size", glGetError() == GL_INVALID_VALUE);
  v[0] = 1;
  v[1] = v[2] = 0;
  glPointParameterfv(GL_POINT_DISTANCE_ATTENUATION, v);
  glPointParameterf(GL_POINT_SIZE_MIN, 0);
  glPointSize(1);
}

/* Tests the secondary color and GL_COLOR_SUM without lighting, also
 * from an array and in a display list. */
static void test_secondary_color(void) {
  static const GLfloat xy[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
  static const GLubyte sec[4][3] = {{0, 0, 255}, {0, 0, 255}, {0, 0, 255}, {0, 0, 255}};
  GLfloat v[4];
  GLuint list;
  reset();
  glEnable(GL_COLOR_SUM);
  glSecondaryColor3f(0, 0.5f, 0);
  full_quad(0.5f, 0, 0, 1, 0);
  expect("color sum", 32, 32, 0x808000, 2);
  glDisable(GL_COLOR_SUM);
  full_quad(0.5f, 0, 0, 1, 0);
  expect("no color sum", 32, 32, 0x800000, 2);
  glGetFloatv(GL_CURRENT_SECONDARY_COLOR, v);
  check("secondary color query", v[1] == 0.5f);
  /* The array. */
  glEnable(GL_COLOR_SUM);
  glColor3f(0, 0, 0);
  glEnableClientState(GL_VERTEX_ARRAY);
  glEnableClientState(GL_SECONDARY_COLOR_ARRAY);
  glVertexPointer(2, GL_FLOAT, 0, xy);
  glSecondaryColorPointer(3, GL_UNSIGNED_BYTE, 0, sec);
  glDrawArrays(GL_QUADS, 0, 4);
  glDisableClientState(GL_SECONDARY_COLOR_ARRAY);
  glDisableClientState(GL_VERTEX_ARRAY);
  expect("secondary color array", 32, 32, 0x0000FF, 0);
  /* The display list. */
  list = glGenLists(1);
  glNewList(list, GL_COMPILE);
  glSecondaryColor3ub(255, 0, 0);
  glEndList();
  glSecondaryColor3f(0, 0, 0);
  glCallList(list);
  glGetFloatv(GL_CURRENT_SECONDARY_COLOR, v);
  check("secondary color in a list", v[0] == 1.0f);
  glDeleteLists(list, 1);
  glDisable(GL_COLOR_SUM);
  glSecondaryColor3f(0, 0, 0);
  dump("secondary-color");
}

/* Tests the fog coordinate. */
static void test_fog_coord(void) {
  GLint src;
  reset();
  glEnable(GL_FOG);
  glFogi(GL_FOG_MODE, GL_LINEAR);
  glFogf(GL_FOG_START, 0);
  glFogf(GL_FOG_END, 10);
  glFogi(GL_FOG_COORDINATE_SOURCE, GL_FOG_COORDINATE);
  glGetIntegerv(GL_FOG_COORDINATE_SOURCE, &src);
  check("fog source query", src == GL_FOG_COORDINATE);
  /* A fog coordinate of 5 of 10: half fog. The eye z (0) is not used. */
  glFogCoordf(5);
  full_quad(1, 1, 1, 1, 0);
  expect("fog coordinate", 32, 32, 0x808080, 2);
  glFogi(GL_FOG_COORDINATE_SOURCE, GL_FRAGMENT_DEPTH);
  full_quad(1, 1, 1, 1, 0);
  expect("fragment depth", 32, 32, 0xFFFFFF, 2);
  glDisable(GL_FOG);
  dump("fog-coord");
}

/* Tests the depth textures and the comparison of GL_ARB_shadow. */
static void test_depth_texture(void) {
  static const GLfloat depths[2] = {0.25f, 0.75f};
  GLfloat back[4];
  GLuint t;
  GLint size = 0;
  reset();
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, 2, 1, 0, GL_DEPTH_COMPONENT, GL_FLOAT, depths);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_DEPTH_SIZE, &size);
  check("depth size", size > 0);
  glGetTexImage(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, GL_FLOAT, back);
  check("depth read back", back[0] > 0.249f && back[0] < 0.251f && back[1] > 0.749f && back[1] < 0.751f);
  glEnable(GL_TEXTURE_2D);
  glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
  /* No comparison: the depth as a luminance. */
  full_quad(1, 1, 1, 1, 0.5f);
  expect("depth as luminance: left", 16, 32, 0x404040, 2);
  expect("depth as luminance: right", 48, 32, 0xBFBFBF, 2);
  /* r = 0.5 <= the depth: only the right half passes. */
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_R_TO_TEXTURE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
  full_quad(1, 1, 1, 1, 0.5f);
  expect("shadow lequal: left", 16, 32, 0x000000, 0);
  expect("shadow lequal: right", 48, 32, 0xFFFFFF, 0);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_GEQUAL);
  full_quad(1, 1, 1, 1, 0.5f);
  expect("shadow gequal: left", 16, 32, 0xFFFFFF, 0);
  expect("shadow gequal: right", 48, 32, 0x000000, 0);
  /* GL_DEPTH_TEXTURE_MODE GL_ALPHA: the color is white, the alpha is
   * the result. */
  glTexParameteri(GL_TEXTURE_2D, GL_DEPTH_TEXTURE_MODE, GL_ALPHA);
  glEnable(GL_ALPHA_TEST);
  glAlphaFunc(GL_GREATER, 0.5f);
  glClear(GL_COLOR_BUFFER_BIT);
  full_quad(0, 1, 0, 1, 0.5f);
  glDisable(GL_ALPHA_TEST);
  expect("shadow alpha: left", 16, 32, 0x00FF00, 0);
  expect("shadow alpha: right", 48, 32, 0x000000, 0);
  dump("shadow");
  /* A color format with depth pixels is an error. */
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 1, 0, GL_DEPTH_COMPONENT, GL_FLOAT, depths);
  check("depth pixels for a color texture", glGetError() == GL_INVALID_OPERATION);
  /* glCopyTexImage2D from the depth buffer. */
  glClearDepth(0.5);
  glClear(GL_DEPTH_BUFFER_BIT);
  glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, 0, 0, 2, 1, 0);
  glGetTexImage(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, GL_FLOAT, back);
  check("depth copied", back[0] > 0.49f && back[0] < 0.51f);
  glClearDepth(1.0);
  glDisable(GL_TEXTURE_2D);
  glDeleteTextures(1, &t);
}

/* Tests the names and the version. */
static void test_names(void) {
  const char *ext = (const char *)glGetString(GL_EXTENSIONS);
  check("version 1.4", strncmp((const char *)glGetString(GL_VERSION), "1.4", 3) == 0);
  check("extension GL_ARB_shadow", strstr(ext, "GL_ARB_shadow") != NULL);
  check("extension GL_EXT_blend_func_separate", strstr(ext, "GL_EXT_blend_func_separate") != NULL);
  check("proc glWindowPos2iARB", sgl_get_proc("glWindowPos2iARB") != NULL);
  check("proc glSecondaryColor3fEXT", sgl_get_proc("glSecondaryColor3fEXT") != NULL);
  check("proc glBlendColorEXT", sgl_get_proc("glBlendColorEXT") == (void *)glBlendColorEXT);
  check("proc glPointParameterfvARB", sgl_get_proc("glPointParameterfvARB") != NULL);
  check("proc glMultiDrawElements", sgl_get_proc("glMultiDrawElements") == (void *)glMultiDrawElements);
  check("proc glFogCoordPointerEXT", sgl_get_proc("glFogCoordPointerEXT") != NULL);
}

/* Runs the tests. */
int main(int argc, char **argv) {
  sgl_context *ctx;
  if (argc > 1) prefix = argv[1];
  ctx = make_context(0);
  test_blend();
  test_stencil_wrap();
  test_lod_bias();
  test_generate_mipmap();
  test_window_pos_and_multi_draw();
  test_point_parameters();
  test_secondary_color();
  test_fog_coord();
  test_depth_texture();
  test_names();
  {
    GLenum e = glGetError();
    if (e != GL_NO_ERROR) {
      printf("FAIL: error %04x was recorded\n", e);
      failures++;
    }
  }
  sgl_destroy_context(ctx);
  if (failures) return 1;
  printf("PASS softgl gl14\n");
  return 0;
}
