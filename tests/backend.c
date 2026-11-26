/* backend.c - the tests of the back-end interface of softgl: a back end
 * in the test records the calls, and the pictures must be the same as
 * without a back end.
 *
 *     backend [prefix]
 *
 * With a prefix, each scene is also written as prefix-name.ppm.
 *
 * Part of clang-retro softgl, a software implementation of OpenGL.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include "harness.h"

/* What the back end of the test saw. */
static struct {
  int allocs, releases, clears, spans, span_pixels, syncs, triangles, lines, points;
  int software, deleted, flushes, finishes, presents, detaches;
  int busy;                  /* work that sync has not finished */
  int draw;                  /* triangle, line and point give 1 (they draw) */
  unsigned clear_mask;       /* the buffers that clear does */
  sgl_clear_rect last_clear;
  sgl_hw_vertex tri[3];
  unsigned long deleted_id;
  const uint32_t *present_pixels;
} rec;

/* Records an allocation of a depth or stencil buffer. */
static void *be_alloc(void *user, size_t bytes) {
  (void)user;
  rec.allocs++;
  return malloc(bytes);
}

/* Records the release of a buffer of be_alloc. */
static void be_release(void *user, void *p) {
  (void)user;
  rec.releases++;
  free(p);
}

/* Clears the buffers of clear_mask in the rectangle, as hardware does. */
static unsigned be_clear(void *user, const sgl_clear_rect *r) {
  unsigned done = r->mask & rec.clear_mask;
  int x, y;
  (void)user;
  rec.clears++;
  rec.last_clear = *r;
  for (y = 0; y < r->height; y++) {
    if (done & GL_COLOR_BUFFER_BIT)
      for (x = 0; x < r->width; x++) r->color[y * r->color_pitch + x] = r->color_value;
    if ((done & GL_DEPTH_BUFFER_BIT) && r->depth_bytes == 2)
      for (x = 0; x < r->width; x++) ((uint16_t *)r->depth)[y * r->depth_pitch + x] = (uint16_t)r->depth_value;
    if ((done & GL_DEPTH_BUFFER_BIT) && r->depth_bytes == 4)
      for (x = 0; x < r->width; x++) ((uint32_t *)r->depth)[y * r->depth_pitch + x] = r->depth_value;
    if (done & GL_STENCIL_BUFFER_BIT) memset(r->stencil + y * r->stencil_pitch, r->stencil_value, (size_t)r->width);
  }
  if (done) rec.busy = 1;
  return done;
}

/* Fills a span, as a blitter does, and marks the work as busy. */
static void be_fill_span(void *user, uint32_t *dst, int count, uint32_t value) {
  (void)user;
  rec.spans++;
  rec.span_pixels += count;
  rec.busy = 1;
  while (count--) *dst++ = value;
}

/* Finishes the work. */
static void be_sync(void *user) {
  (void)user;
  rec.syncs++;
  rec.busy = 0;
}

/* Records a triangle; draws it when rec.draw is set. */
static int be_triangle(void *user, sgl_context *c, const sgl_hw_vertex *a, const sgl_hw_vertex *b,
                       const sgl_hw_vertex *d) {
  (void)user, (void)c;
  rec.triangles++;
  rec.tri[0] = *a;
  rec.tri[1] = *b;
  rec.tri[2] = *d;
  return rec.draw;
}

/* Records a line; draws it when rec.draw is set. */
static int be_line(void *user, sgl_context *c, const sgl_hw_vertex *a, const sgl_hw_vertex *b) {
  (void)user, (void)c, (void)a, (void)b;
  rec.lines++;
  return rec.draw;
}

/* Records a point; draws it when rec.draw is set. */
static int be_point(void *user, sgl_context *c, const sgl_hw_vertex *a) {
  (void)user, (void)c, (void)a;
  rec.points++;
  return rec.draw;
}

/* Records that softgl draws in software after the hardware. */
static void be_software(void *user, sgl_context *c) {
  (void)user, (void)c;
  rec.software++;
}

/* Records a deleted texture. */
static void be_texture_deleted(void *user, unsigned long id) {
  (void)user;
  rec.deleted++;
  rec.deleted_id = id;
}

/* Records glFlush and glFinish. */
static void be_flush(void *user, int finish) {
  (void)user;
  if (finish)
    rec.finishes++;
  else
    rec.flushes++;
}

/* Records the display of a frame. */
static void be_present(void *user, sgl_context *c, const uint32_t *px, int w, int h, long rowpixels) {
  (void)user, (void)c, (void)w, (void)h, (void)rowpixels;
  rec.presents++;
  rec.present_pixels = px;
}

/* Records the end of the use of the back end. */
static void be_detach(void *user, sgl_context *c) {
  (void)user, (void)c;
  rec.detaches++;
}

/* The 2D back end: buffers, clear, spans and the display. */
static const sgl_backend twod = {
    .alloc = be_alloc, .release = be_release, .clear = be_clear, .fill_span = be_fill_span,
    .sync = be_sync, .flush = be_flush, .present = be_present, .detach = be_detach};

/* The 3D back end: primitives. */
static const sgl_backend threed = {
    .triangle = be_triangle, .line = be_line, .point = be_point, .software = be_software,
    .texture_deleted = be_texture_deleted, .flush = be_flush, .detach = be_detach};

/* Draws the scene of the comparison: a flat triangle, a smooth one and a
 * flat one with depth test. */
static void scene(void) {
  reset();
  glClearColor(0.25f, 0.5f, 0.75f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glShadeModel(GL_FLAT);
  glBegin(GL_TRIANGLES);
  glColor3f(1, 0, 0);
  glVertex2f(-1, -1);
  glVertex2f(0, -1);
  glVertex2f(-1, 0);
  glEnd();
  glShadeModel(GL_SMOOTH);
  glBegin(GL_TRIANGLES);
  glColor3f(1, 0, 0);
  glVertex2f(0, 0);
  glColor3f(0, 1, 0);
  glVertex2f(1, 0);
  glColor3f(0, 0, 1);
  glVertex2f(0, 1);
  glEnd();
  glEnable(GL_DEPTH_TEST);
  glColor3f(1, 1, 0);
  glBegin(GL_QUADS);
  glVertex3f(-1, 0.2f, 0);
  glVertex3f(-0.2f, 0.2f, 0);
  glVertex3f(-0.2f, 1, 0);
  glVertex3f(-1, 1, 0);
  glEnd();
  glDisable(GL_DEPTH_TEST);
}

/* Tests the 2D entries: the picture is the same as without a back end,
 * and the entries see the correct values. */
static void test_twod(sgl_context *ctx) {
  static uint32_t want[W * H];
  int spans;
  scene();
  memcpy(want, pixels, sizeof want);
  memset(&rec, 0, sizeof rec);
  rec.clear_mask = GL_COLOR_BUFFER_BIT;
  check("set the 2D back end", sgl_set_backend(ctx, &twod, NULL));
  check("the depth buffer is made through alloc", rec.allocs == 1);
  memset(pixels, 0, sizeof pixels);
  scene();
  check("clear is called", rec.clears == 2); /* reset() and scene() */
  check("clear gets color and depth", rec.last_clear.mask == (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
  check("clear gets the color value", rec.last_clear.color_value == 0xFF4080BFu);
  check("clear gets the rectangle", rec.last_clear.width == W && rec.last_clear.height == H &&
                                        rec.last_clear.color == pixels && rec.last_clear.color_pitch == W);
  check("clear gets the depth value", rec.last_clear.depth_bytes == 2 ? rec.last_clear.depth_value == 0xFFFF
                                                                       : rec.last_clear.depth_value != 0);
  check("the flat triangle is filled by spans", rec.spans > 0 && rec.span_pixels > 100);
  spans = rec.spans;
  check("the same picture with the back end", memcmp(want, pixels, sizeof want) == 0);
  dump("twod");
  /* The smooth triangle and the depth-tested quad waited for the spans. */
  check("sync before softgl draws", rec.syncs >= 2);
  /* The spans do not change for the smooth and depth-tested
   * primitives: one triangle only gave spans. */
  check("only flat primitives without tests give spans", spans <= 32);
  /* glReadPixels waits for the work. */
  {
    GLubyte px[4];
    rec.busy = 0;
    glColor3f(1, 1, 1);
    glShadeModel(GL_FLAT);
    glRectf(-1, -1, 1, 1);
    check("a white rectangle gives spans", rec.busy);
    glReadPixels(1, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    check("glReadPixels waits", !rec.busy && px[0] == 255);
  }
  /* A scissor box gives a smaller rectangle; a color mask keeps the
   * clear of the color buffer in softgl. */
  glEnable(GL_SCISSOR_TEST);
  glScissor(8, 4, 16, 10);
  glClear(GL_COLOR_BUFFER_BIT);
  check("clear of the scissor box", rec.last_clear.width == 16 && rec.last_clear.height == 10 &&
                                        rec.last_clear.color == pixels + (H - 14) * W + 8);
  expect("the back end cleared the scissor box", 8, 4, 0x4080BF, 0);
  expect("outside the scissor box", 7, 4, 0xFFFFFF, 0);
  glDisable(GL_SCISSOR_TEST);
  rec.clears = 0;
  glColorMask(GL_TRUE, GL_FALSE, GL_TRUE, GL_TRUE);
  glClear(GL_COLOR_BUFFER_BIT);
  check("a color mask clears in softgl", rec.clears == 0);
  expect("the color mask keeps green", 1, 1, 0x40FFBF, 0);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  /* The stencil buffer is made through alloc. */
  rec.allocs = 0;
  rec.clear_mask = GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT;
  glClearStencil(5);
  glClear(GL_STENCIL_BUFFER_BIT);
  check("the stencil buffer is made through alloc", rec.allocs == 1);
  check("clear gets the stencil value", rec.last_clear.mask == GL_STENCIL_BUFFER_BIT &&
                                            rec.last_clear.stencil_value == 5);
  {
    GLubyte s = 0;
    glReadPixels(3, 3, 1, 1, GL_STENCIL_INDEX, GL_UNSIGNED_BYTE, &s);
    check("the back end cleared the stencil buffer", s == 5);
  }
  /* glFlush, glFinish and sgl_present. */
  glFlush();
  glFinish();
  check("glFlush and glFinish", rec.flushes == 1 && rec.finishes == 1);
  glRectf(-1, -1, 1, 1);
  sgl_present(ctx);
  check("sgl_present waits and presents", !rec.busy && rec.presents == 1 && rec.present_pixels == pixels);
  /* No back end: the buffers are released and made again. */
  rec.releases = 0;
  check("remove the back end", sgl_set_backend(ctx, NULL, NULL));
  check("release of depth and stencil", rec.releases == 2 && rec.detaches == 1);
  scene();
  check("the same picture without the back end", memcmp(want, pixels, sizeof want) == 0);
}

/* Tests the 3D entries: the vertices, the fallback to software, and the
 * texture ids. */
static void test_threed(sgl_context *ctx) {
  GLuint tex;
  unsigned long id, serial, serial2;
  static const GLubyte img[4 * 4 * 4] = {0};
  memset(&rec, 0, sizeof rec);
  check("set the 3D back end", sgl_set_backend(ctx, &threed, NULL));
  reset();
  /* The back end draws: softgl does not. */
  rec.draw = 1;
  glColor3f(1, 0, 0);
  glBegin(GL_TRIANGLES);
  glVertex3f(-1, -1, 0.5f);
  glVertex3f(1, -1, 0.5f);
  glVertex3f(-1, 1, 0.5f);
  glEnd();
  check("the back end gets the triangle", rec.triangles == 1);
  expect("softgl does not draw it", 4, 4, 0x000000, 0);
  check("window x and y", rec.tri[0].x == 0 && rec.tri[0].y == 0 && rec.tri[1].x == W && rec.tri[2].y == H);
  check("window z", rec.tri[0].z > 0.74f && rec.tri[0].z < 0.76f);
  check("color", rec.tri[1].color[0] == 1 && rec.tri[1].color[1] == 0 && rec.tri[1].color[3] == 1);
  check("1 / w", rec.tri[2].w == 1);
  glBegin(GL_LINES);
  glVertex2f(-1, 0);
  glVertex2f(1, 0);
  glEnd();
  glBegin(GL_POINTS);
  glVertex2f(0, 0);
  glEnd();
  check("the back end gets lines and points", rec.lines == 1 && rec.points == 1);
  /* The polygon offset moves window z. */
  glEnable(GL_POLYGON_OFFSET_FILL);
  glPolygonOffset(0, 4096);
  glBegin(GL_TRIANGLES);
  glVertex3f(-1, -1, 0.5f);
  glVertex3f(1, -1, 0.5f);
  glVertex3f(-1, 1, 0.5f);
  glEnd();
  glDisable(GL_POLYGON_OFFSET_FILL);
  check("polygon offset", rec.tri[0].z > 0.7501f && rec.tri[0].z < 0.9f);
  /* softgl draws after the hardware: software is called once. */
  check("no software call yet", rec.software == 0);
  rec.draw = 0;
  glBegin(GL_TRIANGLES);
  glVertex2f(-1, -1);
  glVertex2f(1, -1);
  glVertex2f(-1, 1);
  glEnd();
  glBegin(GL_TRIANGLES);
  glVertex2f(-1, -1);
  glVertex2f(1, -1);
  glVertex2f(-1, 1);
  glEnd();
  check("software is called once", rec.software == 1);
  expect("softgl draws when the back end cannot", 4, 4, 0xFF0000, 0);
  /* The texture ids and serials. */
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  check("no id without an image", sgl_texture_id(ctx, 0, &serial) == 0);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
  id = sgl_texture_id(ctx, 0, &serial);
  check("a texture id", id != 0);
  check("the id stays", sgl_texture_id(ctx, 0, &serial2) == id && serial2 == serial);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, img);
  sgl_texture_id(ctx, 0, &serial2);
  check("glTexSubImage2D changes the serial", serial2 != serial);
  serial = serial2;
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  sgl_texture_id(ctx, 0, &serial2);
  check("glTexParameter changes the serial", serial2 != serial);
  glDeleteTextures(1, &tex);
  check("texture_deleted", rec.deleted == 1 && rec.deleted_id == id);
  check("the default texture has no image", sgl_texture_id(ctx, 0, NULL) == 0);
  sgl_set_backend(ctx, NULL, NULL);
  check("detach", rec.detaches == 1);
}

/* Tests color index mode: the flat fill writes the index. */
static void test_index(void) {
  sgl_context *ctx = make_context(8);
  memset(&rec, 0, sizeof rec);
  sgl_set_backend(ctx, &twod, NULL);
  glViewport(0, 0, W, H);
  glClearIndex(3);
  glClear(GL_COLOR_BUFFER_BIT);
  glIndexi(7);
  glShadeModel(GL_FLAT);
  glRectf(-1, -1, 0, 0);
  check("index fill by spans", rec.spans > 0);
  check("index value", pixels[(H - 1) * W] == 7 && pixels[0] == 3);
  sgl_make_current(NULL);
  sgl_destroy_context(ctx);
  check("destroy releases and detaches", rec.releases == 1 && rec.detaches == 1);
}

/* Runs the tests. */
int main(int argc, char **argv) {
  sgl_context *ctx;
  if (argc > 1) prefix = argv[1];
  ctx = make_context(0);
  test_twod(ctx);
  test_threed(ctx);
  {
    GLenum e = glGetError();
    if (e != GL_NO_ERROR) {
      printf("FAIL: error %04x was recorded\n", e);
      failures++;
    }
  }
  sgl_destroy_context(ctx);
  test_index();
  if (failures) return 1;
  printf("PASS softgl backend\n");
  return 0;
}
