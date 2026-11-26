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

/* The kinds of fetch of a texture unit. */
enum { FETCH_PLAIN, FETCH_3D, FETCH_CUBE, FETCH_DEPTH };

/* The state of one texture unit for one primitive. */
typedef struct tunit {
  int kind;                     /* a FETCH_ value */
  const uint32_t *texels;
  int tw, th, wmask, hmask, wlog;
  int hlog, dmask;              /* a 3D texture: images of 1 << (wlog + hlog) texels */
  int wrap_s, wrap_t, wrap_r;   /* WRAP_ values */
  uint32_t border;              /* the border color, for WRAP_BORDER */
  int linear;
  GLenum env, base;
  int envc[4];
  int one_d;                    /* a 1D texture: t is not used */
  int three_d;                  /* a 3D texture: r selects the image */
  int cube;                     /* a cube map: s, t and r select the face and the texel */
  const uint32_t *face[6];      /* a cube map: the texels of each face */
  /* A depth texture (OpenGL 1.4): the texels are depths of
   * SGL_TEX_DEPTH_MAX. cmp is the function of GL_TEXTURE_COMPARE_FUNC
   * when the compare mode is GL_COMPARE_R_TO_TEXTURE, or 0. */
  int depth;
  GLenum cmp;
  uint32_t border_depth;
  /* GL_COMBINE: the functions, the sources (SRC_ values or the index of
   * a texture of the frag) and the operands of the arguments, and the
   * scales as shifts. */
  GLenum crgb, calpha;
  int srgb[3], salpha[3];
  GLenum orgb[3], oalpha[3];
  int rgb_shift, alpha_shift;
} tunit;

/* The sources of GL_COMBINE that are not textures. A source that is a
 * texture is the index of its texture in the frag (0 and more). */
enum { SRC_CONSTANT = -1, SRC_PRIMARY = -2, SRC_PREVIOUS = -3, SRC_WHITE = -4 };

/* The state of the fragment operations for one primitive. */
typedef struct frag {
  sgl_context *c;
  int x0, y0, x1, y1;          /* the pixels that can change: [x0, x1) x [y0, y1) */
  int depth_test, depth_write;
  GLenum depth_func;
  int blend;
  GLenum bsrc, bdst, bsrc_a, bdst_a; /* the factors of RGB and of alpha */
  GLenum beq;                   /* the blend equation */
  int bc[4];                    /* the constant color of blending, 0 to 255 */
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
  int combine;                  /* a texture uses GL_COMBINE */
  int late_z;                   /* the alpha test is on: the stencil and depth
                                 * tests come after it */
  GLuint *count;                /* OpenGL 1.5: the counter of the active
                                 * occlusion query, or NULL */
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
  /* lambda = log2(rho) + the bias of the texture and of the unit, in
   * the limits of GL_TEXTURE_MIN_LOD and GL_TEXTURE_MAX_LOD. Without a
   * bias, the logarithm is only needed when the texture is minified. */
  GLfloat bias = t->lod_bias + ua->lod_bias, lambda;
  if (bias > SGL_MAX_LOD_BIAS) bias = SGL_MAX_LOD_BIAS;
  if (bias < -SGL_MAX_LOD_BIAS) bias = -SGL_MAX_LOD_BIAS;
  if (bias == 0.0f)
    lambda = rho <= 1.0f ? 0.0f : logf(rho) * 1.442695f;
  else
    lambda = (rho > 0.0f ? logf(rho) * 1.442695f : -2.0f * SGL_MAX_LOD_BIAS) + bias;
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
  x->wrap_r = wrap_of(t->wrap_r);
  x->hlog = lv->hlog;
  x->dmask = lv->depth - 1;
  x->three_d = t->target == GL_TEXTURE_3D;
  x->cube = t->target == GL_TEXTURE_CUBE_MAP;
  if (x->cube) {
    /* sgl_base_level and t->complete make sure that each face has the
     * level. */
    x->face[0] = lv->texels;
    for (i = 1; i < 6; i++) x->face[i] = t->faces[i - 1][level].texels;
  }
  x->border = 0;
  for (i = 0; i < 4; i++) {
    static const int shift[4] = {16, 8, 0, 24};
    x->border |= (uint32_t)(sgl_clamp01(t->border_color[i]) * 255.0f + 0.5f) << shift[i];
  }
  x->linear = linear;
  x->env = ua->env_mode;
  x->base = lv->base;
  x->one_d = t->target == GL_TEXTURE_1D;
  x->depth = lv->base == GL_DEPTH_COMPONENT;
  x->cmp = 0;
  if (x->depth) {
    /* The texel of a depth texture is a luminance, an intensity or an
     * alpha (GL_DEPTH_TEXTURE_MODE). */
    x->base = t->depth_mode;
    if (t->compare_mode == GL_COMPARE_R_TO_TEXTURE) x->cmp = t->compare_func;
    x->border_depth = (uint32_t)(sgl_clamp01(t->border_color[0]) * (GLfloat)SGL_TEX_DEPTH_MAX);
  }
  x->kind = x->three_d ? FETCH_3D : x->cube ? FETCH_CUBE : x->depth ? FETCH_DEPTH : FETCH_PLAIN;
  x->envc[0] = (int)(ua->env_color[0] * 255.0f + 0.5f);
  x->envc[1] = (int)(ua->env_color[1] * 255.0f + 0.5f);
  x->envc[2] = (int)(ua->env_color[2] * 255.0f + 0.5f);
  x->envc[3] = (int)(ua->env_color[3] * 255.0f + 0.5f);
  if (x->env == GL_COMBINE) {
    x->crgb = ua->combine_rgb;
    x->calpha = ua->combine_alpha;
    for (i = 0; i < 3; i++) {
      x->orgb[i] = ua->op_rgb[i];
      x->oalpha[i] = ua->op_alpha[i];
    }
    x->rgb_shift = ua->rgb_scale == 4.0f ? 2 : ua->rgb_scale == 2.0f ? 1 : 0;
    x->alpha_shift = ua->alpha_scale == 4.0f ? 2 : ua->alpha_scale == 2.0f ? 1 : 0;
  }
}

/* Gives the SRC_ value or the texture index of a source of GL_COMBINE
 * of texture k of f. */
static int source_of(const frag *f, int k, GLenum src) {
  int i;
  switch (src) {
    case GL_TEXTURE: return k;
    case GL_CONSTANT: return SRC_CONSTANT;
    case GL_PRIMARY_COLOR: return SRC_PRIMARY;
    case GL_PREVIOUS: return SRC_PREVIOUS;
    default:
      /* GL_TEXTUREn: the texture of unit n, when it is on. */
      for (i = 0; i < f->ntex; i++)
        if ((GLenum)(GL_TEXTURE0 + f->unit[i]) == src) return i;
      return SRC_WHITE;
  }
}

/* Completes the texture state of a primitive when all the textures are
 * set up: the sources of GL_COMBINE, which can name other units. */
static void finish_textures(frag *f) {
  int k, i;
  f->combine = 0;
  for (k = 0; k < f->ntex; k++) {
    tunit *x = &f->tu[k];
    const sgl_unit_attrib *ua = &f->c->a.unit[f->unit[k]];
    if (x->env != GL_COMBINE) continue;
    f->combine = 1;
    for (i = 0; i < 3; i++) {
      x->srgb[i] = source_of(f, k, ua->src_rgb[i]);
      x->salpha[i] = source_of(f, k, ua->src_alpha[i]);
    }
  }
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
  /* Blending that gives the source is off. */
  f->blend = a->enabled[SGL_BLEND] &&
             !(a->blend_eq == GL_FUNC_ADD && a->blend_src == GL_ONE && a->blend_dst == GL_ZERO &&
               a->blend_src_alpha == GL_ONE && a->blend_dst_alpha == GL_ZERO);
  f->bsrc = a->blend_src;
  f->bdst = a->blend_dst;
  f->bsrc_a = a->blend_src_alpha;
  f->bdst_a = a->blend_dst_alpha;
  f->beq = a->blend_eq;
  f->bc[0] = (int)(a->blend_color[0] * 255.0f + 0.5f);
  f->bc[1] = (int)(a->blend_color[1] * 255.0f + 0.5f);
  f->bc[2] = (int)(a->blend_color[2] * 255.0f + 0.5f);
  f->bc[3] = (int)(a->blend_color[3] * 255.0f + 0.5f);
  f->alpha_test = a->enabled[SGL_ALPHA_TEST] && a->alpha_func != GL_ALWAYS;
  f->afunc = a->alpha_func;
  f->aref = (int)(a->alpha_ref * 255.0f + 0.5f);
  f->fog = a->enabled[SGL_FOG];
  f->fogc[0] = (int)(a->fog_color[0] * 255.0f + 0.5f);
  f->fogc[1] = (int)(a->fog_color[1] * 255.0f + 0.5f);
  f->fogc[2] = (int)(a->fog_color[2] * 255.0f + 0.5f);
  /* The stencil buffer is made on first use: when the test is on before
   * a glClear of the stencil buffer, it is all 0. */
  f->stencil = a->enabled[SGL_STENCIL_TEST] && sgl_need_stencil(c);
  f->keep = (a->color_mask[3] ? 0 : 0xFF000000u) | (a->color_mask[0] ? 0 : 0x00FF0000u) |
            (a->color_mask[1] ? 0 : 0x0000FF00u) | (a->color_mask[2] ? 0 : 0x000000FFu);
  f->masked = f->keep != 0;
  f->write_color = f->keep != 0xFFFFFFFFu && a->draw_buffer != GL_NONE;
  f->ntex = 0;
  f->combine = 0;
  f->count = c->query ? &c->query->result : NULL;
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
  /* The stencil and the depth tests come after the alpha test. Without
   * the alpha test, they come first: a fragment that fails them gets
   * no texture. */
  f->late_z = f->alpha_test;
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

/* Gives the texel at x, y (texel indices from wrap_coord) of the image
 * img of a unit. */
static inline uint32_t texel_at(const tunit *f, const uint32_t *img, int x, int y) {
  if ((x | y) < 0) return f->border;
  return img[(y << f->wlog) + x];
}

/* Gives a texel of image img of the texture of a unit at s, t (texel
 * units, 16.16). */
static inline uint32_t fetch_at(const tunit *f, const uint32_t *img, int32_t s, int32_t t) {
  if (!f->linear) {
    int x = s >> 16, y = t >> 16;
    if (f->wrap_s == WRAP_REPEAT && f->wrap_t == WRAP_REPEAT)
      return img[((y & f->hmask) << f->wlog) + (x & f->wmask)];
    return texel_at(f, img, wrap_coord(x, f->wmask, f->wrap_s), wrap_coord(y, f->hmask, f->wrap_t));
  } else {
    int32_t u = s - 0x8000, v = t - 0x8000;
    int x0 = u >> 16, y0 = v >> 16;
    int fx = (u >> 8) & 0xFF, fy = (v >> 8) & 0xFF;
    int x1 = wrap_coord(x0 + 1, f->wmask, f->wrap_s), y1 = wrap_coord(y0 + 1, f->hmask, f->wrap_t);
    uint32_t p00, p10, p01, p11, out = 0;
    int shift;
    x0 = wrap_coord(x0, f->wmask, f->wrap_s);
    y0 = wrap_coord(y0, f->hmask, f->wrap_t);
    p00 = texel_at(f, img, x0, y0);
    p10 = texel_at(f, img, x1, y0);
    p01 = texel_at(f, img, x0, y1);
    p11 = texel_at(f, img, x1, y1);
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

/* Gives the image z of a 3D texture, or NULL for the border. */
static inline const uint32_t *image_of(const tunit *f, int z) {
  return z < 0 ? NULL : f->texels + ((size_t)z << (f->wlog + f->hlog));
}

/* Gives a texel of a 3D texture at s, t, r (texel units, 16.16). The
 * linear filter mixes the two nearest images. */
static uint32_t fetch3(const tunit *f, int32_t s, int32_t t, int32_t r) {
  const uint32_t *i0, *i1;
  uint32_t p0, p1, out = 0;
  int z0, fz, shift;
  if (!f->linear) {
    i0 = image_of(f, wrap_coord(r >> 16, f->dmask, f->wrap_r));
    return i0 ? fetch_at(f, i0, s, t) : f->border;
  }
  r -= 0x8000;
  z0 = r >> 16;
  fz = (r >> 8) & 0xFF;
  i0 = image_of(f, wrap_coord(z0, f->dmask, f->wrap_r));
  i1 = image_of(f, wrap_coord(z0 + 1, f->dmask, f->wrap_r));
  p0 = i0 ? fetch_at(f, i0, s, t) : f->border;
  p1 = i1 ? fetch_at(f, i1, s, t) : f->border;
  for (shift = 0; shift < 32; shift += 8) {
    int c0 = (int)((p0 >> shift) & 0xFF), c1 = (int)((p1 >> shift) & 0xFF);
    out |= (uint32_t)(c0 + (SGL_MUL16(c1 - c0, fz) >> 8)) << shift;
  }
  return out;
}

/* Gives a texel of a cube map for the direction s, t, r (16.16). The
 * largest component selects the face; the other two, divided by it,
 * select the texel. */
static uint32_t fetch_cube(const tunit *f, int32_t s, int32_t t, int32_t r) {
  int32_t as = s < 0 ? -s : s, at = t < 0 ? -t : t, ar = r < 0 ? -r : r, sc, tc, ma;
  int face;
  if (as >= at && as >= ar) {
    ma = as;
    face = s >= 0 ? 0 : 1;
    sc = s >= 0 ? -r : r;
    tc = -t;
  } else if (at >= ar) {
    ma = at;
    face = t >= 0 ? 2 : 3;
    sc = s;
    tc = t >= 0 ? r : -r;
  } else {
    ma = ar;
    face = r >= 0 ? 4 : 5;
    sc = r >= 0 ? s : -s;
    tc = -t;
  }
  if (ma == 0) return f->face[0][0];
  /* Make ma less than 2^15, so that sc * 2^15 fits in 32 bits. */
  while (ma > 0x7FFF) {
    ma >>= 1;
    sc >>= 1;
    tc >>= 1;
  }
  /* (sc / ma + 1) / 2 in 0.16, then in texels (16.16). */
  sc = (sc * 32768 / ma + 32768) * f->tw;
  tc = (tc * 32768 / ma + 32768) * f->th;
  return fetch_at(f, f->face[face], sc, tc);
}

/* Gives the depth texel at x, y (texel indices from wrap_coord) of a
 * unit. */
static inline uint32_t depth_at(const tunit *f, int x, int y) {
  if ((x | y) < 0) return f->border_depth;
  return f->texels[(y << f->wlog) + x];
}

/* Gives the value (0 to 255) of the depth texel d of a unit: the
 * result of the comparison with ref, or the depth. */
static inline int depth_value(const tunit *f, uint32_t d, uint32_t ref) {
  if (!f->cmp) return (int)(d >> 22);
  return compare(f->cmp, ref, d) ? 255 : 0;
}

/* Gives a texel of a depth texture at s, t (texel units, 16.16). r is
 * the reference of the comparison, in units of SGL_TEX_DEPTH_MAX. The
 * linear filter mixes the results of four comparisons, which makes
 * the edges of shadows soft. */
static uint32_t fetch_depth(const tunit *f, int32_t s, int32_t t, int32_t r) {
  uint32_t ref = r < 0 ? 0 : ((uint32_t)r > SGL_TEX_DEPTH_MAX ? SGL_TEX_DEPTH_MAX : (uint32_t)r);
  int v;
  if (!f->linear) {
    v = depth_value(f, depth_at(f, wrap_coord(s >> 16, f->wmask, f->wrap_s), wrap_coord(t >> 16, f->hmask, f->wrap_t)),
                    ref);
  } else {
    int32_t u = s - 0x8000, w = t - 0x8000;
    int x0 = u >> 16, y0 = w >> 16, fx = (u >> 8) & 0xFF, fy = (w >> 8) & 0xFF;
    int x1 = wrap_coord(x0 + 1, f->wmask, f->wrap_s), y1 = wrap_coord(y0 + 1, f->hmask, f->wrap_t);
    int c00, c10, c01, c11, top, bot;
    x0 = wrap_coord(x0, f->wmask, f->wrap_s);
    y0 = wrap_coord(y0, f->hmask, f->wrap_t);
    c00 = depth_value(f, depth_at(f, x0, y0), ref);
    c10 = depth_value(f, depth_at(f, x1, y0), ref);
    c01 = depth_value(f, depth_at(f, x0, y1), ref);
    c11 = depth_value(f, depth_at(f, x1, y1), ref);
    top = c00 + (SGL_MUL16(c10 - c00, fx) >> 8);
    bot = c01 + (SGL_MUL16(c11 - c01, fx) >> 8);
    v = top + (SGL_MUL16(bot - top, fy) >> 8);
  }
  switch (f->base) {
    case GL_INTENSITY: return (uint32_t)v * 0x01010101u;
    case GL_ALPHA: return ((uint32_t)v << 24) | 0x00FFFFFFu;
    default: return 0xFF000000u | (uint32_t)v * 0x010101u;
  }
}

/* Gives a texel of the texture of a unit at s, t and, for a 3D
 * texture, r (texel units, 16.16). For a cube map, s, t and r are the
 * direction (16.16). For a depth texture, r is the reference of the
 * comparison. */
static inline uint32_t fetch(const tunit *f, int32_t s, int32_t t, int32_t r) {
  switch (f->kind) {
    case FETCH_PLAIN: return fetch_at(f, f->texels, s, t);
    case FETCH_3D: return fetch3(f, s, t, r);
    case FETCH_CUBE: return fetch_cube(f, s, t, r);
    default: return fetch_depth(f, s, t, r);
  }
}

/* Sets the scales of texture k of a primitive: from the texture
 * coordinates to the values that fetch takes. */
static void set_scales(GLfloat *ss, GLfloat *ts, GLfloat *ps, const tunit *x) {
  if (x->cube) {
    *ss = *ts = *ps = 65536.0f;
    return;
  }
  *ss = (GLfloat)x->tw * 65536.0f;
  *ts = x->one_d ? 0.0f : (GLfloat)x->th * 65536.0f;
  *ps = x->three_d ? (GLfloat)(x->dmask + 1) * 65536.0f : x->cmp ? (GLfloat)SGL_TEX_DEPTH_MAX : 0.0f;
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

/* Gives the color (0 to 255) of a source of GL_COMBINE of unit x: tx
 * has the texels of the textures of the frag, prim the primary color
 * and prev the result of the unit before. */
static inline void combine_source(const frag *f, const tunit *x, int src, const uint32_t *tx,
                                  const int prim[4], const int prev[4], int out[4]) {
  int i;
  uint32_t t;
  switch (src) {
    case SRC_CONSTANT: for (i = 0; i < 4; i++) out[i] = x->envc[i]; return;
    case SRC_PRIMARY: for (i = 0; i < 4; i++) out[i] = prim[i]; return;
    case SRC_PREVIOUS: for (i = 0; i < 4; i++) out[i] = prev[i]; return;
    case SRC_WHITE: out[0] = out[1] = out[2] = out[3] = 255; return;
    default:
      t = tx[src];
      if (f->tu[src].base == GL_ALPHA) {
        /* An alpha texture has the color 0. */
        out[0] = out[1] = out[2] = 0;
      } else {
        out[0] = (int)((t >> 16) & 0xFF);
        out[1] = (int)((t >> 8) & 0xFF);
        out[2] = (int)(t & 0xFF);
      }
      out[3] = (int)(t >> 24);
      return;
  }
}

/* Gives the value of a GL_COMBINE function fn of the arguments a0, a1
 * and a2 (0 to 255). */
static inline int combine_value(GLenum fn, int a0, int a1, int a2) {
  int v;
  switch (fn) {
    case GL_REPLACE: return a0;
    case GL_ADD: v = a0 + a1; break;
    case GL_ADD_SIGNED: v = a0 + a1 - 128; break;
    case GL_INTERPOLATE: return mul255(a0, a2) + mul255(a1, 255 - a2);
    case GL_SUBTRACT: v = a0 - a1; break;
    default: return mul255(a0, a1); /* GL_MODULATE */
  }
  return v < 0 ? 0 : (v > 255 ? 255 : v);
}

/* Applies the GL_COMBINE environment of texture k of f to rgba. */
static void combine(const frag *f, int k, const uint32_t *tx, const int prim[4], int rgba[4]) {
  const tunit *x = &f->tu[k];
  int arg[3][4], a[3], out[4], i, j;
  int prev[4];
  for (i = 0; i < 4; i++) prev[i] = rgba[i];
  /* The color arguments. */
  for (j = 0; j < 3; j++) {
    int src[4];
    combine_source(f, x, x->srgb[j], tx, prim, prev, src);
    for (i = 0; i < 3; i++) {
      switch (x->orgb[j]) {
        case GL_ONE_MINUS_SRC_COLOR: arg[j][i] = 255 - src[i]; break;
        case GL_SRC_ALPHA: arg[j][i] = src[3]; break;
        case GL_ONE_MINUS_SRC_ALPHA: arg[j][i] = 255 - src[3]; break;
        default: arg[j][i] = src[i]; break;
      }
    }
  }
  if (x->crgb == GL_DOT3_RGB || x->crgb == GL_DOT3_RGBA) {
    /* 4 * sum((a0 - 0.5) * (a1 - 0.5)), with 255 for 1. */
    int32_t sum = 0;
    for (i = 0; i < 3; i++) sum += SGL_MUL16(arg[0][i] - 128, arg[1][i] - 128);
    sum = (sum >> 6) << x->rgb_shift;
    sum = sum < 0 ? 0 : (sum > 255 ? 255 : sum);
    out[0] = out[1] = out[2] = (int)sum;
    if (x->crgb == GL_DOT3_RGBA) {
      for (i = 0; i < 4; i++) rgba[i] = out[i < 3 ? i : 0];
      return;
    }
  } else {
    for (i = 0; i < 3; i++) {
      int v = combine_value(x->crgb, arg[0][i], arg[1][i], arg[2][i]) << x->rgb_shift;
      out[i] = v > 255 ? 255 : v;
    }
  }
  /* The alpha arguments. */
  for (j = 0; j < 3; j++) {
    int src[4];
    combine_source(f, x, x->salpha[j], tx, prim, prev, src);
    a[j] = x->oalpha[j] == GL_ONE_MINUS_SRC_ALPHA ? 255 - src[3] : src[3];
  }
  {
    int v = combine_value(x->calpha, a[0], a[1], a[2]) << x->alpha_shift;
    out[3] = v > 255 ? 255 : v;
  }
  for (i = 0; i < 4; i++) rgba[i] = out[i];
}

/* Applies the environments of all the textures of f, when one uses
 * GL_COMBINE: tx has their texels. */
static void env_all(const frag *f, const uint32_t *tx, int rgba[4]) {
  int prim[4], k;
  prim[0] = rgba[0];
  prim[1] = rgba[1];
  prim[2] = rgba[2];
  prim[3] = rgba[3];
  for (k = 0; k < f->ntex; k++) {
    if (f->tu[k].env == GL_COMBINE)
      combine(f, k, tx, prim, rgba);
    else
      texenv(&f->tu[k], tx[k], rgba);
  }
}

/* Gives a blend factor (0 to 255) of one component. s is the source
 * color, d the destination, sa and da their alphas; cc is the component
 * of the constant color and ca its alpha. */
static inline int factor(GLenum f, int s, int d, int sa, int da, int cc, int ca) {
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
    case GL_CONSTANT_COLOR: return cc;
    case GL_ONE_MINUS_CONSTANT_COLOR: return 255 - cc;
    case GL_CONSTANT_ALPHA: return ca;
    case GL_ONE_MINUS_CONSTANT_ALPHA: return 255 - ca;
    default: return 255;
  }
}

/* Gives one blended component (0 to 255) by the blend equation eq: s
 * and d are the source and the destination, fs and fd their factors. */
static inline int blend_eq(GLenum eq, int s, int d, int fs, int fd) {
  int v;
  if (eq == GL_FUNC_ADD) {
    /* The usual equation first. */
    v = mul255(s, fs) + mul255(d, fd);
    return v > 255 ? 255 : v;
  }
  switch (eq) {
    case GL_FUNC_SUBTRACT: v = mul255(s, fs) - mul255(d, fd); break;
    case GL_FUNC_REVERSE_SUBTRACT: v = mul255(d, fd) - mul255(s, fs); break;
    case GL_MIN: return s < d ? s : d;
    case GL_MAX: return s > d ? s : d;
    default: v = mul255(s, fs) + mul255(d, fd); break;
  }
  return v < 0 ? 0 : (v > 255 ? 255 : v);
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
    case GL_INCR_WRAP: v++; break; /* OpenGL 1.4: 255 becomes 0 */
    case GL_DECR_WRAP: v--; break;
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

/* Runs the stencil and depth tests of a fragment with window z z at
 * index i of the depth buffer, and counts it for the occlusion query.
 * Gives 1 when the fragment passes. */
static inline int test_z(const frag *f, size_t i, uint32_t z) {
  sgl_context *c = f->c;
  if (f->stencil) {
    if (!stencil_depth(f, i, z)) return 0;
  } else if (f->depth_test) {
    uint32_t zz = z >> SGL_DEPTH_SHIFT;
    if (!compare(f->depth_func, zz, (uint32_t)c->depth[i])) return 0;
    if (f->depth_write) c->depth[i] = (sgl_depth_t)zz;
  }
  if (f->count) (*f->count)++;
  return 1;
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
    int dc[3], i, da = (int)(d >> 24), sa = rgba[3], ca = f->bc[3];
    dc[0] = (int)((d >> 16) & 0xFF);
    dc[1] = (int)((d >> 8) & 0xFF);
    dc[2] = (int)(d & 0xFF);
    for (i = 0; i < 3; i++) {
      int s = rgba[i];
      rgba[i] = blend_eq(f->beq, s, dc[i], factor(f->bsrc, s, dc[i], sa, da, f->bc[i], ca),
                         factor(f->bdst, s, dc[i], sa, da, f->bc[i], ca));
    }
    /* For alpha, the color factors are those of alpha, and
     * GL_SRC_ALPHA_SATURATE is 1. */
    rgba[3] = blend_eq(f->beq, sa, da,
                       f->bsrc_a == GL_SRC_ALPHA_SATURATE ? 255 : factor(f->bsrc_a, sa, da, sa, da, ca, ca),
                       factor(f->bdst_a, sa, da, sa, da, ca, ca));
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
  return (int64_t)SGL_MUL16(a >> 16, n) * 65536 + (int64_t)((uint32_t)(uint16_t)a * (uint32_t)(uint16_t)n);
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
  plane p[SGL_MAX_TEXTURE_UNITS];   /* the r coordinate, for a 3D texture */
  fplane fz, fr, fg, fb, fal, ffog;  /* the planes in fixed point */
  fplane fsr, fsg, fsb;         /* the secondary color */
  fplane fs[SGL_MAX_TEXTURE_UNITS], ft[SGL_MAX_TEXTURE_UNITS], fp[SGL_MAX_TEXTURE_UNITS];
  GLfloat ox, oy;               /* the position of vertex a */
  int smooth, persp;
  int flat[4];
  int sec;                      /* 1: add the secondary color (smooth), 2: add flat_sec */
  int flat_sec[3];
  int three_d;                  /* a texture of the frag is 3D or a cube map: the r planes are used */
  /* Texels in the coordinates 0 to 1, for each texture of the frag. */
  GLfloat sscale[SGL_MAX_TEXTURE_UNITS], tscale[SGL_MAX_TEXTURE_UNITS];
  GLfloat pscale[SGL_MAX_TEXTURE_UNITS];
} tri;

/* Draws the pixels [xs, xe) of row y of a triangle in color index
 * mode: the index, z and fog only. */
static void span_index(const frag *f, const tri *t, int y, int xs, int xe) {
  sgl_context *c = f->c;
  int row = c->height - 1 - y;
  uint32_t *cp = c->color + SGL_MUL16(row, c->rowpixels) + xs;
  size_t di = (size_t)SGL_MUL16(row, c->width) + (size_t)xs;
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
  for (x = 0; x < n; x++, cp++, di++, z += dz, r += dr, fg += dfg) {
    if (f->stipple && !((c->a.stipple[y & 31] >> ((xs + x) & 31)) & 1)) continue;
    if (!test_z(f, di, (uint32_t)z)) continue;
    if (f->write_color) finish_index(f, cp, t->smooth ? index_of(r) : t->flat[0], col(fg));
  }
}

/* Draws the pixels [xs, xe) of row y of a triangle. */
static void span(const frag *f, const tri *t, int y, int xs, int xe) {
  sgl_context *c = f->c;
  int row = c->height - 1 - y;
  uint32_t *cp = c->color + SGL_MUL16(row, c->rowpixels) + xs;
  size_t di = (size_t)SGL_MUL16(row, c->width) + (size_t)xs;
  int32_t z = fplane_at(&t->fz, xs, y), dz = t->fz.dx;
  int32_t r = 0, g = 0, b = 0, al = 0, dr = 0, dg = 0, db = 0, dal = 0;
  int32_t fg = 0xFF0000, dfg = 0;
  /* Zero for the compilers that cannot see that ntex limits the use. */
  int32_t s[SGL_MAX_TEXTURE_UNITS] = {0}, tt[SGL_MAX_TEXTURE_UNITS] = {0};
  int32_t ds[SGL_MAX_TEXTURE_UNITS] = {0}, dt[SGL_MAX_TEXTURE_UNITS] = {0};
  int32_t rr[SGL_MAX_TEXTURE_UNITS] = {0}, dr3[SGL_MAX_TEXTURE_UNITS] = {0};
  GLfloat sw[SGL_MAX_TEXTURE_UNITS], tw[SGL_MAX_TEXTURE_UNITS], qw[SGL_MAX_TEXTURE_UNITS];
  GLfloat rw[SGL_MAX_TEXTURE_UNITS];
  int three = t->three_d;
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
      if (three) {
        rr[k] = fplane_at(&t->fp[k], xs, y);
        dr3[k] = t->fp[k].dx;
      }
    }
  } else if (ntex) {
    /* Perspective needs a division: the coordinates stay floats. */
    GLfloat px = (GLfloat)xs + 0.5f - t->ox, py = (GLfloat)y + 0.5f - t->oy;
    for (k = 0; k < ntex; k++) {
      sw[k] = t->s[k].a0 + t->s[k].dx * px + t->s[k].dy * py;
      tw[k] = t->t[k].a0 + t->t[k].dx * px + t->t[k].dy * py;
      qw[k] = t->q[k].a0 + t->q[k].dx * px + t->q[k].dy * py;
      if (three) rw[k] = t->p[k].a0 + t->p[k].dx * px + t->p[k].dy * py;
    }
  }
  for (x = 0; x < n; x++, cp++, di++) {
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
        if (three) {
          GLfloat r0 = rw[k] * q0 * t->pscale[k], r1;
          rw[k] += t->p[k].dx * (GLfloat)m;
          r1 = rw[k] * q1 * t->pscale[k];
          rr[k] = fix(r0);
          dr3[k] = fix((r1 - r0) / (GLfloat)m);
        }
      }
      left = m;
    }
    if (f->stipple && !((c->a.stipple[y & 31] >> ((xs + x) & 31)) & 1)) goto next;
    if (!f->late_z && !test_z(f, di, (uint32_t)z)) goto next;
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
    if (f->combine) {
      uint32_t tx[SGL_MAX_TEXTURE_UNITS];
      for (k = 0; k < ntex; k++) tx[k] = fetch(&f->tu[k], s[k], tt[k], rr[k]);
      env_all(f, tx, rgba);
    } else {
      for (k = 0; k < ntex; k++) texenv(&f->tu[k], fetch(&f->tu[k], s[k], tt[k], rr[k]), rgba);
    }
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
    if (f->late_z && !test_z(f, di, (uint32_t)z)) goto next;
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
    if (three)
      for (k = 0; k < ntex; k++) rr[k] += dr3[k];
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
  t.three_d = 0;
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
      GLfloat rho, dsdx, dsdy, dtdx, dtdy, drdx, drdy, qc;
      if (t.persp) {
        make_plane(&t.s[k], xy, ta[0] * wa, tb[0] * wb, td[0] * wd, inv_det);
        make_plane(&t.t[k], xy, ta[1] * wa, tb[1] * wb, td[1] * wd, inv_det);
        make_plane(&t.q[k], xy, ta[3] * wa, tb[3] * wb, td[3] * wd, inv_det);
        make_plane(&t.p[k], xy, ta[2] * wa, tb[2] * wb, td[2] * wd, inv_det);
        /* The rate of change at the middle of the triangle, for the
         * level of the mipmap. */
        qc = (ta[3] * wa + tb[3] * wb + td[3] * wd) / 3.0f;
        if (qc == 0.0f) qc = 1.0f;
        dsdx = t.s[k].dx / qc;
        dsdy = t.s[k].dy / qc;
        dtdx = t.t[k].dx / qc;
        dtdy = t.t[k].dy / qc;
        drdx = t.p[k].dx / qc;
        drdy = t.p[k].dy / qc;
      } else {
        GLfloat qa = ta[3] != 0.0f ? 1.0f / ta[3] : 1.0f;
        make_plane(&t.s[k], xy, ta[0] * qa, tb[0] * qa, td[0] * qa, inv_det);
        make_plane(&t.t[k], xy, ta[1] * qa, tb[1] * qa, td[1] * qa, inv_det);
        make_plane(&t.p[k], xy, ta[2] * qa, tb[2] * qa, td[2] * qa, inv_det);
        dsdx = t.s[k].dx;
        dsdy = t.s[k].dy;
        dtdx = t.t[k].dx;
        dtdy = t.t[k].dy;
        drdx = t.p[k].dx;
        drdy = t.p[k].dy;
      }
      {
        const sgl_level *bl = sgl_base_level(tex);
        GLfloat w = (GLfloat)bl->width, h = (GLfloat)bl->height;
        GLfloat rx = dsdx * dsdx * w * w + dtdx * dtdx * h * h;
        GLfloat ry = dsdy * dsdy * w * w + dtdy * dtdy * h * h;
        if (tex->target == GL_TEXTURE_3D) {
          GLfloat d = (GLfloat)bl->depth;
          rx += drdx * drdx * d * d;
          ry += drdy * drdy * d * d;
        } else if (tex->target == GL_TEXTURE_CUBE_MAP) {
          /* A direction of length 1 crosses a face of w texels in a
           * change of 2: approximately w / 2 texels for each unit. */
          GLfloat q = w * w * 0.25f;
          rx = (dsdx * dsdx + dtdx * dtdx + drdx * drdx) * q;
          ry = (dsdy * dsdy + dtdy * dtdy + drdy * drdy) * q;
        }
        rho = sqrtf(rx > ry ? rx : ry);
      }
      setup_texture(&f, k, us[k], tex, rho);
      set_scales(&t.sscale[k], &t.tscale[k], &t.pscale[k], &f.tu[k]);
      if (f.tu[k].three_d || f.tu[k].cube || f.tu[k].cmp) t.three_d = 1;
    }
    f.ntex = n;
    finish_textures(&f);
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
        if (t.three_d) make_fplane(&t.fp[k], &t.p[k], x0, y0, ex, ey, t.pscale[k]);
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
 * (s, t, q, r) of each texture of f. */
static void plot(const frag *f, const tri *t, int x, int y, uint32_t z, const int rgba_in[4],
                 int fogf, const GLfloat (*tex)[4]) {
  sgl_context *c = f->c;
  long row;
  size_t di;
  int rgba[4], k;
  if (x < f->x0 || x >= f->x1 || y < f->y0 || y >= f->y1) return;
  row = (long)(c->height - 1 - y);
  di = (size_t)row * (size_t)c->width + (size_t)x;
  if (!f->late_z && !test_z(f, di, z)) return;
  if (f->index_mode) {
    if (f->write_color) finish_index(f, c->color + row * c->rowpixels + x, rgba_in[0], fogf);
    return;
  }
  rgba[0] = rgba_in[0];
  rgba[1] = rgba_in[1];
  rgba[2] = rgba_in[2];
  rgba[3] = rgba_in[3];
  if (tex && f->ntex) {
    uint32_t tx[SGL_MAX_TEXTURE_UNITS] = {0};
    for (k = 0; k < f->ntex; k++) {
      GLfloat q = tex[k][2] != 0.0f ? 1.0f / tex[k][2] : 1.0f;
      tx[k] = fetch(&f->tu[k], fix(tex[k][0] * q * t->sscale[k]), fix(tex[k][1] * q * t->tscale[k]),
                    fix(tex[k][3] * q * t->pscale[k]));
    }
    if (f->combine)
      env_all(f, tx, rgba);
    else
      for (k = 0; k < f->ntex; k++) texenv(&f->tu[k], tx[k], rgba);
  }
  if (t && t->sec) {
    rgba[0] = add255(rgba[0], t->flat_sec[0]);
    rgba[1] = add255(rgba[1], t->flat_sec[1]);
    rgba[2] = add255(rgba[2], t->flat_sec[2]);
  }
  if (f->alpha_test && !compare(f->afunc, (uint32_t)rgba[3], (uint32_t)f->aref)) return;
  if (f->late_z && !test_z(f, di, z)) return;
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
    set_scales(&t->sscale[k], &t->tscale[k], &t->pscale[k], &f->tu[k]);
    f->ntex = k + 1;
  }
  finish_textures(f);
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
    GLfloat tc[SGL_MAX_TEXTURE_UNITS][4];
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
      tc[k][3] = ta[2] + (tb[2] - ta[2]) * u;
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
  int size, x0, y0, x, y, k, rgba[4];
  GLfloat sz = c->a.point_size;
  GLfloat tc[SGL_MAX_TEXTURE_UNITS][4];
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
  if (c->point_atten) {
    /* OpenGL 1.4: the size changes by sqrt(1 / (a + b d + c d^2)) for
     * the distance d from the eye. */
    const GLfloat *k3 = c->a.point_atten;
    GLfloat d = sqrtf(a->eye[0] * a->eye[0] + a->eye[1] * a->eye[1] + a->eye[2] * a->eye[2]);
    GLfloat q = k3[0] + k3[1] * d + k3[2] * d * d;
    sz = q > 0.0f ? sz / sqrtf(q) : SGL_MAX_POINT_SIZE;
  }
  if (sz < c->a.point_min) sz = c->a.point_min;
  if (sz > c->a.point_max) sz = c->a.point_max;
  if (sz > SGL_MAX_POINT_SIZE) sz = SGL_MAX_POINT_SIZE;
  size = (int)(sz + 0.5f);
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
    tc[k][3] = a->tex[f.unit[k]][2];
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
