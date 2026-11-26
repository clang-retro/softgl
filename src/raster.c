/* raster.c - the rasterizer of the software OpenGL: triangles, lines and
 * points become fragments, and the fragments go through the texture,
 * fog, alpha, stencil, depth and blend operations into the buffers.
 *
 * The setup of a primitive is in float. The steps along the edges and
 * the spans are in integers: colors, fog and texture coordinates in
 * 16.16 fixed point, and z in 31 bits.
 *
 * Part of clang-retro softgl, a software implementation of OpenGL.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include <math.h>
#include <stdlib.h>

#include "sgl.h"

/* The number of pixels between the exact texture coordinates of a span
 * with perspective correction. Between them the coordinates are linear. */
#define PERSP_STEP 16

/* The wrap modes of the rasterizer. GL_CLAMP is WRAP_EDGE. */
enum { WRAP_REPEAT, WRAP_EDGE, WRAP_BORDER, WRAP_MIRROR };

/* Gives the WRAP_ value of a wrap mode of glTexParameter. */
static int wrap_of(GLenum mode) {
  switch (mode) {
    case GL_REPEAT: return WRAP_REPEAT;
    case GL_CLAMP_TO_BORDER: return WRAP_BORDER;
    case GL_MIRRORED_REPEAT: return WRAP_MIRROR;
    default: return WRAP_EDGE;
  }
}

/* The state of one texture unit for one primitive. */
typedef struct tunit {
  const uint32_t *texels;
  int tw, th, wmask, hmask, wlog;
  int wrap_s, wrap_t;           /* WRAP_ values */
  uint32_t border;              /* the border color, for WRAP_BORDER */
  int linear;
  GLenum env, base;
  int envc[4];
  int one_d;                    /* a 1D texture: t is not used */
} tunit;

/* The state of the fragment operations for one primitive. */
typedef struct frag {
  sgl_context *c;
  int x0, y0, x1, y1;          /* the pixels that can change: [x0, x1) x [y0, y1) */
  int depth_test, depth_write;
  GLenum depth_func;
  int blend;
  GLenum bsrc, bdst;
  int alpha_test;
  GLenum afunc;
  int aref;
  int fog;
  int fogc[3];
  int stencil;
  int masked;                   /* the color mask is not all on */
  uint32_t keep;                /* the bits of the old pixel that the color mask keeps */
  int write_color;              /* some color is written */
  int logic;                    /* the logical operation is on */
  GLenum logic_op;
  int index_mode;               /* color index mode: rgba[0] is the index */
  uint32_t imask;               /* the index bits that can change */
  uint32_t index_max;
  int fog_index;
  int stipple;                  /* the polygon stipple is on */
  /* The textures: ntex units that are on, in the order of the units. */
  int ntex;
  int unit[SGL_MAX_TEXTURE_UNITS]; /* the texture unit of each */
  tunit tu[SGL_MAX_TEXTURE_UNITS];
} frag;

/* Fills the state of texture unit u (the k-th unit that is on) for a
 * primitive from texture t: the level of detail for the scale rho
 * (texels for each pixel), the filter, the wrap modes and the
 * environment. */
static void setup_texture(frag *f, int k, int u, const sgl_texture *t, GLfloat rho) {
  sgl_context *c = f->c;
  const sgl_unit_attrib *ua = &c->a.unit[u];
  tunit *x = &f->tu[k];
  const sgl_level *lv;
  int base = t->base_level, level = base, linear, i;
  GLenum filter;
  /* lambda = log2(rho), in the limits of GL_TEXTURE_MIN_LOD and
   * GL_TEXTURE_MAX_LOD. The logarithm is only needed when the texture
   * is minified. */
  GLfloat lambda = rho <= 1.0f ? 0.0f : logf(rho) * 1.442695f;
  if (lambda < t->min_lod) lambda = t->min_lod;
  if (lambda > t->max_lod) lambda = t->max_lod;
  if (lambda <= 0.0f) {
    filter = t->mag_filter;
  } else {
    filter = t->min_filter;
    if (filter != GL_NEAREST && filter != GL_LINEAR && t->complete) {
      /* The level of the mipmap: the nearest one to lambda. */
      int max = base;
      while (max + 1 < SGL_MAX_TEXTURE_LEVELS && max + 1 <= t->max_level && t->level[max + 1].texels) max++;
      level = base + (int)(lambda + 0.5f);
      if (filter == GL_NEAREST_MIPMAP_LINEAR || filter == GL_LINEAR_MIPMAP_LINEAR) level = base + (int)lambda;
      if (level > max) level = max;
    }
  }
  linear = filter == GL_LINEAR || filter == GL_LINEAR_MIPMAP_NEAREST || filter == GL_LINEAR_MIPMAP_LINEAR;
  lv = &t->level[level];
  f->unit[k] = u;
  x->texels = lv->texels;
  x->tw = lv->width;
  x->th = lv->height;
  x->wmask = lv->width - 1;
  x->hmask = lv->height - 1;
  x->wlog = lv->wlog;
  x->wrap_s = wrap_of(t->wrap_s);
  /* A 1D texture has one row: t does not change the texel. */
  x->wrap_t = t->target == GL_TEXTURE_1D ? WRAP_REPEAT : wrap_of(t->wrap_t);
  x->border = 0;
  for (i = 0; i < 4; i++) {
    static const int shift[4] = {16, 8, 0, 24};
    x->border |= (uint32_t)(sgl_clamp01(t->border_color[i]) * 255.0f + 0.5f) << shift[i];
  }
  x->linear = linear;
  x->env = ua->env_mode;
  x->base = lv->base;
  x->one_d = t->target == GL_TEXTURE_1D;
  x->envc[0] = (int)(ua->env_color[0] * 255.0f + 0.5f);
  x->envc[1] = (int)(ua->env_color[1] * 255.0f + 0.5f);
  x->envc[2] = (int)(ua->env_color[2] * 255.0f + 0.5f);
  x->envc[3] = (int)(ua->env_color[3] * 255.0f + 0.5f);
}

/* Fills the fragment state of a primitive from the context. */
static void setup_frag(frag *f, sgl_context *c) {
  const sgl_attrib *a = &c->a;
  f->c = c;
  f->x0 = 0;
  f->y0 = 0;
  f->x1 = c->width;
  f->y1 = c->height;
  if (a->enabled[SGL_SCISSOR_TEST]) {
    if (a->scissor[0] > f->x0) f->x0 = a->scissor[0];
    if (a->scissor[1] > f->y0) f->y0 = a->scissor[1];
    if (a->scissor[0] + a->scissor[2] < f->x1) f->x1 = a->scissor[0] + a->scissor[2];
    if (a->scissor[1] + a->scissor[3] < f->y1) f->y1 = a->scissor[1] + a->scissor[3];
  }
  f->depth_test = a->enabled[SGL_DEPTH_TEST];
  f->depth_write = f->depth_test && a->depth_mask;
  f->depth_func = a->depth_func;
  f->blend = a->enabled[SGL_BLEND] && !(a->blend_src == GL_ONE && a->blend_dst == GL_ZERO);
  f->bsrc = a->blend_src;
  f->bdst = a->blend_dst;
  f->alpha_test = a->enabled[SGL_ALPHA_TEST] && a->alpha_func != GL_ALWAYS;
  f->afunc = a->alpha_func;
  f->aref = (int)(a->alpha_ref * 255.0f + 0.5f);
  f->fog = a->enabled[SGL_FOG];
  f->fogc[0] = (int)(a->fog_color[0] * 255.0f + 0.5f);
  f->fogc[1] = (int)(a->fog_color[1] * 255.0f + 0.5f);
  f->fogc[2] = (int)(a->fog_color[2] * 255.0f + 0.5f);
  f->stencil = a->enabled[SGL_STENCIL_TEST];
  f->keep = (a->color_mask[3] ? 0 : 0xFF000000u) | (a->color_mask[0] ? 0 : 0x00FF0000u) |
            (a->color_mask[1] ? 0 : 0x0000FF00u) | (a->color_mask[2] ? 0 : 0x000000FFu);
  f->masked = f->keep != 0;
  f->write_color = f->keep != 0xFFFFFFFFu && a->draw_buffer != GL_NONE;
  f->ntex = 0;
  f->stipple = a->enabled[SGL_POLYGON_STIPPLE];
  f->index_mode = c->index_mode;
  f->logic_op = a->logic_op;
  if (c->index_mode) {
    /* Color index mode: no blending, alpha test or texture. */
    f->blend = 0;
    f->alpha_test = 0;
    f->index_max = c->index_max;
    f->imask = a->index_mask & c->index_max;
    f->write_color = f->imask != 0 && a->draw_buffer != GL_NONE;
    f->logic = a->enabled[SGL_INDEX_LOGIC_OP] && a->logic_op != GL_COPY;
    f->fog_index = (int)(a->fog_index + 0.5f);
  } else {
    f->logic = a->enabled[SGL_COLOR_LOGIC_OP] && a->logic_op != GL_COPY;
    /* The logical operation replaces blending. */
    if (f->logic) f->blend = 0;
  }
}

/* Gives the logical operation op of the source value s and the
 * destination value d. */
static inline uint32_t logic(GLenum op, uint32_t s, uint32_t d) {
  switch (op) {
    case GL_CLEAR: return 0;
    case GL_AND: return s & d;
    case GL_AND_REVERSE: return s & ~d;
    case GL_COPY: return s;
    case GL_AND_INVERTED: return ~s & d;
    case GL_NOOP: return d;
    case GL_XOR: return s ^ d;
    case GL_OR: return s | d;
    case GL_NOR: return ~(s | d);
    case GL_EQUIV: return ~(s ^ d);
    case GL_INVERT: return ~d;
    case GL_OR_REVERSE: return s | ~d;
    case GL_COPY_INVERTED: return ~s;
    case GL_OR_INVERTED: return ~s | d;
    case GL_NAND: return ~(s & d);
    default: return 0xFFFFFFFFu; /* GL_SET */
  }
}

/* Gives (a * b) / 255 for a and b in [0, 255]. */
static inline int mul255(int a, int b) {
  int32_t t = SGL_MUL16(a, b) + 128;
  return (t + (t >> 8)) >> 8;
}

/* Gives the texel index of x (any integer) by the wrap mode of a size
 * of mask + 1 texels (a power of two), or -1 for the border. */
static inline int wrap_coord(int x, int mask, int mode) {
  switch (mode) {
    case WRAP_REPEAT: return x & mask;
    case WRAP_MIRROR: {
      int p = x & (2 * mask + 1);
      return p > mask ? 2 * mask + 1 - p : p;
    }
    case WRAP_BORDER: return x < 0 || x > mask ? -1 : x;
    default: return x < 0 ? 0 : (x > mask ? mask : x);
  }
}

/* Gives the texel at x, y (texel indices from wrap_coord). */
static inline uint32_t texel_at(const tunit *f, int x, int y) {
  if ((x | y) < 0) return f->border;
  return f->texels[(y << f->wlog) + x];
}

/* Gives a texel of the texture of a unit at s, t (texel units, 16.16). */
static inline uint32_t fetch(const tunit *f, int32_t s, int32_t t) {
  if (!f->linear) {
    int x = s >> 16, y = t >> 16;
    if (f->wrap_s == WRAP_REPEAT && f->wrap_t == WRAP_REPEAT)
      return f->texels[((y & f->hmask) << f->wlog) + (x & f->wmask)];
    return texel_at(f, wrap_coord(x, f->wmask, f->wrap_s), wrap_coord(y, f->hmask, f->wrap_t));
  } else {
    int32_t u = s - 0x8000, v = t - 0x8000;
    int x0 = u >> 16, y0 = v >> 16;
    int fx = (u >> 8) & 0xFF, fy = (v >> 8) & 0xFF;
    int x1 = wrap_coord(x0 + 1, f->wmask, f->wrap_s), y1 = wrap_coord(y0 + 1, f->hmask, f->wrap_t);
    uint32_t p00, p10, p01, p11, out = 0;
    int shift;
    x0 = wrap_coord(x0, f->wmask, f->wrap_s);
    y0 = wrap_coord(y0, f->hmask, f->wrap_t);
    p00 = texel_at(f, x0, y0);
    p10 = texel_at(f, x1, y0);
    p01 = texel_at(f, x0, y1);
    p11 = texel_at(f, x1, y1);
    for (shift = 0; shift < 32; shift += 8) {
      int c00 = (int)((p00 >> shift) & 0xFF), c10 = (int)((p10 >> shift) & 0xFF);
      int c01 = (int)((p01 >> shift) & 0xFF), c11 = (int)((p11 >> shift) & 0xFF);
      /* 8-bit weights, so that each product fits in 16 bits. */
      int top = c00 + (SGL_MUL16(c10 - c00, fx) >> 8);
      int bot = c01 + (SGL_MUL16(c11 - c01, fx) >> 8);
      int v2 = top + (SGL_MUL16(bot - top, fy) >> 8);
      out |= (uint32_t)v2 << shift;
    }
    return out;
  }
}

/* Gives a + b, not more than 255. */
static inline int add255(int a, int b) { return a + b > 255 ? 255 : a + b; }

/* Combines the fragment color rgba (0 to 255) with a texel by the
 * texture environment of a unit. rgba is the result of the unit before
 * it, or the primary color for the first unit. */
static inline void texenv(const tunit *f, uint32_t texel, int rgba[4]) {
  int tr = (int)((texel >> 16) & 0xFF), tg = (int)((texel >> 8) & 0xFF);
  int tb = (int)(texel & 0xFF), ta = (int)(texel >> 24);
  switch (f->env) {
    case GL_REPLACE:
      switch (f->base) {
        case GL_ALPHA: rgba[3] = ta; break;
        case GL_RGB: case GL_LUMINANCE: rgba[0] = tr; rgba[1] = tg; rgba[2] = tb; break;
        default: rgba[0] = tr; rgba[1] = tg; rgba[2] = tb; rgba[3] = ta; break;
      }
      break;
    case GL_DECAL:
      if (f->base == GL_RGB) {
        rgba[0] = tr;
        rgba[1] = tg;
        rgba[2] = tb;
      } else if (f->base == GL_RGBA) {
        rgba[0] = mul255(rgba[0], 255 - ta) + mul255(tr, ta);
        rgba[1] = mul255(rgba[1], 255 - ta) + mul255(tg, ta);
        rgba[2] = mul255(rgba[2], 255 - ta) + mul255(tb, ta);
      }
      break;
    case GL_BLEND:
      if (f->base != GL_ALPHA) {
        rgba[0] = mul255(rgba[0], 255 - tr) + mul255(f->envc[0], tr);
        rgba[1] = mul255(rgba[1], 255 - tg) + mul255(f->envc[1], tg);
        rgba[2] = mul255(rgba[2], 255 - tb) + mul255(f->envc[2], tb);
      }
      if (f->base == GL_INTENSITY)
        rgba[3] = mul255(rgba[3], 255 - ta) + mul255(f->envc[3], ta);
      else if (f->base != GL_RGB && f->base != GL_LUMINANCE)
        rgba[3] = mul255(rgba[3], ta);
      break;
    case GL_ADD:
      if (f->base != GL_ALPHA) {
        rgba[0] = add255(rgba[0], tr);
        rgba[1] = add255(rgba[1], tg);
        rgba[2] = add255(rgba[2], tb);
      }
      if (f->base == GL_INTENSITY)
        rgba[3] = add255(rgba[3], ta);
      else if (f->base != GL_RGB && f->base != GL_LUMINANCE)
        rgba[3] = mul255(rgba[3], ta);
      break;
    default: /* GL_MODULATE */
      if (f->base != GL_ALPHA) {
        rgba[0] = mul255(rgba[0], tr);
        rgba[1] = mul255(rgba[1], tg);
        rgba[2] = mul255(rgba[2], tb);
      }
      if (f->base != GL_RGB && f->base != GL_LUMINANCE) rgba[3] = mul255(rgba[3], ta);
      break;
  }
}

/* Gives a blend factor (0 to 255) of one component. s is the source
 * color, d the destination, sa and da their alphas. */
static inline int factor(GLenum f, int s, int d, int sa, int da) {
  switch (f) {
    case GL_ZERO: return 0;
    case GL_ONE: return 255;
    case GL_SRC_COLOR: return s;
    case GL_ONE_MINUS_SRC_COLOR: return 255 - s;
    case GL_DST_COLOR: return d;
    case GL_ONE_MINUS_DST_COLOR: return 255 - d;
    case GL_SRC_ALPHA: return sa;
    case GL_ONE_MINUS_SRC_ALPHA: return 255 - sa;
    case GL_DST_ALPHA: return da;
    case GL_ONE_MINUS_DST_ALPHA: return 255 - da;
    case GL_SRC_ALPHA_SATURATE: {
      int m = 255 - da;
      return sa < m ? sa : m;
    }
    default: return 255;
  }
}

/* Gives 1 when the comparison func of a and b passes. */
static inline int compare(GLenum func, uint32_t a, uint32_t b) {
  switch (func) {
    case GL_NEVER: return 0;
    case GL_LESS: return a < b;
    case GL_EQUAL: return a == b;
    case GL_LEQUAL: return a <= b;
    case GL_GREATER: return a > b;
    case GL_NOTEQUAL: return a != b;
    case GL_GEQUAL: return a >= b;
    default: return 1;
  }
}

/* Applies a stencil operation to the value *s. */
static void stencil_op(const sgl_context *c, GLenum op, uint8_t *s) {
  uint8_t v = *s, w = (uint8_t)c->a.stencil_writemask;
  switch (op) {
    case GL_ZERO: v = 0; break;
    case GL_REPLACE: v = (uint8_t)c->a.stencil_ref; break;
    case GL_INCR: if (v < 255) v++; break;
    case GL_DECR: if (v > 0) v--; break;
    case GL_INVERT: v = (uint8_t)~v; break;
    default: return;
  }
  *s = (uint8_t)((*s & ~w) | (v & w));
}

/* Runs the stencil and depth tests of a fragment at index i of the
 * depth buffer. Gives 1 when the fragment passes both. */
static int stencil_depth(const frag *f, size_t i, uint32_t z) {
  sgl_context *c = f->c;
  uint8_t *s = &c->stencil[i];
  uint32_t m = c->a.stencil_mask & 0xFF;
  int zpass;
  if (!compare(c->a.stencil_func, (uint32_t)c->a.stencil_ref & m, *s & m)) {
    stencil_op(c, c->a.stencil_fail, s);
    return 0;
  }
  zpass = !f->depth_test ||
          compare(f->depth_func, z >> SGL_DEPTH_SHIFT, (uint32_t)c->depth[i]);
  stencil_op(c, zpass ? c->a.stencil_zpass : c->a.stencil_zfail, s);
  if (zpass && f->depth_write) c->depth[i] = (sgl_depth_t)(z >> SGL_DEPTH_SHIFT);
  return zpass;
}

/* Runs the operations after the texture on a fragment and writes it to
 * the pixel px. rgba is 0 to 255; fogf is the fog factor 0 to 255. */
static inline void finish(const frag *f, uint32_t *px, int rgba[4], int fogf) {
  uint32_t out;
  if (f->fog) {
    int g = 255 - fogf;
    rgba[0] = mul255(rgba[0], fogf) + mul255(f->fogc[0], g);
    rgba[1] = mul255(rgba[1], fogf) + mul255(f->fogc[1], g);
    rgba[2] = mul255(rgba[2], fogf) + mul255(f->fogc[2], g);
  }
  if (f->blend) {
    uint32_t d = *px;
    int dr = (int)((d >> 16) & 0xFF), dg = (int)((d >> 8) & 0xFF), db = (int)(d & 0xFF);
    int da = (int)(d >> 24), sa = rgba[3], k;
    int r = mul255(rgba[0], factor(f->bsrc, rgba[0], dr, sa, da)) +
            mul255(dr, factor(f->bdst, rgba[0], dr, sa, da));
    int g = mul255(rgba[1], factor(f->bsrc, rgba[1], dg, sa, da)) +
            mul255(dg, factor(f->bdst, rgba[1], dg, sa, da));
    int b = mul255(rgba[2], factor(f->bsrc, rgba[2], db, sa, da)) +
            mul255(db, factor(f->bdst, rgba[2], db, sa, da));
    k = f->bsrc == GL_SRC_ALPHA_SATURATE ? 255 : factor(f->bsrc, sa, da, sa, da);
    rgba[3] = mul255(sa, k) + mul255(da, factor(f->bdst, sa, da, sa, da));
    rgba[0] = r > 255 ? 255 : r;
    rgba[1] = g > 255 ? 255 : g;
    rgba[2] = b > 255 ? 255 : b;
    if (rgba[3] > 255) rgba[3] = 255;
  }
  out = ((uint32_t)rgba[3] << 24) | ((uint32_t)rgba[0] << 16) | ((uint32_t)rgba[1] << 8) | (uint32_t)rgba[2];
  if (f->logic) out = logic(f->logic_op, out, *px);
  if (f->masked) out = (out & ~f->keep) | (*px & f->keep);
  *px = out;
}

/* Runs the operations after the depth test on a fragment of color index
 * mode and writes it to the pixel px: fog, the logical operation and the
 * index mask. idx is the index; fogf is the fog factor 0 to 255. */
static void finish_index(const frag *f, uint32_t *px, int idx, int fogf) {
  uint32_t out;
  if (f->fog) idx += (int)(((int32_t)f->fog_index * (255 - fogf) + 127) / 255);
  out = (uint32_t)(idx < 0 ? 0 : idx) & f->index_max;
  if (f->logic) out = logic(f->logic_op, out, *px) & f->index_max;
  *px = ((*px & ~f->imask) | (out & f->imask)) & f->index_max;
}

/* The fixed point of the color index planes: 12 bits of fraction, so
 * that an index of 16 bits fits in 32 bits. */
#define INDEX_FRAC 12

/* Gives the index of a color index plane value, rounded. */
static inline int index_of(int32_t v) { return (v + (1 << (INDEX_FRAC - 1))) >> INDEX_FRAC; }

/* Clamps a 16.16 color to [0, 255]. */
static inline int col(int32_t v) {
  int x = v >> 16;
  return x < 0 ? 0 : (x > 255 ? 255 : x);
}

/* The values of an attribute plane: the value at the first pixel of the
 * span and the step for one pixel to the right. */
typedef struct plane {
  GLfloat a0, dx, dy;           /* value at (x0, y0) and the gradients */
} plane;

/* Computes the gradients of an attribute with values va, vb, vd at the
 * three vertices. */
static void make_plane(plane *p, const GLfloat *xy, GLfloat va, GLfloat vb, GLfloat vd,
                       GLfloat inv_det) {
  GLfloat d1 = vb - va, d2 = vd - va;
  p->dx = (d1 * xy[3] - d2 * xy[1]) * inv_det;
  p->dy = (d2 * xy[0] - d1 * xy[2]) * inv_det;
  p->a0 = va;
}

/* Gives a float as a 32-bit integer, saturated. */
static inline int32_t fix(GLfloat v) {
  if (v >= 2147483520.0f) return 0x7FFFFF80;
  if (v <= -2147483520.0f) return -0x7FFFFF80;
  return (int32_t)v;
}

/* An attribute plane in fixed point, so that a span starts without
 * floating-point arithmetic (slow on a machine without an FPU, such as
 * the 68000): the value at the center of pixel (x, y) is
 * base + dx * (x - x0) + dy * (y - y0). */
typedef struct fplane {
  int32_t base, dx, dy;
  int x0, y0;
} fplane;

/* Converts the plane p to fixed point, scaled by k. The base is the
 * value at the center of pixel (x0, y0), the pixel nearest to the
 * point where p has the value a0; ex and ey are the offsets of that
 * center from that point. The base is then small and exact in a float. */
static void make_fplane(fplane *fp, const plane *p, int x0, int y0, GLfloat ex, GLfloat ey, GLfloat k) {
  GLfloat dx = p->dx, dy = p->dy, a0 = p->a0;
  if (k != 1.0f) {
    dx *= k;
    dy *= k;
    a0 *= k;
  }
  fp->x0 = x0;
  fp->y0 = y0;
  fp->base = fix(a0 + dx * ex + dy * ey);
  fp->dx = fix(dx);
  fp->dy = fix(dy);
}

/* Gives a * n for 0 <= n < 32768 in 64 bits, with two 16-bit multiplies
 * (the 68000 has no 32-bit multiply). */
static inline int64_t mul_n(int32_t a, int n) {
  return ((int64_t)SGL_MUL16(a >> 16, n) << 16) + (int64_t)((uint32_t)(uint16_t)a * (uint32_t)(uint16_t)n);
}

/* Gives the value of the plane fp at the center of pixel (x, y), as a
 * 32-bit integer, saturated. */
static inline int32_t fplane_at(const fplane *fp, int x, int y) {
  int ddx = x - fp->x0, ddy = y - fp->y0;
  int64_t v = fp->base;
  v += ddx >= 0 ? mul_n(fp->dx, ddx) : -mul_n(fp->dx, -ddx);
  v += ddy >= 0 ? mul_n(fp->dy, ddy) : -mul_n(fp->dy, -ddy);
  if (v > 0x7FFFFF80) return 0x7FFFFF80;
  if (v < -0x7FFFFF80) return -0x7FFFFF80;
  return (int32_t)v;
}

/* The values of a triangle that the span function reads. */
typedef struct tri {
  plane z, r, g, b, al, fog, sr, sg, sb;
  plane s[SGL_MAX_TEXTURE_UNITS], t[SGL_MAX_TEXTURE_UNITS], q[SGL_MAX_TEXTURE_UNITS];
  fplane fz, fr, fg, fb, fal, ffog;  /* the planes in fixed point */
  fplane fsr, fsg, fsb;         /* the secondary color */
  fplane fs[SGL_MAX_TEXTURE_UNITS], ft[SGL_MAX_TEXTURE_UNITS];
  GLfloat ox, oy;               /* the position of vertex a */
  int smooth, persp;
  int flat[4];
  int sec;                      /* 1: add the secondary color (smooth), 2: add flat_sec */
  int flat_sec[3];
  /* Texels in the coordinates 0 to 1, for each texture of the frag. */
  GLfloat sscale[SGL_MAX_TEXTURE_UNITS], tscale[SGL_MAX_TEXTURE_UNITS];
} tri;

/* Draws the pixels [xs, xe) of row y of a triangle in color index
 * mode: the index, z and fog only. */
static void span_index(const frag *f, const tri *t, int y, int xs, int xe) {
  sgl_context *c = f->c;
  int row = c->height - 1 - y;
  uint32_t *cp = c->color + SGL_MUL16(row, c->rowpixels) + xs;
  size_t di = (size_t)SGL_MUL16(row, c->width) + (size_t)xs;
  sgl_depth_t *zp = c->depth + di;
  int32_t z = fplane_at(&t->fz, xs, y), dz = t->fz.dx;
  int32_t r = 0, dr = 0, fg = 0xFF0000, dfg = 0;
  int x, n = xe - xs;
  if (t->smooth) {
    r = fplane_at(&t->fr, xs, y);
    dr = t->fr.dx;
  }
  if (f->fog) {
    fg = fplane_at(&t->ffog, xs, y);
    dfg = t->ffog.dx;
  }
  for (x = 0; x < n; x++, cp++, zp++, di++, z += dz, r += dr, fg += dfg) {
    if (f->stipple && !((c->a.stipple[y & 31] >> ((xs + x) & 31)) & 1)) continue;
    if (c->stencil && f->stencil) {
      if (!stencil_depth(f, di, (uint32_t)z)) continue;
    } else if (f->depth_test) {
      uint32_t zz = (uint32_t)z >> SGL_DEPTH_SHIFT;
      if (!compare(f->depth_func, zz, (uint32_t)*zp)) continue;
      if (f->depth_write) *zp = (sgl_depth_t)zz;
    }
    if (f->write_color) finish_index(f, cp, t->smooth ? index_of(r) : t->flat[0], col(fg));
  }
}

/* Draws the pixels [xs, xe) of row y of a triangle. */
static void span(const frag *f, const tri *t, int y, int xs, int xe) {
  sgl_context *c = f->c;
  int row = c->height - 1 - y;
  uint32_t *cp = c->color + SGL_MUL16(row, c->rowpixels) + xs;
  size_t di = (size_t)SGL_MUL16(row, c->width) + (size_t)xs;
  sgl_depth_t *zp = c->depth + di;
  int32_t z = fplane_at(&t->fz, xs, y), dz = t->fz.dx;
  int32_t r = 0, g = 0, b = 0, al = 0, dr = 0, dg = 0, db = 0, dal = 0;
  int32_t fg = 0xFF0000, dfg = 0;
  /* Zero for the compilers that cannot see that ntex limits the use. */
  int32_t s[SGL_MAX_TEXTURE_UNITS] = {0}, tt[SGL_MAX_TEXTURE_UNITS] = {0};
  int32_t ds[SGL_MAX_TEXTURE_UNITS] = {0}, dt[SGL_MAX_TEXTURE_UNITS] = {0};
  GLfloat sw[SGL_MAX_TEXTURE_UNITS], tw[SGL_MAX_TEXTURE_UNITS], qw[SGL_MAX_TEXTURE_UNITS];
  int32_t sr = 0, sg = 0, sb = 0, dsr = 0, dsg = 0, dsb = 0;
  int x, k, n = xe - xs, left = 0, ntex = f->ntex;
  if (t->smooth) {
    r = fplane_at(&t->fr, xs, y);
    g = fplane_at(&t->fg, xs, y);
    b = fplane_at(&t->fb, xs, y);
    al = fplane_at(&t->fal, xs, y);
    dr = t->fr.dx;
    dg = t->fg.dx;
    db = t->fb.dx;
    dal = t->fal.dx;
  }
  if (f->fog) {
    fg = fplane_at(&t->ffog, xs, y);
    dfg = t->ffog.dx;
  }
  if (t->sec == 1) {
    sr = fplane_at(&t->fsr, xs, y);
    sg = fplane_at(&t->fsg, xs, y);
    sb = fplane_at(&t->fsb, xs, y);
    dsr = t->fsr.dx;
    dsg = t->fsg.dx;
    dsb = t->fsb.dx;
  }
  if (ntex && !t->persp) {
    for (k = 0; k < ntex; k++) {
      s[k] = fplane_at(&t->fs[k], xs, y);
      tt[k] = fplane_at(&t->ft[k], xs, y);
      ds[k] = t->fs[k].dx;
      dt[k] = t->ft[k].dx;
    }
  } else if (ntex) {
    /* Perspective needs a division: the coordinates stay floats. */
    GLfloat px = (GLfloat)xs + 0.5f - t->ox, py = (GLfloat)y + 0.5f - t->oy;
    for (k = 0; k < ntex; k++) {
      sw[k] = t->s[k].a0 + t->s[k].dx * px + t->s[k].dy * py;
      tw[k] = t->t[k].a0 + t->t[k].dx * px + t->t[k].dy * py;
      qw[k] = t->q[k].a0 + t->q[k].dx * px + t->q[k].dy * py;
    }
  }
  for (x = 0; x < n; x++, cp++, zp++, di++) {
    int rgba[4];
    if (t->persp && left == 0) {
      /* The exact coordinates here and PERSP_STEP pixels on; linear
       * between. */
      int m = n - x < PERSP_STEP ? n - x : PERSP_STEP;
      for (k = 0; k < ntex; k++) {
        GLfloat q0 = qw[k] != 0.0f ? 1.0f / qw[k] : 0.0f;
        GLfloat s0 = sw[k] * q0 * t->sscale[k], t0 = tw[k] * q0 * t->tscale[k], q1, s1, t1;
        sw[k] += t->s[k].dx * (GLfloat)m;
        tw[k] += t->t[k].dx * (GLfloat)m;
        qw[k] += t->q[k].dx * (GLfloat)m;
        q1 = qw[k] != 0.0f ? 1.0f / qw[k] : 0.0f;
        s1 = sw[k] * q1 * t->sscale[k];
        t1 = tw[k] * q1 * t->tscale[k];
        s[k] = fix(s0);
        tt[k] = fix(t0);
        ds[k] = fix((s1 - s0) / (GLfloat)m);
        dt[k] = fix((t1 - t0) / (GLfloat)m);
      }
      left = m;
    }
    if (f->stipple && !((c->a.stipple[y & 31] >> ((xs + x) & 31)) & 1)) goto next;
    if (c->stencil && f->stencil) {
      if (!stencil_depth(f, di, (uint32_t)z)) goto next;
    } else if (f->depth_test) {
      uint32_t zz = (uint32_t)z >> SGL_DEPTH_SHIFT;
      if (!compare(f->depth_func, zz, (uint32_t)*zp)) goto next;
      if (f->depth_write) *zp = (sgl_depth_t)zz;
    }
    if (t->smooth) {
      rgba[0] = col(r);
      rgba[1] = col(g);
      rgba[2] = col(b);
      rgba[3] = col(al);
    } else {
      rgba[0] = t->flat[0];
      rgba[1] = t->flat[1];
      rgba[2] = t->flat[2];
      rgba[3] = t->flat[3];
    }
    for (k = 0; k < ntex; k++) texenv(&f->tu[k], fetch(&f->tu[k], s[k], tt[k]), rgba);
    if (t->sec == 1) {
      rgba[0] = add255(rgba[0], col(sr));
      rgba[1] = add255(rgba[1], col(sg));
      rgba[2] = add255(rgba[2], col(sb));
    } else if (t->sec) {
      rgba[0] = add255(rgba[0], t->flat_sec[0]);
      rgba[1] = add255(rgba[1], t->flat_sec[1]);
      rgba[2] = add255(rgba[2], t->flat_sec[2]);
    }
    if (f->alpha_test && !compare(f->afunc, (uint32_t)rgba[3], (uint32_t)f->aref)) goto next;
    if (f->write_color) finish(f, cp, rgba, col(fg));
  next:
    z += dz;
    r += dr;
    g += dg;
    b += db;
    al += dal;
    fg += dfg;
    sr += dsr;
    sg += dsg;
    sb += dsb;
    for (k = 0; k < ntex; k++) {
      s[k] += ds[k];
      tt[k] += dt[k];
    }
    if (left) left--;
  }
}

/* Gives the first pixel whose center is at or right of x (or above y). */
static inline int first_pixel(GLfloat x) { return (int)ceilf(x - 0.5f); }

/* Draws a triangle (see sgl.h). side picks the front or back color;
 * flat, when not NULL, is the color of flat shading; offset is nonzero
 * when the polygon offset applies. */
void sgl_raster_triangle(sgl_context *c, const sgl_vertex *a, const sgl_vertex *b,
                         const sgl_vertex *d, int side, const GLfloat *flat, GLfloat offset) {
  frag f;
  tri t;
  const sgl_vertex *v[3], *tmp;
  GLfloat xy[4], det, inv_det, za, zb, zd;
  int ystart, yend, y;
  unsigned units;
  int flat_shade = flat != NULL; /* before a one-color triangle is made flat */
  setup_frag(&f, c);
  if (f.x0 >= f.x1 || f.y0 >= f.y1) return;
  units = sgl_texture_units(c);
  xy[0] = b->win[0] - a->win[0];
  xy[1] = b->win[1] - a->win[1];
  xy[2] = d->win[0] - a->win[0];
  xy[3] = d->win[1] - a->win[1];
  det = xy[0] * xy[3] - xy[2] * xy[1];
  if (det == 0.0f) return;
  inv_det = 1.0f / det;
  za = a->win[2] * (GLfloat)SGL_Z_MAX;
  zb = b->win[2] * (GLfloat)SGL_Z_MAX;
  zd = d->win[2] * (GLfloat)SGL_Z_MAX;
  make_plane(&t.z, xy, za, zb, zd, inv_det);
  if (offset != 0.0f) {
    GLfloat m = fabsf(t.z.dx) > fabsf(t.z.dy) ? fabsf(t.z.dx) : fabsf(t.z.dy);
    GLfloat o = c->a.offset_factor * m +
                c->a.offset_units * (GLfloat)(1u << (31 - SGL_DEPTH_BITS + (SGL_DEPTH_BITS == 32 ? 8 : 0)));
    t.z.a0 += o;
  }
  t.ox = a->win[0];
  t.oy = a->win[1];
  t.smooth = flat == NULL;
  if (t.smooth && c->index_mode) {
    const GLfloat k = (GLfloat)(1 << INDEX_FRAC);
    make_plane(&t.r, xy, a->color[side][0] * k, b->color[side][0] * k, d->color[side][0] * k, inv_det);
    if (t.r.dx == 0 && t.r.dy == 0) {
      t.smooth = 0;
      flat = a->color[side];
    }
  } else if (t.smooth) {
    const GLfloat *ca = a->color[side], *cb = b->color[side], *cd = d->color[side];
    const GLfloat k = 255.0f * 65536.0f;
    make_plane(&t.r, xy, ca[0] * k, cb[0] * k, cd[0] * k, inv_det);
    make_plane(&t.g, xy, ca[1] * k, cb[1] * k, cd[1] * k, inv_det);
    make_plane(&t.b, xy, ca[2] * k, cb[2] * k, cd[2] * k, inv_det);
    make_plane(&t.al, xy, ca[3] * k, cb[3] * k, cd[3] * k, inv_det);
    /* A triangle of one color is drawn flat: it is faster. */
    if (t.r.dx == 0 && t.r.dy == 0 && t.g.dx == 0 && t.g.dy == 0 && t.b.dx == 0 && t.b.dy == 0 &&
        t.al.dx == 0 && t.al.dy == 0) {
      t.smooth = 0;
      flat = ca;
    }
  }
  if (!t.smooth && c->index_mode) {
    t.flat[0] = (int)(flat[0] + 0.5f);
  } else if (!t.smooth) {
    int i;
    for (i = 0; i < 4; i++) t.flat[i] = (int)(sgl_clamp01(flat[i]) * 255.0f + 0.5f);
  }
  if (f.fog) {
    const GLfloat k = 255.0f * 65536.0f;
    make_plane(&t.fog, xy, a->fog * k, b->fog * k, d->fog * k, inv_det);
  }
  t.sec = 0;
  if (c->sec_on && flat_shade) {
    int i;
    t.sec = 2;
    for (i = 0; i < 3; i++) t.flat_sec[i] = (int)(sgl_clamp01(c->flat_spec[i]) * 255.0f + 0.5f);
  } else if (c->sec_on) {
    const GLfloat *ca = a->spec[side], *cb = b->spec[side], *cd = d->spec[side];
    const GLfloat k = 255.0f * 65536.0f;
    t.sec = 1;
    make_plane(&t.sr, xy, ca[0] * k, cb[0] * k, cd[0] * k, inv_det);
    make_plane(&t.sg, xy, ca[1] * k, cb[1] * k, cd[1] * k, inv_det);
    make_plane(&t.sb, xy, ca[2] * k, cb[2] * k, cd[2] * k, inv_det);
  }
  t.persp = 0;
  if (units) {
    /* The coordinates s / w, t / w and q / w are linear in the window. */
    GLfloat wa = a->win[3], wb = b->win[3], wd = d->win[3];
    sgl_texture *texs[SGL_MAX_TEXTURE_UNITS];
    int us[SGL_MAX_TEXTURE_UNITS], n = 0, u, k;
    int persp = fabsf(wa - wb) > 0.001f * fabsf(wa) || fabsf(wa - wd) > 0.001f * fabsf(wa);
    for (u = 0; u < SGL_MAX_TEXTURE_UNITS; u++) {
      sgl_texture *tex;
      if (!(units & (1u << u))) continue;
      tex = sgl_unit_texture(c, u);
      if (!sgl_base_level(tex)) continue;
      if (a->tex[u][3] != b->tex[u][3] || a->tex[u][3] != d->tex[u][3]) persp = 1;
      texs[n] = tex;
      us[n++] = u;
    }
    t.persp = persp && n > 0 && c->a.hint_perspective != GL_FASTEST;
    for (k = 0; k < n; k++) {
      const GLfloat *ta = a->tex[us[k]], *tb = b->tex[us[k]], *td = d->tex[us[k]];
      sgl_texture *tex = texs[k];
      GLfloat rho, dsdx, dsdy, dtdx, dtdy, qc;
      if (t.persp) {
        make_plane(&t.s[k], xy, ta[0] * wa, tb[0] * wb, td[0] * wd, inv_det);
        make_plane(&t.t[k], xy, ta[1] * wa, tb[1] * wb, td[1] * wd, inv_det);
        make_plane(&t.q[k], xy, ta[3] * wa, tb[3] * wb, td[3] * wd, inv_det);
        /* The rate of change at the middle of the triangle, for the
         * level of the mipmap. */
        qc = (ta[3] * wa + tb[3] * wb + td[3] * wd) / 3.0f;
        if (qc == 0.0f) qc = 1.0f;
        dsdx = t.s[k].dx / qc;
        dsdy = t.s[k].dy / qc;
        dtdx = t.t[k].dx / qc;
        dtdy = t.t[k].dy / qc;
      } else {
        GLfloat qa = ta[3] != 0.0f ? 1.0f / ta[3] : 1.0f;
        make_plane(&t.s[k], xy, ta[0] * qa, tb[0] * qa, td[0] * qa, inv_det);
        make_plane(&t.t[k], xy, ta[1] * qa, tb[1] * qa, td[1] * qa, inv_det);
        dsdx = t.s[k].dx;
        dsdy = t.s[k].dy;
        dtdx = t.t[k].dx;
        dtdy = t.t[k].dy;
      }
      {
        const sgl_level *bl = sgl_base_level(tex);
        GLfloat w = (GLfloat)bl->width, h = (GLfloat)bl->height;
        GLfloat rx = dsdx * dsdx * w * w + dtdx * dtdx * h * h;
        GLfloat ry = dsdy * dsdy * w * w + dtdy * dtdy * h * h;
        rho = sqrtf(rx > ry ? rx : ry);
      }
      setup_texture(&f, k, us[k], tex, rho);
      t.sscale[k] = (GLfloat)f.tu[k].tw * 65536.0f;
      t.tscale[k] = f.tu[k].one_d ? 0.0f : (GLfloat)f.tu[k].th * 65536.0f;
    }
    f.ntex = n;
  }
  /* The planes in fixed point, for the spans, from the pixel nearest
   * to vertex a. */
  {
    int x0 = (int)floorf(t.ox), y0 = (int)floorf(t.oy);
    GLfloat ex = (GLfloat)x0 + 0.5f - t.ox, ey = (GLfloat)y0 + 0.5f - t.oy;
    make_fplane(&t.fz, &t.z, x0, y0, ex, ey, 1.0f);
    if (t.smooth) {
      make_fplane(&t.fr, &t.r, x0, y0, ex, ey, 1.0f);
      if (!c->index_mode) {
        make_fplane(&t.fg, &t.g, x0, y0, ex, ey, 1.0f);
        make_fplane(&t.fb, &t.b, x0, y0, ex, ey, 1.0f);
        make_fplane(&t.fal, &t.al, x0, y0, ex, ey, 1.0f);
      }
    }
    if (f.fog) make_fplane(&t.ffog, &t.fog, x0, y0, ex, ey, 1.0f);
    if (t.sec == 1) {
      make_fplane(&t.fsr, &t.sr, x0, y0, ex, ey, 1.0f);
      make_fplane(&t.fsg, &t.sg, x0, y0, ex, ey, 1.0f);
      make_fplane(&t.fsb, &t.sb, x0, y0, ex, ey, 1.0f);
    }
    if (!t.persp) {
      int k;
      for (k = 0; k < f.ntex; k++) {
        make_fplane(&t.fs[k], &t.s[k], x0, y0, ex, ey, t.sscale[k]);
        make_fplane(&t.ft[k], &t.t[k], x0, y0, ex, ey, t.tscale[k]);
      }
    }
  }
  /* Sort by y, bottom first. */
  v[0] = a;
  v[1] = b;
  v[2] = d;
  if (v[1]->win[1] < v[0]->win[1]) tmp = v[0], v[0] = v[1], v[1] = tmp;
  if (v[2]->win[1] < v[1]->win[1]) tmp = v[1], v[1] = v[2], v[2] = tmp;
  if (v[1]->win[1] < v[0]->win[1]) tmp = v[0], v[0] = v[1], v[1] = tmp;
  ystart = first_pixel(v[0]->win[1]);
  yend = first_pixel(v[2]->win[1]);
  if (ystart < f.y0) ystart = f.y0;
  if (yend > f.y1) yend = f.y1;
  if (ystart >= yend) return;
  {
    /* The long edge from v[0] to v[2], and the short edges. */
    GLfloat x0 = v[0]->win[0], y0 = v[0]->win[1];
    GLfloat x1 = v[1]->win[0], y1 = v[1]->win[1];
    GLfloat x2 = v[2]->win[0], y2 = v[2]->win[1];
    GLfloat dlong = (x2 - x0) / (y2 - y0);
    GLfloat dtop = y2 - y1 != 0.0f ? (x2 - x1) / (y2 - y1) : 0.0f;
    GLfloat dbot = y1 - y0 != 0.0f ? (x1 - x0) / (y1 - y0) : 0.0f;
    /* The long edge is on the left when v[1] is right of it. */
    int long_left = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0) > 0.0f;
    /* Step the edges in 16.16 from the center of the first row. */
    for (y = ystart; y < yend;) {
      GLfloat yc = (GLfloat)y + 0.5f;
      int seg_end = yc < y1 ? first_pixel(y1) : yend;
      GLfloat xs_short, ds_short;
      int32_t xl, xr, dxl, dxr;
      if (seg_end > yend) seg_end = yend;
      if (yc < y1) {
        xs_short = x0 + (yc - y0) * dbot;
        ds_short = dbot;
      } else {
        xs_short = x1 + (yc - y1) * dtop;
        ds_short = dtop;
      }
      {
        GLfloat xlong = x0 + (yc - y0) * dlong;
        if (long_left) {
          xl = fix(xlong * 65536.0f);
          dxl = fix(dlong * 65536.0f);
          xr = fix(xs_short * 65536.0f);
          dxr = fix(ds_short * 65536.0f);
        } else {
          xl = fix(xs_short * 65536.0f);
          dxl = fix(ds_short * 65536.0f);
          xr = fix(xlong * 65536.0f);
          dxr = fix(dlong * 65536.0f);
        }
      }
      for (; y < seg_end; y++) {
        int xs = (xl - 0x8000 + 0xFFFF) >> 16, xe = (xr - 0x8000 + 0xFFFF) >> 16;
        if (xs < f.x0) xs = f.x0;
        if (xe > f.x1) xe = f.x1;
        if (xs < xe) (f.index_mode ? span_index : span)(&f, &t, y, xs, xe);
        xl += dxl;
        xr += dxr;
      }
    }
  }
}

/* Writes one fragment with fixed attributes: for points, lines and
 * pixel rectangles. tex, when not NULL, has the texture coordinates
 * (s, t, q) of each texture of f. */
static void plot(const frag *f, const tri *t, int x, int y, uint32_t z, const int rgba_in[4],
                 int fogf, const GLfloat (*tex)[3]) {
  sgl_context *c = f->c;
  long row;
  size_t di;
  int rgba[4], k;
  if (x < f->x0 || x >= f->x1 || y < f->y0 || y >= f->y1) return;
  row = (long)(c->height - 1 - y);
  di = (size_t)row * (size_t)c->width + (size_t)x;
  if (c->stencil && f->stencil) {
    if (!stencil_depth(f, di, z)) return;
  } else if (f->depth_test) {
    uint32_t zz = z >> SGL_DEPTH_SHIFT;
    if (!compare(f->depth_func, zz, (uint32_t)c->depth[di])) return;
    if (f->depth_write) c->depth[di] = (sgl_depth_t)zz;
  }
  if (f->index_mode) {
    if (f->write_color) finish_index(f, c->color + row * c->rowpixels + x, rgba_in[0], fogf);
    return;
  }
  rgba[0] = rgba_in[0];
  rgba[1] = rgba_in[1];
  rgba[2] = rgba_in[2];
  rgba[3] = rgba_in[3];
  if (tex)
    for (k = 0; k < f->ntex; k++) {
      GLfloat q = tex[k][2] != 0.0f ? 1.0f / tex[k][2] : 1.0f;
      texenv(&f->tu[k], fetch(&f->tu[k], fix(tex[k][0] * q * t->sscale[k]), fix(tex[k][1] * q * t->tscale[k])),
             rgba);
    }
  if (t && t->sec) {
    rgba[0] = add255(rgba[0], t->flat_sec[0]);
    rgba[1] = add255(rgba[1], t->flat_sec[1]);
    rgba[2] = add255(rgba[2], t->flat_sec[2]);
  }
  if (f->alpha_test && !compare(f->afunc, (uint32_t)rgba[3], (uint32_t)f->aref)) return;
  if (f->write_color) finish(f, c->color + row * c->rowpixels + x, rgba, fogf);
}

/* Fills the texture state of a point or a line from the context: the
 * units that are on, at level 0 of their textures. */
static void setup_simple_texture(frag *f, tri *t) {
  sgl_context *c = f->c;
  unsigned units = sgl_texture_units(c);
  int u;
  for (u = 0; u < SGL_MAX_TEXTURE_UNITS; u++) {
    sgl_texture *tex;
    int k = f->ntex;
    if (!(units & (1u << u))) continue;
    tex = sgl_unit_texture(c, u);
    if (!sgl_base_level(tex)) continue;
    setup_texture(f, k, u, tex, 1.0f);
    t->sscale[k] = (GLfloat)f->tu[k].tw * 65536.0f;
    t->tscale[k] = f->tu[k].one_d ? 0.0f : (GLfloat)f->tu[k].th * 65536.0f;
    f->ntex = k + 1;
  }
}

/* Gives a color 0 to 1 as an integer 0 to 255. */
static inline int byte(GLfloat v) { return (int)(sgl_clamp01(v) * 255.0f + 0.5f); }

/* Draws a line (see sgl.h). */
void sgl_raster_line(sgl_context *c, const sgl_vertex *a, const sgl_vertex *b, int side,
                     const GLfloat *flat, GLfloat offset) {
  frag f;
  tri t;
  GLfloat dx = b->win[0] - a->win[0], dy = b->win[1] - a->win[1];
  GLfloat len = fabsf(dx) > fabsf(dy) ? fabsf(dx) : fabsf(dy);
  int steps, i, w, k, xmajor = fabsf(dx) >= fabsf(dy);
  int stipple = c->a.enabled[SGL_LINE_STIPPLE];
  (void)offset;
  setup_frag(&f, c);
  if (f.x0 >= f.x1 || f.y0 >= f.y1) return;
  setup_simple_texture(&f, &t);
  t.sec = c->sec_on ? 2 : 0;
  steps = (int)(len + 0.5f);
  if (steps < 1) steps = 1;
  w = (int)(c->a.line_width + 0.5f);
  if (w < 1) w = 1;
  for (i = 0; i < steps; i++) {
    GLfloat u = ((GLfloat)i + 0.5f) / (GLfloat)steps;
    GLfloat x = a->win[0] + dx * u, y = a->win[1] + dy * u;
    GLfloat z = a->win[2] + (b->win[2] - a->win[2]) * u;
    int rgba[4], fogf;
    GLfloat tc[SGL_MAX_TEXTURE_UNITS][3];
    if (stipple) {
      int bit = (i / c->a.line_repeat) & 15;
      if (!((c->a.line_pattern >> bit) & 1)) continue;
    }
    if (c->index_mode) {
      const GLfloat *ca = a->color[side], *cb = b->color[side];
      rgba[0] = (int)((flat ? flat[0] : ca[0] + (cb[0] - ca[0]) * u) + 0.5f);
    } else if (flat) {
      rgba[0] = byte(flat[0]);
      rgba[1] = byte(flat[1]);
      rgba[2] = byte(flat[2]);
      rgba[3] = byte(flat[3]);
    } else {
      const GLfloat *ca = a->color[side], *cb = b->color[side];
      rgba[0] = byte(ca[0] + (cb[0] - ca[0]) * u);
      rgba[1] = byte(ca[1] + (cb[1] - ca[1]) * u);
      rgba[2] = byte(ca[2] + (cb[2] - ca[2]) * u);
      rgba[3] = byte(ca[3] + (cb[3] - ca[3]) * u);
    }
    fogf = byte(a->fog + (b->fog - a->fog) * u);
    if (t.sec) {
      int i2;
      for (i2 = 0; i2 < 3; i2++)
        t.flat_sec[i2] = byte(flat ? c->flat_spec[i2] : a->spec[side][i2] + (b->spec[side][i2] - a->spec[side][i2]) * u);
    }
    for (k = 0; k < f.ntex; k++) {
      const GLfloat *ta = a->tex[f.unit[k]], *tb = b->tex[f.unit[k]];
      tc[k][0] = ta[0] + (tb[0] - ta[0]) * u;
      tc[k][1] = ta[1] + (tb[1] - ta[1]) * u;
      tc[k][2] = ta[3] + (tb[3] - ta[3]) * u;
    }
    for (k = 0; k < w; k++) {
      int o = k - (w - 1) / 2;
      int px = (int)floorf(x) + (xmajor ? 0 : o), py = (int)floorf(y) + (xmajor ? o : 0);
      plot(&f, &t, px, py, (uint32_t)fix(sgl_clamp01(z) * (GLfloat)SGL_Z_MAX), rgba, fogf, tc);
    }
  }
}

/* Draws a point (see sgl.h). */
void sgl_raster_point(sgl_context *c, const sgl_vertex *a, int side, GLfloat offset) {
  frag f;
  tri t;
  int size = (int)(c->a.point_size + 0.5f), x0, y0, x, y, k, rgba[4];
  GLfloat tc[SGL_MAX_TEXTURE_UNITS][3];
  uint32_t z = (uint32_t)fix(sgl_clamp01(a->win[2]) * (GLfloat)SGL_Z_MAX);
  (void)offset;
  setup_frag(&f, c);
  if (f.x0 >= f.x1 || f.y0 >= f.y1) return;
  setup_simple_texture(&f, &t);
  t.sec = c->sec_on ? 2 : 0;
  if (t.sec) {
    t.flat_sec[0] = byte(a->spec[side][0]);
    t.flat_sec[1] = byte(a->spec[side][1]);
    t.flat_sec[2] = byte(a->spec[side][2]);
  }
  if (size < 1) size = 1;
  x0 = (int)floorf(a->win[0] - (GLfloat)(size - 1) * 0.5f);
  y0 = (int)floorf(a->win[1] - (GLfloat)(size - 1) * 0.5f);
  rgba[0] = byte(a->color[side][0]);
  rgba[1] = byte(a->color[side][1]);
  rgba[2] = byte(a->color[side][2]);
  rgba[3] = byte(a->color[side][3]);
  if (c->index_mode) rgba[0] = (int)(a->color[side][0] + 0.5f);
  for (k = 0; k < f.ntex; k++) {
    tc[k][0] = a->tex[f.unit[k]][0];
    tc[k][1] = a->tex[f.unit[k]][1];
    tc[k][2] = a->tex[f.unit[k]][3];
  }
  for (y = y0; y < y0 + size; y++)
    for (x = x0; x < x0 + size; x++) plot(&f, &t, x, y, z, rgba, byte(a->fog), tc);
}

/* Writes one fragment of a pixel rectangle (glDrawPixels, glBitmap) at
 * window position x, y with window z z (0 to SGL_Z_MAX) and color rgba
 * (0 to 1). */
void sgl_write_fragment(sgl_context *c, int x, int y, uint32_t z, const GLfloat rgba[4]) {
  frag f;
  int v[4];
  setup_frag(&f, c);
  v[0] = byte(rgba[0]);
  v[1] = byte(rgba[1]);
  v[2] = byte(rgba[2]);
  v[3] = byte(rgba[3]);
  if (c->index_mode) v[0] = (int)(rgba[0] + 0.5f);
  plot(&f, NULL, x, y, z, v, 255, NULL);
}

/* Narrows [x0, x1) x [y0, y1) to the pixels that can change: the buffer
 * and the scissor box. */
void sgl_clip_rect(sgl_context *c, int *x0, int *y0, int *x1, int *y1) {
  frag f;
  setup_frag(&f, c);
  if (*x0 < f.x0) *x0 = f.x0;
  if (*y0 < f.y0) *y0 = f.y0;
  if (*x1 > f.x1) *x1 = f.x1;
  if (*y1 > f.y1) *y1 = f.y1;
}
