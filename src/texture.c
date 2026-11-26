/* texture.c - the textures of the software OpenGL: texture objects,
 * images, parameters, the environment, coordinate generation and the
 * paletted textures of GL_EXT_paletted_texture.
 *
 * Texels are kept as 0xAARRGGBB. A luminance texel has the luminance in
 * red, green and blue; an intensity texel has it in all four.
 *
 * Part of clang-retro softgl, a software implementation of OpenGL.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include <math.h>
#include <stdlib.h>

#include "sgl.h"

/* Sets the parameters of a texture object to the values of the
 * specification. */
static void init_texture(sgl_texture *t, GLuint name, GLenum target) {
  memset(t, 0, sizeof *t);
  t->name = name;
  t->target = target;
  t->min_filter = GL_NEAREST_MIPMAP_LINEAR;
  t->mag_filter = GL_LINEAR;
  t->wrap_s = t->wrap_t = t->wrap_r = GL_REPEAT;
  t->max_level = 1000;
  t->min_lod = -1000.0f;
  t->max_lod = 1000.0f;
  t->priority = 1.0f;
  t->palette_format = GL_RGBA;
  t->refs = 1;
}

/* Frees the images of a texture object. */
static void free_levels(sgl_texture *t) {
  int i;
  for (i = 0; i < SGL_MAX_TEXTURE_LEVELS; i++) {
    free(t->level[i].texels);
    free(t->level[i].indices);
    t->level[i].texels = NULL;
    t->level[i].indices = NULL;
  }
}

/* Binds the default textures to all the texture units. */
static void bind_defaults(sgl_context *c) {
  int u;
  for (u = 0; u < SGL_MAX_TEXTURE_UNITS; u++) {
    c->tex_1d[u] = &c->default_1d;
    c->tex_2d[u] = &c->default_2d;
    c->tex_3d[u] = &c->default_3d;
  }
}

/* Sets up the default textures of a new context. All the texture units
 * share them, as the specification tells. */
void sgl_texture_init(sgl_context *c) {
  init_texture(&c->default_1d, 0, GL_TEXTURE_1D);
  init_texture(&c->default_2d, 0, GL_TEXTURE_2D);
  init_texture(&c->default_3d, 0, GL_TEXTURE_3D);
  bind_defaults(c);
}

/* Lets go of the textures that a context holds: its default textures
 * and its references to bound objects. */
void sgl_texture_release(sgl_context *c) {
  free_levels(&c->default_1d);
  free_levels(&c->default_2d);
  free_levels(&c->default_3d);
  bind_defaults(c);
}

/* Frees all the texture objects of a set of shared objects. */
void sgl_texture_free_all(sgl_shared *s) {
  int i;
  for (i = 0; i < 64; i++) {
    sgl_texture *t = s->textures[i];
    while (t) {
      sgl_texture *next = t->next;
      free_levels(t);
      free(t);
      t = next;
    }
    s->textures[i] = NULL;
  }
}

/* Finds a texture object by name, or gives NULL. */
static sgl_texture *find_texture(sgl_shared *s, GLuint name) {
  sgl_texture *t = s->textures[name & 63];
  while (t && t->name != name) t = t->next;
  return t;
}

/* Gives the texture of texture unit u for the primitives: the 3D
 * texture when it is on, else the 2D texture, else the 1D texture, or
 * NULL. */
sgl_texture *sgl_unit_texture(sgl_context *c, int u) {
  if (c->a.enabled[SGL_UCAP(u, SGL_U_TEXTURE_3D)]) return c->tex_3d[u];
  if (c->a.enabled[SGL_UCAP(u, SGL_U_TEXTURE_2D)]) return c->tex_2d[u];
  if (c->a.enabled[SGL_UCAP(u, SGL_U_TEXTURE_1D)]) return c->tex_1d[u];
  return NULL;
}

/* Gives the first level of a texture that is used (GL_TEXTURE_BASE_LEVEL),
 * or NULL when it has no image: then the unit acts as if its texture
 * is off. */
const sgl_level *sgl_base_level(const sgl_texture *t) {
  if (!t || t->base_level >= SGL_MAX_TEXTURE_LEVELS || !t->level[t->base_level].texels) return NULL;
  return &t->level[t->base_level];
}

/* Gives the texture units that have a texture on, as bits (bit u for
 * unit u). In color index mode there are no textures. */
unsigned sgl_texture_units(const sgl_context *c) {
  unsigned m = 0;
  int u;
  if (c->index_mode) return 0;
  for (u = 0; u < SGL_MAX_TEXTURE_UNITS; u++)
    if (c->a.enabled[SGL_UCAP(u, SGL_U_TEXTURE_1D)] || c->a.enabled[SGL_UCAP(u, SGL_U_TEXTURE_2D)] ||
        c->a.enabled[SGL_UCAP(u, SGL_U_TEXTURE_3D)])
      m |= 1u << u;
  return m;
}

/* Gives the bound texture of a target in the active texture unit, or
 * NULL for an invalid target. */
static sgl_texture *bound(sgl_context *c, GLenum target) {
  int u = c->a.active_texture;
  if (target == GL_TEXTURE_2D) return c->tex_2d[u];
  if (target == GL_TEXTURE_1D) return c->tex_1d[u];
  if (target == GL_TEXTURE_3D) return c->tex_3d[u];
  return NULL;
}

/* Gives the base format of an internal format, or 0 for an invalid
 * one. */
static GLenum base_format(GLint internal) {
  switch (internal) {
    case 1: case GL_LUMINANCE: case GL_LUMINANCE4: case GL_LUMINANCE8: case GL_LUMINANCE12:
    case GL_LUMINANCE16:
      return GL_LUMINANCE;
    case 2: case GL_LUMINANCE_ALPHA: case GL_LUMINANCE4_ALPHA4: case GL_LUMINANCE6_ALPHA2:
    case GL_LUMINANCE8_ALPHA8: case GL_LUMINANCE12_ALPHA4: case GL_LUMINANCE12_ALPHA12:
    case GL_LUMINANCE16_ALPHA16:
      return GL_LUMINANCE_ALPHA;
    case 3: case GL_RGB: case GL_R3_G3_B2: case GL_RGB4: case GL_RGB5: case GL_RGB8:
    case GL_RGB10: case GL_RGB12: case GL_RGB16:
      return GL_RGB;
    case 4: case GL_RGBA: case GL_RGBA2: case GL_RGBA4: case GL_RGB5_A1: case GL_RGBA8:
    case GL_RGB10_A2: case GL_RGBA12: case GL_RGBA16:
      return GL_RGBA;
    case GL_ALPHA: case GL_ALPHA4: case GL_ALPHA8: case GL_ALPHA12: case GL_ALPHA16:
      return GL_ALPHA;
    case GL_INTENSITY: case GL_INTENSITY4: case GL_INTENSITY8: case GL_INTENSITY12:
    case GL_INTENSITY16:
      return GL_INTENSITY;
    case GL_COLOR_INDEX1_EXT: case GL_COLOR_INDEX2_EXT: case GL_COLOR_INDEX4_EXT:
    case GL_COLOR_INDEX8_EXT: case GL_COLOR_INDEX12_EXT: case GL_COLOR_INDEX16_EXT:
      return GL_COLOR_INDEX;
    default:
      return 0;
  }
}

/* Gives log2(n) when n is a power of two, or -1. */
static int log2_exact(int n) {
  int i = 0;
  if (n <= 0 || (n & (n - 1))) return -1;
  while ((1 << i) < n) i++;
  return i;
}

/* Gives a texel of base format base from RGBA bytes p. */
static uint32_t texel(GLenum base, const uint8_t *p) {
  switch (base) {
    case GL_LUMINANCE:
      return 0xFF000000u | ((uint32_t)p[0] << 16) | ((uint32_t)p[0] << 8) | p[0];
    case GL_LUMINANCE_ALPHA:
      return ((uint32_t)p[3] << 24) | ((uint32_t)p[0] << 16) | ((uint32_t)p[0] << 8) | p[0];
    case GL_INTENSITY:
      return ((uint32_t)p[0] << 24) | ((uint32_t)p[0] << 16) | ((uint32_t)p[0] << 8) | p[0];
    case GL_ALPHA:
      return ((uint32_t)p[3] << 24) | 0x00FFFFFFu;
    case GL_RGB:
      return 0xFF000000u | ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
    default:
      return ((uint32_t)p[3] << 24) | ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
  }
}

/* Makes the texels of a paletted level from its indices and the
 * palette of the texture. */
static void expand_palette(sgl_texture *t, sgl_level *lv) {
  size_t i, n = (size_t)lv->width * lv->height * lv->depth;
  if (!lv->indices || !lv->texels) return;
  for (i = 0; i < n; i++) {
    int k = lv->indices[i];
    lv->texels[i] = k < t->palette_size ? t->palette[k] : 0xFF000000u;
  }
}

/* Computes if the texture has all the levels of a mipmap. */
static void check_complete(sgl_texture *t) {
  int i, b = t->base_level, w, h, d;
  t->complete = 0;
  if (b >= SGL_MAX_TEXTURE_LEVELS || !t->level[b].texels) return;
  w = t->level[b].width;
  h = t->level[b].height;
  d = t->level[b].depth;
  for (i = b + 1; i < SGL_MAX_TEXTURE_LEVELS && i <= t->max_level && (w > 1 || h > 1 || d > 1); i++) {
    w = w > 1 ? w / 2 : 1;
    h = h > 1 ? h / 2 : 1;
    d = d > 1 ? d / 2 : 1;
    if (!t->level[i].texels || t->level[i].width != w || t->level[i].height != h ||
        t->level[i].depth != d)
      return;
  }
  t->complete = 1;
}

/* Gives the pixel store of image z of a 3D image of rows of height h:
 * the rows of the image follow GL_UNPACK_SKIP_IMAGES and
 * GL_UNPACK_IMAGE_HEIGHT. For a 1D or 2D image (three is 0) it is ps. */
static sgl_pixelstore image_store(const sgl_pixelstore *ps, int three, int h, int z) {
  sgl_pixelstore p = *ps;
  if (three) p.skip_rows += (ps->skip_images + z) * (ps->image_height > 0 ? ps->image_height : h);
  return p;
}

/* Unpacks d images of W x H pixels to RGBA bytes rgba (W * H * 4 bytes
 * for each image). Gives 0 for an invalid format or type. */
static int unpack_images(const sgl_pixelstore *ps, int three, int W, int H, int d, GLenum format,
                         GLenum type, const void *pixels, uint8_t *rgba) {
  int z;
  for (z = 0; z < d; z++) {
    sgl_pixelstore p = image_store(ps, three, H, z);
    if (!sgl_unpack_rgba8(&p, W, H, format, type, pixels, rgba + (size_t)z * W * H * 4)) return 0;
  }
  return 1;
}

/* Unpacks d images of W x H color indices to bytes idx. */
static void unpack_index_images(const sgl_pixelstore *ps, int three, int W, int H, int d, GLenum type,
                                const void *pixels, uint8_t *idx) {
  int z;
  for (z = 0; z < d; z++) {
    sgl_pixelstore p = image_store(ps, three, H, z);
    sgl_unpack_index(&p, W, H, type, pixels, idx + (size_t)z * W * H);
  }
}

/* Tells if target is a target of glTexImage3D. */
static int is_3d(GLenum target) { return target == GL_TEXTURE_3D || target == GL_PROXY_TEXTURE_3D; }

/* Sets level lv of the bound texture of target to an image of w x h x d
 * pixels (d is 1 for a 1D or 2D texture). The border is in w, h and,
 * for a 3D texture, in d. Gives 1 on success. */
static int set_image(sgl_context *c, GLenum target, GLint level, GLint internal, int w, int h, int d,
                     GLint border, GLenum format, GLenum type, const void *pixels,
                     const sgl_pixelstore *ps) {
  sgl_texture *t;
  sgl_level *lv;
  GLenum base = base_format(internal);
  int three = is_3d(target), db = three ? border : 0;
  int wl = log2_exact(w - 2 * border), hl = log2_exact(h - 2 * border), dl = log2_exact(d - 2 * db);
  int max = three ? SGL_MAX_3D_TEXTURE_SIZE : SGL_MAX_TEXTURE_SIZE;
  int W = w, H = h, D = d, x, y, z;
  size_t n;
  if (level < 0 || level >= SGL_MAX_TEXTURE_LEVELS || border < 0 || border > 1 || wl < 0 ||
      hl < 0 || dl < 0 || w > max + 2 || h > max + 2 || d > max + 2) {
    sgl_error(GL_INVALID_VALUE);
    return 0;
  }
  if (target == GL_PROXY_TEXTURE_2D || target == GL_PROXY_TEXTURE_1D || target == GL_PROXY_TEXTURE_3D)
    return 1;
  t = bound(c, target);
  if (!t) {
    sgl_error(GL_INVALID_ENUM);
    return 0;
  }
  if (!base) {
    sgl_error(GL_INVALID_VALUE);
    return 0;
  }
  if (sgl_format_components(format) == 0) {
    sgl_error(GL_INVALID_ENUM);
    return 0;
  }
  /* The border texels are dropped: the texture is the inside. */
  w -= 2 * border;
  h -= 2 * border;
  d -= 2 * db;
  lv = &t->level[level];
  n = (size_t)w * h * d;
  free(lv->texels);
  free(lv->indices);
  lv->indices = NULL;
  lv->texels = (uint32_t *)malloc(n * 4);
  if (!lv->texels) {
    sgl_error(GL_OUT_OF_MEMORY);
    return 0;
  }
  lv->width = w;
  lv->height = h;
  lv->depth = d;
  lv->wlog = wl;
  lv->hlog = hl;
  lv->dlog = dl;
  lv->internal = (GLenum)internal;
  lv->border = border;
  if (base == GL_COLOR_INDEX) {
    lv->base = base_format((GLint)t->palette_format);
    if (lv->base == 0 || lv->base == GL_COLOR_INDEX) lv->base = GL_RGBA;
    lv->indices = (uint8_t *)malloc(n ? n : 1);
    if (!lv->indices) {
      sgl_error(GL_OUT_OF_MEMORY);
      return 0;
    }
    if (pixels) {
      uint8_t *all = (uint8_t *)malloc((size_t)W * H * D);
      if (!all) {
        sgl_error(GL_OUT_OF_MEMORY);
        return 0;
      }
      unpack_index_images(ps, three, W, H, D, type, pixels, all);
      for (z = 0; z < d; z++)
        for (y = 0; y < h; y++)
          memcpy(lv->indices + ((size_t)z * h + y) * w,
                 all + (((size_t)(z + db) * H + y + border) * W + border), (size_t)w);
      free(all);
    } else {
      memset(lv->indices, 0, n);
    }
    expand_palette(t, lv);
  } else {
    lv->base = base;
    if (pixels) {
      uint8_t *rgba = (uint8_t *)malloc((size_t)W * H * D * 4);
      if (!rgba) {
        sgl_error(GL_OUT_OF_MEMORY);
        return 0;
      }
      if (!unpack_images(ps, three, W, H, D, format, type, pixels, rgba)) {
        free(rgba);
        sgl_error(GL_INVALID_ENUM);
        return 0;
      }
      for (z = 0; z < d; z++)
        for (y = 0; y < h; y++) {
          const uint8_t *src = rgba + (((size_t)(z + db) * H + y + border) * W + border) * 4;
          uint32_t *dst = lv->texels + ((size_t)z * h + y) * w;
          for (x = 0; x < w; x++, src += 4) dst[x] = texel(base, src);
        }
      free(rgba);
    } else {
      memset(lv->texels, 0, n * 4);
    }
  }
  check_complete(t);
  return 1;
}

/* glTexImage2D: sets a level of the 2D texture. */
void APIENTRY sgl_TexImage2D(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h,
                             GLint border, GLenum format, GLenum type, const void *pixels) {
  sgl_context *c = sgl_ctx;
  if (target != GL_TEXTURE_2D && target != GL_PROXY_TEXTURE_2D) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  set_image(c, target, level, internal, w, h, 1, border, format, type, pixels, &c->c.unpack);
}

/* glTexImage1D: sets a level of the 1D texture (a 2D texture one texel
 * high). */
void APIENTRY sgl_TexImage1D(GLenum target, GLint level, GLint internal, GLsizei w, GLint border,
                             GLenum format, GLenum type, const void *pixels) {
  sgl_context *c = sgl_ctx;
  if (target != GL_TEXTURE_1D && target != GL_PROXY_TEXTURE_1D) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  set_image(c, target, level, internal, w, 1 + 2 * border, 1, border, format, type, pixels,
            &c->c.unpack);
}

/* Changes texels [x, x + w) x [y, y + h) x [z, z + d) of level lv of
 * the bound texture of target. */
static void sub_image(sgl_context *c, GLenum target, GLint level, int x, int y, int z, int w, int h,
                      int d, GLenum format, GLenum type, const void *pixels,
                      const sgl_pixelstore *ps) {
  sgl_texture *t = bound(c, target);
  sgl_level *lv;
  int i, j, k, three = is_3d(target);
  if (!t) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  if (level < 0 || level >= SGL_MAX_TEXTURE_LEVELS) {
    sgl_error(GL_INVALID_VALUE);
    return;
  }
  lv = &t->level[level];
  if (!lv->texels || x < 0 || y < 0 || z < 0 || x + w > lv->width || y + h > lv->height ||
      z + d > lv->depth) {
    sgl_error(GL_INVALID_VALUE);
    return;
  }
  if (w <= 0 || h <= 0 || d <= 0) return;
  if (lv->indices && format == GL_COLOR_INDEX) {
    uint8_t *idx = (uint8_t *)malloc((size_t)w * h * d);
    if (!idx) {
      sgl_error(GL_OUT_OF_MEMORY);
      return;
    }
    unpack_index_images(ps, three, w, h, d, type, pixels, idx);
    for (k = 0; k < d; k++)
      for (j = 0; j < h; j++)
        memcpy(lv->indices + ((size_t)(z + k) * lv->height + y + j) * lv->width + x,
               idx + ((size_t)k * h + j) * w, (size_t)w);
    free(idx);
    expand_palette(t, lv);
  } else {
    uint8_t *rgba = (uint8_t *)malloc((size_t)w * h * d * 4);
    if (!rgba) {
      sgl_error(GL_OUT_OF_MEMORY);
      return;
    }
    if (!unpack_images(ps, three, w, h, d, format, type, pixels, rgba)) {
      free(rgba);
      sgl_error(GL_INVALID_ENUM);
      return;
    }
    for (k = 0; k < d; k++)
      for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
          lv->texels[((size_t)(z + k) * lv->height + y + j) * lv->width + x + i] =
              texel(lv->base, rgba + (((size_t)k * h + j) * w + i) * 4);
    free(rgba);
  }
}

/* glTexSubImage2D: changes a rectangle of a level of the 2D texture. */
void APIENTRY sgl_TexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h,
                                GLenum format, GLenum type, const void *pixels) {
  if (target != GL_TEXTURE_2D) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  sub_image(sgl_ctx, target, level, x, y, 0, w, h, 1, format, type, pixels, &sgl_ctx->c.unpack);
}

/* glTexSubImage1D: changes a span of a level of the 1D texture. */
void APIENTRY sgl_TexSubImage1D(GLenum target, GLint level, GLint x, GLsizei w, GLenum format,
                                GLenum type, const void *pixels) {
  if (target != GL_TEXTURE_1D) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  sub_image(sgl_ctx, target, level, x, 0, 0, w, 1, 1, format, type, pixels, &sgl_ctx->c.unpack);
}

/* Reads w x h pixels of the color buffer at x, y as RGBA bytes, rows
 * bottom first. */
static uint8_t *read_color(sgl_context *c, int x, int y, int w, int h) {
  uint8_t *p = (uint8_t *)malloc((size_t)w * h * 4 + 4);
  int i, j;
  if (!p) return NULL;
  for (j = 0; j < h; j++)
    for (i = 0; i < w; i++) {
      int px = x + i, py = y + j;
      uint32_t v = 0;
      uint8_t *o = p + ((size_t)j * w + i) * 4;
      if (c->color && px >= 0 && py >= 0 && px < c->width && py < c->height)
        v = c->color[(long)(c->height - 1 - py) * c->rowpixels + px];
      o[0] = (uint8_t)(v >> 16);
      o[1] = (uint8_t)(v >> 8);
      o[2] = (uint8_t)v;
      o[3] = (uint8_t)(v >> 24);
    }
  return p;
}

/* glCopyTexImage2D: sets a level of the 2D texture from the color
 * buffer. */
void APIENTRY sgl_CopyTexImage2D(GLenum target, GLint level, GLenum internal, GLint x, GLint y,
                                 GLsizei w, GLsizei h, GLint border) {
  sgl_context *c = sgl_ctx;
  uint8_t *p;
  if (is_3d(target)) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  p = read_color(c, x, y, w, h);
  if (!p) {
    sgl_error(GL_OUT_OF_MEMORY);
    return;
  }
  set_image(c, target, level, (GLint)internal, w, h, 1, border, GL_RGBA, GL_UNSIGNED_BYTE, p,
            &sgl_tight_store);
  free(p);
}

/* glCopyTexImage1D: sets a level of the 1D texture from the color
 * buffer. */
void APIENTRY sgl_CopyTexImage1D(GLenum target, GLint level, GLenum internal, GLint x, GLint y,
                                 GLsizei w, GLint border) {
  sgl_context *c = sgl_ctx;
  uint8_t *p;
  if (is_3d(target)) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  p = read_color(c, x, y, w, 1);
  if (!p) {
    sgl_error(GL_OUT_OF_MEMORY);
    return;
  }
  set_image(c, target, level, (GLint)internal, w, 1, 1, border, GL_RGBA, GL_UNSIGNED_BYTE, p,
            &sgl_tight_store);
  free(p);
}

/* glCopyTexSubImage2D: changes a rectangle of the 2D texture from the
 * color buffer. */
void APIENTRY sgl_CopyTexSubImage2D(GLenum target, GLint level, GLint xoff, GLint yoff, GLint x,
                                    GLint y, GLsizei w, GLsizei h) {
  sgl_context *c = sgl_ctx;
  uint8_t *p = read_color(c, x, y, w, h);
  if (!p) {
    sgl_error(GL_OUT_OF_MEMORY);
    return;
  }
  sub_image(c, target, level, xoff, yoff, 0, w, h, 1, GL_RGBA, GL_UNSIGNED_BYTE, p, &sgl_tight_store);
  free(p);
}

/* glCopyTexSubImage1D: changes a span of the 1D texture from the color
 * buffer. */
void APIENTRY sgl_CopyTexSubImage1D(GLenum target, GLint level, GLint xoff, GLint x, GLint y,
                                    GLsizei w) {
  sgl_context *c = sgl_ctx;
  uint8_t *p = read_color(c, x, y, w, 1);
  if (!p) {
    sgl_error(GL_OUT_OF_MEMORY);
    return;
  }
  sub_image(c, target, level, xoff, 0, 0, w, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p, &sgl_tight_store);
  free(p);
}

/* glTexImage3D: sets a level of the 3D texture. */
void APIENTRY sgl_TexImage3D(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h,
                             GLsizei d, GLint border, GLenum format, GLenum type, const void *pixels) {
  sgl_context *c = sgl_ctx;
  if (!is_3d(target)) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  set_image(c, target, level, internal, w, h, d, border, format, type, pixels, &c->c.unpack);
}

/* glTexSubImage3D: changes a box of a level of the 3D texture. */
void APIENTRY sgl_TexSubImage3D(GLenum target, GLint level, GLint x, GLint y, GLint z, GLsizei w,
                                GLsizei h, GLsizei d, GLenum format, GLenum type, const void *pixels) {
  if (target != GL_TEXTURE_3D) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  sub_image(sgl_ctx, target, level, x, y, z, w, h, d, format, type, pixels, &sgl_ctx->c.unpack);
}

/* glCopyTexSubImage3D: changes a rectangle of image z of the 3D texture
 * from the color buffer. */
void APIENTRY sgl_CopyTexSubImage3D(GLenum target, GLint level, GLint xoff, GLint yoff, GLint zoff,
                                    GLint x, GLint y, GLsizei w, GLsizei h) {
  sgl_context *c = sgl_ctx;
  uint8_t *p;
  if (target != GL_TEXTURE_3D) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  p = read_color(c, x, y, w, h);
  if (!p) {
    sgl_error(GL_OUT_OF_MEMORY);
    return;
  }
  sub_image(c, target, level, xoff, yoff, zoff, w, h, 1, GL_RGBA, GL_UNSIGNED_BYTE, p, &sgl_tight_store);
  free(p);
}

/* glGetTexImage: gives a level of a texture. */
void APIENTRY sgl_GetTexImage(GLenum target, GLint level, GLenum format, GLenum type, void *pixels) {
  sgl_context *c = sgl_ctx;
  sgl_texture *t = bound(c, target);
  if (!t || level < 0 || level >= SGL_MAX_TEXTURE_LEVELS) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  if (!t->level[level].texels) return;
  /* The images of a 3D texture follow each other as rows. */
  sgl_pack_texels(c, t->level[level].width, t->level[level].height * t->level[level].depth,
                  t->level[level].texels, format,
                  type, pixels);
}

/* glTexParameterfv: sets a parameter of the bound texture of target. */
void APIENTRY sgl_TexParameterfv(GLenum target, GLenum pname, const GLfloat *params) {
  sgl_texture *t = bound(sgl_ctx, target);
  GLenum v = (GLenum)params[0];
  if (!t) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  switch (pname) {
    case GL_TEXTURE_MIN_FILTER:
      if (v != GL_NEAREST && v != GL_LINEAR && v != GL_NEAREST_MIPMAP_NEAREST &&
          v != GL_LINEAR_MIPMAP_NEAREST && v != GL_NEAREST_MIPMAP_LINEAR && v != GL_LINEAR_MIPMAP_LINEAR) {
        sgl_error(GL_INVALID_ENUM);
        return;
      }
      t->min_filter = v;
      break;
    case GL_TEXTURE_MAG_FILTER:
      if (v != GL_NEAREST && v != GL_LINEAR) {
        sgl_error(GL_INVALID_ENUM);
        return;
      }
      t->mag_filter = v;
      break;
    case GL_TEXTURE_WRAP_S:
    case GL_TEXTURE_WRAP_T:
    case GL_TEXTURE_WRAP_R:
      /* GL_CLAMP is done as GL_CLAMP_TO_EDGE: the border texels are not
       * kept. */
      if (v != GL_REPEAT && v != GL_CLAMP && v != GL_CLAMP_TO_EDGE && v != GL_CLAMP_TO_BORDER &&
          v != GL_MIRRORED_REPEAT) {
        sgl_error(GL_INVALID_ENUM);
        return;
      }
      if (pname == GL_TEXTURE_WRAP_S)
        t->wrap_s = v;
      else if (pname == GL_TEXTURE_WRAP_T)
        t->wrap_t = v;
      else
        t->wrap_r = v;
      break;
    case GL_TEXTURE_BASE_LEVEL:
    case GL_TEXTURE_MAX_LEVEL:
      if (params[0] < 0.0f) {
        sgl_error(GL_INVALID_VALUE);
        return;
      }
      if (pname == GL_TEXTURE_BASE_LEVEL)
        t->base_level = (GLint)params[0];
      else
        t->max_level = (GLint)params[0];
      check_complete(t);
      break;
    case GL_TEXTURE_MIN_LOD: t->min_lod = params[0]; break;
    case GL_TEXTURE_MAX_LOD: t->max_lod = params[0]; break;
    case GL_TEXTURE_BORDER_COLOR: memcpy(t->border_color, params, sizeof t->border_color); break;
    case GL_TEXTURE_PRIORITY: t->priority = sgl_clamp01(params[0]); break;
    default: sgl_error(GL_INVALID_ENUM); break;
  }
}

/* glGetTexParameterfv: gives a parameter of the bound texture. */
void APIENTRY sgl_GetTexParameterfv(GLenum target, GLenum pname, GLfloat *params) {
  sgl_texture *t = bound(sgl_ctx, target);
  if (!t) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  switch (pname) {
    case GL_TEXTURE_MIN_FILTER: params[0] = (GLfloat)t->min_filter; break;
    case GL_TEXTURE_MAG_FILTER: params[0] = (GLfloat)t->mag_filter; break;
    case GL_TEXTURE_WRAP_S: params[0] = (GLfloat)t->wrap_s; break;
    case GL_TEXTURE_WRAP_T: params[0] = (GLfloat)t->wrap_t; break;
    case GL_TEXTURE_WRAP_R: params[0] = (GLfloat)t->wrap_r; break;
    case GL_TEXTURE_BASE_LEVEL: params[0] = (GLfloat)t->base_level; break;
    case GL_TEXTURE_MAX_LEVEL: params[0] = (GLfloat)t->max_level; break;
    case GL_TEXTURE_MIN_LOD: params[0] = t->min_lod; break;
    case GL_TEXTURE_MAX_LOD: params[0] = t->max_lod; break;
    case GL_TEXTURE_BORDER_COLOR: memcpy(params, t->border_color, sizeof t->border_color); break;
    case GL_TEXTURE_PRIORITY: params[0] = t->priority; break;
    case GL_TEXTURE_RESIDENT: params[0] = 1.0f; break;
    default: sgl_error(GL_INVALID_ENUM); break;
  }
}

/* glGetTexParameteriv: as glGetTexParameterfv, as integers. */
void APIENTRY sgl_GetTexParameteriv(GLenum target, GLenum pname, GLint *params) {
  GLfloat f[4];
  int i, n = pname == GL_TEXTURE_BORDER_COLOR ? 4 : 1;
  sgl_GetTexParameterfv(target, pname, f);
  for (i = 0; i < n; i++)
    params[i] = pname == GL_TEXTURE_BORDER_COLOR ? (GLint)(f[i] * 2147483647.0f) : (GLint)f[i];
}

/* glGetTexLevelParameterfv: gives a value of a level of a texture. */
void APIENTRY sgl_GetTexLevelParameterfv(GLenum target, GLint level, GLenum pname, GLfloat *params) {
  GLint i;
  sgl_GetTexLevelParameteriv(target, level, pname, &i);
  params[0] = (GLfloat)i;
}

/* glGetTexLevelParameteriv: gives a value of a level of a texture. For
 * a proxy target the values are those of a texture that would fit. */
void APIENTRY sgl_GetTexLevelParameteriv(GLenum target, GLint level, GLenum pname, GLint *params) {
  sgl_context *c = sgl_ctx;
  const sgl_level *lv;
  sgl_texture *t;
  if (target == GL_PROXY_TEXTURE_2D || target == GL_PROXY_TEXTURE_1D) {
    /* Every proxy image of a valid size fits. */
    params[0] = pname == GL_TEXTURE_WIDTH || pname == GL_TEXTURE_HEIGHT ? SGL_MAX_TEXTURE_SIZE
                : pname == GL_TEXTURE_DEPTH ? 1 : 0;
    return;
  }
  if (target == GL_PROXY_TEXTURE_3D) {
    params[0] = pname == GL_TEXTURE_WIDTH || pname == GL_TEXTURE_HEIGHT || pname == GL_TEXTURE_DEPTH
                    ? SGL_MAX_3D_TEXTURE_SIZE : 0;
    return;
  }
  t = bound(c, target);
  if (!t || level < 0 || level >= SGL_MAX_TEXTURE_LEVELS) {
    sgl_error(GL_INVALID_VALUE);
    return;
  }
  lv = &t->level[level];
  switch (pname) {
    case GL_TEXTURE_WIDTH: params[0] = lv->texels ? lv->width + 2 * lv->border : 0; break;
    case GL_TEXTURE_HEIGHT: params[0] = lv->texels ? lv->height + 2 * lv->border : 0; break;
    case GL_TEXTURE_DEPTH:
      params[0] = !lv->texels ? 0 : lv->depth + (t->target == GL_TEXTURE_3D ? 2 * lv->border : 0);
      break;
    case GL_TEXTURE_BORDER: params[0] = lv->border; break;
    case GL_TEXTURE_INTERNAL_FORMAT: params[0] = lv->texels ? (GLint)lv->internal : 1; break;
    case GL_TEXTURE_RED_SIZE: case GL_TEXTURE_GREEN_SIZE: case GL_TEXTURE_BLUE_SIZE:
    case GL_TEXTURE_ALPHA_SIZE: case GL_TEXTURE_LUMINANCE_SIZE: case GL_TEXTURE_INTENSITY_SIZE:
      params[0] = lv->texels ? 8 : 0;
      break;
    default: sgl_error(GL_INVALID_ENUM); break;
  }
}

/* glTexEnvfv: sets a parameter of the texture environment. */
void APIENTRY sgl_TexEnvfv(GLenum target, GLenum pname, const GLfloat *params) {
  sgl_context *c = sgl_ctx;
  sgl_unit_attrib *u = &c->a.unit[c->a.active_texture];
  if (target != GL_TEXTURE_ENV) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  if (pname == GL_TEXTURE_ENV_MODE) {
    GLenum m = (GLenum)params[0];
    if (m != GL_MODULATE && m != GL_DECAL && m != GL_BLEND && m != GL_REPLACE && m != GL_ADD) {
      sgl_error(GL_INVALID_ENUM);
      return;
    }
    u->env_mode = m;
  } else if (pname == GL_TEXTURE_ENV_COLOR) {
    int i;
    for (i = 0; i < 4; i++) u->env_color[i] = sgl_clamp01(params[i]);
  } else
    sgl_error(GL_INVALID_ENUM);
}

/* glGetTexEnvfv: gives a parameter of the texture environment. */
void APIENTRY sgl_GetTexEnvfv(GLenum target, GLenum pname, GLfloat *params) {
  sgl_context *c = sgl_ctx;
  (void)target;
  const sgl_unit_attrib *u = &c->a.unit[c->a.active_texture];
  if (pname == GL_TEXTURE_ENV_MODE)
    params[0] = (GLfloat)u->env_mode;
  else if (pname == GL_TEXTURE_ENV_COLOR)
    memcpy(params, u->env_color, sizeof u->env_color);
  else
    sgl_error(GL_INVALID_ENUM);
}

/* glGetTexEnviv: as glGetTexEnvfv, as integers. */
void APIENTRY sgl_GetTexEnviv(GLenum target, GLenum pname, GLint *params) {
  sgl_context *c = sgl_ctx;
  (void)target;
  const sgl_unit_attrib *u = &c->a.unit[c->a.active_texture];
  if (pname == GL_TEXTURE_ENV_MODE)
    params[0] = (GLint)u->env_mode;
  else if (pname == GL_TEXTURE_ENV_COLOR) {
    int i;
    for (i = 0; i < 4; i++) params[i] = (GLint)(u->env_color[i] * 2147483647.0f);
  } else
    sgl_error(GL_INVALID_ENUM);
}

/* glTexGenfv: sets a parameter of the generation of a texture
 * coordinate. An eye plane is kept in eye coordinates. */
void APIENTRY sgl_TexGenfv(GLenum coord, GLenum pname, const GLfloat *params) {
  sgl_context *c = sgl_ctx;
  sgl_unit_attrib *u = &c->a.unit[c->a.active_texture];
  int i = (int)coord - GL_S;
  if (i < 0 || i > 3) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  switch (pname) {
    case GL_TEXTURE_GEN_MODE: {
      GLenum m = (GLenum)params[0];
      if (m != GL_OBJECT_LINEAR && m != GL_EYE_LINEAR && (m != GL_SPHERE_MAP || i > 1)) {
        sgl_error(GL_INVALID_ENUM);
        return;
      }
      u->gen_mode[i] = m;
      break;
    }
    case GL_OBJECT_PLANE: memcpy(u->gen_object[i], params, 4 * sizeof(GLfloat)); break;
    case GL_EYE_PLANE: {
      const GLfloat *m;
      GLfloat *p = u->gen_eye[i];
      sgl_update_inverse(c);
      m = c->inv_modelview.m;
      p[0] = params[0] * m[0] + params[1] * m[1] + params[2] * m[2] + params[3] * m[3];
      p[1] = params[0] * m[4] + params[1] * m[5] + params[2] * m[6] + params[3] * m[7];
      p[2] = params[0] * m[8] + params[1] * m[9] + params[2] * m[10] + params[3] * m[11];
      p[3] = params[0] * m[12] + params[1] * m[13] + params[2] * m[14] + params[3] * m[15];
      break;
    }
    default: sgl_error(GL_INVALID_ENUM); break;
  }
}

/* glGetTexGenfv: gives a parameter of the generation of a coordinate. */
void APIENTRY sgl_GetTexGenfv(GLenum coord, GLenum pname, GLfloat *params) {
  sgl_context *c = sgl_ctx;
  const sgl_unit_attrib *u = &c->a.unit[c->a.active_texture];
  int i = (int)coord - GL_S;
  if (i < 0 || i > 3) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  switch (pname) {
    case GL_TEXTURE_GEN_MODE: params[0] = (GLfloat)u->gen_mode[i]; break;
    case GL_OBJECT_PLANE: memcpy(params, u->gen_object[i], 4 * sizeof(GLfloat)); break;
    case GL_EYE_PLANE: memcpy(params, u->gen_eye[i], 4 * sizeof(GLfloat)); break;
    default: sgl_error(GL_INVALID_ENUM); break;
  }
}

/* glGetTexGendv: as glGetTexGenfv, as doubles. */
void APIENTRY sgl_GetTexGendv(GLenum coord, GLenum pname, GLdouble *params) {
  GLfloat f[4];
  int i, n = pname == GL_TEXTURE_GEN_MODE ? 1 : 4;
  sgl_GetTexGenfv(coord, pname, f);
  for (i = 0; i < n; i++) params[i] = f[i];
}

/* glGetTexGeniv: as glGetTexGenfv, as integers. */
void APIENTRY sgl_GetTexGeniv(GLenum coord, GLenum pname, GLint *params) {
  GLfloat f[4];
  int i, n = pname == GL_TEXTURE_GEN_MODE ? 1 : 4;
  sgl_GetTexGenfv(coord, pname, f);
  for (i = 0; i < n; i++) params[i] = (GLint)f[i];
}

/* glGenTextures: gives n names that are not in use. */
void APIENTRY sgl_GenTextures(GLsizei n, GLuint *textures) {
  sgl_shared *s = sgl_ctx->shared;
  int i;
  if (n < 0) {
    sgl_error(GL_INVALID_VALUE);
    return;
  }
  for (i = 0; i < n; i++) {
    while (find_texture(s, s->next_texture) || s->next_texture == 0) s->next_texture++;
    textures[i] = s->next_texture++;
  }
}

/* glBindTexture: makes a texture object the texture of target. A new
 * name makes a new object. */
void APIENTRY sgl_BindTexture(GLenum target, GLuint name) {
  sgl_context *c = sgl_ctx;
  sgl_shared *s = c->shared;
  sgl_texture *t;
  if (target != GL_TEXTURE_1D && target != GL_TEXTURE_2D && target != GL_TEXTURE_3D) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  if (name == 0) {
    t = target == GL_TEXTURE_2D ? &c->default_2d
        : target == GL_TEXTURE_3D ? &c->default_3d : &c->default_1d;
  } else {
    t = find_texture(s, name);
    if (!t) {
      t = (sgl_texture *)malloc(sizeof *t);
      if (!t) {
        sgl_error(GL_OUT_OF_MEMORY);
        return;
      }
      init_texture(t, name, target);
      t->next = s->textures[name & 63];
      s->textures[name & 63] = t;
    } else if (t->target != target) {
      sgl_error(GL_INVALID_OPERATION);
      return;
    }
  }
  if (target == GL_TEXTURE_2D)
    c->tex_2d[c->a.active_texture] = t;
  else if (target == GL_TEXTURE_3D)
    c->tex_3d[c->a.active_texture] = t;
  else
    c->tex_1d[c->a.active_texture] = t;
}

/* glDeleteTextures: deletes texture objects. A bound one is unbound. */
void APIENTRY sgl_DeleteTextures(GLsizei n, const GLuint *textures) {
  sgl_context *c = sgl_ctx;
  sgl_shared *s = c->shared;
  int i, u;
  for (i = 0; i < n; i++) {
    GLuint name = textures[i];
    sgl_texture **pp, *t;
    if (name == 0) continue;
    pp = &s->textures[name & 63];
    while (*pp && (*pp)->name != name) pp = &(*pp)->next;
    t = *pp;
    if (!t) continue;
    *pp = t->next;
    for (u = 0; u < SGL_MAX_TEXTURE_UNITS; u++) {
      if (c->tex_2d[u] == t) c->tex_2d[u] = &c->default_2d;
      if (c->tex_1d[u] == t) c->tex_1d[u] = &c->default_1d;
      if (c->tex_3d[u] == t) c->tex_3d[u] = &c->default_3d;
    }
    free_levels(t);
    free(t);
  }
}

/* glIsTexture: tells if name is a texture object. */
GLboolean APIENTRY sgl_IsTexture(GLuint name) {
  return name && find_texture(sgl_ctx->shared, name) ? GL_TRUE : GL_FALSE;
}

/* glAreTexturesResident: all textures are in memory. */
GLboolean APIENTRY sgl_AreTexturesResident(GLsizei n, const GLuint *textures, GLboolean *residences) {
  int i;
  (void)textures;
  for (i = 0; i < n; i++) residences[i] = GL_TRUE;
  return GL_TRUE;
}

/* glPrioritizeTextures: keeps the priorities. */
void APIENTRY sgl_PrioritizeTextures(GLsizei n, const GLuint *textures, const GLclampf *priorities) {
  int i;
  for (i = 0; i < n; i++) {
    sgl_texture *t = find_texture(sgl_ctx->shared, textures[i]);
    if (t) t->priority = sgl_clamp01(priorities[i]);
  }
}

/* Sets entries [start, start + count) of the palette of a texture from
 * an image of count pixels. */
static void set_palette(sgl_texture *t, int start, int count, GLenum format, GLenum type,
                        const void *data) {
  uint8_t rgba[256 * 4];
  int i;
  if (start < 0 || count < 0 || start + count > 256) {
    sgl_error(GL_INVALID_VALUE);
    return;
  }
  if (!data || count == 0) return;
  if (!sgl_unpack_rgba8(&sgl_ctx->c.unpack, count, 1, format, type, data, rgba)) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  for (i = 0; i < count; i++) t->palette[start + i] = texel(base_format((GLint)t->palette_format), rgba + i * 4);
  for (i = 0; i < SGL_MAX_TEXTURE_LEVELS; i++) expand_palette(t, &t->level[i]);
}

/* glColorTableEXT: sets the palette of the bound texture. For a proxy
 * target it only records the size, which is good up to 256. */
void APIENTRY sgl_ColorTableEXT(GLenum target, GLenum internal, GLsizei width, GLenum format,
                                GLenum type, const void *table) {
  sgl_context *c = sgl_ctx;
  sgl_texture *t;
  int i;
  if (width < 1 || width > 256 || (width & (width - 1))) {
    sgl_error(GL_INVALID_VALUE);
    return;
  }
  if (target == GL_PROXY_TEXTURE_2D || target == GL_PROXY_TEXTURE_1D) {
    c->proxy_palette_size = width;
    return;
  }
  t = bound(c, target);
  if (!t) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  t->palette_format = internal;
  t->palette_size = width;
  for (i = 0; i < SGL_MAX_TEXTURE_LEVELS; i++)
    if (t->level[i].indices) {
      t->level[i].base = base_format((GLint)internal);
      if (!t->level[i].base || t->level[i].base == GL_COLOR_INDEX) t->level[i].base = GL_RGBA;
    }
  set_palette(t, 0, width, format, type, table);
}

/* glColorSubTableEXT: changes entries of the palette of the bound
 * texture. */
void APIENTRY sgl_ColorSubTableEXT(GLenum target, GLsizei start, GLsizei count, GLenum format,
                                   GLenum type, const void *data) {
  sgl_texture *t = bound(sgl_ctx, target);
  if (!t) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  set_palette(t, start, count, format, type, data);
}

/* glGetColorTableEXT: gives the palette of the bound texture. */
void APIENTRY sgl_GetColorTableEXT(GLenum target, GLenum format, GLenum type, void *data) {
  sgl_context *c = sgl_ctx;
  sgl_texture *t = bound(c, target);
  if (!t) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  sgl_pack_texels(c, t->palette_size, 1, t->palette, format, type, data);
}

/* glGetColorTableParameterivEXT: gives a value of a palette. */
void APIENTRY sgl_GetColorTableParameterivEXT(GLenum target, GLenum pname, GLint *params) {
  sgl_context *c = sgl_ctx;
  int size;
  GLenum fmt = GL_RGBA;
  if (target == GL_PROXY_TEXTURE_2D || target == GL_PROXY_TEXTURE_1D) {
    size = c->proxy_palette_size;
  } else {
    sgl_texture *t = bound(c, target);
    if (!t) {
      sgl_error(GL_INVALID_ENUM);
      return;
    }
    size = t->palette_size;
    fmt = t->palette_format;
  }
  switch (pname) {
    case GL_COLOR_TABLE_WIDTH_EXT: params[0] = size; break;
    case GL_COLOR_TABLE_FORMAT_EXT: params[0] = (GLint)fmt; break;
    case GL_COLOR_TABLE_RED_SIZE_EXT: case GL_COLOR_TABLE_GREEN_SIZE_EXT:
    case GL_COLOR_TABLE_BLUE_SIZE_EXT: case GL_COLOR_TABLE_ALPHA_SIZE_EXT:
      params[0] = 8;
      break;
    default: params[0] = 0; break;
  }
}

/* glGetColorTableParameterfvEXT: as glGetColorTableParameterivEXT. */
void APIENTRY sgl_GetColorTableParameterfvEXT(GLenum target, GLenum pname, GLfloat *params) {
  GLint i = 0;
  sgl_GetColorTableParameterivEXT(target, pname, &i);
  params[0] = (GLfloat)i;
}

/* Gives the texture unit of a name GL_TEXTURE0 + i, or -1 (with
 * GL_INVALID_ENUM) for a unit that the renderer does not have. */
static int unit_of(GLenum texture) {
  if (texture < GL_TEXTURE0 || texture >= GL_TEXTURE0 + SGL_MAX_TEXTURE_UNITS) {
    sgl_error(GL_INVALID_ENUM);
    return -1;
  }
  return (int)(texture - GL_TEXTURE0);
}

/* glActiveTexture: sets the texture unit that the texture state
 * functions change. The texture matrix stack follows it. */
void APIENTRY sgl_ActiveTexture(GLenum texture) {
  sgl_context *c = sgl_ctx;
  int u = unit_of(texture);
  if (u < 0) return;
  if (c->stack == &c->texture[c->a.active_texture]) c->stack = &c->texture[u];
  c->a.active_texture = u;
}

/* glClientActiveTexture: sets the texture unit of the texture
 * coordinate array functions. */
void APIENTRY sgl_ClientActiveTexture(GLenum texture) {
  int u = unit_of(texture);
  if (u >= 0) sgl_ctx->c.client_active = u;
}

/* glMultiTexCoord4f: sets the current texture coordinates of a texture
 * unit. */
void APIENTRY sgl_MultiTexCoord4f(GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q) {
  int u = unit_of(target);
  GLfloat *v;
  if (u < 0) return;
  v = sgl_ctx->a.tex[u];
  v[0] = s;
  v[1] = t;
  v[2] = r;
  v[3] = q;
}
