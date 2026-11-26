/* warp3d.c - the Warp3D back end of softgl: a 3D card (Permedia 2,
 * ViRGE, Voodoo and others) draws the triangles, lines and points
 * through warp3d.library. softgl keeps the GL state, the transform,
 * lighting and clipping, and draws in software what the card cannot do.
 * See docs/amiga.md.
 *
 * The frame is in the bitmap of the card; the depth buffer is the Z
 * buffer of Warp3D. When softgl must draw in software, the back end
 * copies the frame and the Z buffer to the buffers of softgl, and copies
 * them back before the next primitive in hardware.
 *
 * Part of clang-retro softgl, a software implementation of OpenGL.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include <exec/memory.h>
#include <intuition/screens.h>
#include <proto/exec.h>
#include <proto/intuition.h>

#include <GL/gl.h>

#include "softgl_amiga.h"
#include "../../src/sgl.h"
#include "warp3d_api.h"

/* The library base of the stubs (warp3d_stubs.s). */
struct Library *sgl_Warp3DBase;

/* A texture of the card: the softgl texture id and serial, and a copy
 * of the texels (Warp3D can read them again when it loads the texture). */
typedef struct w3d_tex {
  unsigned long id, serial;
  W3D_Texture *tex;
  uint32_t *texels;
  ULONG bytes;
  unsigned long used;      /* the frame of the last use, for the replacement */
} w3d_tex;

#define TEXTURES 32

/* The state of the Warp3D back end. */
typedef struct w3d_state {
  W3D_Context *w3d;
  sgl_context *ctx;
  int width, height;
  int has_z;               /* Warp3D gave a Z buffer */
  int locked;              /* the hardware is locked */
  int soft_newer[2];       /* the buffers of softgl are newer: color, depth */
  int hw_newer[2];         /* the buffers of the card are newer: color, depth */
  unsigned features;       /* the W3D_Q_ features that the driver has (bits below) */
  int max_tex;             /* the maximum texture width and height */
  /* The state that is set on the card now, to set only what changes. */
  ULONG on;                /* the W3D_ states that are on */
  int key_valid;
  ULONG zmode, asrc, adst, amode;
  W3D_Float aref;
  W3D_Scissor scissor;
  unsigned long frame;
  w3d_tex tex[TEXTURES];
  W3D_Double *zrow;        /* one row of Z values for the copies */
  UBYTE *zmask;
} w3d_state;

/* The features of the driver. */
#define F_POINT 1u
#define F_LINE 2u
#define F_TEX 4u
#define F_SPEC 8u
#define F_Z 16u
#define F_ALPHA 32u
#define F_BLEND 64u
#define F_SCISSOR 128u
#define F_PERSP 256u

/* Gives 1 when the driver has a query. */
static int has(W3D_Context *w, ULONG q) {
  ULONG r = sgl_W3D_Query(w, q, 0);
  return r == W3D_FULLY_SUPPORTED || r == W3D_PARTIALLY_SUPPORTED;
}

/* Locks the hardware for drawing. Gives 0 when the drawing region is
 * not visible. */
static int lock(w3d_state *s) {
  if (s->locked) return 1;
  if (sgl_W3D_LockHardware(s->w3d) != W3D_SUCCESS) return 0;
  s->locked = 1;
  return 1;
}

/* Unlocks the hardware. */
static void unlock(w3d_state *s) {
  if (!s->locked) return;
  sgl_W3D_UnLockHardware(s->w3d);
  s->locked = 0;
}

/* Gives the address of row y of the drawing region. */
static uint8_t *region_row(w3d_state *s, int y) {
  return (uint8_t *)s->w3d->drawmem + (long)(s->w3d->yoffset + y) * s->w3d->bprow;
}

/* Copies row y between the color buffer of softgl and the drawing region:
 * to_card 1 writes the card, 0 reads it. */
static void copy_row(w3d_state *s, int y, int to_card) {
  uint32_t *p = s->ctx->color + (long)y * s->ctx->rowpixels;
  uint8_t *r = region_row(s, y);
  ULONG f = s->w3d->format;
  int x, n = s->width;
  if (f == W3D_FMT_A8R8G8B8) {
    if (to_card)
      CopyMem(p, r, (ULONG)n * 4);
    else
      CopyMem(r, p, (ULONG)n * 4);
  } else if (f == W3D_FMT_B8G8R8A8) {
    for (x = 0; x < n; x++) {
      uint8_t *q = r + x * 4;
      if (to_card) {
        uint32_t v = p[x];
        q[0] = (uint8_t)v, q[1] = (uint8_t)(v >> 8), q[2] = (uint8_t)(v >> 16), q[3] = (uint8_t)(v >> 24);
      } else {
        p[x] = ((uint32_t)q[3] << 24) | ((uint32_t)q[2] << 16) | ((uint32_t)q[1] << 8) | q[0];
      }
    }
  } else {
    /* 16 bits: 5-6-5 or 5-5-5, big-endian or byte-swapped (PC). */
    int six = f == W3D_FMT_R5G6B5 || f == W3D_FMT_R5G6B5PC;
    int pc = f == W3D_FMT_R5G6B5PC || f == W3D_FMT_R5G5B5PC;
    for (x = 0; x < n; x++) {
      uint8_t *q = r + x * 2;
      unsigned v;
      if (to_card) {
        uint32_t c = p[x];
        unsigned rr = (c >> 19) & 31, bb = (c >> 3) & 31;
        v = six ? (rr << 11) | (((c >> 10) & 63) << 5) | bb : (rr << 10) | (((c >> 11) & 31) << 5) | bb;
        q[pc ? 1 : 0] = (uint8_t)(v >> 8);
        q[pc ? 0 : 1] = (uint8_t)v;
      } else {
        unsigned rr, gg, bb;
        v = ((unsigned)q[pc ? 1 : 0] << 8) | q[pc ? 0 : 1];
        if (six) {
          rr = (v >> 11) & 31, gg = (v >> 5) & 63, bb = v & 31;
          gg = (gg << 2) | (gg >> 4);
        } else {
          rr = (v >> 10) & 31, gg = (v >> 5) & 31, bb = v & 31;
          gg = (gg << 3) | (gg >> 2);
        }
        p[x] = 0xFF000000u | (((rr << 3) | (rr >> 2)) << 16) | (gg << 8) | ((bb << 3) | (bb >> 2));
      }
    }
  }
}

/* Copies the Z buffer of the card to the depth buffer of softgl (to_card
 * 0) or back (to_card 1). */
static void copy_depth(w3d_state *s, int to_card) {
  sgl_context *c = s->ctx;
  int x, y;
  if (!s->has_z || !c->depth || !s->zrow) return;
  for (y = 0; y < s->height; y++) {
    sgl_depth_t *d = c->depth + (long)y * c->width;
    if (to_card) {
      for (x = 0; x < s->width; x++)
        s->zrow[x] = (W3D_Double)((uint32_t)d[x] << SGL_DEPTH_SHIFT) / (W3D_Double)SGL_Z_MAX;
      sgl_W3D_WriteZSpan(s->w3d, 0, (ULONG)y, (ULONG)s->width, s->zrow, s->zmask);
    } else {
      sgl_W3D_ReadZSpan(s->w3d, 0, (ULONG)y, (ULONG)s->width, s->zrow);
      for (x = 0; x < s->width; x++)
        d[x] = (sgl_depth_t)((uint32_t)(s->zrow[x] * (W3D_Double)SGL_Z_MAX) >> SGL_DEPTH_SHIFT);
    }
  }
}

/* Makes the card agree with softgl before the card draws: copies the
 * buffers that softgl changed. Gives 0 when the card cannot draw now. */
static int to_hardware(w3d_state *s) {
  int y;
  if (!lock(s)) return 0;
  if (s->soft_newer[0]) {
    for (y = 0; y < s->height; y++) copy_row(s, y, 1);
    s->soft_newer[0] = 0;
  }
  if (s->soft_newer[1]) {
    copy_depth(s, 1);
    s->soft_newer[1] = 0;
  }
  return 1;
}

/* The software entry: softgl draws next, so the buffers of the card go
 * to the buffers of softgl. */
static void w3d_software(void *user, sgl_context *c) {
  w3d_state *s = (w3d_state *)user;
  int y;
  (void)c;
  if (lock(s)) {
    sgl_W3D_WaitIdle(s->w3d);
    if (s->hw_newer[0])
      for (y = 0; y < s->height; y++) copy_row(s, y, 0);
    if (s->hw_newer[1]) copy_depth(s, 0);
  }
  unlock(s);
  s->hw_newer[0] = s->hw_newer[1] = 0;
  s->soft_newer[0] = s->soft_newer[1] = 1;
}

/* Gives the Warp3D compare mode of a GL compare function. */
static ULONG cmp_mode(GLenum f) {
  switch (f) {
    case GL_NEVER: return W3D_A_NEVER;
    case GL_LESS: return W3D_A_LESS;
    case GL_GEQUAL: return W3D_A_GEQUAL;
    case GL_LEQUAL: return W3D_A_LEQUAL;
    case GL_GREATER: return W3D_A_GREATER;
    case GL_NOTEQUAL: return W3D_A_NOTEQUAL;
    case GL_EQUAL: return W3D_A_EQUAL;
    default: return W3D_A_ALWAYS;
  }
}

/* Gives the Warp3D blend factor of a GL blend factor, or 0 when Warp3D
 * has none. */
static ULONG blend_factor(GLenum f) {
  switch (f) {
    case GL_ZERO: return W3D_ZERO;
    case GL_ONE: return W3D_ONE;
    case GL_SRC_COLOR: return W3D_SRC_COLOR;
    case GL_DST_COLOR: return W3D_DST_COLOR;
    case GL_ONE_MINUS_SRC_COLOR: return W3D_ONE_MINUS_SRC_COLOR;
    case GL_ONE_MINUS_DST_COLOR: return W3D_ONE_MINUS_DST_COLOR;
    case GL_SRC_ALPHA: return W3D_SRC_ALPHA;
    case GL_ONE_MINUS_SRC_ALPHA: return W3D_ONE_MINUS_SRC_ALPHA;
    case GL_DST_ALPHA: return W3D_DST_ALPHA;
    case GL_ONE_MINUS_DST_ALPHA: return W3D_ONE_MINUS_DST_ALPHA;
    case GL_SRC_ALPHA_SATURATE: return W3D_SRC_ALPHA_SATURATE;
    default: return 0;
  }
}

/* Gives the Warp3D texture environment of a GL one, or 0. */
static ULONG env_mode(GLenum m) {
  switch (m) {
    case GL_REPLACE: return W3D_REPLACE;
    case GL_MODULATE: return W3D_MODULATE;
    case GL_DECAL: return W3D_DECAL;
    case GL_BLEND: return W3D_BLEND;
    default: return 0;
  }
}

/* Gives the texture of unit 0 when the card can use it, NULL when no
 * texture is on, and (sgl_texture *)1 when the card cannot draw it. */
static sgl_texture *usable_texture(w3d_state *s, sgl_context *c) {
  unsigned units = sgl_texture_units(c);
  sgl_texture *t;
  const sgl_level *lv;
  if (!units) return NULL;
  if (units != 1u || !(s->features & F_TEX)) return (sgl_texture *)1;
  t = sgl_unit_texture(c, 0);
  lv = t ? sgl_base_level(t) : NULL;
  if (!lv) return NULL;
  if (t->target != GL_TEXTURE_2D || t->palette_size || t->compare_mode != GL_NONE) return (sgl_texture *)1;
  if (lv->base != GL_RGB && lv->base != GL_RGBA) return (sgl_texture *)1;
  if (lv->width > s->max_tex || lv->height > s->max_tex || lv->wlog < 0 || lv->hlog < 0) return (sgl_texture *)1;
  if (!env_mode(c->a.unit[0].env_mode)) return (sgl_texture *)1;
  return t;
}

/* Gives 1 when the card can draw a primitive in the GL state of c.
 * lines is 1 for a line, 2 for a point. */
static int can_draw(w3d_state *s, sgl_context *c, int kind) {
  const sgl_attrib *a = &c->a;
  if (c != s->ctx || c->index_mode || c->query || c->width != s->width || c->height != s->height) return 0;
  if (kind == 1 && (!(s->features & F_LINE) || a->enabled[SGL_LINE_STIPPLE] || a->enabled[SGL_LINE_SMOOTH]))
    return 0;
  if (kind == 2 && (!(s->features & F_POINT) || a->enabled[SGL_POINT_SMOOTH])) return 0;
  if (kind == 0 && (a->enabled[SGL_POLYGON_STIPPLE] || a->enabled[SGL_POLYGON_SMOOTH])) return 0;
  if (a->enabled[SGL_STENCIL_TEST] || a->enabled[SGL_FOG] || a->draw_buffer == GL_NONE) return 0;
  if (a->enabled[SGL_COLOR_LOGIC_OP] && a->logic_op != GL_COPY) return 0;
  if (!a->color_mask[0] || !a->color_mask[1] || !a->color_mask[2] || !a->color_mask[3]) return 0;
  if (a->enabled[SGL_DEPTH_TEST] && !s->has_z) return 0;
  if (a->enabled[SGL_ALPHA_TEST] && a->alpha_func != GL_ALWAYS && !(s->features & F_ALPHA)) return 0;
  if (a->enabled[SGL_SCISSOR_TEST] && !(s->features & F_SCISSOR)) return 0;
  if (c->sec_on && !(s->features & F_SPEC)) return 0;
  if (a->enabled[SGL_BLEND]) {
    if (!(s->features & F_BLEND) || a->blend_eq != GL_FUNC_ADD || a->blend_src != a->blend_src_alpha ||
        a->blend_dst != a->blend_dst_alpha || !blend_factor(a->blend_src) || !blend_factor(a->blend_dst))
      return 0;
  }
  if (usable_texture(s, c) == (sgl_texture *)1) return 0;
  return 1;
}

/* Sets state on or off on the card when it changes. */
static void set_state(w3d_state *s, ULONG state, int on) {
  if (s->key_valid && ((s->on & state) != 0) == (on != 0)) return;
  sgl_W3D_SetState(s->w3d, state, on ? W3D_ENABLE : W3D_DISABLE);
  if (on)
    s->on |= state;
  else
    s->on &= ~state;
}

/* Sets the state of the card from the GL state of c. */
static void apply_state(w3d_state *s, sgl_context *c, int textured) {
  const sgl_attrib *a = &c->a;
  int depth = a->enabled[SGL_DEPTH_TEST];
  int alpha = a->enabled[SGL_ALPHA_TEST] && a->alpha_func != GL_ALWAYS;
  int blend = a->enabled[SGL_BLEND];
  int sc = a->enabled[SGL_SCISSOR_TEST];
  if (!s->key_valid) s->on = 0;
  set_state(s, W3D_GOURAUD, 1);
  set_state(s, W3D_TEXMAPPING, textured);
  set_state(s, W3D_PERSPECTIVE, textured && (s->features & F_PERSP) && a->hint_perspective != GL_FASTEST);
  set_state(s, W3D_ZBUFFER, depth);
  set_state(s, W3D_ZBUFFERUPDATE, depth && a->depth_mask);
  set_state(s, W3D_ALPHATEST, alpha);
  set_state(s, W3D_BLENDING, blend);
  set_state(s, W3D_SPECULAR, c->sec_on);
  set_state(s, W3D_SCISSOR, sc);
  if (depth && (!s->key_valid || s->zmode != cmp_mode(a->depth_func))) {
    s->zmode = cmp_mode(a->depth_func);
    sgl_W3D_SetZCompareMode(s->w3d, s->zmode);
  }
  if (alpha && (!s->key_valid || s->amode != cmp_mode(a->alpha_func) || s->aref != a->alpha_ref)) {
    s->amode = cmp_mode(a->alpha_func);
    s->aref = a->alpha_ref;
    sgl_W3D_SetAlphaMode(s->w3d, s->amode, &s->aref);
  }
  if (blend && (!s->key_valid || s->asrc != blend_factor(a->blend_src) || s->adst != blend_factor(a->blend_dst))) {
    s->asrc = blend_factor(a->blend_src);
    s->adst = blend_factor(a->blend_dst);
    sgl_W3D_SetBlendMode(s->w3d, s->asrc, s->adst);
  }
  if (sc) {
    /* The GL box is from the bottom; the Warp3D box is from the top. */
    W3D_Scissor b;
    b.left = a->scissor[0];
    b.top = s->height - (a->scissor[1] + a->scissor[3]);
    b.width = a->scissor[2];
    b.height = a->scissor[3];
    if (!s->key_valid || memcmp(&b, &s->scissor, sizeof b) != 0) {
      s->scissor = b;
      sgl_W3D_SetScissor(s->w3d, &s->scissor);
    }
  }
  s->key_valid = 1;
}

/* Frees one texture of the card. */
static void free_tex(w3d_state *s, w3d_tex *e) {
  if (e->tex) sgl_W3D_FreeTexObj(s->w3d, e->tex);
  if (e->texels) FreeMem(e->texels, e->bytes);
  memset(e, 0, sizeof *e);
}

/* Gives the Warp3D filter of the GL filters of a texture. The card gets
 * no mipmaps: a mipmap filter uses the filter of its base level. */
static ULONG filter_of(GLenum f) {
  return f == GL_LINEAR || f == GL_LINEAR_MIPMAP_NEAREST || f == GL_LINEAR_MIPMAP_LINEAR ? W3D_LINEAR
                                                                                          : W3D_NEAREST;
}

/* Gives the card texture of t: loads it when it is new or changed.
 * Gives NULL on failure. */
static W3D_Texture *card_texture(w3d_state *s, sgl_context *c, sgl_texture *t) {
  unsigned long serial, id = sgl_texture_id(c, 0, &serial);
  const sgl_level *lv = sgl_base_level(t);
  w3d_tex *e = NULL, *old = &s->tex[0];
  int i;
  ULONG err = 0;
  struct TagItem tags[5];
  W3D_Color env;
  for (i = 0; i < TEXTURES; i++) {
    if (s->tex[i].id == id) e = &s->tex[i];
    if (s->tex[i].used < old->used) old = &s->tex[i];
  }
  if (e && e->serial == serial) {
    e->used = s->frame;
    return e->tex;
  }
  if (!e) e = old;
  free_tex(s, e);
  e->bytes = (ULONG)lv->width * (ULONG)lv->height * 4;
  e->texels = (uint32_t *)AllocMem(e->bytes, MEMF_ANY);
  if (!e->texels) return NULL;
  /* The texels of softgl are 0xAARRGGBB: in the memory of the 68k, this
   * is W3D_A8R8G8B8. */
  CopyMem(lv->texels, e->texels, e->bytes);
  tags[0].ti_Tag = W3D_ATO_IMAGE, tags[0].ti_Data = (IPTR)e->texels;
  tags[1].ti_Tag = W3D_ATO_FORMAT, tags[1].ti_Data = W3D_A8R8G8B8;
  tags[2].ti_Tag = W3D_ATO_WIDTH, tags[2].ti_Data = (IPTR)lv->width;
  tags[3].ti_Tag = W3D_ATO_HEIGHT, tags[3].ti_Data = (IPTR)lv->height;
  tags[4].ti_Tag = TAG_DONE, tags[4].ti_Data = 0;
  e->tex = sgl_W3D_AllocTexObj(s->w3d, &err, tags);
  if (!e->tex || err != W3D_SUCCESS) {
    e->tex = NULL;
    free_tex(s, e);
    return NULL;
  }
  e->id = id;
  e->serial = serial;
  e->used = s->frame;
  sgl_W3D_SetFilter(s->w3d, e->tex, filter_of(t->min_filter), filter_of(t->mag_filter));
  sgl_W3D_SetWrapMode(s->w3d, e->tex, t->wrap_s == GL_REPEAT ? W3D_REPEAT : W3D_CLAMP,
                      t->wrap_t == GL_REPEAT ? W3D_REPEAT : W3D_CLAMP, NULL);
  env.r = c->a.unit[0].env_color[0];
  env.g = c->a.unit[0].env_color[1];
  env.b = c->a.unit[0].env_color[2];
  env.a = c->a.unit[0].env_color[3];
  sgl_W3D_SetTexEnv(s->w3d, e->tex, env_mode(c->a.unit[0].env_mode), &env);
  return e->tex;
}

/* Fills a Warp3D vertex from a softgl window vertex. tw and th are the
 * size of the texture in texels (0 without a texture). */
static void vertex(const w3d_state *s, const sgl_hw_vertex *h, int tw, int th, W3D_Vertex *v) {
  v->x = h->x;
  v->y = (W3D_Float)s->height - h->y;
  v->z = h->z;
  v->w = h->w;
  if (tw) {
    W3D_Float q = h->tex[0][3] != 0.0f ? 1.0f / h->tex[0][3] : 1.0f;
    v->u = h->tex[0][0] * q * (W3D_Float)tw;
    v->v = h->tex[0][1] * q * (W3D_Float)th;
  } else {
    v->u = v->v = 0.0f;
  }
  v->tex3d = 0.0f;
  v->color.r = h->color[0];
  v->color.g = h->color[1];
  v->color.b = h->color[2];
  v->color.a = h->color[3];
  v->spec.r = h->spec[0];
  v->spec.g = h->spec[1];
  v->spec.b = h->spec[2];
  v->l = 0.0f;
}

/* Prepares the card to draw a primitive of kind (0 triangle, 1 line, 2
 * point): the state, the buffers and the texture. Gives 0 when the card
 * cannot draw it; else 1, with the texture in *tex and its size. */
static int prepare(w3d_state *s, sgl_context *c, int kind, W3D_Texture **tex, int *tw, int *th) {
  sgl_texture *t;
  if (!can_draw(s, c, kind)) return 0;
  t = usable_texture(s, c);
  *tex = NULL;
  *tw = *th = 0;
  if (!to_hardware(s)) return 0;
  if (t) {
    *tex = card_texture(s, c, t);
    if (!*tex) return 0;
    *tw = sgl_base_level(t)->width;
    *th = sgl_base_level(t)->height;
  }
  apply_state(s, c, *tex != NULL);
  s->hw_newer[0] = 1;
  if (c->a.enabled[SGL_DEPTH_TEST] && c->a.depth_mask) s->hw_newer[1] = 1;
  return 1;
}

/* The triangle entry. */
static int w3d_triangle(void *user, sgl_context *c, const sgl_hw_vertex *a, const sgl_hw_vertex *b,
                        const sgl_hw_vertex *d) {
  w3d_state *s = (w3d_state *)user;
  W3D_Triangle t;
  int tw, th;
  if (!prepare(s, c, 0, &t.tex, &tw, &th)) return 0;
  vertex(s, a, tw, th, &t.v1);
  vertex(s, b, tw, th, &t.v2);
  vertex(s, d, tw, th, &t.v3);
  t.st_pattern = NULL;
  return sgl_W3D_DrawTriangle(s->w3d, &t) == W3D_SUCCESS;
}

/* The line entry. */
static int w3d_line(void *user, sgl_context *c, const sgl_hw_vertex *a, const sgl_hw_vertex *b) {
  w3d_state *s = (w3d_state *)user;
  W3D_Line l;
  int tw, th;
  if (!prepare(s, c, 1, &l.tex, &tw, &th)) return 0;
  vertex(s, a, tw, th, &l.v1);
  vertex(s, b, tw, th, &l.v2);
  l.linewidth = c->a.line_width;
  l.st_enable = 0;
  l.st_pattern = 0;
  l.st_factor = 0;
  return sgl_W3D_DrawLine(s->w3d, &l) == W3D_SUCCESS;
}

/* The point entry. */
static int w3d_point(void *user, sgl_context *c, const sgl_hw_vertex *a) {
  w3d_state *s = (w3d_state *)user;
  W3D_Point p;
  int tw, th;
  if (c->point_atten || !prepare(s, c, 2, &p.tex, &tw, &th)) return 0;
  vertex(s, a, tw, th, &p.v1);
  p.pointsize = c->a.point_size;
  return sgl_W3D_DrawPoint(s->w3d, &p) == W3D_SUCCESS;
}

/* The clear entry: the card clears the whole frame and the Z buffer.
 * A clear of a part (the scissor box) stays in softgl. */
static unsigned w3d_clear(void *user, const sgl_clear_rect *r) {
  w3d_state *s = (w3d_state *)user;
  unsigned done = 0;
  if (r->width != s->width || r->height != s->height || !lock(s)) return 0;
  if (r->mask & GL_COLOR_BUFFER_BIT) {
    if (sgl_W3D_ClearDrawRegion(s->w3d, (ULONG)r->color_value) == W3D_SUCCESS) {
      done |= GL_COLOR_BUFFER_BIT;
      s->soft_newer[0] = 0;
      s->hw_newer[0] = 1;
    }
  }
  if ((r->mask & GL_DEPTH_BUFFER_BIT) && s->has_z) {
    W3D_Double z = (W3D_Double)((uint32_t)r->depth_value << SGL_DEPTH_SHIFT) / (W3D_Double)SGL_Z_MAX;
    if (sgl_W3D_ClearZBuffer(s->w3d, &z) == W3D_SUCCESS) {
      done |= GL_DEPTH_BUFFER_BIT;
      s->soft_newer[1] = 0;
      s->hw_newer[1] = 1;
    }
  }
  /* The next use of the buffers by softgl copies them from the card. */
  if (done) s->ctx->be_pending |= SGL_BE_HARDWARE;
  return done;
}

/* The flush entry: glFinish waits for the card and unlocks it. */
static void w3d_flush(void *user, int finish) {
  w3d_state *s = (w3d_state *)user;
  if (!finish) return;
  if (s->locked) sgl_W3D_WaitIdle(s->w3d);
  unlock(s);
}

/* The texture_deleted entry: frees the card texture. */
static void w3d_texture_deleted(void *user, unsigned long id) {
  w3d_state *s = (w3d_state *)user;
  int i;
  for (i = 0; i < TEXTURES; i++)
    if (s->tex[i].id == id) free_tex(s, &s->tex[i]);
}

/* The present entry: copies what softgl drew last, finishes the frame
 * of the card, and flips the screen buffers. */
static void w3d_present(void *user, sgl_context *c, const uint32_t *pixels, int width, int height,
                        long rowpixels) {
  sgl_amiga_warp3d *w = (sgl_amiga_warp3d *)c->be_user;
  w3d_state *s = (w3d_state *)user;
  (void)pixels, (void)width, (void)height, (void)rowpixels;
  if (s->soft_newer[0] && lock(s)) {
    int y;
    for (y = 0; y < s->height; y++) copy_row(s, y, 1);
    s->soft_newer[0] = 0;
  }
  if (s->locked) sgl_W3D_FlushFrame(s->w3d);
  unlock(s);
  s->frame++;
  if (w->screen && w->buffers[0] && w->buffers[1]) {
    struct ScreenBuffer *shown = w->buffers[w->back];
    ChangeScreenBuffer(w->screen, shown);
    w->back ^= 1;
    sgl_W3D_SetDrawRegion(s->w3d, w->buffers[w->back]->sb_BitMap, 0, NULL);
    s->key_valid = 0;
    /* The new back buffer has an old frame. */
    s->hw_newer[0] = 1;
    s->soft_newer[0] = 0;
  }
}

/* Frees the Warp3D context, the textures and the library. */
static void w3d_free(w3d_state *s) {
  int i;
  if (!s) return;
  if (s->w3d) {
    unlock(s);
    for (i = 0; i < TEXTURES; i++) free_tex(s, &s->tex[i]);
    if (s->has_z) sgl_W3D_FreeZBuffer(s->w3d);
    sgl_W3D_DestroyContext(s->w3d);
  }
  if (s->zrow) FreeMem(s->zrow, (ULONG)s->width * sizeof(W3D_Double));
  if (s->zmask) FreeMem(s->zmask, (ULONG)s->width);
  FreeMem(s, sizeof *s);
}

/* The detach entry: frees all. */
static void w3d_detach(void *user, sgl_context *c) {
  sgl_amiga_warp3d *w = (sgl_amiga_warp3d *)c->be_user;
  (void)user;
  w3d_free((w3d_state *)w->state);
  w->state = NULL;
  if (sgl_Warp3DBase && w->opened) {
    CloseLibrary(sgl_Warp3DBase);
    sgl_Warp3DBase = NULL;
    w->opened = 0;
  }
}

/* The entries go to the state, not to the structure of the program:
 * these wrappers give each entry its state. */
static unsigned e_clear(void *u, const sgl_clear_rect *r) { return w3d_clear(((sgl_amiga_warp3d *)u)->state, r); }

/* Gives the triangle to the state. */
static int e_triangle(void *u, sgl_context *c, const sgl_hw_vertex *a, const sgl_hw_vertex *b,
                      const sgl_hw_vertex *d) {
  return w3d_triangle(((sgl_amiga_warp3d *)u)->state, c, a, b, d);
}

/* Gives the line to the state. */
static int e_line(void *u, sgl_context *c, const sgl_hw_vertex *a, const sgl_hw_vertex *b) {
  return w3d_line(((sgl_amiga_warp3d *)u)->state, c, a, b);
}

/* Gives the point to the state. */
static int e_point(void *u, sgl_context *c, const sgl_hw_vertex *a) {
  return w3d_point(((sgl_amiga_warp3d *)u)->state, c, a);
}

/* Gives the software call to the state. */
static void e_software(void *u, sgl_context *c) { w3d_software(((sgl_amiga_warp3d *)u)->state, c); }

/* Gives the deleted texture to the state. */
static void e_texture_deleted(void *u, unsigned long id) {
  w3d_texture_deleted(((sgl_amiga_warp3d *)u)->state, id);
}

/* Gives glFlush and glFinish to the state. */
static void e_flush(void *u, int finish) { w3d_flush(((sgl_amiga_warp3d *)u)->state, finish); }

/* Gives the frame to the state. */
static void e_present(void *u, sgl_context *c, const uint32_t *p, int w, int h, long rp) {
  w3d_present(((sgl_amiga_warp3d *)u)->state, c, p, w, h, rp);
}

/* The entries of the Warp3D back end. */
static const sgl_backend w3d_backend = {
    .clear = e_clear, .triangle = e_triangle, .line = e_line, .point = e_point,
    .software = e_software, .texture_deleted = e_texture_deleted, .flush = e_flush,
    .present = e_present, .detach = w3d_detach};

/* Gives the Warp3D back end to ctx (see softgl_amiga.h). */
int sgl_amiga_warp3d_attach(sgl_amiga_warp3d *w, sgl_context *ctx) {
  w3d_state *s;
  struct BitMap *bm;
  struct TagItem tags[3];
  ULONG err = 0, drivers;
  if (!w || !ctx) return 0;
  bm = w->buffers[0] && w->buffers[1] ? w->buffers[1]->sb_BitMap : w->bitmap;
  if (!bm) return 0;
  w->back = 1;
  w->opened = 0;
  if (!sgl_Warp3DBase) {
    sgl_Warp3DBase = OpenLibrary((CONST_STRPTR) "Warp3D.library", 2);
    if (!sgl_Warp3DBase) return 0;
    w->opened = 1;
  }
  /* The CPU driver of Warp3D is slower than softgl on a 68k. */
  drivers = sgl_W3D_CheckDriver();
  if (!(drivers & W3D_DRIVER_3DHW) && !(w->flags & SGL_AMIGA_W3D_ALLOW_CPU)) goto fail;
  s = (w3d_state *)AllocMem(sizeof *s, MEMF_ANY | MEMF_CLEAR);
  if (!s) goto fail;
  w->state = s;
  tags[0].ti_Tag = W3D_CC_BITMAP, tags[0].ti_Data = (IPTR)bm;
  tags[1].ti_Tag = W3D_CC_DRIVERTYPE, tags[1].ti_Data = W3D_DRIVER_BEST;
  tags[2].ti_Tag = TAG_DONE, tags[2].ti_Data = 0;
  s->w3d = sgl_W3D_CreateContext(&err, tags);
  if (!s->w3d || err != W3D_SUCCESS) {
    s->w3d = NULL;
    goto fail;
  }
  /* softgl draws 32-bit pixels; the copies know these formats. */
  if (s->w3d->format != W3D_FMT_A8R8G8B8 && s->w3d->format != W3D_FMT_B8G8R8A8 &&
      s->w3d->format != W3D_FMT_R5G6B5 && s->w3d->format != W3D_FMT_R5G6B5PC &&
      s->w3d->format != W3D_FMT_R5G5B5 && s->w3d->format != W3D_FMT_R5G5B5PC)
    goto fail;
  if (!has(s->w3d, W3D_Q_DRAW_TRIANGLE) || !has(s->w3d, W3D_Q_GOURAUDSHADING)) goto fail;
  s->ctx = ctx;
  s->width = s->w3d->width;
  s->height = s->w3d->height;
  s->features = (has(s->w3d, W3D_Q_DRAW_POINT) ? F_POINT : 0) | (has(s->w3d, W3D_Q_DRAW_LINE) ? F_LINE : 0) |
                (has(s->w3d, W3D_Q_TEXMAPPING) ? F_TEX : 0) | (has(s->w3d, W3D_Q_SPECULAR) ? F_SPEC : 0) |
                (has(s->w3d, W3D_Q_ALPHATEST) ? F_ALPHA : 0) | (has(s->w3d, W3D_Q_BLENDING) ? F_BLEND : 0) |
                (has(s->w3d, W3D_Q_SCISSOR) ? F_SCISSOR : 0) | (has(s->w3d, W3D_Q_PERSPECTIVE) ? F_PERSP : 0);
  s->max_tex = (int)sgl_W3D_Query(s->w3d, W3D_Q_MAXTEXWIDTH, 0);
  if ((int)sgl_W3D_Query(s->w3d, W3D_Q_MAXTEXHEIGHT, 0) < s->max_tex)
    s->max_tex = (int)sgl_W3D_Query(s->w3d, W3D_Q_MAXTEXHEIGHT, 0);
  if (has(s->w3d, W3D_Q_ZBUFFER) && sgl_W3D_AllocZBuffer(s->w3d) == W3D_SUCCESS) {
    s->has_z = 1;
    s->zrow = (W3D_Double *)AllocMem((ULONG)s->width * sizeof(W3D_Double), MEMF_ANY);
    s->zmask = (UBYTE *)AllocMem((ULONG)s->width, MEMF_ANY);
    if (!s->zrow || !s->zmask) goto fail;
    memset(s->zmask, 1, (size_t)s->width);
  }
  s->soft_newer[0] = s->soft_newer[1] = 1;
  if (!sgl_set_backend(ctx, &w3d_backend, w)) goto fail;
  return 1;
fail:
  w3d_free((w3d_state *)w->state);
  w->state = NULL;
  if (w->opened) {
    CloseLibrary(sgl_Warp3DBase);
    sgl_Warp3DBase = NULL;
    w->opened = 0;
  }
  return 0;
}
