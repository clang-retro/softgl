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
  t->compare_mode = GL_NONE;
  t->compare_func = GL_LEQUAL;
  t->depth_mode = GL_LUMINANCE;
  t->palette_format = GL_RGBA;
  t->refs = 1;
}

/* Frees the images of a texture object. */
static void free_levels(sgl_texture *t) {
  int i;
  for (i = 0; i < SGL_MAX_TEXTURE_LEVELS; i++) {
    free(t->level[i].texels);
    free(t->level[i].indices);
    free(t->level[i].compressed);
    t->level[i].texels = NULL;
    t->level[i].indices = NULL;
    t->level[i].compressed = NULL;
  }
  if (t->faces) {
    int f;
    for (f = 0; f < 5; f++)
      for (i = 0; i < SGL_MAX_TEXTURE_LEVELS; i++) {
        free(t->faces[f][i].texels);
        free(t->faces[f][i].indices);
        free(t->faces[f][i].compressed);
      }
    free(t->faces);
    t->faces = NULL;
  }
}

/* Binds the default textures to all the texture units. */
static void bind_defaults(sgl_context *c) {
  int u;
  for (u = 0; u < SGL_MAX_TEXTURE_UNITS; u++) {
    c->tex_1d[u] = &c->default_1d;
    c->tex_2d[u] = &c->default_2d;
    c->tex_3d[u] = &c->default_3d;
    c->tex_cube[u] = &c->default_cube;
  }
}

/* Sets up the default textures of a new context. All the texture units
 * share them, as the specification tells. */
void sgl_texture_init(sgl_context *c) {
  init_texture(&c->default_1d, 0, GL_TEXTURE_1D);
  init_texture(&c->default_2d, 0, GL_TEXTURE_2D);
  init_texture(&c->default_3d, 0, GL_TEXTURE_3D);
  init_texture(&c->default_cube, 0, GL_TEXTURE_CUBE_MAP);
  bind_defaults(c);
}

/* Lets go of the textures that a context holds: its default textures
 * and its references to bound objects. */
void sgl_texture_release(sgl_context *c) {
  free_levels(&c->default_1d);
  free_levels(&c->default_2d);
  free_levels(&c->default_3d);
  free_levels(&c->default_cube);
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

/* Gives the texture of texture unit u for the primitives: the cube map
 * when it is on, else the 3D texture, else the 2D texture, else the 1D
 * texture, or NULL. */
sgl_texture *sgl_unit_texture(sgl_context *c, int u) {
  if (c->a.enabled[SGL_UCAP(u, SGL_U_TEXTURE_CUBE_MAP)]) return c->tex_cube[u];
  if (c->a.enabled[SGL_UCAP(u, SGL_U_TEXTURE_3D)]) return c->tex_3d[u];
  if (c->a.enabled[SGL_UCAP(u, SGL_U_TEXTURE_2D)]) return c->tex_2d[u];
  if (c->a.enabled[SGL_UCAP(u, SGL_U_TEXTURE_1D)]) return c->tex_1d[u];
  return NULL;
}

/* Gives the first level of a texture that is used (GL_TEXTURE_BASE_LEVEL),
 * or NULL when it has no image: then the unit acts as if its texture
 * is off. */
const sgl_level *sgl_base_level(const sgl_texture *t) {
  int b, f;
  if (!t || t->base_level >= SGL_MAX_TEXTURE_LEVELS || !t->level[t->base_level].texels) return NULL;
  b = t->base_level;
  if (t->target == GL_TEXTURE_CUBE_MAP) {
    /* A cube map needs the six faces, square and of one size. */
    if (!t->faces || t->level[b].width != t->level[b].height) return NULL;
    for (f = 0; f < 5; f++)
      if (!t->faces[f][b].texels || t->faces[f][b].width != t->level[b].width ||
          t->faces[f][b].height != t->level[b].height)
        return NULL;
  }
  return &t->level[b];
}

/* Gives the levels of face face (0 to 5: +X, -X, +Y, -Y, +Z, -Z) of a
 * texture, or NULL when the face has no levels. Face 0 is the levels of
 * a texture that is not a cube map. */
sgl_level *sgl_face_levels(sgl_texture *t, int face) {
  if (face <= 0) return t->level;
  return t->faces ? t->faces[face - 1] : NULL;
}

/* Gives the face (0 to 5) of a target of the images of a cube map, 0
 * for the other targets, or -1 for GL_TEXTURE_CUBE_MAP itself, which
 * has no images. */
/* Tells if target is the target of an image of a cube map face. */
static int is_face(GLenum target) {
  return target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X && target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z;
}

static int face_of(GLenum target) {
  if (is_face(target))
    return (int)(target - GL_TEXTURE_CUBE_MAP_POSITIVE_X);
  return target == GL_TEXTURE_CUBE_MAP ? -1 : 0;
}

/* Gives the texture units that have a texture on, as bits (bit u for
 * unit u). In color index mode there are no textures. */
unsigned sgl_texture_units(const sgl_context *c) {
  unsigned m = 0;
  int u;
  if (c->index_mode) return 0;
  for (u = 0; u < SGL_MAX_TEXTURE_UNITS; u++)
    if (c->a.enabled[SGL_UCAP(u, SGL_U_TEXTURE_1D)] || c->a.enabled[SGL_UCAP(u, SGL_U_TEXTURE_2D)] ||
        c->a.enabled[SGL_UCAP(u, SGL_U_TEXTURE_3D)] || c->a.enabled[SGL_UCAP(u, SGL_U_TEXTURE_CUBE_MAP)])
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
  if (target == GL_TEXTURE_CUBE_MAP || is_face(target))
    return c->tex_cube[u];
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
    case GL_COMPRESSED_ALPHA: return GL_ALPHA;
    case GL_COMPRESSED_LUMINANCE: return GL_LUMINANCE;
    case GL_COMPRESSED_LUMINANCE_ALPHA: return GL_LUMINANCE_ALPHA;
    case GL_COMPRESSED_INTENSITY: return GL_INTENSITY;
    case GL_COMPRESSED_RGB: case GL_COMPRESSED_RGB_S3TC_DXT1_EXT: return GL_RGB;
    case GL_COMPRESSED_RGBA: case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT: case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
    case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
      return GL_RGBA;
    case GL_DEPTH_COMPONENT: case GL_DEPTH_COMPONENT16: case GL_DEPTH_COMPONENT24:
    case GL_DEPTH_COMPONENT32:
      return GL_DEPTH_COMPONENT;
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

/* Tells if the levels lv of a texture are all the levels of a mipmap,
 * from the base level b to the maximum level max. */
static int chain_complete(const sgl_level *lv, int b, int max) {
  int i, w, h, d;
  if (b >= SGL_MAX_TEXTURE_LEVELS || !lv[b].texels) return 0;
  w = lv[b].width;
  h = lv[b].height;
  d = lv[b].depth;
  for (i = b + 1; i < SGL_MAX_TEXTURE_LEVELS && i <= max && (w > 1 || h > 1 || d > 1); i++) {
    w = w > 1 ? w / 2 : 1;
    h = h > 1 ? h / 2 : 1;
    d = d > 1 ? d / 2 : 1;
    if (!lv[i].texels || lv[i].width != w || lv[i].height != h || lv[i].depth != d) return 0;
  }
  return 1;
}

/* Computes if the texture has all the levels of a mipmap: for a cube
 * map, on all the faces. */
static void check_complete(sgl_texture *t) {
  int f;
  sgl_texture_touch(t);
  t->complete = chain_complete(t->level, t->base_level, t->max_level);
  if (t->target != GL_TEXTURE_CUBE_MAP || !t->complete) return;
  if (!t->faces) {
    t->complete = 0;
    return;
  }
  for (f = 0; f < 5; f++)
    if (!chain_complete(t->faces[f], t->base_level, t->max_level)) t->complete = 0;
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

/* Makes the levels after the base level of face face of a texture from
 * the base level (GL_GENERATE_MIPMAP of OpenGL 1.4). Each texel of a
 * level is the mean of the 2, 4 or 8 texels of the level before. */
static void generate_mipmaps(sgl_texture *t, int face) {
  sgl_level *lv = sgl_face_levels(t, face);
  int i;
  sgl_texture_touch(t);
  if (!lv || t->base_level >= SGL_MAX_TEXTURE_LEVELS || !lv[t->base_level].texels) return;
  for (i = t->base_level; i + 1 < SGL_MAX_TEXTURE_LEVELS && i < t->max_level; i++) {
    const sgl_level *src = &lv[i];
    sgl_level *dst = &lv[i + 1];
    int w = src->width, h = src->height, d = src->depth, x, y, z;
    int sx = w > 1 ? 2 : 1, sy = h > 1 ? 2 : 1, sz = d > 1 ? 2 : 1, n = sx * sy * sz;
    /* n is 2, 4 or 8: the mean is a shift, without a 64-bit division. */
    int sh = (sx > 1) + (sy > 1) + (sz > 1);
    int nw = w / sx, nh = h / sy, nd = d / sz;
    uint32_t *out;
    if (n == 1) break;
    out = (uint32_t *)malloc((size_t)nw * nh * nd * 4);
    if (!out) {
      sgl_error(GL_OUT_OF_MEMORY);
      return;
    }
    for (z = 0; z < nd; z++)
      for (y = 0; y < nh; y++)
        for (x = 0; x < nw; x++) {
          /* The sums: of the four components, or of the depth. */
          uint32_t sum[4] = {0, 0, 0, 0};
          uint64_t dsum = 0;
          int i2, j2, k2, ch;
          for (k2 = 0; k2 < sz; k2++)
            for (j2 = 0; j2 < sy; j2++)
              for (i2 = 0; i2 < sx; i2++) {
                uint32_t v = src->texels[((size_t)(z * sz + k2) * h + (size_t)(y * sy + j2)) * w +
                                         (size_t)(x * sx + i2)];
                if (src->base == GL_DEPTH_COMPONENT) {
                  dsum += v;
                } else {
                  for (ch = 0; ch < 4; ch++) sum[ch] += (v >> (ch * 8)) & 0xFF;
                }
              }
          if (src->base == GL_DEPTH_COMPONENT) {
            out[((size_t)z * nh + y) * nw + x] = (uint32_t)(dsum >> sh);
          } else {
            uint32_t v = 0;
            for (ch = 0; ch < 4; ch++) v |= ((sum[ch] + (uint32_t)n / 2) >> sh) << (ch * 8);
            out[((size_t)z * nh + y) * nw + x] = v;
          }
        }
    free(dst->texels);
    free(dst->indices);
    free(dst->compressed);
    dst->texels = out;
    dst->indices = NULL;
    dst->compressed = NULL;
    dst->compressed_size = 0;
    dst->width = nw;
    dst->height = nh;
    dst->depth = nd;
    dst->wlog = log2_exact(nw);
    dst->hlog = log2_exact(nh);
    dst->dlog = log2_exact(nd);
    dst->internal = src->internal;
    dst->base = src->base;
    dst->border = 0;
  }
  check_complete(t);
}

/* Makes the mipmap of face face of texture t again when the image of
 * level level changed and t has GL_GENERATE_MIPMAP. */
static void changed_level(sgl_texture *t, int face, int level) {
  if (t->generate_mipmap && level == t->base_level) generate_mipmaps(t, face);
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
  if ((is_face(target) || target == GL_PROXY_TEXTURE_CUBE_MAP) && (w != h || w > SGL_MAX_CUBE_MAP_SIZE + 2)) {
    /* The faces of a cube map are square. */
    sgl_error(GL_INVALID_VALUE);
    return 0;
  }
  if (target == GL_PROXY_TEXTURE_2D || target == GL_PROXY_TEXTURE_1D || target == GL_PROXY_TEXTURE_3D ||
      target == GL_PROXY_TEXTURE_CUBE_MAP)
    return 1;
  t = bound(c, target);
  if (!t || face_of(target) < 0) {
    sgl_error(GL_INVALID_ENUM);
    return 0;
  }
  if (face_of(target) > 0 && !t->faces) {
    t->faces = (sgl_level(*)[SGL_MAX_TEXTURE_LEVELS])calloc(5, sizeof *t->faces);
    if (!t->faces) {
      sgl_error(GL_OUT_OF_MEMORY);
      return 0;
    }
  }
  if (!base) {
    sgl_error(GL_INVALID_VALUE);
    return 0;
  }
  if (sgl_format_components(format) == 0) {
    sgl_error(GL_INVALID_ENUM);
    return 0;
  }
  /* A depth texture (OpenGL 1.4) is 1D or 2D, and takes depth pixels
   * only. */
  if ((base == GL_DEPTH_COMPONENT) != (format == GL_DEPTH_COMPONENT) ||
      (base == GL_DEPTH_COMPONENT && target != GL_TEXTURE_1D && target != GL_TEXTURE_2D)) {
    sgl_error(GL_INVALID_OPERATION);
    return 0;
  }
  /* The border texels are dropped: the texture is the inside. */
  w -= 2 * border;
  h -= 2 * border;
  d -= 2 * db;
  lv = &sgl_face_levels(t, face_of(target))[level];
  n = (size_t)w * h * d;
  free(lv->texels);
  free(lv->indices);
  free(lv->compressed);
  lv->indices = NULL;
  lv->compressed = NULL;
  lv->compressed_size = 0;
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
  } else if (base == GL_DEPTH_COMPONENT) {
    lv->base = base;
    if (pixels) {
      uint32_t *dz = (uint32_t *)malloc((size_t)W * H * 4);
      if (!dz) {
        sgl_error(GL_OUT_OF_MEMORY);
        return 0;
      }
      if (!sgl_unpack_depth(c, ps, W, H, type, pixels, dz, SGL_TEX_DEPTH_MAX)) {
        free(dz);
        sgl_error(GL_INVALID_ENUM);
        return 0;
      }
      for (y = 0; y < h; y++)
        memcpy(lv->texels + (size_t)y * w, dz + ((size_t)(y + border) * W + border), (size_t)w * 4);
      free(dz);
    } else {
      memset(lv->texels, 0, n * 4);
    }
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
  changed_level(t, face_of(target), level);
  return 1;
}

/* glTexImage2D: sets a level of the 2D texture. */
void APIENTRY sgl_TexImage2D(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h,
                             GLint border, GLenum format, GLenum type, const void *pixels) {
  sgl_context *c = sgl_ctx;
  if (target != GL_TEXTURE_2D && target != GL_PROXY_TEXTURE_2D && target != GL_PROXY_TEXTURE_CUBE_MAP &&
      !is_face(target)) {
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
  if (t) sgl_texture_touch(t);
  int i, j, k, three = is_3d(target);
  if (!t) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  if (level < 0 || level >= SGL_MAX_TEXTURE_LEVELS) {
    sgl_error(GL_INVALID_VALUE);
    return;
  }
  if (face_of(target) < 0 || !sgl_face_levels(t, face_of(target))) {
    sgl_error(face_of(target) < 0 ? GL_INVALID_ENUM : GL_INVALID_VALUE);
    return;
  }
  lv = &sgl_face_levels(t, face_of(target))[level];
  if (!lv->texels || x < 0 || y < 0 || z < 0 || x + w > lv->width || y + h > lv->height ||
      z + d > lv->depth) {
    sgl_error(GL_INVALID_VALUE);
    return;
  }
  if (w <= 0 || h <= 0 || d <= 0) return;
  if ((lv->base == GL_DEPTH_COMPONENT) != (format == GL_DEPTH_COMPONENT)) {
    sgl_error(GL_INVALID_OPERATION);
    return;
  }
  if (lv->base == GL_DEPTH_COMPONENT) {
    uint32_t *dz = (uint32_t *)malloc((size_t)w * h * 4);
    if (!dz) {
      sgl_error(GL_OUT_OF_MEMORY);
      return;
    }
    if (!sgl_unpack_depth(c, ps, w, h, type, pixels, dz, SGL_TEX_DEPTH_MAX)) {
      free(dz);
      sgl_error(GL_INVALID_ENUM);
      return;
    }
    for (j = 0; j < h; j++) memcpy(lv->texels + (size_t)(y + j) * lv->width + x, dz + (size_t)j * w, (size_t)w * 4);
    free(dz);
  } else if (lv->indices && format == GL_COLOR_INDEX) {
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
  changed_level(t, face_of(target), level);
}

/* glTexSubImage2D: changes a rectangle of a level of the 2D texture. */
void APIENTRY sgl_TexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h,
                                GLenum format, GLenum type, const void *pixels) {
  if (target != GL_TEXTURE_2D && !is_face(target)) {
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
  sgl_be_wait(c);
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

/* Reads w x h values of the depth buffer at x, y as floats 0 to 1, rows
 * bottom first. */
static GLfloat *read_depth_floats(sgl_context *c, int x, int y, int w, int h) {
  size_t i, n = (size_t)w * h;
  int32_t *z = (int32_t *)malloc(n * 4 + 4);
  if (!z) return NULL;
  sgl_read_depth(c, x, y, w, h, z);
  for (i = 0; i < n; i++) {
    GLfloat f = (GLfloat)((double)(uint32_t)z[i] / (double)SGL_Z_MAX);
    memcpy(&z[i], &f, 4);
  }
  return (GLfloat *)z;
}

/* Tells if level level of the image of target is a depth texture. */
static int level_is_depth(sgl_context *c, GLenum target, GLint level) {
  sgl_texture *t = bound(c, target);
  const sgl_level *lv;
  if (!t || face_of(target) < 0 || level < 0 || level >= SGL_MAX_TEXTURE_LEVELS) return 0;
  lv = sgl_face_levels(t, face_of(target));
  return lv && lv[level].texels && lv[level].base == GL_DEPTH_COMPONENT;
}

/* Sets a level of a 1D or 2D texture of a depth format from the depth
 * buffer (OpenGL 1.4). */
static void copy_depth_image(sgl_context *c, GLenum target, GLint level, GLenum internal, GLint x,
                             GLint y, GLsizei w, GLsizei h, GLint border) {
  GLfloat *p = read_depth_floats(c, x, y, w, h);
  if (!p) {
    sgl_error(GL_OUT_OF_MEMORY);
    return;
  }
  set_image(c, target, level, (GLint)internal, w, h, 1, border, GL_DEPTH_COMPONENT, GL_FLOAT, p,
            &sgl_tight_store);
  free(p);
}

/* Changes a rectangle of a depth texture from the depth buffer. */
static void copy_depth_sub(sgl_context *c, GLenum target, GLint level, GLint xoff, GLint yoff, GLint x,
                           GLint y, GLsizei w, GLsizei h) {
  GLfloat *p = read_depth_floats(c, x, y, w, h);
  if (!p) {
    sgl_error(GL_OUT_OF_MEMORY);
    return;
  }
  sub_image(c, target, level, xoff, yoff, 0, w, h, 1, GL_DEPTH_COMPONENT, GL_FLOAT, p, &sgl_tight_store);
  free(p);
}

/* glCopyTexImage2D: sets a level of the 2D texture from the color
 * buffer, or from the depth buffer for a depth format. */
void APIENTRY sgl_CopyTexImage2D(GLenum target, GLint level, GLenum internal, GLint x, GLint y,
                                 GLsizei w, GLsizei h, GLint border) {
  sgl_context *c = sgl_ctx;
  uint8_t *p;
  if (is_3d(target)) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  if (base_format((GLint)internal) == GL_DEPTH_COMPONENT) {
    copy_depth_image(c, target, level, internal, x, y, w, h, border);
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
  if (base_format((GLint)internal) == GL_DEPTH_COMPONENT) {
    copy_depth_image(c, target, level, internal, x, y, w, 1, border);
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
  uint8_t *p;
  if (level_is_depth(c, target, level)) {
    copy_depth_sub(c, target, level, xoff, yoff, x, y, w, h);
    return;
  }
  p = read_color(c, x, y, w, h);
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
  uint8_t *p;
  if (level_is_depth(c, target, level)) {
    copy_depth_sub(c, target, level, xoff, 0, x, y, w, 1);
    return;
  }
  p = read_color(c, x, y, w, 1);
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
  const sgl_level *lv;
  if (!t || face_of(target) < 0 || level < 0 || level >= SGL_MAX_TEXTURE_LEVELS) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  if (!sgl_face_levels(t, face_of(target))) return;
  lv = &sgl_face_levels(t, face_of(target))[level];
  if (!lv->texels) return;
  if ((lv->base == GL_DEPTH_COMPONENT) != (format == GL_DEPTH_COMPONENT)) {
    sgl_error(GL_INVALID_OPERATION);
    return;
  }
  if (format == GL_DEPTH_COMPONENT) {
    /* Depth as floats or unsigned integers, without the pack store, as
     * glReadPixels gives them. */
    size_t i, n = (size_t)lv->width * lv->height;
    for (i = 0; i < n; i++) {
      double v = (double)lv->texels[i] / (double)SGL_TEX_DEPTH_MAX;
      if (type == GL_FLOAT)
        ((GLfloat *)pixels)[i] = (GLfloat)v;
      else if (type == GL_UNSIGNED_INT)
        ((GLuint *)pixels)[i] = (GLuint)(v * 4294967295.0);
      else if (type == GL_UNSIGNED_SHORT)
        ((GLushort *)pixels)[i] = (GLushort)(v * 65535.0);
      else if (type == GL_UNSIGNED_BYTE)
        ((GLubyte *)pixels)[i] = (GLubyte)(v * 255.0);
      else {
        sgl_error(GL_INVALID_ENUM);
        return;
      }
    }
    return;
  }
  /* The images of a 3D texture follow each other as rows. */
  sgl_pack_texels(c, lv->width, lv->height * lv->depth, lv->texels, format, type, pixels);
}

/* glTexParameterfv: sets a parameter of the bound texture of target. */
void APIENTRY sgl_TexParameterfv(GLenum target, GLenum pname, const GLfloat *params) {
  sgl_texture *t = is_face(target) ? NULL : bound(sgl_ctx, target);
  GLenum v = sgl_enum(params[0]);
  if (!t) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  sgl_texture_touch(t);
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
    case GL_TEXTURE_LOD_BIAS: t->lod_bias = params[0]; break;
    case GL_GENERATE_MIPMAP:
      t->generate_mipmap = params[0] != 0.0f ? GL_TRUE : GL_FALSE;
      break;
    case GL_TEXTURE_COMPARE_MODE:
      if (v != GL_NONE && v != GL_COMPARE_R_TO_TEXTURE) {
        sgl_error(GL_INVALID_ENUM);
        return;
      }
      t->compare_mode = v;
      break;
    case GL_TEXTURE_COMPARE_FUNC:
      /* OpenGL 1.5 permits all eight functions; OpenGL 1.4 only
       * GL_LEQUAL and GL_GEQUAL. */
      if (v < GL_NEVER || v > GL_ALWAYS) {
        sgl_error(GL_INVALID_ENUM);
        return;
      }
      t->compare_func = v;
      break;
    case GL_DEPTH_TEXTURE_MODE:
      if (v != GL_LUMINANCE && v != GL_INTENSITY && v != GL_ALPHA) {
        sgl_error(GL_INVALID_ENUM);
        return;
      }
      t->depth_mode = v;
      break;
    default: sgl_error(GL_INVALID_ENUM); break;
  }
}

/* glGetTexParameterfv: gives a parameter of the bound texture. */
void APIENTRY sgl_GetTexParameterfv(GLenum target, GLenum pname, GLfloat *params) {
  sgl_texture *t = is_face(target) ? NULL : bound(sgl_ctx, target);
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
    case GL_TEXTURE_LOD_BIAS: params[0] = t->lod_bias; break;
    case GL_GENERATE_MIPMAP: params[0] = t->generate_mipmap ? 1.0f : 0.0f; break;
    case GL_TEXTURE_COMPARE_MODE: params[0] = (GLfloat)t->compare_mode; break;
    case GL_TEXTURE_COMPARE_FUNC: params[0] = (GLfloat)t->compare_func; break;
    case GL_DEPTH_TEXTURE_MODE: params[0] = (GLfloat)t->depth_mode; break;
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
  if (target == GL_PROXY_TEXTURE_CUBE_MAP) {
    params[0] = pname == GL_TEXTURE_WIDTH || pname == GL_TEXTURE_HEIGHT ? SGL_MAX_CUBE_MAP_SIZE
                : pname == GL_TEXTURE_DEPTH ? 1 : 0;
    return;
  }
  t = bound(c, target);
  if (!t || face_of(target) < 0 || level < 0 || level >= SGL_MAX_TEXTURE_LEVELS) {
    sgl_error(t && face_of(target) < 0 ? GL_INVALID_ENUM : GL_INVALID_VALUE);
    return;
  }
  if (!sgl_face_levels(t, face_of(target))) {
    params[0] = pname == GL_TEXTURE_INTERNAL_FORMAT ? 1 : 0;
    return;
  }
  lv = &sgl_face_levels(t, face_of(target))[level];
  switch (pname) {
    case GL_TEXTURE_WIDTH: params[0] = lv->texels ? lv->width + 2 * lv->border : 0; break;
    case GL_TEXTURE_HEIGHT: params[0] = lv->texels ? lv->height + 2 * lv->border : 0; break;
    case GL_TEXTURE_DEPTH:
      params[0] = !lv->texels ? 0 : lv->depth + (t->target == GL_TEXTURE_3D ? 2 * lv->border : 0);
      break;
    case GL_TEXTURE_BORDER: params[0] = lv->border; break;
    case GL_TEXTURE_COMPRESSED: params[0] = lv->texels && lv->compressed ? GL_TRUE : GL_FALSE; break;
    case GL_TEXTURE_COMPRESSED_IMAGE_SIZE:
      if (!lv->compressed) {
        sgl_error(GL_INVALID_OPERATION);
        return;
      }
      params[0] = (GLint)lv->compressed_size;
      break;
    case GL_TEXTURE_INTERNAL_FORMAT: params[0] = lv->texels ? (GLint)lv->internal : 1; break;
    case GL_TEXTURE_RED_SIZE: case GL_TEXTURE_GREEN_SIZE: case GL_TEXTURE_BLUE_SIZE:
    case GL_TEXTURE_ALPHA_SIZE: case GL_TEXTURE_LUMINANCE_SIZE: case GL_TEXTURE_INTENSITY_SIZE:
      params[0] = lv->texels && lv->base != GL_DEPTH_COMPONENT ? 8 : 0;
      break;
    case GL_TEXTURE_DEPTH_SIZE:
      /* The texels keep 30 bits; the report is 24, the size that the
       * applications ask for. */
      params[0] = lv->texels && lv->base == GL_DEPTH_COMPONENT ? 24 : 0;
      break;
    default: sgl_error(GL_INVALID_ENUM); break;
  }
}

/* Tells if v is a source of GL_COMBINE: GL_TEXTURE, GL_CONSTANT,
 * GL_PRIMARY_COLOR, GL_PREVIOUS or (GL_ARB_texture_env_crossbar) the
 * texture of a unit. */
static int valid_source(GLenum v) {
  return v == GL_TEXTURE || v == GL_CONSTANT || v == GL_PRIMARY_COLOR || v == GL_PREVIOUS ||
         (v >= GL_TEXTURE0 && v < GL_TEXTURE0 + SGL_MAX_TEXTURE_UNITS);
}

/* Gives the member of the state of a unit that a GL_COMBINE parameter
 * of glTexEnv names, or NULL. *alpha tells if it is an alpha operand. */
static GLenum *combine_param(sgl_unit_attrib *u, GLenum pname, int *alpha) {
  *alpha = 0;
  switch (pname) {
    case GL_COMBINE_RGB: return &u->combine_rgb;
    case GL_COMBINE_ALPHA: *alpha = 1; return &u->combine_alpha;
    case GL_SOURCE0_RGB: case GL_SOURCE1_RGB: case GL_SOURCE2_RGB: return &u->src_rgb[pname - GL_SOURCE0_RGB];
    case GL_SOURCE0_ALPHA: case GL_SOURCE1_ALPHA: case GL_SOURCE2_ALPHA:
      return &u->src_alpha[pname - GL_SOURCE0_ALPHA];
    case GL_OPERAND0_RGB: case GL_OPERAND1_RGB: case GL_OPERAND2_RGB:
      return &u->op_rgb[pname - GL_OPERAND0_RGB];
    case GL_OPERAND0_ALPHA: case GL_OPERAND1_ALPHA: case GL_OPERAND2_ALPHA:
      *alpha = 1;
      return &u->op_alpha[pname - GL_OPERAND0_ALPHA];
    default: return NULL;
  }
}

/* glTexEnvfv: sets a parameter of the texture environment of the active
 * texture unit. */
void APIENTRY sgl_TexEnvfv(GLenum target, GLenum pname, const GLfloat *params) {
  sgl_context *c = sgl_ctx;
  sgl_unit_attrib *u = &c->a.unit[c->a.active_texture];
  GLenum v = sgl_enum(params[0]), *p;
  int alpha, ok;
  if (target == GL_TEXTURE_FILTER_CONTROL && pname == GL_TEXTURE_LOD_BIAS) {
    /* OpenGL 1.4: the bias of the level of detail of the unit. */
    u->lod_bias = params[0];
    return;
  }
  if (target != GL_TEXTURE_ENV) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  if (pname == GL_TEXTURE_ENV_MODE) {
    if (v != GL_MODULATE && v != GL_DECAL && v != GL_BLEND && v != GL_REPLACE && v != GL_ADD &&
        v != GL_COMBINE) {
      sgl_error(GL_INVALID_ENUM);
      return;
    }
    u->env_mode = v;
  } else if (pname == GL_TEXTURE_ENV_COLOR) {
    int i;
    for (i = 0; i < 4; i++) u->env_color[i] = sgl_clamp01(params[i]);
  } else if (pname == GL_RGB_SCALE || pname == GL_ALPHA_SCALE) {
    if (params[0] != 1.0f && params[0] != 2.0f && params[0] != 4.0f) {
      sgl_error(GL_INVALID_VALUE);
      return;
    }
    if (pname == GL_RGB_SCALE)
      u->rgb_scale = params[0];
    else
      u->alpha_scale = params[0];
  } else if ((p = combine_param(u, pname, &alpha)) != NULL) {
    if (pname == GL_COMBINE_RGB || pname == GL_COMBINE_ALPHA)
      ok = v == GL_REPLACE || v == GL_MODULATE || v == GL_ADD || v == GL_ADD_SIGNED ||
           v == GL_INTERPOLATE || v == GL_SUBTRACT ||
           (pname == GL_COMBINE_RGB && (v == GL_DOT3_RGB || v == GL_DOT3_RGBA));
    else if (pname >= GL_SOURCE0_RGB && pname <= GL_SOURCE2_ALPHA)
      ok = valid_source(v);
    else
      ok = v == GL_SRC_ALPHA || v == GL_ONE_MINUS_SRC_ALPHA ||
           (!alpha && (v == GL_SRC_COLOR || v == GL_ONE_MINUS_SRC_COLOR));
    if (!ok) {
      sgl_error(GL_INVALID_ENUM);
      return;
    }
    *p = v;
  } else
    sgl_error(GL_INVALID_ENUM);
}

/* glGetTexEnvfv: gives a parameter of the texture environment of the
 * active texture unit. */
void APIENTRY sgl_GetTexEnvfv(GLenum target, GLenum pname, GLfloat *params) {
  sgl_context *c = sgl_ctx;
  sgl_unit_attrib *u = &c->a.unit[c->a.active_texture];
  GLenum *p;
  int alpha;
  if (target == GL_TEXTURE_FILTER_CONTROL && pname == GL_TEXTURE_LOD_BIAS) {
    params[0] = u->lod_bias;
    return;
  }
  if (target != GL_TEXTURE_ENV) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  if (pname == GL_TEXTURE_ENV_MODE)
    params[0] = (GLfloat)u->env_mode;
  else if (pname == GL_TEXTURE_ENV_COLOR)
    memcpy(params, u->env_color, sizeof u->env_color);
  else if (pname == GL_RGB_SCALE)
    params[0] = u->rgb_scale;
  else if (pname == GL_ALPHA_SCALE)
    params[0] = u->alpha_scale;
  else if ((p = combine_param(u, pname, &alpha)) != NULL)
    params[0] = (GLfloat)*p;
  else
    sgl_error(GL_INVALID_ENUM);
}

/* glGetTexEnviv: as glGetTexEnvfv, as integers. */
void APIENTRY sgl_GetTexEnviv(GLenum target, GLenum pname, GLint *params) {
  GLfloat f[4];
  int i;
  sgl_GetTexEnvfv(target, pname, f);
  if (pname == GL_TEXTURE_ENV_COLOR)
    for (i = 0; i < 4; i++) params[i] = (GLint)(f[i] * 2147483647.0f);
  else
    params[0] = (GLint)f[0];
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
      GLenum m = sgl_enum(params[0]);
      if (m != GL_OBJECT_LINEAR && m != GL_EYE_LINEAR && (m != GL_SPHERE_MAP || i > 1) &&
          ((m != GL_NORMAL_MAP && m != GL_REFLECTION_MAP) || i > 2)) {
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
  if (target != GL_TEXTURE_1D && target != GL_TEXTURE_2D && target != GL_TEXTURE_3D &&
      target != GL_TEXTURE_CUBE_MAP) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  if (name == 0) {
    t = target == GL_TEXTURE_2D ? &c->default_2d
        : target == GL_TEXTURE_3D ? &c->default_3d
        : target == GL_TEXTURE_CUBE_MAP ? &c->default_cube : &c->default_1d;
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
  else if (target == GL_TEXTURE_CUBE_MAP)
    c->tex_cube[c->a.active_texture] = t;
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
      if (c->tex_cube[u] == t) c->tex_cube[u] = &c->default_cube;
    }
    if (t->id && c->be && c->be->texture_deleted) c->be->texture_deleted(c->be_user, t->id);
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
  sgl_texture_touch(t);
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
  if (t->faces) {
    int f;
    for (f = 0; f < 5; f++)
      for (i = 0; i < SGL_MAX_TEXTURE_LEVELS; i++) expand_palette(t, &t->faces[f][i]);
  }
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

/* Gives the number of bytes of a 4 x 4 block of an S3TC format, or 0
 * for a format that is not S3TC. */
static int s3tc_block_bytes(GLenum format) {
  switch (format) {
    case GL_COMPRESSED_RGB_S3TC_DXT1_EXT: case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT: return 8;
    case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT: case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT: return 16;
    default: return 0;
  }
}

/* Gives the number of bytes of an S3TC image of w x h pixels. */
static size_t s3tc_size(GLenum format, int w, int h) {
  return (size_t)((w + 3) / 4) * (size_t)((h + 3) / 4) * (size_t)s3tc_block_bytes(format);
}

/* Gives the RGB bytes of a 5-6-5 color of S3TC. */
static void rgb565(unsigned v, uint8_t out[3]) {
  unsigned r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
  out[0] = (uint8_t)((r << 3) | (r >> 2));
  out[1] = (uint8_t)((g << 2) | (g >> 4));
  out[2] = (uint8_t)((b << 3) | (b >> 2));
}

/* Decodes the color part of an S3TC block (8 bytes at p) into 16 RGBA
 * pixels out (row by row). four is 1 for DXT3 and DXT5, which always
 * use four colors; for DXT1 with c0 <= c1 the fourth color is black,
 * transparent when alpha1 is 1. */
static void decode_color_block(const uint8_t *p, int four, int alpha1, uint8_t out[16][4]) {
  unsigned c0 = (unsigned)p[0] | ((unsigned)p[1] << 8), c1 = (unsigned)p[2] | ((unsigned)p[3] << 8);
  uint32_t bits = (uint32_t)p[4] | ((uint32_t)p[5] << 8) | ((uint32_t)p[6] << 16) | ((uint32_t)p[7] << 24);
  uint8_t pal[4][4];
  int i, k;
  rgb565(c0, pal[0]);
  rgb565(c1, pal[1]);
  pal[0][3] = pal[1][3] = pal[2][3] = pal[3][3] = 255;
  for (k = 0; k < 3; k++) {
    if (four || c0 > c1) {
      pal[2][k] = (uint8_t)((2 * pal[0][k] + pal[1][k]) / 3);
      pal[3][k] = (uint8_t)((pal[0][k] + 2 * pal[1][k]) / 3);
    } else {
      pal[2][k] = (uint8_t)((pal[0][k] + pal[1][k]) / 2);
      pal[3][k] = 0;
    }
  }
  if (!four && c0 <= c1 && alpha1) pal[3][3] = 0;
  for (i = 0; i < 16; i++) memcpy(out[i], pal[(bits >> (2 * i)) & 3], 4);
}

/* Decodes the alpha part of a DXT5 block (8 bytes at p) into the alpha
 * of 16 pixels. */
static void decode_dxt5_alpha(const uint8_t *p, uint8_t out[16][4]) {
  int a0 = p[0], a1 = p[1], i;
  int pal[8];
  /* The 48 bits of the indices, 3 bits for each pixel, as two 24-bit
   * halves (the 68000 has no 64-bit registers). */
  uint32_t lo = (uint32_t)p[2] | ((uint32_t)p[3] << 8) | ((uint32_t)p[4] << 16);
  uint32_t hi = (uint32_t)p[5] | ((uint32_t)p[6] << 8) | ((uint32_t)p[7] << 16);
  pal[0] = a0;
  pal[1] = a1;
  if (a0 > a1) {
    for (i = 1; i < 7; i++) pal[i + 1] = ((7 - i) * a0 + i * a1) / 7;
  } else {
    for (i = 1; i < 5; i++) pal[i + 1] = ((5 - i) * a0 + i * a1) / 5;
    pal[6] = 0;
    pal[7] = 255;
  }
  for (i = 0; i < 16; i++) {
    unsigned idx = i < 8 ? (lo >> (3 * i)) & 7 : (hi >> (3 * (i - 8))) & 7;
    out[i][3] = (uint8_t)pal[idx];
  }
}

/* Decodes an S3TC image of w x h pixels to RGBA bytes rgba (rows bottom
 * first, as the blocks are). */
static void s3tc_decode(GLenum format, int w, int h, const uint8_t *src, uint8_t *rgba) {
  int bx, by, i, bytes = s3tc_block_bytes(format);
  for (by = 0; by < (h + 3) / 4; by++)
    for (bx = 0; bx < (w + 3) / 4; bx++, src += bytes) {
      uint8_t px[16][4];
      if (format == GL_COMPRESSED_RGBA_S3TC_DXT3_EXT) {
        decode_color_block(src + 8, 1, 0, px);
        for (i = 0; i < 16; i++) {
          int a = (src[i / 2] >> (4 * (i & 1))) & 15;
          px[i][3] = (uint8_t)(a * 17);
        }
      } else if (format == GL_COMPRESSED_RGBA_S3TC_DXT5_EXT) {
        decode_color_block(src + 8, 1, 0, px);
        decode_dxt5_alpha(src, px);
      } else {
        decode_color_block(src, 0, format == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, px);
      }
      for (i = 0; i < 16; i++) {
        int x = bx * 4 + (i & 3), y = by * 4 + i / 4;
        if (x < w && y < h) memcpy(rgba + ((size_t)y * w + x) * 4, px[i], 4);
      }
    }
}

/* glCompressedTexImage2D: sets a level of the 2D texture or of a face
 * of the cube map from an S3TC image. The renderer decodes the image;
 * it keeps the blocks for glGetCompressedTexImage. */
void APIENTRY sgl_CompressedTexImage2D(GLenum target, GLint level, GLenum internal, GLsizei w, GLsizei h,
                                       GLint border, GLsizei size, const void *data) {
  sgl_context *c = sgl_ctx;
  uint8_t *rgba;
  sgl_texture *t;
  sgl_level *lv;
  if (target != GL_TEXTURE_2D && target != GL_PROXY_TEXTURE_2D && !is_face(target) &&
      target != GL_PROXY_TEXTURE_CUBE_MAP) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  if (!s3tc_block_bytes(internal)) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  if (border != 0 || w <= 0 || h <= 0) {
    sgl_error(GL_INVALID_VALUE);
    return;
  }
  if ((size_t)size != s3tc_size(internal, w, h)) {
    sgl_error(GL_INVALID_VALUE);
    return;
  }
  if (target == GL_PROXY_TEXTURE_2D || target == GL_PROXY_TEXTURE_CUBE_MAP) {
    set_image(c, target, level, (GLint)internal, w, h, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL, &sgl_tight_store);
    return;
  }
  rgba = (uint8_t *)malloc((size_t)w * h * 4);
  if (!rgba) {
    sgl_error(GL_OUT_OF_MEMORY);
    return;
  }
  s3tc_decode(internal, w, h, (const uint8_t *)data, rgba);
  if (set_image(c, target, level, (GLint)internal, w, h, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba,
                &sgl_tight_store)) {
    t = bound(c, target);
    lv = &sgl_face_levels(t, face_of(target))[level];
    lv->compressed = (uint8_t *)malloc((size_t)size);
    if (lv->compressed) {
      memcpy(lv->compressed, data, (size_t)size);
      lv->compressed_size = (size_t)size;
    }
  }
  free(rgba);
}

/* glCompressedTexSubImage2D: changes a rectangle of a level from S3TC
 * blocks. x, y, w and h must be on the edges of the blocks (multiples of
 * 4, or the edge of the level). */
void APIENTRY sgl_CompressedTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w,
                                          GLsizei h, GLenum format, GLsizei size, const void *data) {
  sgl_context *c = sgl_ctx;
  sgl_texture *t;
  sgl_level *lv;
  uint8_t *rgba;
  int bytes = s3tc_block_bytes(format), j;
  if (target != GL_TEXTURE_2D && !is_face(target)) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  t = bound(c, target);
  if (level < 0 || level >= SGL_MAX_TEXTURE_LEVELS || !sgl_face_levels(t, face_of(target))) {
    sgl_error(GL_INVALID_VALUE);
    return;
  }
  lv = &sgl_face_levels(t, face_of(target))[level];
  if (!bytes || !lv->compressed || lv->internal != format) {
    sgl_error(GL_INVALID_OPERATION);
    return;
  }
  if (x < 0 || y < 0 || w < 0 || h < 0 || x + w > lv->width || y + h > lv->height || (x & 3) || (y & 3) ||
      ((w & 3) && x + w != lv->width) || ((h & 3) && y + h != lv->height)) {
    sgl_error(GL_INVALID_OPERATION);
    return;
  }
  if ((size_t)size != s3tc_size(format, w, h)) {
    sgl_error(GL_INVALID_VALUE);
    return;
  }
  if (w == 0 || h == 0) return;
  rgba = (uint8_t *)malloc((size_t)w * h * 4);
  if (!rgba) {
    sgl_error(GL_OUT_OF_MEMORY);
    return;
  }
  s3tc_decode(format, w, h, (const uint8_t *)data, rgba);
  sub_image(c, target, level, x, y, 0, w, h, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba, &sgl_tight_store);
  free(rgba);
  /* The kept blocks: each row of blocks of the rectangle. */
  {
    int bw = (lv->width + 3) / 4, rw = (w + 3) / 4;
    for (j = 0; j < (h + 3) / 4; j++)
      memcpy(lv->compressed + ((size_t)(y / 4 + j) * bw + x / 4) * bytes,
             (const uint8_t *)data + (size_t)j * rw * bytes, (size_t)rw * bytes);
  }
}

/* glGetCompressedTexImage: gives the S3TC blocks of a level. */
void APIENTRY sgl_GetCompressedTexImage(GLenum target, GLint level, void *img) {
  sgl_context *c = sgl_ctx;
  sgl_texture *t;
  sgl_level *lv;
  if (target != GL_TEXTURE_2D && !is_face(target)) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  t = bound(c, target);
  if (level < 0 || level >= SGL_MAX_TEXTURE_LEVELS || !sgl_face_levels(t, face_of(target))) {
    sgl_error(GL_INVALID_VALUE);
    return;
  }
  lv = &sgl_face_levels(t, face_of(target))[level];
  if (!lv->compressed) {
    sgl_error(GL_INVALID_OPERATION);
    return;
  }
  memcpy(img, lv->compressed, lv->compressed_size);
}

/* glCompressedTexImage1D: there is no compressed 1D format. */
void APIENTRY sgl_CompressedTexImage1D(GLenum target, GLint level, GLenum internal, GLsizei w, GLint border,
                                       GLsizei size, const void *data) {
  (void)target, (void)internal, (void)level, (void)w, (void)border, (void)size, (void)data;
  sgl_error(GL_INVALID_ENUM);
}

/* glCompressedTexImage3D: there is no compressed 3D format. */
void APIENTRY sgl_CompressedTexImage3D(GLenum target, GLint level, GLenum internal, GLsizei w, GLsizei h,
                                       GLsizei d, GLint border, GLsizei size, const void *data) {
  (void)target, (void)internal, (void)level, (void)w, (void)h, (void)d, (void)border, (void)size;
  (void)data;
  sgl_error(GL_INVALID_ENUM);
}

/* glCompressedTexSubImage1D: there is no compressed 1D format. */
void APIENTRY sgl_CompressedTexSubImage1D(GLenum target, GLint level, GLint x, GLsizei w, GLenum format,
                                          GLsizei size, const void *data) {
  (void)target, (void)level, (void)x, (void)w, (void)format, (void)size, (void)data;
  sgl_error(GL_INVALID_OPERATION);
}

/* glCompressedTexSubImage3D: there is no compressed 3D format. */
void APIENTRY sgl_CompressedTexSubImage3D(GLenum target, GLint level, GLint x, GLint y, GLint z, GLsizei w,
                                          GLsizei h, GLsizei d, GLenum format, GLsizei size, const void *data) {
  (void)target, (void)level, (void)x, (void)y, (void)z, (void)w, (void)h, (void)d, (void)format;
  (void)size, (void)data;
  sgl_error(GL_INVALID_OPERATION);
}
