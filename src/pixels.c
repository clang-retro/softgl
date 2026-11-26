/* pixels.c - the pixel operations of the software OpenGL: the pixel
 * store, unpacking and packing of images, glDrawPixels, glReadPixels,
 * glCopyPixels, glBitmap, glClear and the pixel transfer state.
 *
 * Part of clang-retro softgl, a software implementation of OpenGL.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include <math.h>
#include <stdlib.h>

#include "sgl.h"

/* The pixel store of a tightly packed image: rows of bytes with no
 * padding. Display lists keep their images in this form. */
const sgl_pixelstore sgl_tight_store = {GL_FALSE, GL_FALSE, 0, 0, 0, 1};

/* Makes the stencil buffer on first use. Gives 0 when there is not
 * sufficient memory. */
int sgl_need_stencil(sgl_context *c) {
  if (c->stencil) return 1;
  c->stencil = (uint8_t *)calloc(c->depth_size ? c->depth_size : 1, 1);
  return c->stencil != NULL;
}

/* Gives the number of components of a pixel format, or 0 for an
 * invalid one. */
int sgl_format_components(GLenum format) {
  switch (format) {
    case GL_COLOR_INDEX:
    case GL_STENCIL_INDEX:
    case GL_DEPTH_COMPONENT:
    case GL_RED:
    case GL_GREEN:
    case GL_BLUE:
    case GL_ALPHA:
    case GL_LUMINANCE:
      return 1;
    case GL_LUMINANCE_ALPHA:
      return 2;
    case GL_RGB:
    case GL_BGR_EXT:
      return 3;
    case GL_RGBA:
    case GL_BGRA_EXT:
      return 4;
    default:
      return 0;
  }
}

/* Gives the size in bytes of one element of type, 0 for GL_BITMAP, or
 * -1 for an invalid type. */
static int type_size(GLenum type) {
  switch (type) {
    case GL_BITMAP: return 0;
    case GL_UNSIGNED_BYTE: case GL_BYTE: return 1;
    case GL_UNSIGNED_SHORT: case GL_SHORT: return 2;
    case GL_UNSIGNED_INT: case GL_INT: case GL_FLOAT: return 4;
    default: return -1;
  }
}

/* A packed pixel type of OpenGL 1.2: all the components of a pixel are
 * in one element of bytes bytes. */
typedef struct packed_type {
  GLenum type;
  int bytes;               /* the size of the element */
  int n;                   /* the number of components: 3 or 4 */
  int rev;                 /* the first component is in the low bits */
  int bits[4];             /* the bits of each component, first to last */
} packed_type;

/* The packed pixel types. */
static const packed_type kPacked[] = {
    {GL_UNSIGNED_BYTE_3_3_2, 1, 3, 0, {3, 3, 2, 0}},
    {GL_UNSIGNED_BYTE_2_3_3_REV, 1, 3, 1, {3, 3, 2, 0}},
    {GL_UNSIGNED_SHORT_5_6_5, 2, 3, 0, {5, 6, 5, 0}},
    {GL_UNSIGNED_SHORT_5_6_5_REV, 2, 3, 1, {5, 6, 5, 0}},
    {GL_UNSIGNED_SHORT_4_4_4_4, 2, 4, 0, {4, 4, 4, 4}},
    {GL_UNSIGNED_SHORT_4_4_4_4_REV, 2, 4, 1, {4, 4, 4, 4}},
    {GL_UNSIGNED_SHORT_5_5_5_1, 2, 4, 0, {5, 5, 5, 1}},
    {GL_UNSIGNED_SHORT_1_5_5_5_REV, 2, 4, 1, {5, 5, 5, 1}},
    {GL_UNSIGNED_INT_8_8_8_8, 4, 4, 0, {8, 8, 8, 8}},
    {GL_UNSIGNED_INT_8_8_8_8_REV, 4, 4, 1, {8, 8, 8, 8}},
    {GL_UNSIGNED_INT_10_10_10_2, 4, 4, 0, {10, 10, 10, 2}},
    {GL_UNSIGNED_INT_2_10_10_10_REV, 4, 4, 1, {10, 10, 10, 2}},
};

/* Gives the packed pixel type type, or NULL when type is not packed. */
static const packed_type *packed_of(GLenum type) {
  size_t i;
  for (i = 0; i < sizeof kPacked / sizeof kPacked[0]; i++)
    if (kPacked[i].type == type) return &kPacked[i];
  return NULL;
}

/* Gives 1 when format can have the packed type pk: three components
 * need GL_RGB or GL_BGR, four need GL_RGBA or GL_BGRA. */
static int packed_format_ok(const packed_type *pk, GLenum format) {
  if (pk->n == 3) return format == GL_RGB || format == GL_BGR;
  return format == GL_RGBA || format == GL_BGRA;
}

/* Gives the shift of component k of a packed type. */
static int packed_shift(const packed_type *pk, int k) {
  int i, below = 0, total = 0;
  for (i = 0; i < pk->n; i++) total += pk->bits[i];
  for (i = 0; i < k; i++) below += pk->bits[i];
  return pk->rev ? below : total - below - pk->bits[k];
}

/* Reads the element of pixel x of a row of a packed type. */
static uint32_t packed_read(const packed_type *pk, const uint8_t *row, int x, int swap) {
  const uint8_t *p = row + (size_t)x * pk->bytes;
  if (pk->bytes == 1) return p[0];
  if (pk->bytes == 2) {
    uint16_t v;
    memcpy(&v, p, 2);
    if (swap) v = (uint16_t)((v >> 8) | (v << 8));
    return v;
  } else {
    uint32_t v;
    memcpy(&v, p, 4);
    if (swap) v = (v >> 24) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | (v << 24);
    return v;
  }
}

/* Gives the components (0 to 1, first to last) of a packed element. */
static void packed_unpack(const packed_type *pk, uint32_t e, GLfloat v[4]) {
  int k;
  for (k = 0; k < pk->n; k++) {
    uint32_t max = (1u << pk->bits[k]) - 1u;
    v[k] = (GLfloat)((e >> packed_shift(pk, k)) & max) / (GLfloat)max;
  }
}

/* Gives the packed element of the components v (0 to 1, first to
 * last). */
static uint32_t packed_pack(const packed_type *pk, const GLfloat *v) {
  uint32_t e = 0;
  int k;
  for (k = 0; k < pk->n; k++) {
    uint32_t max = (1u << pk->bits[k]) - 1u;
    e |= (uint32_t)(sgl_clamp01(v[k]) * (GLfloat)max + 0.5f) << packed_shift(pk, k);
  }
  return e;
}

/* Writes a packed element at pixel x of a row. */
static void packed_write(const packed_type *pk, uint8_t *row, int x, uint32_t e) {
  uint8_t *p = row + (size_t)x * pk->bytes;
  if (pk->bytes == 1) {
    p[0] = (uint8_t)e;
  } else if (pk->bytes == 2) {
    uint16_t v = (uint16_t)e;
    memcpy(p, &v, 2);
  } else {
    memcpy(p, &e, 4);
  }
}

/* Gives the number of bytes from one row of an image to the next. */
static size_t row_bytes(const sgl_pixelstore *ps, int w, int comps, int size) {
  int len = ps->row_length > 0 ? ps->row_length : w;
  size_t bytes;
  int align = ps->alignment > 0 ? ps->alignment : 1;
  if (size == 0) {
    bytes = ((size_t)len * (size_t)comps + 7) / 8;
    return (bytes + (size_t)align - 1) / (size_t)align * (size_t)align;
  }
  bytes = (size_t)len * (size_t)comps * (size_t)size;
  if (size < align) bytes = (bytes + (size_t)align - 1) / (size_t)align * (size_t)align;
  return bytes;
}

/* Gives the number of bytes that an image of w x h pixels in format and
 * type takes with the pixel store ps, from the pointer that the program
 * gives. */
size_t sgl_image_bytes(const sgl_pixelstore *ps, int w, int h, GLenum format, GLenum type) {
  int comps = sgl_format_components(format), size = type_size(type);
  const packed_type *pk = packed_of(type);
  size_t rb;
  if (pk) {
    /* One element for each pixel. */
    comps = 1;
    size = pk->bytes;
  }
  if (comps == 0 || size < 0 || w <= 0 || h <= 0) return 0;
  rb = row_bytes(ps, w, comps, size);
  if (size == 0)
    return rb * (size_t)(h + ps->skip_rows) + ((size_t)ps->skip_pixels * comps + 7) / 8;
  return rb * (size_t)(h + ps->skip_rows) + (size_t)ps->skip_pixels * comps * size;
}

/* Reads element i of a row of type type as a value 0 to 1 (a float for
 * GL_FLOAT). */
static GLfloat element(const uint8_t *row, int i, GLenum type, int swap) {
  switch (type) {
    case GL_UNSIGNED_BYTE: return row[i] / 255.0f;
    case GL_BYTE: {
      GLfloat v = (GLfloat)(int8_t)row[i] / 127.0f;
      return v < -1.0f ? -1.0f : v;
    }
    case GL_UNSIGNED_SHORT:
    case GL_SHORT: {
      uint16_t v;
      memcpy(&v, row + i * 2, 2);
      if (swap) v = (uint16_t)((v >> 8) | (v << 8));
      if (type == GL_SHORT) return (GLfloat)(int16_t)v / 32767.0f;
      return v / 65535.0f;
    }
    default: {
      uint32_t v;
      memcpy(&v, row + i * 4, 4);
      if (swap) v = (v >> 24) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | (v << 24);
      if (type == GL_FLOAT) {
        GLfloat f;
        memcpy(&f, &v, 4);
        return f;
      }
      if (type == GL_INT) return (GLfloat)(int32_t)v / 2147483647.0f;
      return (GLfloat)v / 4294967295.0f;
    }
  }
}

/* Gives a value 0 to 1 as a byte 0 to 255. */
static uint8_t to_byte(GLfloat v) { return (uint8_t)(sgl_clamp01(v) * 255.0f + 0.5f); }

/* Gives 1 when the pixel transfer of the context changes colors. */
static int transfer_on(const sgl_context *c) {
  const sgl_attrib *a = &c->a;
  return a->red_scale != 1.0f || a->green_scale != 1.0f || a->blue_scale != 1.0f ||
         a->alpha_scale != 1.0f || a->red_bias != 0.0f || a->green_bias != 0.0f ||
         a->blue_bias != 0.0f || a->alpha_bias != 0.0f;
}

/* Reads element x of a row of indices of type type as an integer. */
static int32_t index_element(const uint8_t *row, int x, GLenum type, int swap) {
  switch (type) {
    case GL_UNSIGNED_BYTE: return row[x];
    case GL_BYTE: return (int8_t)row[x];
    case GL_UNSIGNED_SHORT:
    case GL_SHORT: {
      uint16_t v;
      memcpy(&v, row + x * 2, 2);
      if (swap) v = (uint16_t)((v >> 8) | (v << 8));
      return type == GL_SHORT ? (int32_t)(int16_t)v : (int32_t)v;
    }
    default: {
      uint32_t v;
      memcpy(&v, row + x * 4, 4);
      if (swap) v = (v >> 24) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | (v << 24);
      if (type == GL_FLOAT) {
        GLfloat f;
        memcpy(&f, &v, 4);
        return (int32_t)f;
      }
      return (int32_t)v;
    }
  }
}

/* Unpacks an image of w x h indices (color or stencil indices) of type
 * type into out, one integer for each pixel, rows bottom first. GL_BITMAP
 * gives 0 and 1. Gives 0 for an invalid type. */
static int unpack_indices(const sgl_pixelstore *ps, int w, int h, GLenum type, const void *pixels,
                          int32_t *out) {
  int size = type_size(type), x, y;
  const uint8_t *base = (const uint8_t *)pixels;
  size_t rb;
  if (size < 0) return 0;
  rb = row_bytes(ps, w, 1, size);
  for (y = 0; y < h; y++) {
    const uint8_t *row = base + (size_t)(y + ps->skip_rows) * rb;
    for (x = 0; x < w; x++) {
      int32_t v;
      if (size == 0) {
        int bit = x + ps->skip_pixels;
        uint8_t b = row[bit >> 3];
        v = ps->lsb_first ? (b >> (bit & 7)) & 1 : (b >> (7 - (bit & 7))) & 1;
      } else {
        v = index_element(row + (size_t)ps->skip_pixels * size, x, type, ps->swap_bytes);
      }
      out[(size_t)y * w + x] = v;
    }
  }
  return 1;
}

/* Gives the value of entry i of a pixel map slot (masked to the size of
 * the map), or 0 for the map of one 0 that is there at first. */
static GLfloat map_entry(const sgl_context *c, int slot, int32_t i) {
  int n = c->pixel_map_size[slot];
  if (!c->pixel_map[slot] || n <= 0) return 0.0f;
  return c->pixel_map[slot][(uint32_t)i & (uint32_t)(n - 1)];
}

/* The slots of the pixel maps, in the order of GL_PIXEL_MAP_I_TO_I. */
enum { MAP_I_TO_I, MAP_S_TO_S, MAP_I_TO_R, MAP_I_TO_G, MAP_I_TO_B, MAP_I_TO_A,
       MAP_R_TO_R, MAP_G_TO_G, MAP_B_TO_B, MAP_A_TO_A };

/* Applies the index shift and offset to an index, then the map of slot
 * when map is on (GL_MAP_COLOR or GL_MAP_STENCIL). */
static int32_t shift_index(const sgl_context *c, int32_t i, int map, int slot) {
  GLint sh = c->a.index_shift;
  if (sh > 0)
    i = (int32_t)((uint32_t)i << (sh > 31 ? 31 : sh));
  else if (sh < 0)
    i >>= (-sh > 31 ? 31 : -sh);
  i += c->a.index_offset;
  if (map) i = (int32_t)map_entry(c, slot, i);
  return i;
}

/* Changes a color index to RGBA bytes through the I_TO_R, I_TO_G,
 * I_TO_B and I_TO_A maps (RGBA mode). */
static void index_to_rgba(const sgl_context *c, int32_t i, uint8_t *o);

/* Applies the R_TO_R, G_TO_G, B_TO_B and A_TO_A maps to a color in
 * [0, 1] (GL_MAP_COLOR). */
static void map_rgba(const sgl_context *c, GLfloat v[4]) {
  int k;
  for (k = 0; k < 4; k++) {
    int n = c->pixel_map_size[MAP_R_TO_R + k];
    GLfloat x = sgl_clamp01(v[k]);
    if (c->pixel_map[MAP_R_TO_R + k] && n > 0)
      v[k] = c->pixel_map[MAP_R_TO_R + k][(int)(x * (GLfloat)(n - 1) + 0.5f)];
    else
      v[k] = 0.0f;
  }
}

/* Unpacks an image of w x h pixels in format and type into out, 4 bytes
 * (R, G, B, A) for each pixel, rows bottom first as in the image. The
 * pixel transfer scale and bias apply. Gives 0 for an invalid format or
 * type. Color index images give the index in R, G and B. */
int sgl_unpack_rgba8(const sgl_pixelstore *ps, int w, int h, GLenum format, GLenum type,
                     const void *pixels, uint8_t *out) {
  int comps = sgl_format_components(format), size = type_size(type), x, y;
  const uint8_t *base = (const uint8_t *)pixels;
  size_t rb;
  int transfer = sgl_ctx && (transfer_on(sgl_ctx) || sgl_ctx->a.map_color) && format != GL_COLOR_INDEX;
  if (format == GL_COLOR_INDEX) {
    /* Color indices become colors through the pixel maps. */
    int32_t *idx = (int32_t *)malloc((size_t)w * h * sizeof(int32_t) + 1);
    size_t i, n = (size_t)w * h;
    if (!idx || !sgl_ctx || !unpack_indices(ps, w, h, type, pixels, idx)) {
      free(idx);
      return 0;
    }
    for (i = 0; i < n; i++) index_to_rgba(sgl_ctx, shift_index(sgl_ctx, idx[i], 0, 0), out + i * 4);
    free(idx);
    return 1;
  }
  {
    const packed_type *pk = packed_of(type);
    if (pk) {
      if (!packed_format_ok(pk, format)) return 0;
      comps = 1;
      size = pk->bytes;
    }
  }
  if (comps == 0 || size <= 0) return 0;
  rb = row_bytes(ps, w, comps, size);
  base += (size_t)ps->skip_rows * rb + (size_t)ps->skip_pixels * comps * size;
  for (y = 0; y < h; y++) {
    const uint8_t *row = base + (size_t)y * rb;
    uint8_t *o = out + (size_t)y * w * 4;
    const packed_type *pk = packed_of(type);
    if (type == GL_UNSIGNED_BYTE && !transfer) {
      /* The common case, without floats. */
      for (x = 0; x < w; x++, row += comps, o += 4) {
        switch (format) {
          case GL_RGBA: o[0] = row[0]; o[1] = row[1]; o[2] = row[2]; o[3] = row[3]; break;
          case GL_RGB: o[0] = row[0]; o[1] = row[1]; o[2] = row[2]; o[3] = 255; break;
          case GL_BGRA_EXT: o[0] = row[2]; o[1] = row[1]; o[2] = row[0]; o[3] = row[3]; break;
          case GL_BGR_EXT: o[0] = row[2]; o[1] = row[1]; o[2] = row[0]; o[3] = 255; break;
          case GL_LUMINANCE: case GL_COLOR_INDEX:
            o[0] = o[1] = o[2] = row[0]; o[3] = 255; break;
          case GL_LUMINANCE_ALPHA: o[0] = o[1] = o[2] = row[0]; o[3] = row[1]; break;
          case GL_ALPHA: o[0] = o[1] = o[2] = 0; o[3] = row[0]; break;
          case GL_RED: o[0] = row[0]; o[1] = o[2] = 0; o[3] = 255; break;
          case GL_GREEN: o[1] = row[0]; o[0] = o[2] = 0; o[3] = 255; break;
          case GL_BLUE: o[2] = row[0]; o[0] = o[1] = 0; o[3] = 255; break;
          default: o[0] = o[1] = o[2] = row[0]; o[3] = 255; break;
        }
      }
      continue;
    }
    for (x = 0; x < w; x++, o += 4) {
      GLfloat v[4] = {0.0f, 0.0f, 0.0f, 1.0f};
      int i0 = x * comps, swap = ps->swap_bytes;
      if (pk) packed_unpack(pk, packed_read(pk, row, x, swap), v);
      else switch (format) {
        case GL_RGBA:
        case GL_BGRA_EXT:
          v[0] = element(row, i0, type, swap);
          v[1] = element(row, i0 + 1, type, swap);
          v[2] = element(row, i0 + 2, type, swap);
          v[3] = element(row, i0 + 3, type, swap);
          break;
        case GL_RGB:
        case GL_BGR_EXT:
          v[0] = element(row, i0, type, swap);
          v[1] = element(row, i0 + 1, type, swap);
          v[2] = element(row, i0 + 2, type, swap);
          break;
        case GL_LUMINANCE_ALPHA:
          v[0] = v[1] = v[2] = element(row, i0, type, swap);
          v[3] = element(row, i0 + 1, type, swap);
          break;
        case GL_ALPHA: v[3] = element(row, i0, type, swap); break;
        case GL_RED: v[0] = element(row, i0, type, swap); break;
        case GL_GREEN: v[1] = element(row, i0, type, swap); break;
        case GL_BLUE: v[2] = element(row, i0, type, swap); break;
        default: v[0] = v[1] = v[2] = element(row, i0, type, swap); break;
      }
      if (format == GL_BGRA_EXT || format == GL_BGR_EXT) {
        GLfloat t = v[0];
        v[0] = v[2];
        v[2] = t;
      }
      if (transfer) {
        const sgl_attrib *a = &sgl_ctx->a;
        v[0] = v[0] * a->red_scale + a->red_bias;
        v[1] = v[1] * a->green_scale + a->green_bias;
        v[2] = v[2] * a->blue_scale + a->blue_bias;
        v[3] = v[3] * a->alpha_scale + a->alpha_bias;
        if (a->map_color) map_rgba(sgl_ctx, v);
      }
      o[0] = to_byte(v[0]);
      o[1] = to_byte(v[1]);
      o[2] = to_byte(v[2]);
      o[3] = to_byte(v[3]);
    }
  }
  return 1;
}

/* Changes a color index to RGBA bytes through the index maps (see the
 * declaration above). */
static void index_to_rgba(const sgl_context *c, int32_t i, uint8_t *o) {
  o[0] = to_byte(map_entry(c, MAP_I_TO_R, i));
  o[1] = to_byte(map_entry(c, MAP_I_TO_G, i));
  o[2] = to_byte(map_entry(c, MAP_I_TO_B, i));
  o[3] = to_byte(map_entry(c, MAP_I_TO_A, i));
}

/* Packs w x h indices (rows bottom first) into dst in type with the pack
 * store of the context. */
static void pack_indices(sgl_context *c, int w, int h, const int32_t *src, GLenum type, void *dst) {
  const sgl_pixelstore *ps = &c->c.pack;
  int size = type_size(type), x, y;
  size_t rb;
  uint8_t *base = (uint8_t *)dst;
  if (size < 0) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  rb = row_bytes(ps, w, 1, size);
  for (y = 0; y < h; y++) {
    uint8_t *row = base + (size_t)(y + ps->skip_rows) * rb;
    for (x = 0; x < w; x++) {
      int32_t v = src[(size_t)y * w + x];
      if (size == 0) {
        int bit = x + ps->skip_pixels;
        uint8_t m = (uint8_t)(ps->lsb_first ? 1u << (bit & 7) : 0x80u >> (bit & 7));
        if (v & 1)
          row[bit >> 3] |= m;
        else
          row[bit >> 3] &= (uint8_t)~m;
        continue;
      }
      {
        uint8_t *e = row + ((size_t)ps->skip_pixels + (size_t)x) * size;
        switch (type) {
          case GL_UNSIGNED_BYTE: case GL_BYTE: *e = (uint8_t)v; break;
          case GL_UNSIGNED_SHORT: case GL_SHORT: {
            uint16_t u = (uint16_t)v;
            memcpy(e, &u, 2);
            break;
          }
          case GL_FLOAT: {
            GLfloat f = (GLfloat)v;
            memcpy(e, &f, 4);
            break;
          }
          default: memcpy(e, &v, 4); break;
        }
      }
    }
  }
}

/* Unpacks an image of color indices (format GL_COLOR_INDEX) into out,
 * one byte for each pixel (the low 8 bits of the index). */
void sgl_unpack_index(const sgl_pixelstore *ps, int w, int h, GLenum type, const void *pixels,
                      uint8_t *out) {
  int size = type_size(type), x, y;
  const uint8_t *base = (const uint8_t *)pixels;
  size_t rb;
  if (size < 0) return;
  rb = row_bytes(ps, w, 1, size);
  if (size == 0) {
    for (y = 0; y < h; y++) {
      const uint8_t *row = base + (size_t)(y + ps->skip_rows) * rb;
      for (x = 0; x < w; x++) {
        int bit = x + ps->skip_pixels;
        uint8_t byte = row[bit >> 3];
        int b = ps->lsb_first ? (byte >> (bit & 7)) & 1 : (byte >> (7 - (bit & 7))) & 1;
        out[(size_t)y * w + x] = (uint8_t)b;
      }
    }
    return;
  }
  base += (size_t)ps->skip_rows * rb + (size_t)ps->skip_pixels * size;
  for (y = 0; y < h; y++) {
    const uint8_t *row = base + (size_t)y * rb;
    for (x = 0; x < w; x++) {
      uint32_t v = 0;
      switch (size) {
        case 1: v = row[x]; break;
        case 2: {
          uint16_t s;
          memcpy(&s, row + x * 2, 2);
          v = s;
          break;
        }
        default:
          if (type == GL_FLOAT) {
            GLfloat f;
            memcpy(&f, row + x * 4, 4);
            v = (uint32_t)(f < 0.0f ? 0.0f : f);
          } else {
            memcpy(&v, row + x * 4, 4);
          }
          break;
      }
      out[(size_t)y * w + x] = (uint8_t)v;
    }
  }
}

/* Packs w x h pixels of 0xAARRGGBB (rows bottom first) into dst in
 * format and type with the pack store of the context. */
static void pack_rgba(sgl_context *c, int w, int h, const uint32_t *src, GLenum format,
                      GLenum type, void *dst) {
  const sgl_pixelstore *ps = &c->c.pack;
  int comps = sgl_format_components(format), size = type_size(type), x, y, k;
  const packed_type *pk = packed_of(type);
  size_t rb;
  uint8_t *base = (uint8_t *)dst;
  if (pk) {
    if (!packed_format_ok(pk, format)) {
      sgl_error(GL_INVALID_OPERATION);
      return;
    }
    comps = 1;
    size = pk->bytes;
  }
  if (comps == 0 || size <= 0) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  rb = row_bytes(ps, w, comps, size);
  base += (size_t)ps->skip_rows * rb + (size_t)ps->skip_pixels * comps * size;
  for (y = 0; y < h; y++) {
    uint8_t *row = base + (size_t)y * rb;
    for (x = 0; x < w; x++) {
      uint32_t p = src[(size_t)y * w + x];
      GLfloat v[4], out[4];
      int n = 0;
      v[0] = ((p >> 16) & 0xFF) / 255.0f;
      v[1] = ((p >> 8) & 0xFF) / 255.0f;
      v[2] = (p & 0xFF) / 255.0f;
      v[3] = (p >> 24) / 255.0f;
      switch (format) {
        case GL_RGBA: out[0] = v[0]; out[1] = v[1]; out[2] = v[2]; out[3] = v[3]; n = 4; break;
        case GL_BGRA_EXT: out[0] = v[2]; out[1] = v[1]; out[2] = v[0]; out[3] = v[3]; n = 4; break;
        case GL_RGB: out[0] = v[0]; out[1] = v[1]; out[2] = v[2]; n = 3; break;
        case GL_BGR_EXT: out[0] = v[2]; out[1] = v[1]; out[2] = v[0]; n = 3; break;
        case GL_RED: out[0] = v[0]; n = 1; break;
        case GL_GREEN: out[0] = v[1]; n = 1; break;
        case GL_BLUE: out[0] = v[2]; n = 1; break;
        case GL_ALPHA: out[0] = v[3]; n = 1; break;
        case GL_LUMINANCE: out[0] = sgl_clamp01(v[0] + v[1] + v[2]); n = 1; break;
        case GL_LUMINANCE_ALPHA: out[0] = sgl_clamp01(v[0] + v[1] + v[2]); out[1] = v[3]; n = 2; break;
        default: out[0] = v[0]; n = 1; break;
      }
      if (pk) {
        packed_write(pk, row, x, packed_pack(pk, out));
        continue;
      }
      for (k = 0; k < n; k++) {
        uint8_t *e = row + ((size_t)x * comps + k) * size;
        switch (type) {
          case GL_UNSIGNED_BYTE: *e = to_byte(out[k]); break;
          case GL_BYTE: *e = (uint8_t)(int8_t)(out[k] * 127.0f); break;
          case GL_UNSIGNED_SHORT: {
            uint16_t s = (uint16_t)(out[k] * 65535.0f + 0.5f);
            memcpy(e, &s, 2);
            break;
          }
          case GL_SHORT: {
            int16_t s = (int16_t)(out[k] * 32767.0f);
            memcpy(e, &s, 2);
            break;
          }
          case GL_UNSIGNED_INT: {
            uint32_t u = (uint32_t)(out[k] * 4294967295.0);
            memcpy(e, &u, 4);
            break;
          }
          case GL_INT: {
            int32_t i = (int32_t)(out[k] * 2147483647.0);
            memcpy(e, &i, 4);
            break;
          }
          default:
            memcpy(e, &out[k], 4);
            break;
        }
      }
    }
  }
}

/* Packs a texture image of w x h texels into dst (glGetTexImage). */
void sgl_pack_texels(sgl_context *c, int w, int h, const uint32_t *texels, GLenum format,
                     GLenum type, void *dst) {
  pack_rgba(c, w, h, texels, format, type, dst);
}

/* Gives the current context, or NULL (with GL_INVALID_OPERATION) between
 * glBegin and glEnd. */
static sgl_context *outside(void) {
  sgl_context *c = sgl_ctx;
  if (c->prim != SGL_NO_PRIM) {
    sgl_error(GL_INVALID_OPERATION);
    return NULL;
  }
  return c;
}

/* Sets a value of a pixel store. */
static void store(sgl_pixelstore *ps, GLenum pname, GLint v) {
  switch (pname) {
    case GL_PACK_SWAP_BYTES: case GL_UNPACK_SWAP_BYTES: ps->swap_bytes = v != 0; break;
    case GL_PACK_LSB_FIRST: case GL_UNPACK_LSB_FIRST: ps->lsb_first = v != 0; break;
    case GL_PACK_ROW_LENGTH: case GL_UNPACK_ROW_LENGTH: ps->row_length = v; break;
    case GL_PACK_SKIP_ROWS: case GL_UNPACK_SKIP_ROWS: ps->skip_rows = v; break;
    case GL_PACK_SKIP_PIXELS: case GL_UNPACK_SKIP_PIXELS: ps->skip_pixels = v; break;
    case GL_PACK_IMAGE_HEIGHT: case GL_UNPACK_IMAGE_HEIGHT:
    case GL_PACK_SKIP_IMAGES: case GL_UNPACK_SKIP_IMAGES:
      if (v < 0) {
        sgl_error(GL_INVALID_VALUE);
        return;
      }
      if (pname == GL_PACK_IMAGE_HEIGHT || pname == GL_UNPACK_IMAGE_HEIGHT)
        ps->image_height = v;
      else
        ps->skip_images = v;
      break;
    case GL_PACK_ALIGNMENT:
    case GL_UNPACK_ALIGNMENT:
      if (v != 1 && v != 2 && v != 4 && v != 8) {
        sgl_error(GL_INVALID_VALUE);
        return;
      }
      ps->alignment = v;
      break;
    default: sgl_error(GL_INVALID_ENUM); break;
  }
}

/* glPixelStorei: sets a value of the pack or unpack store. */
void APIENTRY sgl_PixelStorei(GLenum pname, GLint param) {
  sgl_context *c = outside();
  if (!c) return;
  switch (pname) {
    case GL_PACK_SWAP_BYTES: case GL_PACK_LSB_FIRST: case GL_PACK_ROW_LENGTH:
    case GL_PACK_SKIP_ROWS: case GL_PACK_SKIP_PIXELS: case GL_PACK_ALIGNMENT:
      store(&c->c.pack, pname, param);
      break;
    default:
      store(&c->c.unpack, pname, param);
      break;
  }
}

/* glPixelStoref: as glPixelStorei. */
void APIENTRY sgl_PixelStoref(GLenum pname, GLfloat param) {
  sgl_PixelStorei(pname, (GLint)(param + (param < 0 ? -0.5f : 0.5f)));
}

/* glPixelTransferf: sets a scale or bias of the pixel transfer. */
void APIENTRY sgl_PixelTransferf(GLenum pname, GLfloat v) {
  sgl_attrib *a = &sgl_ctx->a;
  switch (pname) {
    case GL_RED_SCALE: a->red_scale = v; break;
    case GL_RED_BIAS: a->red_bias = v; break;
    case GL_GREEN_SCALE: a->green_scale = v; break;
    case GL_GREEN_BIAS: a->green_bias = v; break;
    case GL_BLUE_SCALE: a->blue_scale = v; break;
    case GL_BLUE_BIAS: a->blue_bias = v; break;
    case GL_ALPHA_SCALE: a->alpha_scale = v; break;
    case GL_ALPHA_BIAS: a->alpha_bias = v; break;
    case GL_DEPTH_SCALE: a->depth_scale = v; break;
    case GL_DEPTH_BIAS: a->depth_bias = v; break;
    case GL_MAP_COLOR: a->map_color = v != 0.0f; break;
    case GL_MAP_STENCIL: a->map_stencil = v != 0.0f; break;
    case GL_INDEX_SHIFT: a->index_shift = (GLint)v; break;
    case GL_INDEX_OFFSET: a->index_offset = (GLint)v; break;
    default: sgl_error(GL_INVALID_ENUM); break;
  }
}

/* glPixelZoom: sets the zoom of glDrawPixels and glCopyPixels. */
void APIENTRY sgl_PixelZoom(GLfloat x, GLfloat y) {
  sgl_ctx->a.zoom_x = x;
  sgl_ctx->a.zoom_y = y;
}

/* Gives the slot of a pixel map, or -1. */
static int map_slot(GLenum map) {
  if (map >= GL_PIXEL_MAP_I_TO_I && map <= GL_PIXEL_MAP_A_TO_A) return (int)(map - GL_PIXEL_MAP_I_TO_I);
  return -1;
}

/* glPixelMapfv: sets a pixel map. The size of a map must be a power of
 * two. */
void APIENTRY sgl_PixelMapfv(GLenum map, GLsizei n, const GLfloat *values) {
  sgl_context *c = sgl_ctx;
  int i = map_slot(map);
  GLfloat *p;
  if (i < 0) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  if (n < 1 || n > 256 || (i <= MAP_I_TO_A && (n & (n - 1)))) {
    sgl_error(GL_INVALID_VALUE);
    return;
  }
  p = (GLfloat *)malloc((size_t)n * sizeof(GLfloat));
  if (!p) {
    sgl_error(GL_OUT_OF_MEMORY);
    return;
  }
  memcpy(p, values, (size_t)n * sizeof(GLfloat));
  free(c->pixel_map[i]);
  c->pixel_map[i] = p;
  c->pixel_map_size[i] = n;
}

/* glPixelMapuiv: as glPixelMapfv, with values that map to [0, 1] (or
 * indices for the index maps). */
void APIENTRY sgl_PixelMapuiv(GLenum map, GLsizei n, const GLuint *values) {
  GLfloat f[256];
  int i, index = map == GL_PIXEL_MAP_I_TO_I || map == GL_PIXEL_MAP_S_TO_S;
  if (n < 1 || n > 256) {
    sgl_error(GL_INVALID_VALUE);
    return;
  }
  for (i = 0; i < n; i++) f[i] = index ? (GLfloat)values[i] : (GLfloat)values[i] / 4294967295.0f;
  SGL_DISPATCH->PixelMapfv(map, n, f);
}

/* glPixelMapusv: as glPixelMapuiv, with 16-bit values. */
void APIENTRY sgl_PixelMapusv(GLenum map, GLsizei n, const GLushort *values) {
  GLfloat f[256];
  int i, index = map == GL_PIXEL_MAP_I_TO_I || map == GL_PIXEL_MAP_S_TO_S;
  if (n < 1 || n > 256) {
    sgl_error(GL_INVALID_VALUE);
    return;
  }
  for (i = 0; i < n; i++) f[i] = index ? (GLfloat)values[i] : (GLfloat)values[i] / 65535.0f;
  SGL_DISPATCH->PixelMapfv(map, n, f);
}

/* glGetPixelMapfv: gives a pixel map. */
void APIENTRY sgl_GetPixelMapfv(GLenum map, GLfloat *values) {
  sgl_context *c = sgl_ctx;
  int i = map_slot(map);
  if (i < 0) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  if (c->pixel_map[i])
    memcpy(values, c->pixel_map[i], (size_t)c->pixel_map_size[i] * sizeof(GLfloat));
  else
    values[0] = 0.0f;
}

/* glGetPixelMapuiv: gives a pixel map as 32-bit values. */
void APIENTRY sgl_GetPixelMapuiv(GLenum map, GLuint *values) {
  sgl_context *c = sgl_ctx;
  int i = map_slot(map), k;
  if (i < 0) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  if (!c->pixel_map[i]) {
    values[0] = 0;
    return;
  }
  for (k = 0; k < c->pixel_map_size[i]; k++) values[k] = (GLuint)(c->pixel_map[i][k] * 4294967295.0);
}

/* glGetPixelMapusv: gives a pixel map as 16-bit values. */
void APIENTRY sgl_GetPixelMapusv(GLenum map, GLushort *values) {
  sgl_context *c = sgl_ctx;
  int i = map_slot(map), k;
  if (i < 0) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  if (!c->pixel_map[i]) {
    values[0] = 0;
    return;
  }
  for (k = 0; k < c->pixel_map_size[i]; k++) values[k] = (GLushort)(c->pixel_map[i][k] * 65535.0f);
}

/* glClear: sets the buffers in mask to their clear values, in the
 * scissor box. */
void APIENTRY sgl_Clear(GLbitfield mask) {
  sgl_context *c = outside();
  int x0 = 0, y0 = 0, x1, y1, x, y;
  if (!c || !c->color) return;
  if (c->render_mode != GL_RENDER) return;
  x1 = c->width;
  y1 = c->height;
  if (c->a.enabled[SGL_SCISSOR_TEST]) sgl_clip_rect(c, &x0, &y0, &x1, &y1);
  if (x0 >= x1 || y0 >= y1) return;
  if (mask & GL_COLOR_BUFFER_BIT) {
    const GLfloat *cc = c->a.clear_color;
    uint32_t v, keep;
    if (c->index_mode) {
      v = (uint32_t)(int32_t)(c->a.clear_index + 0.5f) & c->index_max;
      keep = c->index_max & ~c->a.index_mask;
    } else {
      v = ((uint32_t)to_byte(cc[3]) << 24) | ((uint32_t)to_byte(cc[0]) << 16) |
          ((uint32_t)to_byte(cc[1]) << 8) | to_byte(cc[2]);
      keep = (c->a.color_mask[3] ? 0 : 0xFF000000u) | (c->a.color_mask[0] ? 0 : 0xFF0000u) |
             (c->a.color_mask[1] ? 0 : 0xFF00u) | (c->a.color_mask[2] ? 0 : 0xFFu);
    }
    for (y = y0; y < y1; y++) {
      uint32_t *row = c->color + (long)(c->height - 1 - y) * c->rowpixels;
      if (keep == 0)
        for (x = x0; x < x1; x++) row[x] = v;
      else if (c->index_mode)
        for (x = x0; x < x1; x++) row[x] = (row[x] & keep) | (v & ~keep & c->index_max);
      else if (keep != 0xFFFFFFFFu)
        for (x = x0; x < x1; x++) row[x] = (row[x] & keep) | (v & ~keep);
    }
  }
  if ((mask & GL_DEPTH_BUFFER_BIT) && c->a.depth_mask && c->depth) {
    uint32_t z = (uint32_t)(c->a.clear_depth * (GLdouble)SGL_Z_MAX) >> SGL_DEPTH_SHIFT;
    for (y = y0; y < y1; y++) {
      sgl_depth_t *row = c->depth + (size_t)(c->height - 1 - y) * c->width;
      for (x = x0; x < x1; x++) row[x] = (sgl_depth_t)z;
    }
  }
  if ((mask & GL_STENCIL_BUFFER_BIT) && sgl_need_stencil(c)) {
    for (y = y0; y < y1; y++) {
      uint8_t *row = c->stencil + (size_t)(c->height - 1 - y) * c->width;
      for (x = x0; x < x1; x++)
        row[x] = (uint8_t)((row[x] & ~c->a.stencil_writemask) |
                           ((uint32_t)c->a.stencil_clear & c->a.stencil_writemask));
    }
  }
  if ((mask & GL_ACCUM_BUFFER_BIT) && c->accum) {
    for (y = y0; y < y1; y++) {
      GLfloat *row = c->accum + (size_t)y * c->width * 4;
      for (x = x0; x < x1; x++) memcpy(row + x * 4, c->a.clear_accum, 4 * sizeof(GLfloat));
    }
  }
}

/* Gives the raster position z as the z of the rasterizer. */
static uint32_t raster_z(const sgl_context *c) {
  return (uint32_t)(sgl_clamp01(c->a.raster_pos[2]) * (GLfloat)SGL_Z_MAX);
}

/* The kinds of pixel rectangles that draw_rect writes. */
enum { RECT_RGBA, RECT_INDEX, RECT_DEPTH, RECT_STENCIL };

/* Writes one value of a pixel rectangle at window position x, y. */
static void rect_pixel(sgl_context *c, int kind, int x, int y, uint32_t z, const uint8_t *rgba,
                       int32_t value) {
  GLfloat col[4];
  switch (kind) {
    case RECT_RGBA:
      col[0] = rgba[0] / 255.0f;
      col[1] = rgba[1] / 255.0f;
      col[2] = rgba[2] / 255.0f;
      col[3] = rgba[3] / 255.0f;
      sgl_write_fragment(c, x, y, z, col);
      break;
    case RECT_INDEX:
      col[0] = (GLfloat)value;
      col[1] = col[2] = col[3] = 0.0f;
      sgl_write_fragment(c, x, y, z, col);
      break;
    case RECT_DEPTH:
      /* A depth image takes the color of the raster position. */
      if (c->index_mode) {
        col[0] = c->a.raster_index;
        sgl_write_fragment(c, x, y, (uint32_t)value, col);
      } else {
        sgl_write_fragment(c, x, y, (uint32_t)value, c->a.raster_color);
      }
      break;
    default: {
      /* A stencil image goes to the stencil buffer, in the scissor box. */
      int x0 = x, y0 = y, x1 = x + 1, y1 = y + 1;
      size_t i;
      uint8_t w = (uint8_t)c->a.stencil_writemask;
      sgl_clip_rect(c, &x0, &y0, &x1, &y1);
      if (x0 >= x1 || y0 >= y1 || !c->stencil) break;
      i = (size_t)(c->height - 1 - y) * c->width + (size_t)x;
      c->stencil[i] = (uint8_t)((c->stencil[i] & ~w) | ((uint8_t)value & w));
      break;
    }
  }
}

/* Draws w x h values of a pixel rectangle (rows bottom first) at the
 * raster position, with the pixel zoom. rgba has 4 bytes for each pixel
 * (RECT_RGBA); values has one value for each pixel (the other kinds). */
static void draw_rect(sgl_context *c, int kind, int w, int h, const uint8_t *rgba,
                      const int32_t *values) {
  GLfloat zx = c->a.zoom_x, zy = c->a.zoom_y;
  GLfloat rx = c->a.raster_pos[0], ry = c->a.raster_pos[1];
  uint32_t z = raster_z(c);
  int x, y;
  for (y = 0; y < h; y++) {
    int ya = (int)floorf(ry + zy * (GLfloat)y), yb = (int)floorf(ry + zy * (GLfloat)(y + 1));
    if (ya > yb) {
      int t = ya;
      ya = yb;
      yb = t;
    }
    for (x = 0; x < w; x++) {
      size_t k = (size_t)y * w + x;
      int xa = (int)floorf(rx + zx * (GLfloat)x), xb = (int)floorf(rx + zx * (GLfloat)(x + 1));
      int i, j;
      if (xa > xb) {
        int t = xa;
        xa = xb;
        xb = t;
      }
      for (j = ya; j < yb; j++)
        for (i = xa; i < xb; i++)
          rect_pixel(c, kind, i, j, z, rgba ? rgba + k * 4 : NULL, values ? values[k] : 0);
    }
  }
}


/* Unpacks a depth image by the pixel store ps into values 0 to max,
 * with the depth scale and bias. Gives 0 for an invalid type. */
int sgl_unpack_depth(sgl_context *c, const sgl_pixelstore *ps, int w, int h, GLenum type,
                     const void *pixels, uint32_t *out, uint32_t max) {
  int size = type_size(type), x, y;
  const uint8_t *base = (const uint8_t *)pixels;
  size_t rb;
  if (size <= 0) return 0;
  rb = row_bytes(ps, w, 1, size);
  base += (size_t)ps->skip_rows * rb + (size_t)ps->skip_pixels * size;
  for (y = 0; y < h; y++) {
    const uint8_t *row = base + (size_t)y * rb;
    for (x = 0; x < w; x++) {
      GLfloat d = element(row, x, type, ps->swap_bytes);
      d = sgl_clamp01(d * c->a.depth_scale + c->a.depth_bias);
      /* In double: a float of max can be more than max. */
      out[(size_t)y * w + x] = (uint32_t)((double)d * (double)max);
    }
  }
  return 1;
}

/* glDrawPixels: draws an image at the raster position. */
void APIENTRY sgl_DrawPixels(GLsizei w, GLsizei h, GLenum format, GLenum type, const void *pixels) {
  sgl_context *c = outside();
  size_t n;
  if (!c) return;
  if (w < 0 || h < 0) {
    sgl_error(GL_INVALID_VALUE);
    return;
  }
  if (sgl_format_components(format) == 0 || (type_size(type) < 0 && !packed_of(type)) ||
      (type == GL_BITMAP && format != GL_COLOR_INDEX && format != GL_STENCIL_INDEX)) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  if (packed_of(type) && !packed_format_ok(packed_of(type), format)) {
    sgl_error(GL_INVALID_OPERATION);
    return;
  }
  if (c->index_mode && format != GL_COLOR_INDEX && format != GL_STENCIL_INDEX &&
      format != GL_DEPTH_COMPONENT) {
    sgl_error(GL_INVALID_OPERATION);
    return;
  }
  if (c->render_mode != GL_RENDER) {
    sgl_feedback_raster(c, (GLfloat)GL_DRAW_PIXEL_TOKEN);
    return;
  }
  if (!c->color || !c->a.raster_valid || w == 0 || h == 0) return;
  n = (size_t)w * h;
  if (format == GL_COLOR_INDEX && c->index_mode) {
    int32_t *v = (int32_t *)malloc(n * sizeof(int32_t));
    size_t i;
    if (!v) {
      sgl_error(GL_OUT_OF_MEMORY);
      return;
    }
    unpack_indices(&c->c.unpack, w, h, type, pixels, v);
    for (i = 0; i < n; i++) v[i] = shift_index(c, v[i], c->a.map_color, MAP_I_TO_I);
    draw_rect(c, RECT_INDEX, w, h, NULL, v);
    free(v);
  } else if (format == GL_STENCIL_INDEX) {
    int32_t *v = (int32_t *)malloc(n * sizeof(int32_t));
    size_t i;
    if (!v || !sgl_need_stencil(c)) {
      free(v);
      sgl_error(GL_OUT_OF_MEMORY);
      return;
    }
    unpack_indices(&c->c.unpack, w, h, type, pixels, v);
    for (i = 0; i < n; i++) v[i] = shift_index(c, v[i], c->a.map_stencil, MAP_S_TO_S);
    draw_rect(c, RECT_STENCIL, w, h, NULL, v);
    free(v);
  } else if (format == GL_DEPTH_COMPONENT) {
    int32_t *v = (int32_t *)malloc(n * sizeof(int32_t));
    if (!v) {
      sgl_error(GL_OUT_OF_MEMORY);
      return;
    }
    if (sgl_unpack_depth(c, &c->c.unpack, w, h, type, pixels, (uint32_t *)v, SGL_Z_MAX)) draw_rect(c, RECT_DEPTH, w, h, NULL, v);
    free(v);
  } else {
    uint8_t *px = (uint8_t *)malloc(n * 4);
    if (!px) {
      sgl_error(GL_OUT_OF_MEMORY);
      return;
    }
    if (!sgl_unpack_rgba8(&c->c.unpack, w, h, format, type, pixels, px))
      sgl_error(GL_INVALID_ENUM);
    else
      draw_rect(c, RECT_RGBA, w, h, px, NULL);
    free(px);
  }
}

/* Reads w x h pixels of the color buffer at x, y into 0xAARRGGBB
 * values (or indices in color index mode), rows bottom first. Pixels
 * outside the buffer are 0. */
static void read_rect(sgl_context *c, int x, int y, int w, int h, uint32_t *out) {
  int i, j;
  for (j = 0; j < h; j++)
    for (i = 0; i < w; i++) {
      int px = x + i, py = y + j;
      out[(size_t)j * w + i] = px >= 0 && py >= 0 && px < c->width && py < c->height
                                   ? c->color[(long)(c->height - 1 - py) * c->rowpixels + px]
                                   : 0;
    }
}

/* Reads w x h values of the stencil buffer at x, y, rows bottom first.
 * Values outside the buffer are 0. */
static void read_stencil(sgl_context *c, int x, int y, int w, int h, int32_t *out) {
  int i, j;
  for (j = 0; j < h; j++)
    for (i = 0; i < w; i++) {
      int px = x + i, py = y + j;
      out[(size_t)j * w + i] = c->stencil && px >= 0 && py >= 0 && px < c->width && py < c->height
                                   ? c->stencil[(size_t)(c->height - 1 - py) * c->width + px]
                                   : 0;
    }
}

/* Reads w x h values of the depth buffer at x, y as window z values of
 * the rasterizer, rows bottom first. Values outside the buffer are the
 * far plane. */
void sgl_read_depth(sgl_context *c, int x, int y, int w, int h, int32_t *out) {
  int i, j;
  for (j = 0; j < h; j++)
    for (i = 0; i < w; i++) {
      int px = x + i, py = y + j;
      uint32_t d = SGL_Z_MAX;
      if (c->depth && px >= 0 && py >= 0 && px < c->width && py < c->height)
        d = (uint32_t)c->depth[(size_t)(c->height - 1 - py) * c->width + px] << SGL_DEPTH_SHIFT;
      out[(size_t)j * w + i] = (int32_t)d;
    }
}

/* glReadPixels: reads a rectangle of the color, depth or stencil buffer. */
void APIENTRY sgl_ReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type,
                             void *pixels) {
  sgl_context *c = outside();
  size_t n, i;
  if (!c || !c->color) return;
  if (w < 0 || h < 0) {
    sgl_error(GL_INVALID_VALUE);
    return;
  }
  if (format == GL_COLOR_INDEX && !c->index_mode) {
    sgl_error(GL_INVALID_OPERATION);
    return;
  }
  if (w == 0 || h == 0) return;
  n = (size_t)w * h;
  if (format == GL_DEPTH_COMPONENT) {
    /* Depth as floats or the integer types, without the pack store's
     * skips. */
    int32_t *d = (int32_t *)malloc(n * sizeof(int32_t));
    if (!d) {
      sgl_error(GL_OUT_OF_MEMORY);
      return;
    }
    sgl_read_depth(c, x, y, w, h, d);
    for (i = 0; i < n; i++) {
      GLfloat v = sgl_clamp01((GLfloat)(uint32_t)d[i] / (GLfloat)SGL_Z_MAX * c->a.depth_scale +
                              c->a.depth_bias);
      if (type == GL_FLOAT)
        ((GLfloat *)pixels)[i] = v;
      else if (type == GL_UNSIGNED_SHORT)
        ((GLushort *)pixels)[i] = (GLushort)(v * 65535.0f);
      else if (type == GL_UNSIGNED_INT)
        ((GLuint *)pixels)[i] = (GLuint)(v * 4294967295.0);
      else if (type == GL_UNSIGNED_BYTE)
        ((GLubyte *)pixels)[i] = (GLubyte)(v * 255.0f);
    }
    free(d);
    return;
  }
  if (format == GL_STENCIL_INDEX || format == GL_COLOR_INDEX) {
    int32_t *v = (int32_t *)malloc(n * sizeof(int32_t));
    if (!v) {
      sgl_error(GL_OUT_OF_MEMORY);
      return;
    }
    if (format == GL_STENCIL_INDEX) {
      read_stencil(c, x, y, w, h, v);
      for (i = 0; i < n; i++) v[i] = shift_index(c, v[i], c->a.map_stencil, MAP_S_TO_S);
    } else {
      read_rect(c, x, y, w, h, (uint32_t *)v);
      for (i = 0; i < n; i++)
        v[i] = shift_index(c, (int32_t)((uint32_t)v[i] & c->index_max), c->a.map_color, MAP_I_TO_I);
    }
    pack_indices(c, w, h, v, type, pixels);
    free(v);
    return;
  }
  {
    uint32_t *tmp = (uint32_t *)malloc(n * 4);
    if (!tmp) {
      sgl_error(GL_OUT_OF_MEMORY);
      return;
    }
    read_rect(c, x, y, w, h, tmp);
    pack_rgba(c, w, h, tmp, format, type, pixels);
    free(tmp);
  }
}

/* glCopyPixels: copies a rectangle of the color, depth or stencil
 * buffer to the raster position. */
void APIENTRY sgl_CopyPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum type) {
  sgl_context *c = outside();
  size_t i, n;
  int32_t *v;
  if (!c) return;
  if (type != GL_COLOR && type != GL_DEPTH && type != GL_STENCIL) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  if (w < 0 || h < 0) {
    sgl_error(GL_INVALID_VALUE);
    return;
  }
  if (c->render_mode != GL_RENDER) {
    sgl_feedback_raster(c, (GLfloat)GL_COPY_PIXEL_TOKEN);
    return;
  }
  if (!c->color || w == 0 || h == 0 || !c->a.raster_valid) return;
  n = (size_t)w * h;
  v = (int32_t *)malloc(n * 4 + 1);
  if (!v) {
    sgl_error(GL_OUT_OF_MEMORY);
    return;
  }
  if (type == GL_DEPTH) {
    sgl_read_depth(c, x, y, w, h, v);
    for (i = 0; i < n; i++) {
      GLfloat d = sgl_clamp01((GLfloat)(uint32_t)v[i] / (GLfloat)SGL_Z_MAX * c->a.depth_scale +
                              c->a.depth_bias);
      v[i] = (int32_t)(d * (GLfloat)SGL_Z_MAX);
    }
    draw_rect(c, RECT_DEPTH, w, h, NULL, v);
  } else if (type == GL_STENCIL) {
    if (sgl_need_stencil(c)) {
      read_stencil(c, x, y, w, h, v);
      for (i = 0; i < n; i++) v[i] = shift_index(c, v[i], c->a.map_stencil, MAP_S_TO_S);
      draw_rect(c, RECT_STENCIL, w, h, NULL, v);
    }
  } else if (c->index_mode) {
    read_rect(c, x, y, w, h, (uint32_t *)v);
    for (i = 0; i < n; i++)
      v[i] = shift_index(c, (int32_t)((uint32_t)v[i] & c->index_max), c->a.map_color, MAP_I_TO_I);
    draw_rect(c, RECT_INDEX, w, h, NULL, v);
  } else {
    uint8_t *px = (uint8_t *)malloc(n * 4);
    uint32_t *tmp = (uint32_t *)v;
    if (!px) {
      free(v);
      sgl_error(GL_OUT_OF_MEMORY);
      return;
    }
    read_rect(c, x, y, w, h, tmp);
    for (i = 0; i < n; i++) {
      px[i * 4] = (uint8_t)(tmp[i] >> 16);
      px[i * 4 + 1] = (uint8_t)(tmp[i] >> 8);
      px[i * 4 + 2] = (uint8_t)tmp[i];
      px[i * 4 + 3] = (uint8_t)(tmp[i] >> 24);
    }
    if (transfer_on(c) || c->a.map_color) {
      for (i = 0; i < n; i++) {
        GLfloat f[4];
        int k;
        const sgl_attrib *a = &c->a;
        for (k = 0; k < 4; k++) f[k] = px[i * 4 + k] / 255.0f;
        f[0] = f[0] * a->red_scale + a->red_bias;
        f[1] = f[1] * a->green_scale + a->green_bias;
        f[2] = f[2] * a->blue_scale + a->blue_bias;
        f[3] = f[3] * a->alpha_scale + a->alpha_bias;
        if (a->map_color) map_rgba(c, f);
        for (k = 0; k < 4; k++) px[i * 4 + k] = to_byte(f[k]);
      }
    }
    draw_rect(c, RECT_RGBA, w, h, px, NULL);
    free(px);
  }
  free(v);
}

/* glBitmap: draws a bitmap in the raster color at the raster position,
 * then moves the raster position. */
void APIENTRY sgl_Bitmap(GLsizei w, GLsizei h, GLfloat xorig, GLfloat yorig, GLfloat xmove,
                         GLfloat ymove, const GLubyte *bitmap) {
  sgl_context *c = outside();
  if (!c) return;
  if (w < 0 || h < 0) {
    sgl_error(GL_INVALID_VALUE);
    return;
  }
  if (!c->a.raster_valid) return;
  if (c->render_mode != GL_RENDER) {
    sgl_feedback_raster(c, (GLfloat)GL_BITMAP_TOKEN);
  } else if (bitmap && w > 0 && h > 0 && c->color) {
    uint8_t *bits = (uint8_t *)malloc((size_t)w * h);
    int x0 = (int)floorf(c->a.raster_pos[0] - xorig), y0 = (int)floorf(c->a.raster_pos[1] - yorig);
    uint32_t z = raster_z(c);
    GLfloat index[4];
    const GLfloat *color = c->a.raster_color;
    int x, y;
    if (!bits) {
      sgl_error(GL_OUT_OF_MEMORY);
      return;
    }
    if (c->index_mode) {
      index[0] = c->a.raster_index;
      index[1] = index[2] = index[3] = 0.0f;
      color = index;
    }
    sgl_unpack_index(&c->c.unpack, w, h, GL_BITMAP, bitmap, bits);
    for (y = 0; y < h; y++)
      for (x = 0; x < w; x++)
        if (bits[(size_t)y * w + x]) sgl_write_fragment(c, x0 + x, y0 + y, z, color);
    free(bits);
  }
  c->a.raster_pos[0] += xmove;
  c->a.raster_pos[1] += ymove;
}

/* glPolygonStipple: keeps the stipple pattern of the polygons. */
void APIENTRY sgl_PolygonStipple(const GLubyte *mask) {
  sgl_context *c = outside();
  uint8_t bits[32 * 32];
  int x, y;
  if (!c) return;
  sgl_unpack_index(&c->c.unpack, 32, 32, GL_BITMAP, mask, bits);
  for (y = 0; y < 32; y++) {
    uint32_t row = 0;
    for (x = 0; x < 32; x++)
      if (bits[y * 32 + x]) row |= 1u << x;
    c->a.stipple[y] = row;
  }
}

/* glGetPolygonStipple: gives the stipple pattern, packed as bits. */
void APIENTRY sgl_GetPolygonStipple(GLubyte *mask) {
  sgl_context *c = sgl_ctx;
  int x, y;
  memset(mask, 0, 128);
  for (y = 0; y < 32; y++)
    for (x = 0; x < 32; x++)
      if (c->a.stipple[y] & (1u << x)) mask[y * 4 + x / 8] |= (GLubyte)(0x80 >> (x & 7));
}
