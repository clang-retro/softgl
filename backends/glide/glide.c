/* glide.c - the 3dfx Glide back end of softgl: a Voodoo card draws the
 * triangles, lines and points through Glide 2. softgl keeps the GL
 * state, the transform, lighting and clipping, and draws in software
 * what the card cannot do. See docs/glide.md.
 *
 * The frame and the depth buffer are on the card. When softgl must draw
 * in software, the back end copies them to the buffers of softgl
 * (grLfbLock), and copies them back before the next primitive on the
 * card.
 *
 * This file calls Glide only through the table of softgl_glide.h, so it
 * compiles on every system; tests/glide.c tests it with a table that
 * records the calls.
 *
 * Part of clang-retro softgl, a software implementation of OpenGL.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <GL/gl.h>

#include "softgl_glide.h"
#include "../../src/sgl.h"
#include "glide_api.h"

/* A texture on the card: the softgl texture id and serial, the TMU, the
 * address and the Glide description. */
typedef struct gr_tex {
  unsigned long id, serial;
  int tmu;
  unsigned long addr, size;
  sgl_gr_texinfo info;
  float sscale, tscale;    /* texture coordinates 0 to 1 to the Glide range */
  int mips;                /* the card has the mipmaps */
  unsigned long used;      /* the frame of the last use */
  unsigned long load;      /* a number that changes at each load */
} gr_tex;

#define TEXTURES 64

/* The keys of the state cache: each one is the arguments of one Glide
 * call, so that the back end calls Glide only when they change. */
enum {
  K_COLOR, K_ALPHA, K_TEX0, K_TEX1, K_BLEND, K_DMODE, K_DFUNC, K_DMASK, K_CMASK, K_AFUNC, K_AREF,
  K_FOG, K_FOGCOLOR, K_CLIP, K_DITHER, K_SOURCE0, K_SOURCE1, K_COUNT
};
#define KEY_ARGS 7

/* The state of the Glide back end. */
typedef struct gr_state {
  const sgl_glide_api *gr;
  sgl_context *ctx;
  int width, height;
  int tmus;                /* the number of texture units: 1 or 2 */
  int soft_newer[2];       /* the buffers of softgl are newer: color, depth */
  int hw_newer[2];         /* the buffers of the card are newer: color, depth */
  int key[K_COUNT][KEY_ARGS];
  unsigned long key_set;   /* the keys with a value */
  float fog_key[5];        /* the GL fog of the fog table */
  int fog_valid;
  unsigned long tex_next[2], tex_min[2], tex_max[2];
  unsigned long frame, loads;
  gr_tex tex[TEXTURES];
  unsigned interval;       /* the grBufferSwap interval */
} gr_state;

/* Sets key k to the n values v. Gives 1 when the key changed: then the
 * caller makes the Glide call. */
static int changed(gr_state *s, int k, int n, const int *v) {
  if ((s->key_set & (1ul << k)) && memcmp(s->key[k], v, (size_t)n * sizeof *v) == 0) return 0;
  memcpy(s->key[k], v, (size_t)n * sizeof *v);
  s->key_set |= 1ul << k;
  return 1;
}

/* Forgets the value of key k, so that the next use calls Glide. */
static void forget(gr_state *s, int k) { s->key_set &= ~(1ul << k); }

/* Gives the 16-bit depth of the card of a softgl depth value. */
static unsigned card_depth(uint32_t d) { return (unsigned)(((uint32_t)d << SGL_DEPTH_SHIFT) >> 15); }

/* Copies the color buffer and the depth buffer between softgl and the
 * card: to_card 1 writes the card, 0 reads it. which has bit 0 for the
 * color and bit 1 for the depth. Row 0 of softgl is the top row, so the
 * lock uses the upper left origin. */
static void copy_buffers(gr_state *s, int which, int to_card) {
  const sgl_glide_api *gr = s->gr;
  sgl_context *c = s->ctx;
  sgl_gr_lfbinfo info;
  int x, y, type = to_card ? GR_LFB_WRITE_ONLY : GR_LFB_READ_ONLY;
  /* A context that is destroyed has no color buffer. */
  if (!c->color) return;
  if (which & 1) {
    memset(&info, 0, sizeof info);
    info.size = (int)sizeof info;
    if (gr->LfbLock(type, GR_BUFFER_BACKBUFFER, to_card ? GR_LFBWRITEMODE_565 : GR_LFBWRITEMODE_ANY,
                    GR_ORIGIN_UPPER_LEFT, 0, &info)) {
      for (y = 0; y < s->height; y++) {
        uint16_t *q = (uint16_t *)((uint8_t *)info.ptr + (unsigned long)y * info.stride);
        uint32_t *p = c->color + (long)y * c->rowpixels;
        if (to_card) {
          for (x = 0; x < s->width; x++) {
            uint32_t v = p[x];
            q[x] = (uint16_t)(((v >> 8) & 0xF800u) | ((v >> 5) & 0x07E0u) | ((v >> 3) & 0x001Fu));
          }
        } else {
          for (x = 0; x < s->width; x++) {
            unsigned v = q[x], r = v >> 11, g = (v >> 5) & 63, b = v & 31;
            p[x] = 0xFF000000u | (((r << 3) | (r >> 2)) << 16) | (((g << 2) | (g >> 4)) << 8) | ((b << 3) | (b >> 2));
          }
        }
      }
      gr->LfbUnlock(type, GR_BUFFER_BACKBUFFER);
    }
  }
  if ((which & 2) && c->depth) {
    memset(&info, 0, sizeof info);
    info.size = (int)sizeof info;
    if (gr->LfbLock(type, GR_BUFFER_AUXBUFFER, to_card ? GR_LFBWRITEMODE_ZA16 : GR_LFBWRITEMODE_ANY,
                    GR_ORIGIN_UPPER_LEFT, 0, &info)) {
      for (y = 0; y < s->height; y++) {
        uint16_t *q = (uint16_t *)((uint8_t *)info.ptr + (unsigned long)y * info.stride);
        sgl_depth_t *d = c->depth + (long)y * c->width;
        if (to_card) {
          for (x = 0; x < s->width; x++) q[x] = (uint16_t)card_depth(d[x]);
        } else {
          for (x = 0; x < s->width; x++)
            d[x] = (sgl_depth_t)((((uint32_t)q[x] << 15) | ((uint32_t)q[x] >> 1)) >> SGL_DEPTH_SHIFT);
        }
      }
      gr->LfbUnlock(type, GR_BUFFER_AUXBUFFER);
    }
  }
}

/* Makes the card agree with softgl before the card draws: copies the
 * buffers that softgl changed. */
static void to_hardware(gr_state *s) {
  int which = (s->soft_newer[0] ? 1 : 0) | (s->soft_newer[1] ? 2 : 0);
  if (which) copy_buffers(s, which, 1);
  s->soft_newer[0] = s->soft_newer[1] = 0;
}

/* The software entry: softgl draws next, so the buffers of the card go
 * to the buffers of softgl. */
static void gr_software(void *user, sgl_context *c) {
  gr_state *s = (gr_state *)((sgl_glide *)user)->state;
  int which = (s->hw_newer[0] ? 1 : 0) | (s->hw_newer[1] ? 2 : 0);
  (void)c;
  if (which) {
    s->gr->SstIdle();
    copy_buffers(s, which, 0);
  }
  s->hw_newer[0] = s->hw_newer[1] = 0;
  s->soft_newer[0] = s->soft_newer[1] = 1;
}

/* Gives the Glide compare function of a GL one. */
static int cmp_func(GLenum f) {
  switch (f) {
    case GL_NEVER: return GR_CMP_NEVER;
    case GL_LESS: return GR_CMP_LESS;
    case GL_EQUAL: return GR_CMP_EQUAL;
    case GL_LEQUAL: return GR_CMP_LEQUAL;
    case GL_GREATER: return GR_CMP_GREATER;
    case GL_NOTEQUAL: return GR_CMP_NOTEQUAL;
    case GL_GEQUAL: return GR_CMP_GEQUAL;
    default: return GR_CMP_ALWAYS;
  }
}

/* Gives the Glide blend factor of a GL source factor (dst 0) or
 * destination factor (dst 1), or -1 when Glide has none. The card has
 * no alpha buffer, so the factors of the destination alpha stay in
 * softgl. */
static int blend_factor(GLenum f, int dst) {
  switch (f) {
    case GL_ZERO: return GR_BLEND_ZERO;
    case GL_ONE: return GR_BLEND_ONE;
    case GL_SRC_ALPHA: return GR_BLEND_SRC_ALPHA;
    case GL_ONE_MINUS_SRC_ALPHA: return GR_BLEND_ONE_MINUS_SRC_ALPHA;
    case GL_DST_COLOR: return dst ? -1 : GR_BLEND_OTHER_COLOR;
    case GL_ONE_MINUS_DST_COLOR: return dst ? -1 : GR_BLEND_ONE_MINUS_OTHER_COLOR;
    case GL_SRC_COLOR: return dst ? GR_BLEND_OTHER_COLOR : -1;
    case GL_ONE_MINUS_SRC_COLOR: return dst ? GR_BLEND_ONE_MINUS_OTHER_COLOR : -1;
    default: return -1;
  }
}

/* Gives the log2 of a size that is a power of 2. */
static int log2i(int n) {
  int l = 0;
  while ((1 << l) < n) l++;
  return l;
}

/* Gives the texture of unit u when the card can use it, NULL when the
 * unit has no texture, and (sgl_texture *)1 when the card cannot draw
 * it. The card takes 2D RGB and RGBA textures of 256 x 256 or less,
 * with an aspect ratio of 8 or less. */
static sgl_texture *usable_texture(gr_state *s, sgl_context *c, int u) {
  unsigned units = sgl_texture_units(c);
  sgl_texture *t;
  const sgl_level *lv;
  GLenum env = c->a.unit[u].env_mode;
  if (!(units & (1u << u))) return NULL;
  if (u >= s->tmus) return (sgl_texture *)1;
  t = sgl_unit_texture(c, u);
  lv = t ? sgl_base_level(t) : NULL;
  if (!lv) return NULL;
  if (t->target != GL_TEXTURE_2D || t->palette_size || t->compare_mode != GL_NONE) return (sgl_texture *)1;
  if (lv->base != GL_RGB && lv->base != GL_RGBA) return (sgl_texture *)1;
  if (lv->wlog < 0 || lv->hlog < 0 || lv->wlog > 8 || lv->hlog > 8) return (sgl_texture *)1;
  if (lv->wlog - lv->hlog > 3 || lv->hlog - lv->wlog > 3) return (sgl_texture *)1;
  if (env != GL_MODULATE && env != GL_REPLACE && env != GL_DECAL) return (sgl_texture *)1;
  if ((s->gr->limits & SGL_GLIDE_LIMIT_SQUARE) && lv->wlog != lv->hlog) return (sgl_texture *)1;
  if ((s->gr->limits & SGL_GLIDE_LIMIT_NO_CLAMP) && (t->wrap_s != GL_REPEAT || t->wrap_t != GL_REPEAT))
    return (sgl_texture *)1;
  /* GL_DECAL of an RGBA texture mixes the texture and the vertex color
   * by the texture alpha. */
  if ((s->gr->limits & SGL_GLIDE_LIMIT_ONE_ALPHA) && env == GL_DECAL && lv->base == GL_RGBA) return (sgl_texture *)1;
  return t;
}

/* Gives 1 when the projection is a perspective projection: then the w
 * of a vertex is its eye depth, which the fog table of Glide uses. */
static int perspective(const sgl_context *c) {
  const GLfloat *m = c->projection.top->m;
  return m[3] == 0.0f && m[7] == 0.0f && m[11] == -1.0f && m[15] == 0.0f;
}

/* Gives 1 when the GL state of c needs none of the functions that
 * the driver gives in its limits (SGL_GLIDE_LIMIT_). The textures are
 * examined in usable_texture, and the vertex alpha in draw. */
static int limits_allow(gr_state *s, const sgl_context *c) {
  const sgl_attrib *a = &c->a;
  unsigned l = s->gr->limits;
  int blend = a->enabled[SGL_BLEND] && !(a->blend_src == GL_ONE && a->blend_dst == GL_ZERO);
  if (!l) return 1;
  if ((l & SGL_GLIDE_LIMIT_ALPHA_TEST) && a->enabled[SGL_ALPHA_TEST] && a->alpha_func != GL_ALWAYS) return 0;
  if ((l & SGL_GLIDE_LIMIT_BLEND) && blend &&
      !(a->blend_src == GL_SRC_ALPHA && a->blend_dst == GL_ONE_MINUS_SRC_ALPHA))
    return 0;
  if ((l & SGL_GLIDE_LIMIT_COLOR_MASK) && !a->color_mask[0]) return 0;
  if ((l & SGL_GLIDE_LIMIT_FOG_ALPHA) && a->enabled[SGL_FOG] && blend) return 0;
  if ((l & SGL_GLIDE_LIMIT_NO_BLEND) && blend) return 0;
  if ((l & SGL_GLIDE_LIMIT_NO_FOG) && a->enabled[SGL_FOG]) return 0;
  return 1;
}

/* Gives 1 when the card can draw a primitive in the GL state of c.
 * kind is 0 for a triangle, 1 for a line, 2 for a point. */
static int can_draw(gr_state *s, sgl_context *c, int kind) {
  const sgl_attrib *a = &c->a;
  sgl_texture *t0, *t1;
  int alpha_used;
  if (c != s->ctx || c->index_mode || c->query || c->width != s->width || c->height != s->height) return 0;
  if (kind == 1 && (a->enabled[SGL_LINE_STIPPLE] || a->enabled[SGL_LINE_SMOOTH])) return 0;
  if (kind == 2 && (a->enabled[SGL_POINT_SMOOTH] || c->point_atten)) return 0;
  if (kind == 0 && (a->enabled[SGL_POLYGON_STIPPLE] || a->enabled[SGL_POLYGON_SMOOTH])) return 0;
  if (a->enabled[SGL_STENCIL_TEST] || a->draw_buffer == GL_NONE) return 0;
  if (a->enabled[SGL_COLOR_LOGIC_OP] && a->logic_op != GL_COPY) return 0;
  /* The card writes red, green and blue together, and has no alpha. */
  if (a->color_mask[0] != a->color_mask[1] || a->color_mask[0] != a->color_mask[2]) return 0;
  if (a->enabled[SGL_FOG] && (a->fog_source == GL_FOG_COORDINATE || !perspective(c))) return 0;
  if (a->enabled[SGL_BLEND]) {
    if (a->blend_eq != GL_FUNC_ADD || a->blend_src != a->blend_src_alpha || a->blend_dst != a->blend_dst_alpha ||
        blend_factor(a->blend_src, 0) < 0 || blend_factor(a->blend_dst, 1) < 0)
      return 0;
  }
  if (!limits_allow(s, c)) return 0;
  t0 = usable_texture(s, c, 0);
  t1 = usable_texture(s, c, 1);
  if (t0 == (sgl_texture *)1 || t1 == (sgl_texture *)1) return 0;
  /* Glide has one iterated color: the secondary color and a texture
   * cannot be on together. */
  if (c->sec_on && (t0 || t1)) return 0;
  if (t1) {
    /* Unit 1 multiplies the texture of unit 0 in the TMUs: unit 1 must
     * use GL_MODULATE, and unit 0 GL_MODULATE or GL_REPLACE. GL_REPLACE
     * of an RGB texture keeps the vertex alpha, which the TMUs lose:
     * then the alpha must not be used. */
    alpha_used = a->enabled[SGL_BLEND] || (a->enabled[SGL_ALPHA_TEST] && a->alpha_func != GL_ALWAYS);
    if (c->a.unit[1].env_mode != GL_MODULATE) return 0;
    if (t0) {
      GLenum e0 = c->a.unit[0].env_mode;
      if (e0 != GL_MODULATE && e0 != GL_REPLACE) return 0;
      if (e0 == GL_REPLACE && sgl_base_level(t0)->base == GL_RGB && alpha_used) return 0;
    }
  }
  return 1;
}

/* Gives the Glide level of a size (256 is GR_LOD_256, 1 is GR_LOD_1). */
static int lod_of(int size) { return GR_LOD_256 + (8 - log2i(size)); }

/* Gives the number of mipmaps below the base level that the card gets:
 * all the levels to 1 x 1, or 0 when the filter is not a mipmap filter
 * or softgl does not have all the levels. */
static int mipmap_count(const sgl_texture *t) {
  const sgl_level *lv = sgl_base_level(t);
  int n = lv->wlog > lv->hlog ? lv->wlog : lv->hlog, i, b = t->base_level;
  if (t->min_filter == GL_NEAREST || t->min_filter == GL_LINEAR || !t->complete) return 0;
  if (b + n >= SGL_MAX_TEXTURE_LEVELS || b + n > t->max_level) return 0;
  for (i = 1; i <= n; i++) {
    const sgl_level *m = &t->level[b + i];
    if (!m->texels || m->width != (lv->width >> i ? lv->width >> i : 1) ||
        m->height != (lv->height >> i ? lv->height >> i : 1))
      return 0;
  }
  return n;
}

/* Frees the textures of a TMU, to use its memory again. */
static void reset_tmu(gr_state *s, int tmu) {
  int i;
  for (i = 0; i < TEXTURES; i++)
    if (s->tex[i].id && s->tex[i].tmu == tmu) memset(&s->tex[i], 0, sizeof s->tex[i]);
  s->tex_next[tmu] = s->tex_min[tmu];
  forget(s, tmu ? K_SOURCE1 : K_SOURCE0);
}

/* Gives an address of size bytes in the memory of a TMU, or 0 when the
 * texture is larger than the memory. When the memory is full, the
 * textures of the TMU are freed. */
static unsigned long alloc_tex(gr_state *s, int tmu, unsigned long size, int retry) {
  unsigned long a = (s->tex_next[tmu] + 7ul) & ~7ul;
  if (a / GR_TEXTURE_BOUNDARY != (a + size - 1) / GR_TEXTURE_BOUNDARY)
    a = (a / GR_TEXTURE_BOUNDARY + 1) * GR_TEXTURE_BOUNDARY;
  if (a + size > s->tex_max[tmu]) {
    if (!retry) return 0;
    reset_tmu(s, tmu);
    return alloc_tex(s, tmu, size, 0);
  }
  s->tex_next[tmu] = a + size;
  return a + 1; /* 0 is the failure: the caller removes the 1 */
}

/* Writes the texels of a level in a format of 16 bits: RGB_565 or
 * ARGB_4444. */
static uint16_t *convert_level(const sgl_level *m, int rgba, uint16_t *out) {
  long i, n = (long)m->width * m->height;
  for (i = 0; i < n; i++) {
    uint32_t v = m->texels[i];
    if (rgba)
      *out++ = (uint16_t)(((v >> 16) & 0xF000u) | ((v >> 12) & 0x0F00u) | ((v >> 8) & 0x00F0u) | ((v >> 4) & 0x000Fu));
    else
      *out++ = (uint16_t)(((v >> 8) & 0xF800u) | ((v >> 5) & 0x07E0u) | ((v >> 3) & 0x001Fu));
  }
  return out;
}

/* Gives the card texture of the texture t of unit u in TMU u: loads it
 * when it is new or changed. Gives NULL on failure. */
static gr_tex *card_texture(gr_state *s, sgl_context *c, sgl_texture *t, int u) {
  const sgl_glide_api *gr = s->gr;
  unsigned long serial, id = sgl_texture_id(c, u, &serial), addr;
  const sgl_level *lv = sgl_base_level(t);
  gr_tex *e = NULL, *old = NULL;
  int i, mips, rgba = lv->base == GL_RGBA;
  long texels = 0;
  uint16_t *buf, *p;
  for (i = 0; i < TEXTURES; i++) {
    gr_tex *x = &s->tex[i];
    if (x->id == id && x->tmu == u) e = x;
    if (!old || x->used < old->used || (x->id == 0 && old->id != 0)) old = x;
  }
  if (e && e->serial == serial) {
    e->used = s->frame;
    return e;
  }
  if (!e) e = old;
  memset(e, 0, sizeof *e);
  mips = mipmap_count(t);
  e->info.large_lod = lod_of(lv->width > lv->height ? lv->width : lv->height);
  e->info.small_lod = mips ? GR_LOD_1 : e->info.large_lod;
  e->info.aspect = GR_ASPECT_1x1 + (lv->hlog - lv->wlog);
  e->info.format = rgba ? GR_TEXFMT_ARGB_4444 : GR_TEXFMT_RGB_565;
  /* The texture coordinates of the long side go from 0 to 256. */
  e->sscale = lv->width >= lv->height ? 256.0f : 256.0f * (float)lv->width / (float)lv->height;
  e->tscale = lv->height >= lv->width ? 256.0f : 256.0f * (float)lv->height / (float)lv->width;
  for (i = 0; i <= mips; i++) texels += (long)t->level[t->base_level + i].width * t->level[t->base_level + i].height;
  buf = (uint16_t *)malloc((size_t)texels * 2);
  if (!buf) return NULL;
  for (i = 0, p = buf; i <= mips; i++) p = convert_level(&t->level[t->base_level + i], rgba, p);
  e->info.data = buf;
  e->size = gr->TexTextureMemRequired(GR_MIPMAPLEVELMASK_BOTH, &e->info);
  addr = alloc_tex(s, u, e->size, 1);
  if (!addr) {
    free(buf);
    memset(e, 0, sizeof *e);
    return NULL;
  }
  e->addr = addr - 1;
  e->tmu = u;
  e->mips = mips;
  gr->TexDownloadMipMap(u, e->addr, GR_MIPMAPLEVELMASK_BOTH, &e->info);
  free(buf);
  e->info.data = NULL;
  e->id = id;
  e->serial = serial;
  e->used = s->frame;
  e->load = ++s->loads;
  return e;
}

/* Selects texture e in its TMU, with the filters and wrap modes of t. */
static void bind_texture(gr_state *s, const sgl_texture *t, gr_tex *e) {
  const sgl_glide_api *gr = s->gr;
  int k[2];
  k[0] = (int)e->load;
  k[1] = (int)(e - s->tex);
  if (!changed(s, e->tmu ? K_SOURCE1 : K_SOURCE0, 2, k)) return;
  gr->TexSource(e->tmu, e->addr, GR_MIPMAPLEVELMASK_BOTH, &e->info);
  gr->TexFilterMode(e->tmu,
                    t->min_filter == GL_LINEAR || t->min_filter == GL_LINEAR_MIPMAP_NEAREST ||
                            t->min_filter == GL_LINEAR_MIPMAP_LINEAR
                        ? GR_TEXTUREFILTER_BILINEAR
                        : GR_TEXTUREFILTER_POINT_SAMPLED,
                    t->mag_filter == GL_LINEAR ? GR_TEXTUREFILTER_BILINEAR : GR_TEXTUREFILTER_POINT_SAMPLED);
  gr->TexClampMode(e->tmu, t->wrap_s == GL_REPEAT ? GR_TEXTURECLAMP_WRAP : GR_TEXTURECLAMP_CLAMP,
                   t->wrap_t == GL_REPEAT ? GR_TEXTURECLAMP_WRAP : GR_TEXTURECLAMP_CLAMP);
  /* One TMU cannot blend two mipmaps: the card uses the nearest. */
  gr->TexMipMapMode(e->tmu, e->mips ? GR_MIPMAP_NEAREST : GR_MIPMAP_DISABLE, 0);
}

/* Gives the GL fog factor at the eye depth z (as fog_factor of
 * src/vertex.c). */
static float fog_at(const sgl_context *c, float z) {
  float f;
  switch (c->a.fog_mode) {
    case GL_LINEAR:
      if (c->a.fog_end == c->a.fog_start) return 1.0f;
      f = (c->a.fog_end - z) / (c->a.fog_end - c->a.fog_start);
      break;
    case GL_EXP:
      f = expf(-c->a.fog_density * z);
      break;
    default: {
      float dz = c->a.fog_density * z;
      f = expf(-dz * dz);
      break;
    }
  }
  return f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
}

/* Gives the eye depth of entry i of the Glide fog table. The table has
 * 4 entries for each power of 2 of w: w = 2^(3 + i / 4) / (8 - i % 4). */
static float fog_w(int i) { return (float)(1ul << (3 + (i >> 2))) / (float)(8 - (i & 3)); }

/* Sets the fog of the card from the GL fog of c. */
static void apply_fog(gr_state *s, sgl_context *c) {
  const sgl_glide_api *gr = s->gr;
  int on = c->a.enabled[SGL_FOG], k[1];
  k[0] = on ? GR_FOG_WITH_TABLE : GR_FOG_DISABLE;
  if (changed(s, K_FOG, 1, k)) gr->FogMode(k[0]);
  if (!on) return;
  {
    float key[5];
    key[0] = (float)c->a.fog_mode, key[1] = c->a.fog_start, key[2] = c->a.fog_end, key[3] = c->a.fog_density;
    key[4] = 0.0f;
    if (!s->fog_valid || memcmp(key, s->fog_key, sizeof key) != 0) {
      unsigned char table[GR_FOG_TABLE_SIZE];
      int i;
      for (i = 0; i < GR_FOG_TABLE_SIZE; i++)
        table[i] = (unsigned char)((1.0f - fog_at(c, fog_w(i))) * 255.0f + 0.5f);
      gr->FogTable(table);
      memcpy(s->fog_key, key, sizeof key);
      s->fog_valid = 1;
    }
  }
  {
    const GLfloat *fc = c->a.fog_color;
    k[0] = (int)(((unsigned long)(fc[0] * 255.0f + 0.5f) << 16) | ((unsigned long)(fc[1] * 255.0f + 0.5f) << 8) |
                 (unsigned long)(fc[2] * 255.0f + 0.5f));
    if (changed(s, K_FOGCOLOR, 1, k)) gr->FogColorValue((unsigned long)k[0]);
  }
}

/* Sets the color and alpha combine of the card for the textures of
 * unit 0 (t0) and unit 1 (t1). */
static void apply_combine(gr_state *s, sgl_context *c, const sgl_texture *t0, const sgl_texture *t1) {
  const sgl_glide_api *gr = s->gr;
  int k[7];
  int cf = GR_COMBINE_FUNCTION_LOCAL, cfac = GR_COMBINE_FACTOR_NONE;
  int af = GR_COMBINE_FUNCTION_LOCAL, afac = GR_COMBINE_FACTOR_NONE;
  const sgl_texture *t = t0 ? t0 : t1;
  if (t) {
    /* The TMUs give the texture of unit 0, times the texture of unit 1
     * when it is on. Without unit 0, unit 1 is in GL_MODULATE. */
    GLenum env = t0 ? c->a.unit[0].env_mode : GL_MODULATE;
    int rgba = sgl_base_level(t)->base == GL_RGBA || t1;
    switch (env) {
      case GL_REPLACE:
        cf = GR_COMBINE_FUNCTION_SCALE_OTHER, cfac = GR_COMBINE_FACTOR_ONE;
        if (rgba) af = GR_COMBINE_FUNCTION_SCALE_OTHER, afac = GR_COMBINE_FACTOR_ONE;
        break;
      case GL_DECAL:
        /* (texture - vertex) * texture alpha + vertex. */
        cf = GR_COMBINE_FUNCTION_BLEND, cfac = GR_COMBINE_FACTOR_TEXTURE_ALPHA;
        break;
      default: /* GL_MODULATE */
        cf = GR_COMBINE_FUNCTION_SCALE_OTHER, cfac = GR_COMBINE_FACTOR_LOCAL;
        af = GR_COMBINE_FUNCTION_SCALE_OTHER, afac = GR_COMBINE_FACTOR_LOCAL;
        break;
    }
  }
  k[0] = cf, k[1] = cfac, k[2] = GR_COMBINE_LOCAL_ITERATED, k[3] = t ? GR_COMBINE_OTHER_TEXTURE : GR_COMBINE_OTHER_ITERATED;
  k[4] = 0;
  if (changed(s, K_COLOR, 5, k)) gr->ColorCombine(k[0], k[1], k[2], k[3], k[4]);
  k[0] = af, k[1] = afac;
  if (changed(s, K_ALPHA, 5, k)) gr->AlphaCombine(k[0], k[1], k[2], k[3], k[4]);
  /* TMU 0 gives its texture (t0), or its texture times the texture of
   * TMU 1 (t0 and t1), or the texture of TMU 1 (t1 only). */
  k[0] = GR_TMU0, k[5] = 0, k[6] = 0;
  if (t0 && t1)
    k[1] = GR_COMBINE_FUNCTION_SCALE_OTHER, k[2] = GR_COMBINE_FACTOR_LOCAL;
  else if (t1)
    k[1] = GR_COMBINE_FUNCTION_SCALE_OTHER, k[2] = GR_COMBINE_FACTOR_ONE;
  else
    k[1] = GR_COMBINE_FUNCTION_LOCAL, k[2] = GR_COMBINE_FACTOR_NONE;
  k[3] = k[1], k[4] = k[2];
  if (changed(s, K_TEX0, 7, k)) gr->TexCombine(k[0], k[1], k[2], k[3], k[4], k[5], k[6]);
  if (t1) {
    k[0] = GR_TMU1, k[1] = GR_COMBINE_FUNCTION_LOCAL, k[2] = GR_COMBINE_FACTOR_NONE;
    k[3] = k[1], k[4] = k[2];
    if (changed(s, K_TEX1, 7, k)) gr->TexCombine(k[0], k[1], k[2], k[3], k[4], k[5], k[6]);
  }
}

/* Sets the clip window of the card: the scissor box, or the screen. */
static void apply_clip(gr_state *s, int x0, int y0, int x1, int y1) {
  int k[4];
  k[0] = x0, k[1] = y0, k[2] = x1, k[3] = y1;
  if (changed(s, K_CLIP, 4, k))
    s->gr->ClipWindow((unsigned long)x0, (unsigned long)y0, (unsigned long)x1, (unsigned long)y1);
}

/* Sets the state of the card from the GL state of c, except the
 * textures and the combine. */
static void apply_state(gr_state *s, sgl_context *c) {
  const sgl_glide_api *gr = s->gr;
  const sgl_attrib *a = &c->a;
  int k[4], depth = a->enabled[SGL_DEPTH_TEST] && c->depth;
  k[0] = depth ? GR_DEPTHBUFFER_ZBUFFER : GR_DEPTHBUFFER_DISABLE;
  if (changed(s, K_DMODE, 1, k)) gr->DepthBufferMode(k[0]);
  if (depth) {
    k[0] = cmp_func(a->depth_func);
    if (changed(s, K_DFUNC, 1, k)) gr->DepthBufferFunction(k[0]);
  }
  k[0] = depth && a->depth_mask;
  if (changed(s, K_DMASK, 1, k)) gr->DepthMask(k[0]);
  k[0] = a->color_mask[0] != 0, k[1] = 0;
  if (changed(s, K_CMASK, 2, k)) gr->ColorMask(k[0], k[1]);
  if (a->enabled[SGL_BLEND]) {
    k[0] = blend_factor(a->blend_src, 0), k[1] = blend_factor(a->blend_dst, 1);
  } else {
    k[0] = GR_BLEND_ONE, k[1] = GR_BLEND_ZERO;
  }
  k[2] = GR_BLEND_ONE, k[3] = GR_BLEND_ZERO;
  if (changed(s, K_BLEND, 4, k)) gr->AlphaBlendFunction(k[0], k[1], k[2], k[3]);
  k[0] = a->enabled[SGL_ALPHA_TEST] ? cmp_func(a->alpha_func) : GR_CMP_ALWAYS;
  if (changed(s, K_AFUNC, 1, k)) gr->AlphaTestFunction(k[0]);
  if (a->enabled[SGL_ALPHA_TEST]) {
    k[0] = (int)(sgl_clamp01(a->alpha_ref) * 255.0f + 0.5f);
    if (changed(s, K_AREF, 1, k)) gr->AlphaTestReferenceValue((unsigned)k[0]);
  }
  k[0] = a->enabled[SGL_DITHER] ? GR_DITHER_4x4 : GR_DITHER_DISABLE;
  if (changed(s, K_DITHER, 1, k)) gr->DitherMode(k[0]);
  if (a->enabled[SGL_SCISSOR_TEST]) {
    int x0 = a->scissor[0], y0 = a->scissor[1], x1 = x0 + a->scissor[2], y1 = y0 + a->scissor[3];
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > s->width) x1 = s->width;
    if (y1 > s->height) y1 = s->height;
    if (x1 < x0) x1 = x0;
    if (y1 < y0) y1 = y0;
    apply_clip(s, x0, y0, x1, y1);
  } else {
    apply_clip(s, 0, 0, s->width, s->height);
  }
  apply_fog(s, c);
}

/* Gives x on the grid of 1/16 pixel of the Voodoo: the card needs
 * vertices on this grid. Adding and removing 3 * 2^18 drops the lower
 * bits of a float. */
static float snap(float x) {
  volatile float v = x + (float)(3L << 18);
  return v - (float)(3L << 18);
}

/* Gives the 0 to 255 value of a color channel of 0 to 1. */
static float channel(float v) { return v <= 0.0f ? 0.0f : v >= 1.0f ? 255.0f : v * 255.0f; }

/* Fills a Glide vertex from a softgl window vertex. e0 and e1 are the
 * textures of TMU 0 and TMU 1 (NULL for none). With add_spec, the
 * secondary color goes into the color (without a texture, the sum of
 * the vertex colors is the GL color sum). */
static void vertex(const sgl_hw_vertex *h, const gr_tex *e0, const gr_tex *e1, int add_spec, sgl_gr_vertex *v) {
  int i;
  memset(v, 0, sizeof *v);
  v->x = snap(h->x);
  v->y = snap(h->y);
  v->ooz = h->z * 65535.0f;
  v->oow = h->w;
  v->r = channel(h->color[0] + (add_spec ? h->spec[0] : 0.0f));
  v->g = channel(h->color[1] + (add_spec ? h->spec[1] : 0.0f));
  v->b = channel(h->color[2] + (add_spec ? h->spec[2] : 0.0f));
  v->a = channel(h->color[3]);
  for (i = 0; i < 2; i++) {
    const gr_tex *e = i ? e1 : e0;
    if (!e) continue;
    /* s / w, t / w and q / w give a perspective (and projective)
     * texture. */
    v->tmu[i].sow = h->tex[i][0] * e->sscale * h->w;
    v->tmu[i].tow = h->tex[i][1] * e->tscale * h->w;
    v->tmu[i].oow = h->tex[i][3] * h->w;
  }
}

/* Gives 1 when the alpha of a primitive is the texture alpha or the
 * vertex alpha alone, for a driver with SGL_GLIDE_LIMIT_ONE_ALPHA:
 * GL_MODULATE of an RGBA texture multiplies the two, so each vertex
 * alpha must then be 1 when the blend or the alpha test uses the alpha. h has the n vertices; t0 is the texture of
 * unit 0. */
static int one_alpha(const sgl_context *c, const sgl_texture *t0, const sgl_hw_vertex *const *h, int n) {
  int i;
  if (!t0 || c->a.unit[0].env_mode != GL_MODULATE || sgl_base_level(t0)->base != GL_RGBA) return 1;
  if (!c->a.enabled[SGL_BLEND] && !(c->a.enabled[SGL_ALPHA_TEST] && c->a.alpha_func != GL_ALWAYS)) return 1;
  for (i = 0; i < n; i++)
    if (h[i]->color[3] < 1.0f) return 0;
  return 1;
}

/* Draws a quad of four Glide vertices as two triangles. */
static void quad(gr_state *s, const sgl_gr_vertex *q) {
  s->gr->DrawTriangle(&q[0], &q[1], &q[2]);
  s->gr->DrawTriangle(&q[0], &q[2], &q[3]);
}

/* Draws a primitive of kind (0 triangle, 1 line, 2 point) with the
 * vertices h on the card. A wide line is a parallelogram and a large
 * point is a square, as in GL. Gives 0 when the card cannot draw it;
 * softgl then draws it. */
static int draw(gr_state *s, sgl_context *c, int kind, const sgl_hw_vertex *const *h) {
  sgl_texture *t0, *t1;
  gr_tex *e0 = NULL, *e1 = NULL;
  sgl_gr_vertex v[3];
  int i, n = 3 - kind;
  if (!can_draw(s, c, kind)) return 0;
  t0 = usable_texture(s, c, 0);
  t1 = usable_texture(s, c, 1);
  if ((s->gr->limits & SGL_GLIDE_LIMIT_ONE_ALPHA) && !one_alpha(c, t0, h, n)) return 0;
  to_hardware(s);
  if (t1 && !(e1 = card_texture(s, c, t1, 1))) return 0;
  if (t0 && !(e0 = card_texture(s, c, t0, 0))) return 0;
  if (e0) bind_texture(s, t0, e0);
  if (e1) bind_texture(s, t1, e1);
  apply_combine(s, c, t0, t1);
  apply_state(s, c);
  s->hw_newer[0] = 1;
  if (c->a.enabled[SGL_DEPTH_TEST] && c->a.depth_mask) s->hw_newer[1] = 1;
  for (i = 0; i < n; i++) vertex(h[i], e0, e1, c->sec_on, &v[i]);
  if (kind == 0) {
    s->gr->DrawTriangle(&v[0], &v[1], &v[2]);
  } else if (kind == 1) {
    float w = c->a.line_width;
    if (w <= 1.5f) {
      s->gr->DrawLine(&v[0], &v[1]);
    } else {
      /* An x-major line is wide in y; a y-major line is wide in x. */
      sgl_gr_vertex q[4];
      float dx = h[1]->x - h[0]->x, dy = h[1]->y - h[0]->y, ox = 0.0f, oy = 0.0f;
      if ((dx < 0 ? -dx : dx) >= (dy < 0 ? -dy : dy))
        oy = w * 0.5f;
      else
        ox = w * 0.5f;
      q[0] = v[0], q[1] = v[1], q[2] = v[1], q[3] = v[0];
      q[0].x = snap(h[0]->x - ox), q[0].y = snap(h[0]->y - oy);
      q[1].x = snap(h[1]->x - ox), q[1].y = snap(h[1]->y - oy);
      q[2].x = snap(h[1]->x + ox), q[2].y = snap(h[1]->y + oy);
      q[3].x = snap(h[0]->x + ox), q[3].y = snap(h[0]->y + oy);
      quad(s, q);
    }
  } else {
    int size = (int)(c->a.point_size + 0.5f);
    if (size <= 1) {
      s->gr->DrawPoint(&v[0]);
    } else {
      sgl_gr_vertex q[4];
      float r = (float)size * 0.5f, x = h[0]->x, y = h[0]->y;
      q[0] = q[1] = q[2] = q[3] = v[0];
      q[0].x = snap(x - r), q[0].y = snap(y - r);
      q[1].x = snap(x + r), q[1].y = snap(y - r);
      q[2].x = snap(x + r), q[2].y = snap(y + r);
      q[3].x = snap(x - r), q[3].y = snap(y + r);
      quad(s, q);
    }
  }
  return 1;
}

/* The triangle entry. */
static int gr_triangle(void *user, sgl_context *c, const sgl_hw_vertex *a, const sgl_hw_vertex *b,
                       const sgl_hw_vertex *d) {
  const sgl_hw_vertex *h[3];
  h[0] = a, h[1] = b, h[2] = d;
  return draw((gr_state *)((sgl_glide *)user)->state, c, 0, h);
}

/* The line entry. */
static int gr_line(void *user, sgl_context *c, const sgl_hw_vertex *a, const sgl_hw_vertex *b) {
  const sgl_hw_vertex *h[2];
  h[0] = a, h[1] = b;
  return draw((gr_state *)((sgl_glide *)user)->state, c, 1, h);
}

/* The point entry. */
static int gr_point(void *user, sgl_context *c, const sgl_hw_vertex *a) {
  const sgl_hw_vertex *h[1];
  h[0] = a;
  return draw((gr_state *)((sgl_glide *)user)->state, c, 2, h);
}

/* The clear entry: the card clears the color and the depth in the
 * rectangle (grBufferClear in the clip window). The stencil stays in
 * softgl. */
static unsigned gr_clear(void *user, const sgl_clear_rect *r) {
  gr_state *s = (gr_state *)((sgl_glide *)user)->state;
  sgl_context *c = s->ctx;
  const sgl_glide_api *gr = s->gr;
  unsigned mask = r->mask & (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  long off;
  int x0, top, k[2];
  if (c->index_mode || c->width != s->width || c->height != s->height) mask &= ~(unsigned)GL_COLOR_BUFFER_BIT;
  if (!c->depth) mask &= ~(unsigned)GL_DEPTH_BUFFER_BIT;
  if (!mask) return 0;
  /* The position of the rectangle: row 0 of softgl is the top row. */
  if (mask & GL_COLOR_BUFFER_BIT) {
    off = (long)(r->color - c->color);
    top = (int)(off / c->rowpixels);
    x0 = (int)(off % c->rowpixels);
  } else {
    off = (long)((sgl_depth_t *)r->depth - c->depth);
    top = (int)(off / c->width);
    x0 = (int)(off % c->width);
  }
  /* The card must have the newest buffers before it clears a part. */
  to_hardware(s);
  apply_clip(s, x0, s->height - (top + r->height), x0 + r->width, s->height - top);
  k[0] = (mask & GL_COLOR_BUFFER_BIT) != 0, k[1] = 0;
  if (changed(s, K_CMASK, 2, k)) gr->ColorMask(k[0], k[1]);
  k[0] = (mask & GL_DEPTH_BUFFER_BIT) != 0;
  if (changed(s, K_DMASK, 1, k)) gr->DepthMask(k[0]);
  gr->BufferClear((unsigned long)r->color_value, 0,
                  (mask & GL_DEPTH_BUFFER_BIT) ? card_depth(r->depth_value) : 0);
  if (mask & GL_COLOR_BUFFER_BIT) s->hw_newer[0] = 1;
  if (mask & GL_DEPTH_BUFFER_BIT) s->hw_newer[1] = 1;
  /* The next use of the buffers by softgl copies them from the card. */
  c->be_pending |= SGL_BE_HARDWARE;
  return mask;
}

/* The flush entry: glFinish waits for the card. */
static void gr_flush(void *user, int finish) {
  gr_state *s = (gr_state *)((sgl_glide *)user)->state;
  if (finish) s->gr->SstIdle();
}

/* The texture_deleted entry: forgets the card textures of id. Their
 * memory is used again when the memory of the TMU is full. */
static void gr_texture_deleted(void *user, unsigned long id) {
  gr_state *s = (gr_state *)((sgl_glide *)user)->state;
  int i;
  for (i = 0; i < TEXTURES; i++)
    if (s->tex[i].id == id) {
      forget(s, s->tex[i].tmu ? K_SOURCE1 : K_SOURCE0);
      memset(&s->tex[i], 0, sizeof s->tex[i]);
    }
}

/* The present entry: copies what softgl drew last and shows the back
 * buffer of the card. */
static void gr_present(void *user, sgl_context *c, const uint32_t *pixels, int width, int height,
                       long rowpixels) {
  gr_state *s = (gr_state *)((sgl_glide *)user)->state;
  (void)c, (void)pixels, (void)width, (void)height, (void)rowpixels;
  to_hardware(s);
  s->gr->BufferSwap((int)s->interval);
  s->frame++;
  /* The new back buffer is the card's: softgl must read it before it
   * draws. */
  s->hw_newer[0] = 1;
  s->soft_newer[0] = 0;
}

/* The detach entry: closes the screen and frees the state. */
static void gr_detach(void *user, sgl_context *c) {
  sgl_glide *g = (sgl_glide *)user;
  gr_state *s = (gr_state *)g->state;
  (void)c;
  if (!s) return;
  s->gr->SstWinClose();
  s->gr->GlideShutdown();
  free(s);
  g->state = NULL;
}

/* The entries of the Glide back end. */
static const sgl_backend gr_backend = {
    .clear = gr_clear, .triangle = gr_triangle, .line = gr_line, .point = gr_point,
    .software = gr_software, .texture_deleted = gr_texture_deleted, .flush = gr_flush,
    .present = gr_present, .detach = gr_detach};

/* Gives the Glide resolution of a screen size, or -1. */
static int resolution(int w, int h) {
  static const struct {
    short w, h, res;
  } t[] = {{320, 200, GR_RESOLUTION_320x200},   {320, 240, GR_RESOLUTION_320x240},
           {400, 256, GR_RESOLUTION_400x256},   {512, 384, GR_RESOLUTION_512x384},
           {640, 200, GR_RESOLUTION_640x200},   {640, 350, GR_RESOLUTION_640x350},
           {640, 400, GR_RESOLUTION_640x400},   {640, 480, GR_RESOLUTION_640x480},
           {800, 600, GR_RESOLUTION_800x600},   {960, 720, GR_RESOLUTION_960x720},
           {856, 480, GR_RESOLUTION_856x480},   {512, 256, GR_RESOLUTION_512x256},
           {1024, 768, GR_RESOLUTION_1024x768}, {1280, 1024, GR_RESOLUTION_1280x1024},
           {1600, 1200, GR_RESOLUTION_1600x1200}, {400, 300, GR_RESOLUTION_400x300}};
  size_t i;
  for (i = 0; i < sizeof t / sizeof t[0]; i++)
    if (t[i].w == w && t[i].h == h) return t[i].res;
  return -1;
}

/* Gives the number of TMUs of board 0 from the result of
 * grSstQueryHardware (GrHwConfiguration, as 32-bit words): the number
 * of boards, then for each board its type and its configuration. A
 * Voodoo and a Voodoo2 have the frame buffer size, the revision and the
 * number of TMUs; a Voodoo Rush has the frame buffer size and the
 * number of TMUs. */
static int tmu_count(const int32_t *hw) {
  int n;
  if (hw[0] < 1) return 0;
  if (hw[1] == GR_SSTTYPE_VOODOO || hw[1] == GR_SSTTYPE_VOODOO2)
    n = hw[4];
  else if (hw[1] == GR_SSTTYPE_SST96)
    n = hw[3];
  else
    n = 1;
  return n < 1 ? 1 : n > 2 ? 2 : n;
}

/* Opens the screen and gives the Glide back end to ctx (see
 * softgl_glide.h). */
int sgl_glide_attach(sgl_glide *g, sgl_context *ctx) {
  const sgl_glide_api *gr;
  gr_state *s;
  int32_t hw[256];
  int res, i;
  if (!g || !ctx || !g->api) return 0;
  gr = g->api;
  res = resolution(g->width, g->height);
  if (res < 0 || ctx->width != g->width || ctx->height != g->height || !ctx->color) return 0;
  gr->GlideInit();
  memset(hw, 0, sizeof hw);
  if (!gr->SstQueryHardware(hw) || tmu_count(hw) < 1) {
    gr->GlideShutdown();
    return 0;
  }
  gr->SstSelect(0);
  if (!gr->SstWinOpen(g->hwnd, res, g->refresh, GR_COLORFORMAT_ARGB, GR_ORIGIN_LOWER_LEFT, 2, 1)) {
    gr->GlideShutdown();
    return 0;
  }
  s = (gr_state *)calloc(1, sizeof *s);
  if (!s) {
    gr->SstWinClose();
    gr->GlideShutdown();
    return 0;
  }
  s->gr = gr;
  s->ctx = ctx;
  s->width = g->width;
  s->height = g->height;
  s->tmus = tmu_count(hw);
  s->interval = (g->flags & SGL_GLIDE_NO_VSYNC) ? 0 : 1;
  for (i = 0; i < s->tmus; i++) {
    s->tex_min[i] = s->tex_next[i] = gr->TexMinAddress(i);
    s->tex_max[i] = gr->TexMaxAddress(i);
  }
  /* softgl culls; the card draws both faces. */
  gr->CullMode(GR_CULL_DISABLE);
  s->soft_newer[0] = s->soft_newer[1] = 1;
  g->state = s;
  if (!sgl_set_backend(ctx, &gr_backend, g)) {
    g->state = NULL;
    free(s);
    gr->SstWinClose();
    gr->GlideShutdown();
    return 0;
  }
  return 1;
}
