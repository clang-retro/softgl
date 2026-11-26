/* softgl.h - the driver interface of the software OpenGL (softgl).
 *
 * softgl is a generic implementation of OpenGL 1.1, as GDI Generic is on
 * Windows. It has the gl functions of OpenGL 1.1 and the extensions of
 * the Windows generic implementation (GL_WIN_swap_hint, GL_EXT_bgra,
 * GL_EXT_paletted_texture and GL_EXT_vertex_array). It does not know a
 * window system: the wgl layer of the program (port/src/opengl.cpp)
 * makes the contexts, gives each one a buffer in memory and copies the
 * buffer to the window.
 *
 * The functions here are for that layer. A program calls the gl
 * functions and the wgl functions only.
 *
 * Part of clang-retro softgl, a software implementation of OpenGL.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#ifndef SOFTGL_H
#define SOFTGL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A rendering context. */
typedef struct sgl_context sgl_context;

/* Makes a rendering context. It shares its display lists and texture
 * objects with share, which can be NULL. Gives NULL when there is not
 * sufficient memory. */
sgl_context *sgl_create_context(sgl_context *share);

/* Makes a rendering context in color index mode, as for a pixel format
 * of type PFD_TYPE_COLORINDEX. index_bits (1 to 16) is the number of
 * bits of an index. Each pixel of the color buffer then holds an index
 * in its low index_bits bits, and the other bits are 0. The window layer
 * changes the indices to colors through its palette when it copies the
 * buffer to the window. Gives NULL when index_bits is not correct or
 * when there is not sufficient memory. */
sgl_context *sgl_create_index_context(sgl_context *share, int index_bits);

/* Destroys a rendering context. If it is current, no context is current
 * after the call. */
void sgl_destroy_context(sgl_context *ctx);

/* Makes ctx the current context of the thread, or no context when ctx
 * is NULL. Gives 1 on success and 0 on failure. */
int sgl_make_current(sgl_context *ctx);

/* Gives the current context of the thread, or NULL. */
sgl_context *sgl_get_current(void);

/* Points ctx at a color buffer of width x height pixels. Each pixel is a
 * 32-bit value 0xAARRGGBB in the byte order of the machine. The top row
 * is first. rowpixels is the number of pixels from one row to the next.
 * The context makes its depth, stencil and accumulation buffers in the
 * same size. The width, the height and rowpixels must not be more than
 * 32767, and rowpixels must not be less than the width. Gives 1 on
 * success, and 0 when a size is not correct or when there is not
 * sufficient memory. */
int sgl_set_buffer(sgl_context *ctx, int width, int height, void *pixels, long rowpixels);

/* Copies the state groups in mask (the GL_..._BIT values of
 * glPushAttrib) from one context to another. Gives 1 on success. */
int sgl_copy_context(sgl_context *from, sgl_context *to, unsigned long mask);

/* Lets ctx use the display lists and texture objects of from. ctx must
 * not have lists or textures of its own yet. Gives 1 on success. */
int sgl_share_lists(sgl_context *from, sgl_context *ctx);

/* Gives the address of an extension function by its name, as
 * wglGetProcAddress does, or NULL. */
void *sgl_get_proc(const char *name);

/* Back ends.
 *
 * A back end lets a driver use hardware for some of the work of a
 * context. softgl always does the GL state, the transform, lighting,
 * clipping, culling, selection and feedback. A back end can then clear
 * the buffers, fill flat spans, draw whole primitives, and show the
 * frame. Each entry can be NULL: softgl then does that work in software.
 * docs/backends.md tells more. */

/* A vertex in window coordinates, as softgl gives it to a back end. */
typedef struct sgl_hw_vertex {
  float x, y, z;     /* x and y in pixels from the bottom-left corner, z 0 to 1 */
  float w;           /* 1 / w of the clip coordinates */
  float color[4];    /* the color (of the correct side, or the flat color), 0 to 1 */
  float spec[3];     /* the secondary color, 0 to 1 */
  float fog;         /* the fog factor: 1 is no fog */
  float tex[2][4];   /* s, t, r and q of the texture units 0 and 1 */
} sgl_hw_vertex;

/* A rectangle of glClear for a back end. Row 0 is the top row of each
 * buffer. The pointers give the first pixel of the rectangle. */
typedef struct sgl_clear_rect {
  unsigned mask;          /* the GL_..._BUFFER_BIT values to clear */
  int width, height;      /* the size of the rectangle in pixels */
  uint32_t *color;        /* the color buffer */
  long color_pitch;       /* pixels from one row of the color buffer to the next */
  uint32_t color_value;   /* the 32-bit value of each color pixel */
  void *depth;            /* the depth buffer */
  int depth_bytes;        /* the size of a depth value: 2 or 4 */
  long depth_pitch;       /* values from one row of the depth buffer to the next */
  uint32_t depth_value;   /* the value of each depth value */
  uint8_t *stencil;       /* the stencil buffer */
  long stencil_pitch;     /* bytes from one row of the stencil buffer to the next */
  uint8_t stencil_value;  /* the value of each stencil byte */
} sgl_clear_rect;

/* The entries of a back end. user is the value of sgl_set_backend. */
typedef struct sgl_backend {
  /* Allocates and frees the depth and stencil buffers, for example in
   * memory that a blitter can get to. alloc gives NULL on failure. */
  void *(*alloc)(void *user, size_t bytes);
  void (*release)(void *user, void *p);
  /* Clears a rectangle. Gives the GL_..._BUFFER_BIT values that it
   * cleared (or started to clear); softgl clears the other buffers. */
  unsigned (*clear)(void *user, const sgl_clear_rect *r);
  /* Sets count pixels from dst to value: one row of a flat triangle.
   * softgl calls it only when no per-pixel test or operation is on.
   * It can start the work and return before the work is done. */
  void (*fill_span)(void *user, uint32_t *dst, int count, uint32_t value);
  /* Waits until all the work of clear and fill_span is done. softgl
   * calls it before it reads or writes the buffers itself. */
  void (*sync)(void *user);
  /* Draws a primitive in hardware. Gives 1 when it drew the primitive,
   * and 0 when softgl must draw it. The back end reads the GL state of
   * c to know if it can. */
  int (*triangle)(void *user, sgl_context *c, const sgl_hw_vertex *a, const sgl_hw_vertex *b,
                  const sgl_hw_vertex *d);
  int (*line)(void *user, sgl_context *c, const sgl_hw_vertex *a, const sgl_hw_vertex *b);
  int (*point)(void *user, sgl_context *c, const sgl_hw_vertex *a);
  /* softgl will draw in software after triangle, line or point drew in
   * hardware. The back end makes the buffers of the context agree with
   * the hardware. */
  void (*software)(void *user, sgl_context *c);
  /* A texture object (the value of sgl_texture_id) is deleted. */
  void (*texture_deleted)(void *user, unsigned long id);
  /* glFlush (finish 0) or glFinish (finish 1). */
  void (*flush)(void *user, int finish);
  /* Shows the frame (sgl_present). */
  void (*present)(void *user, sgl_context *c, const uint32_t *pixels, int width, int height,
                  long rowpixels);
  /* The back end is no longer used by c: the context is destroyed or
   * has a different back end. */
  void (*detach)(void *user, sgl_context *c);
} sgl_backend;

/* Gives ctx the back end be (which must stay valid while ctx uses it),
 * or no back end when be is NULL. Set it before sgl_set_buffer, so that
 * alloc makes the depth buffer. Gives 1 on success. */
int sgl_set_backend(sgl_context *ctx, const sgl_backend *be, void *user);

/* Finishes the drawing of ctx and shows the frame through the present
 * entry of its back end. Without a present entry, it only finishes the
 * drawing; the window layer then shows the color buffer itself. */
void sgl_present(sgl_context *ctx);

/* Gives the id of the texture object bound to the 2D target of unit u
 * of ctx, and its serial: the serial changes each time the image or a
 * parameter of the texture changes. A back end keeps its copy of a
 * texture by id and makes it again when the serial changes. Gives 0
 * when ctx has no 2D texture with an image on that unit. */
unsigned long sgl_texture_id(sgl_context *ctx, int u, unsigned long *serial);

#ifdef __cplusplus
}
#endif

#endif
