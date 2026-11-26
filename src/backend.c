/* backend.c - the back ends of the software OpenGL: the entries that let
 * a driver use hardware for clears, flat spans, primitives and the
 * display of the frame (see softgl.h and docs/backends.md).
 *
 * Part of clang-retro softgl, a software implementation of OpenGL.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include <math.h>
#include <stdlib.h>

#include "sgl.h"

/* The counter of the texture ids and serials. It is not safe for
 * threads; a race gives only one more copy of a texture. */
static unsigned long texture_counter;

/* Gives a texture a new serial: its image or a parameter changed. */
void sgl_texture_touch(sgl_texture *t) { t->serial = ++texture_counter; }

/* Gives the id and the serial of a 2D texture (see softgl.h). */
unsigned long sgl_texture_id(sgl_context *c, int u, unsigned long *serial) {
  sgl_texture *t;
  if (!c || u < 0 || u >= SGL_MAX_TEXTURE_UNITS) return 0;
  t = c->tex_2d[u];
  if (!t || !sgl_base_level(t)) return 0;
  if (!t->id) t->id = ++texture_counter;
  if (!t->serial) sgl_texture_touch(t);
  if (serial) *serial = t->serial;
  return t->id;
}

/* Finishes the work of the back end before softgl uses the buffers:
 * waits for the clears and the spans, and makes the buffers agree with
 * the hardware. */
void sgl_be_finish_work(sgl_context *c) {
  unsigned p = c->be_pending;
  c->be_pending = 0;
  if (!c->be) return;
  if ((p & SGL_BE_ASYNC) && c->be->sync) c->be->sync(c->be_user);
  if ((p & SGL_BE_HARDWARE) && c->be->software) c->be->software(c->be_user, c);
}

/* Allocates a depth or stencil buffer of bytes bytes, through the back
 * end when it has an alloc entry. zero sets all the bytes to 0. Gives
 * NULL on failure. */
void *sgl_be_alloc(sgl_context *c, size_t bytes, int zero) {
  void *p;
  if (bytes == 0) bytes = 1;
  if (!c->be || !c->be->alloc) return zero ? calloc(bytes, 1) : malloc(bytes);
  p = c->be->alloc(c->be_user, bytes);
  if (p && zero) memset(p, 0, bytes);
  return p;
}

/* Frees a buffer that sgl_be_alloc made. */
void sgl_be_free(sgl_context *c, void *p) {
  if (!p) return;
  if (c->be && c->be->alloc && c->be->release)
    c->be->release(c->be_user, p);
  else if (!c->be || !c->be->alloc)
    free(p);
}

/* Gives a context a back end (see softgl.h). The depth and stencil
 * buffers are freed: sgl_set_buffer makes them again through the new
 * back end. */
int sgl_set_backend(sgl_context *c, const sgl_backend *be, void *user) {
  if (!c) return 0;
  sgl_be_wait(c);
  sgl_be_free(c, c->depth);
  sgl_be_free(c, c->stencil);
  c->depth = NULL;
  c->stencil = NULL;
  c->depth_size = 0;
  if (c->be && c->be->detach) c->be->detach(c->be_user, c);
  c->be = be;
  c->be_user = user;
  c->be_pending = 0;
  if (c->color) return sgl_set_buffer(c, c->width, c->height, c->color, c->rowpixels);
  return 1;
}

/* Finishes the drawing and shows the frame (see softgl.h). With a
 * present entry, only the work of clear and fill_span is waited for: a
 * hardware back end shows the frame of the hardware itself. */
void sgl_present(sgl_context *c) {
  if (!c) return;
  if (c->be && c->be->present && c->color) {
    if ((c->be_pending & SGL_BE_ASYNC) && c->be->sync) c->be->sync(c->be_user);
    c->be_pending &= ~SGL_BE_ASYNC;
    c->be->present(c->be_user, c, c->color, c->width, c->height, c->rowpixels);
  } else {
    sgl_be_wait(c);
  }
}

/* Fills the window vertex of a back end from a vertex. side picks the
 * color; flat and flat_spec, when not NULL, replace the colors. */
static void hw_vertex(const sgl_context *c, const sgl_vertex *v, int side, const GLfloat *flat,
                      const GLfloat *flat_spec, sgl_hw_vertex *o) {
  const GLfloat *col = flat ? flat : v->color[side];
  const GLfloat *sp = flat_spec ? flat_spec : v->spec[side];
  int i, u;
  o->x = v->win[0];
  o->y = v->win[1];
  o->z = v->win[2];
  o->w = v->win[3];
  for (i = 0; i < 4; i++) o->color[i] = col[i];
  if (c->sec_on)
    for (i = 0; i < 3; i++) o->spec[i] = sp[i];
  else
    o->spec[0] = o->spec[1] = o->spec[2] = 0.0f;
  o->fog = v->fog;
  for (u = 0; u < 2; u++)
    for (i = 0; i < 4; i++)
      o->tex[u][i] = u < SGL_MAX_TEXTURE_UNITS ? v->tex[u][i] : (i == 3 ? 1.0f : 0.0f);
}

/* Marks that a primitive was drawn in hardware. */
static int drawn(sgl_context *c) {
  c->be_pending |= SGL_BE_HARDWARE;
  return 1;
}

/* Gives the triangle to the triangle entry of the back end. zoffset is
 * nonzero when the polygon offset applies: it is added to the window z
 * of the three vertices here. Gives 1 when the back end drew it. */
int sgl_be_triangle(sgl_context *c, const sgl_vertex *a, const sgl_vertex *b, const sgl_vertex *d,
                    int side, const GLfloat *flat, GLfloat zoffset) {
  sgl_hw_vertex v[3];
  const GLfloat *fs = flat ? c->flat_spec : NULL;
  hw_vertex(c, a, side, flat, fs, &v[0]);
  hw_vertex(c, b, side, flat, fs, &v[1]);
  hw_vertex(c, d, side, flat, fs, &v[2]);
  if (zoffset != 0.0f) {
    GLfloat x1 = v[1].x - v[0].x, y1 = v[1].y - v[0].y, x2 = v[2].x - v[0].x, y2 = v[2].y - v[0].y;
    GLfloat z1 = v[1].z - v[0].z, z2 = v[2].z - v[0].z, det = x1 * y2 - x2 * y1, m = 0.0f, o;
    int i;
    if (det != 0.0f) {
      GLfloat dzdx = fabsf((z1 * y2 - z2 * y1) / det), dzdy = fabsf((x1 * z2 - x2 * z1) / det);
      m = dzdx > dzdy ? dzdx : dzdy;
    }
    o = c->a.offset_factor * m + c->a.offset_units / (GLfloat)(1ul << (SGL_DEPTH_BITS < 24 ? SGL_DEPTH_BITS : 24));
    for (i = 0; i < 3; i++) v[i].z = sgl_clamp01(v[i].z + o);
  }
  if (!c->be->triangle(c->be_user, c, &v[0], &v[1], &v[2])) return 0;
  return drawn(c);
}

/* Gives the line to the line entry of the back end; side picks the
 * color. Gives 1 when the back end drew it. */
int sgl_be_line(sgl_context *c, const sgl_vertex *a, const sgl_vertex *b, int side,
                const GLfloat *flat) {
  sgl_hw_vertex v[2];
  const GLfloat *fs = flat ? c->flat_spec : NULL;
  hw_vertex(c, a, side, flat, fs, &v[0]);
  hw_vertex(c, b, side, flat, fs, &v[1]);
  if (!c->be->line(c->be_user, c, &v[0], &v[1])) return 0;
  return drawn(c);
}

/* Gives the point to the point entry of the back end; side picks the
 * color. Gives 1 when the back end drew it. */
int sgl_be_point(sgl_context *c, const sgl_vertex *a, int side) {
  sgl_hw_vertex v;
  hw_vertex(c, a, side, NULL, NULL, &v);
  if (!c->be->point(c->be_user, c, &v)) return 0;
  return drawn(c);
}
