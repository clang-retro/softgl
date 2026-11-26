/* gl15.c - the tests of the OpenGL 1.5 features of softgl: the buffer
 * objects (vertex arrays, elements, map, sub data, deletion, display
 * lists and glPushClientAttrib), the occlusion queries and the shadow
 * functions of GL_EXT_shadow_funcs.
 *
 *     gl15 [prefix]
 *
 * With a prefix, each scene is also written as prefix-name.ppm.
 *
 * Part of clang-retro softgl, a software implementation of OpenGL.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include <GL/glext.h>

#include "harness.h"

/* The vertices of a quad over the buffer (x, y), then the color of
 * each vertex (r, g, b). */
static const GLfloat quad_data[4 * 2 + 4 * 3] = {
    -1, -1, 1, -1, 1, 1, -1, 1,
    0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0,
};

/* Gives the error and clears it. */
static GLenum error(void) { return glGetError(); }

/* Tests the vertex arrays and the elements in buffer objects. */
static void test_buffers(void) {
  static const GLubyte idx[4] = {0, 1, 2, 3};
  GLuint b[2];
  GLint v;
  GLfloat *map, back[2];
  void *p;
  GLuint list;
  reset();
  glGenBuffers(2, b);
  check("gen buffers", b[0] != 0 && b[1] != 0 && b[0] != b[1]);
  check("not a buffer before the bind", !glIsBuffer(b[0]));
  glBindBuffer(GL_ARRAY_BUFFER, b[0]);
  check("a buffer after the bind", glIsBuffer(b[0]));
  glBufferData(GL_ARRAY_BUFFER, sizeof quad_data, quad_data, GL_STATIC_DRAW);
  glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &v);
  check("buffer size", v == (GLint)sizeof quad_data);
  glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_USAGE, &v);
  check("buffer usage", v == GL_STATIC_DRAW);
  /* The pointers are offsets in the buffer. */
  glVertexPointer(2, GL_FLOAT, 0, (const void *)0);
  glColorPointer(3, GL_FLOAT, 0, (const void *)(8 * sizeof(GLfloat)));
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glGetIntegerv(GL_VERTEX_ARRAY_BUFFER_BINDING, &v);
  check("vertex array buffer binding", v == (GLint)b[0]);
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &v);
  check("array buffer binding is 0", v == 0);
  glEnableClientState(GL_VERTEX_ARRAY);
  glEnableClientState(GL_COLOR_ARRAY);
  glDrawArrays(GL_QUADS, 0, 4);
  expect("draw from a buffer", 32, 32, 0x00FF00, 0);
  /* The elements in a buffer. */
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, b[1]);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof idx, idx, GL_STATIC_DRAW);
  glClear(GL_COLOR_BUFFER_BIT);
  glDrawElements(GL_QUADS, 4, GL_UNSIGNED_BYTE, (const void *)0);
  expect("elements from a buffer", 32, 32, 0x00FF00, 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  /* The map: change the colors to blue. */
  glBindBuffer(GL_ARRAY_BUFFER, b[0]);
  map = (GLfloat *)glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
  check("map", map != NULL);
  glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_MAPPED, &v);
  check("mapped", v == GL_TRUE);
  glGetBufferPointerv(GL_ARRAY_BUFFER, GL_BUFFER_MAP_POINTER, &p);
  check("map pointer", p == (void *)map);
  check("map twice", glMapBuffer(GL_ARRAY_BUFFER, GL_READ_ONLY) == NULL && error() == GL_INVALID_OPERATION);
  if (map) {
    int i;
    for (i = 0; i < 4; i++) {
      map[8 + i * 3 + 1] = 0;
      map[8 + i * 3 + 2] = 1;
    }
  }
  check("unmap", glUnmapBuffer(GL_ARRAY_BUFFER) == GL_TRUE);
  check("unmap twice", glUnmapBuffer(GL_ARRAY_BUFFER) == GL_FALSE && error() == GL_INVALID_OPERATION);
  glDrawArrays(GL_QUADS, 0, 4);
  expect("draw after the map", 32, 32, 0x0000FF, 0);
  /* Sub data. */
  back[0] = 0.5f;
  back[1] = 0.25f;
  glBufferSubData(GL_ARRAY_BUFFER, 4 * sizeof(GLfloat), 2 * sizeof(GLfloat), back);
  back[0] = back[1] = 0;
  glGetBufferSubData(GL_ARRAY_BUFFER, 4 * sizeof(GLfloat), 2 * sizeof(GLfloat), back);
  check("sub data", back[0] == 0.5f && back[1] == 0.25f);
  glBufferSubData(GL_ARRAY_BUFFER, sizeof quad_data - 4, 8, back);
  check("sub data out of the buffer", error() == GL_INVALID_VALUE);
  glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof quad_data, quad_data);
  /* A display list keeps the vertices, not the buffer. */
  list = glGenLists(1);
  glNewList(list, GL_COMPILE);
  glDrawArrays(GL_QUADS, 0, 4);
  glEndList();
  /* glPushClientAttrib keeps the bindings; the buffer lives while the
   * saved state has it. */
  glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
  glDeleteBuffers(1, &b[0]);
  glGetIntegerv(GL_VERTEX_ARRAY_BUFFER_BINDING, &v);
  check("delete unbinds the vertex array", v == 0);
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &v);
  check("delete unbinds the array buffer", v == 0);
  check("deleted", !glIsBuffer(b[0]));
  glPopClientAttrib();
  glGetIntegerv(GL_VERTEX_ARRAY_BUFFER_BINDING, &v);
  check("pop restores the binding", v == (GLint)b[0]);
  glClear(GL_COLOR_BUFFER_BIT);
  glDrawArrays(GL_QUADS, 0, 4);
  expect("draw from the saved buffer", 32, 32, 0x00FF00, 0);
  glDisableClientState(GL_COLOR_ARRAY);
  glDisableClientState(GL_VERTEX_ARRAY);
  glVertexPointer(2, GL_FLOAT, 0, quad_data);
  glColorPointer(3, GL_FLOAT, 0, quad_data + 8);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glClear(GL_COLOR_BUFFER_BIT);
  glCallList(list);
  expect("display list after the delete", 32, 32, 0x00FF00, 0);
  glDeleteLists(list, 1);
  glDeleteBuffers(1, &b[1]);
  /* Errors. */
  glBufferData(GL_ARRAY_BUFFER, 4, NULL, GL_STATIC_DRAW);
  check("buffer data without a buffer", error() == GL_INVALID_OPERATION);
  glBindBuffer(GL_TEXTURE_2D, 1);
  check("bad target", error() == GL_INVALID_ENUM);
  dump("buffers");
}

/* Tests the occlusion queries, and that a fragment that fails the
 * alpha test does not change the depth buffer. */
static void test_queries(void) {
  GLuint q[2], n = 99, avail = 0, list;
  GLint cur = -1, bits = 0;
  reset();
  glGenQueries(2, q);
  check("gen queries", q[0] != 0 && q[1] != 0 && q[0] != q[1]);
  glBeginQuery(GL_SAMPLES_PASSED, q[0]);
  glGetQueryiv(GL_SAMPLES_PASSED, GL_CURRENT_QUERY, &cur);
  check("current query", cur == (GLint)q[0]);
  glGetQueryiv(GL_SAMPLES_PASSED, GL_QUERY_COUNTER_BITS, &bits);
  check("counter bits", bits >= 24);
  glBeginQuery(GL_SAMPLES_PASSED, q[1]);
  check("two active queries", error() == GL_INVALID_OPERATION);
  glColor3f(1, 1, 1);
  glRectf(-0.5f, -0.5f, 0.5f, 0.5f);
  glEndQuery(GL_SAMPLES_PASSED);
  glGetQueryObjectuiv(q[0], GL_QUERY_RESULT_AVAILABLE, &avail);
  glGetQueryObjectuiv(q[0], GL_QUERY_RESULT, &n);
  check("query available", avail == GL_TRUE);
  check("query: 32 x 32 samples", n == 32 * 32);
  check("is query", glIsQuery(q[0]));
  /* A quad behind the near one: no sample passes. */
  glEnable(GL_DEPTH_TEST);
  glClear(GL_DEPTH_BUFFER_BIT);
  glRectf(-1, -1, 1, 1);
  glBeginQuery(GL_SAMPLES_PASSED, q[1]);
  glBegin(GL_QUADS);
  glVertex3f(-1, -1, 0.5f);
  glVertex3f(1, -1, 0.5f);
  glVertex3f(1, 1, 0.5f);
  glVertex3f(-1, 1, 0.5f);
  glEnd();
  glEndQuery(GL_SAMPLES_PASSED);
  glGetQueryObjectuiv(q[1], GL_QUERY_RESULT, &n);
  check("hidden: 0 samples", n == 0);
  /* The alpha test removes the fragments before the depth test. */
  glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
  glEnable(GL_ALPHA_TEST);
  glAlphaFunc(GL_GREATER, 0.5f);
  glBeginQuery(GL_SAMPLES_PASSED, q[1]);
  glColor4f(1, 0, 0, 0.25f);
  glRectf(-1, -1, 1, 1);
  glEndQuery(GL_SAMPLES_PASSED);
  glGetQueryObjectuiv(q[1], GL_QUERY_RESULT, &n);
  check("alpha test: 0 samples", n == 0);
  glDisable(GL_ALPHA_TEST);
  glColor3f(0, 1, 0);
  glBegin(GL_QUADS);
  glVertex3f(-1, -1, 0.5f);
  glVertex3f(1, -1, 0.5f);
  glVertex3f(1, 1, 0.5f);
  glVertex3f(-1, 1, 0.5f);
  glEnd();
  expect("alpha test does not write the depth", 32, 32, 0x00FF00, 0);
  glDisable(GL_DEPTH_TEST);
  /* A query in a display list. */
  list = glGenLists(1);
  glNewList(list, GL_COMPILE);
  glBeginQuery(GL_SAMPLES_PASSED, q[0]);
  glRectf(0, 0, 0.5f, 0.5f);
  glEndQuery(GL_SAMPLES_PASSED);
  glEndList();
  glCallList(list);
  glGetQueryObjectuiv(q[0], GL_QUERY_RESULT, &n);
  check("query in a display list: 16 x 16 samples", n == 16 * 16);
  glDeleteLists(list, 1);
  glDeleteQueries(2, q);
  check("deleted query", !glIsQuery(q[0]));
  glGetQueryObjectuiv(q[0], GL_QUERY_RESULT, &n);
  check("result of a deleted query", error() == GL_INVALID_OPERATION);
  glEndQuery(GL_SAMPLES_PASSED);
  check("end without a query", error() == GL_INVALID_OPERATION);
  dump("queries");
}

/* Tests the eight functions of GL_TEXTURE_COMPARE_FUNC. */
static void test_shadow_funcs(void) {
  static const GLfloat depths[2] = {0.25f, 0.75f};
  /* r = 0.5 against 0.25 (left) and 0.75 (right). */
  static const struct {
    GLenum func;
    uint32_t left, right;
  } cases[] = {
      {GL_NEVER, 0, 0},          {GL_ALWAYS, 0xFFFFFF, 0xFFFFFF}, {GL_LESS, 0, 0xFFFFFF},
      {GL_GREATER, 0xFFFFFF, 0}, {GL_EQUAL, 0, 0},                {GL_NOTEQUAL, 0xFFFFFF, 0xFFFFFF},
  };
  GLuint t;
  unsigned i;
  reset();
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, 2, 1, 0, GL_DEPTH_COMPONENT, GL_FLOAT, depths);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_R_TO_TEXTURE);
  glEnable(GL_TEXTURE_2D);
  glColor3f(1, 1, 1);
  for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    char what[64];
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, (GLint)cases[i].func);
    glBegin(GL_QUADS);
    glTexCoord3f(0, 0, 0.5f);
    glVertex2f(-1, -1);
    glTexCoord3f(1, 0, 0.5f);
    glVertex2f(1, -1);
    glTexCoord3f(1, 1, 0.5f);
    glVertex2f(1, 1);
    glTexCoord3f(0, 1, 0.5f);
    glVertex2f(-1, 1);
    glEnd();
    snprintf(what, sizeof what, "compare func %04x: left", cases[i].func);
    expect(what, 16, 32, cases[i].left, 0);
    snprintf(what, sizeof what, "compare func %04x: right", cases[i].func);
    expect(what, 48, 32, cases[i].right, 0);
  }
  glDisable(GL_TEXTURE_2D);
  glDeleteTextures(1, &t);
}

/* Tests the names and the version. */
static void test_names(void) {
  const char *ext = (const char *)glGetString(GL_EXTENSIONS);
  check("version 1.5", strncmp((const char *)glGetString(GL_VERSION), "1.5", 3) == 0);
  check("extension GL_ARB_vertex_buffer_object", strstr(ext, "GL_ARB_vertex_buffer_object") != NULL);
  check("extension GL_ARB_occlusion_query", strstr(ext, "GL_ARB_occlusion_query") != NULL);
  check("extension GL_EXT_shadow_funcs", strstr(ext, "GL_EXT_shadow_funcs") != NULL);
  check("proc glGenBuffersARB", sgl_get_proc("glGenBuffersARB") == (void *)glGenBuffersARB);
  check("proc glMapBuffer", sgl_get_proc("glMapBuffer") == (void *)glMapBuffer);
  check("proc glBeginQueryARB", sgl_get_proc("glBeginQueryARB") != NULL);
  check("proc glGetQueryObjectuiv", sgl_get_proc("glGetQueryObjectuiv") != NULL);
}

/* Tests that a buffer of a context lives in a context that shares
 * the objects. */
static void test_share(sgl_context *ctx) {
  sgl_context *other = sgl_create_context(ctx);
  GLuint b;
  GLint v = 0;
  check("shared context", other != NULL);
  if (!other) return;
  glGenBuffers(1, &b);
  glBindBuffer(GL_ARRAY_BUFFER, b);
  glBufferData(GL_ARRAY_BUFFER, 16, NULL, GL_DYNAMIC_DRAW);
  sgl_make_current(other);
  glBindBuffer(GL_ARRAY_BUFFER, b);
  glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &v);
  check("shared buffer", v == 16);
  sgl_make_current(ctx);
  glDeleteBuffers(1, &b);
  sgl_make_current(other);
  /* The other context keeps its binding to the deleted buffer. */
  glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &v);
  check("deleted buffer stays bound in the other context", v == 16);
  sgl_make_current(ctx);
  sgl_destroy_context(other);
  sgl_make_current(ctx);
}

/* Runs the tests. */
int main(int argc, char **argv) {
  sgl_context *ctx;
  if (argc > 1) prefix = argv[1];
  ctx = make_context(0);
  test_buffers();
  test_queries();
  test_shadow_funcs();
  test_names();
  test_share(ctx);
  {
    GLenum e = glGetError();
    if (e != GL_NO_ERROR) {
      printf("FAIL: error %04x was recorded\n", e);
      failures++;
    }
  }
  sgl_destroy_context(ctx);
  if (failures) return 1;
  printf("PASS softgl gl15\n");
  return 0;
}
