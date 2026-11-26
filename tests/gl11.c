/* gl11.c - tests of the OpenGL 1.1 functions that render.c does not
 * examine: selection, feedback, the logical operation, the polygon
 * stipple, the pixel transfer of indices, stencil and depth images, and
 * color index mode.
 *
 * Part of clang-retro softgl, a software implementation of OpenGL.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include "harness.h"

/* Draws a quad from x0, y0 to x1, y1 at depth z (clip coordinates). */
static void quad(GLfloat x0, GLfloat y0, GLfloat x1, GLfloat y1, GLfloat z) {
  glBegin(GL_QUADS);
  glVertex3f(x0, y0, z);
  glVertex3f(x1, y0, z);
  glVertex3f(x1, y1, z);
  glVertex3f(x0, y1, z);
  glEnd();
}

/* Selection: two named quads, one outside the view volume. */
static void test_select(void) {
  GLuint buf[64];
  GLint hits;
  reset();
  glSelectBuffer(64, buf);
  glRenderMode(GL_SELECT);
  glInitNames();
  glPushName(7);
  quad(-0.5f, -0.5f, 0.5f, 0.5f, 0.0f);
  glLoadName(8);
  quad(2.0f, 2.0f, 3.0f, 3.0f, 0.0f); /* clipped away: no hit */
  glLoadName(9);
  glBegin(GL_TRIANGLES);
  glVertex3f(-1, -1, -0.5f);
  glVertex3f(1, -1, 0.5f);
  glVertex3f(0, 1, 0.5f);
  glEnd();
  hits = glRenderMode(GL_RENDER);
  check("select: two hits", hits == 2);
  check("select: first record", buf[0] == 1 && buf[3] == 7);
  check("select: first z is the middle", buf[1] > 0x7F000000u && buf[1] < 0x81000000u);
  check("select: second record", buf[4] == 1 && buf[7] == 9);
  check("select: second z range", buf[5] < 0x41000000u && buf[6] > 0xBF000000u);
  check("select: nothing drawn", at(32, 32) == 0);
  /* A buffer that is too small gives -1. */
  glSelectBuffer(2, buf);
  glRenderMode(GL_SELECT);
  glInitNames();
  glPushName(1);
  quad(-0.5f, -0.5f, 0.5f, 0.5f, 0.0f);
  check("select: overflow", glRenderMode(GL_RENDER) == -1);
}

/* Feedback: a clipped triangle, a line strip, a point and a marker. */
static void test_feedback(void) {
  GLfloat buf[256];
  GLint n;
  reset();
  glFeedbackBuffer(256, GL_3D_COLOR, buf);
  glRenderMode(GL_FEEDBACK);
  glColor3f(1, 0, 0);
  glBegin(GL_TRIANGLES);
  glVertex2f(-1, -1);
  glVertex2f(1, -1);
  glVertex2f(-1, 1);
  glEnd();
  glPassThrough(42.0f);
  glBegin(GL_LINE_STRIP);
  glVertex2f(-1, 0);
  glVertex2f(0, 0);
  glVertex2f(0, 1);
  glEnd();
  glBegin(GL_POINTS);
  glVertex2f(0, 0);
  glEnd();
  n = glRenderMode(GL_RENDER);
  /* polygon: 2 + 3 * 7, pass-through: 2, lines: 2 * (1 + 2 * 7),
   * point: 1 + 7. */
  check("feedback: count", n == 23 + 2 + 30 + 8);
  check("feedback: polygon token", buf[0] == (GLfloat)GL_POLYGON_TOKEN && buf[1] == 3.0f);
  check("feedback: first vertex", buf[2] == 0.0f && buf[3] == 0.0f && buf[4] == 0.5f);
  check("feedback: first color", buf[5] == 1.0f && buf[6] == 0.0f && buf[8] == 1.0f);
  check("feedback: pass-through", buf[23] == (GLfloat)GL_PASS_THROUGH_TOKEN && buf[24] == 42.0f);
  check("feedback: line reset", buf[25] == (GLfloat)GL_LINE_RESET_TOKEN);
  check("feedback: line", buf[40] == (GLfloat)GL_LINE_TOKEN);
  check("feedback: point", buf[55] == (GLfloat)GL_POINT_TOKEN && buf[56] == 32.0f);
  /* Clipping makes a polygon of 4 vertices. */
  glFeedbackBuffer(256, GL_2D, buf);
  glRenderMode(GL_FEEDBACK);
  glBegin(GL_TRIANGLES);
  glVertex2f(-1, -1);
  glVertex2f(3, -1);
  glVertex2f(-1, 1);
  glEnd();
  n = glRenderMode(GL_RENDER);
  check("feedback: clipped polygon", n == 2 + 4 * 2 && buf[1] == 4.0f);
}

/* glBegin(GL_POINTS) draws: GL_POINTS is 0, the same value as GL_NONE. */
static void test_points(void) {
  reset();
  glColor3f(1, 1, 1);
  glPointSize(3);
  glBegin(GL_POINTS);
  glVertex2f(0, 0);
  glEnd();
  glPointSize(1);
  expect("points", 32, 32, 0xFFFFFF, 0);
}

/* The logical operation in RGBA mode. */
static void test_logic_op(void) {
  reset();
  glClearColor(1, 0, 1, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  glEnable(GL_COLOR_LOGIC_OP);
  glLogicOp(GL_XOR);
  glColor3f(1, 1, 0);
  quad(-1, -1, 0, 1, 0);
  glLogicOp(GL_INVERT);
  quad(0, -1, 1, 1, 0);
  glDisable(GL_COLOR_LOGIC_OP);
  dump("logic-op");
  expect("logic op xor", 16, 32, 0x00FFFF, 0);
  expect("logic op invert", 48, 32, 0x00FF00, 0);
}

/* The polygon stipple: a checker of 1-pixel squares. */
static void test_stipple(void) {
  GLubyte pattern[128];
  int i;
  for (i = 0; i < 128; i++) pattern[i] = (i / 4) & 1 ? 0xAA : 0x55;
  reset();
  glPolygonStipple(pattern);
  glEnable(GL_POLYGON_STIPPLE);
  glColor3f(1, 1, 1);
  quad(-1, -1, 1, 1, 0);
  glDisable(GL_POLYGON_STIPPLE);
  dump("stipple");
  /* Row 0 is 0x55: bit 7 of each byte (the left pixel) is 0. */
  expect("stipple 0,0", 0, 0, 0x000000, 0);
  expect("stipple 1,0", 1, 0, 0xFFFFFF, 0);
  expect("stipple 0,1", 0, 1, 0xFFFFFF, 0);
  expect("stipple 1,1", 1, 1, 0x000000, 0);
}

/* Color index images in RGBA mode through the pixel maps, stencil
 * images and depth images. */
static void test_pixels(void) {
  static const GLfloat red[2] = {0.0f, 1.0f}, green[2] = {1.0f, 0.0f}, one[2] = {1.0f, 1.0f};
  GLubyte idx[4] = {0, 1, 0, 1}, st[4] = {5, 6, 7, 8}, back[4];
  GLfloat depth[4] = {0.25f, 0.25f, 0.25f, 0.25f};
  reset();
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glPixelMapfv(GL_PIXEL_MAP_I_TO_R, 2, red);
  glPixelMapfv(GL_PIXEL_MAP_I_TO_G, 2, green);
  glPixelMapfv(GL_PIXEL_MAP_I_TO_A, 2, one);
  glRasterPos2i(0, 0);
  glDrawPixels(4, 1, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, idx);
  expect("index image 0", 32, 32, 0x00FF00, 0);
  expect("index image 1", 33, 32, 0xFF0000, 0);
  /* Stencil images go to the stencil buffer. */
  glDrawPixels(2, 2, GL_STENCIL_INDEX, GL_UNSIGNED_BYTE, st);
  memset(back, 0, sizeof back);
  glReadPixels(32, 32, 2, 2, GL_STENCIL_INDEX, GL_UNSIGNED_BYTE, back);
  check("stencil image", back[0] == 5 && back[1] == 6 && back[2] == 7 && back[3] == 8);
  glPixelTransferi(GL_INDEX_OFFSET, 10);
  glReadPixels(32, 32, 1, 1, GL_STENCIL_INDEX, GL_UNSIGNED_BYTE, back);
  check("stencil offset", back[0] == 15);
  glPixelTransferi(GL_INDEX_OFFSET, 0);
  /* Depth images write the depth buffer with the raster color. */
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_ALWAYS);
  glColor3f(0, 0, 1);
  glRasterPos2i(-1, -1);
  glDrawPixels(2, 2, GL_DEPTH_COMPONENT, GL_FLOAT, depth);
  expect("depth image color", 0, 0, 0x0000FF, 0);
  glReadPixels(0, 0, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, depth);
  check("depth image value", depth[0] > 0.24f && depth[0] < 0.26f);
  glDepthFunc(GL_LESS);
  glDisable(GL_DEPTH_TEST);
}

/* Color index mode: clear, flat and smooth indices, the index mask,
 * the logical operation and the fog index. */
static void test_index_mode(void) {
  sgl_context *ctx = make_context(8), *old;
  GLint v;
  GLubyte back[2];
  glGetIntegerv(GL_INDEX_BITS, &v);
  check("index mode: bits", v == 8);
  glGetIntegerv(GL_INDEX_MODE, &v);
  check("index mode: on", v == GL_TRUE);
  reset();
  glClearIndex(3);
  glClear(GL_COLOR_BUFFER_BIT);
  check("index mode: clear", pixels[0] == 3);
  glIndexi(200);
  quad(-1, -1, 0, 1, 0);
  check("index mode: flat", (pixels[(H - 1 - 32) * W + 16]) == 200);
  /* A smooth line of indices 0 to 64 across the window. */
  glBegin(GL_TRIANGLES);
  glIndexi(0);
  glVertex2f(0, -1);
  glIndexi(64);
  glVertex2f(1, -1);
  glIndexi(64);
  glVertex2f(1, 1);
  glEnd();
  v = (GLint)pixels[(H - 1 - 2) * W + 48];
  check("index mode: smooth", v >= 30 && v <= 34);
  /* The index mask keeps the high bits. */
  glIndexMask(0x0F);
  glIndexi(0xAB);
  quad(-1, -1, 0, 1, 0);
  check("index mode: mask", (pixels[(H - 1 - 32) * W + 16]) == 0xCB);
  glIndexMask(0xFF);
  /* The logical operation. */
  glEnable(GL_INDEX_LOGIC_OP);
  glLogicOp(GL_XOR);
  glIndexi(0xFF);
  quad(-1, -1, 0, 1, 0);
  check("index mode: xor", (pixels[(H - 1 - 32) * W + 16]) == 0x34);
  glDisable(GL_INDEX_LOGIC_OP);
  /* Fog adds the fog index where the fog is thick. */
  glEnable(GL_FOG);
  glFogi(GL_FOG_MODE, GL_LINEAR);
  glFogf(GL_FOG_START, 0);
  glFogf(GL_FOG_END, 0);
  glFogf(GL_FOG_INDEX, 0);
  glFogf(GL_FOG_END, 1);
  glFogf(GL_FOG_INDEX, 16);
  glIndexi(10);
  glMatrixMode(GL_PROJECTION);
  glOrtho(-1, 1, -1, 1, 0, 10);
  glMatrixMode(GL_MODELVIEW);
  quad(-1, -1, 0, 1, -5); /* eye distance 5: the fog factor is 0 */
  check("index mode: fog", (pixels[(H - 1 - 32) * W + 16]) == 26);
  glDisable(GL_FOG);
  /* glReadPixels gives the indices. */
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(16, 32, 2, 1, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, back);
  check("index mode: read", back[0] == 26 && back[1] == 26);
  /* Lighting gives an index between the ambient and specular indices. */
  {
    GLfloat idx[3] = {16, 32, 48};
    reset();
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glMaterialfv(GL_FRONT, GL_COLOR_INDEXES, idx);
    glNormal3f(0, 0, 1);
    quad(-1, -1, 1, 1, 0);
    v = (GLint)pixels[(H - 1 - 32) * W + 32];
    check("index mode: lit", v >= 16 && v <= 48);
    glDisable(GL_LIGHTING);
  }
  check("index mode: no error", glGetError() == GL_NO_ERROR);
  old = ctx;
  sgl_destroy_context(old);
}

/* GLU NURBS: a flat bicubic surface and a curve, drawn, and a curve
 * given back through the callbacks of the tessellator mode. */
static int nurbs_vertices;

/* Counts the vertices that the NURBS tessellator gives. */
static void GLAPIENTRY nurbs_vertex(GLfloat *v) {
  (void)v;
  nurbs_vertices++;
}

/* Draws the NURBS scenes. */
static void test_nurbs(void) {
  GLfloat knots[8] = {0, 0, 0, 0, 1, 1, 1, 1};
  GLfloat ctl[4][4][3];
  GLfloat curve[4][3] = {{-0.8f, -0.8f, 0}, {-0.3f, 0.8f, 0}, {0.3f, 0.8f, 0}, {0.8f, -0.8f, 0}};
  GLUnurbs *nurb = gluNewNurbsRenderer();
  int i, j;
  check("nurbs: renderer", nurb != NULL);
  if (!nurb) return;
  for (i = 0; i < 4; i++)
    for (j = 0; j < 4; j++) {
      ctl[i][j][0] = -0.8f + 1.6f * (GLfloat)i / 3.0f;
      ctl[i][j][1] = -0.8f + 1.6f * (GLfloat)j / 3.0f;
      ctl[i][j][2] = 0.0f;
    }
  reset();
  glColor3f(0, 1, 0);
  gluNurbsProperty(nurb, GLU_SAMPLING_TOLERANCE, 25.0f);
  gluNurbsProperty(nurb, GLU_DISPLAY_MODE, GLU_FILL);
  gluBeginSurface(nurb);
  gluNurbsSurface(nurb, 8, knots, 8, knots, 4 * 3, 3, &ctl[0][0][0], 4, 4, GL_MAP2_VERTEX_3);
  gluEndSurface(nurb);
  dump("nurbs-surface");
  expect("nurbs surface middle", 32, 32, 0x00FF00, 0);
  expect("nurbs surface outside", 1, 1, 0x000000, 0);
  /* A curve in tessellator mode gives its vertices to the callback. */
  gluNurbsProperty(nurb, GLU_NURBS_MODE, GLU_NURBS_TESSELLATOR);
  gluNurbsCallback(nurb, GLU_NURBS_VERTEX, (void (GLAPIENTRY *)(void))nurbs_vertex);
  gluBeginCurve(nurb);
  gluNurbsCurve(nurb, 8, knots, 3, &curve[0][0], 4, GL_MAP1_VERTEX_3);
  gluEndCurve(nurb);
  check("nurbs: tessellated curve", nurbs_vertices > 2);
  gluDeleteNurbsRenderer(nurb);
}

/* Runs the tests. */
int main(int argc, char **argv) {
  sgl_context *ctx;
  if (argc > 1) prefix = argv[1];
  ctx = make_context(0);
  test_select();
  test_feedback();
  test_points();
  test_logic_op();
  test_stipple();
  test_pixels();
  test_nurbs();
  {
    GLenum e = glGetError();
    if (e != GL_NO_ERROR) {
      printf("FAIL: error %04x was recorded\n", e);
      failures++;
    }
  }
  sgl_destroy_context(ctx);
  test_index_mode();
  if (failures) return 1;
  printf("PASS softgl gl11\n");
  return 0;
}
