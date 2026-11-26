/* warp3d_api.h - the parts of the Warp3D interface (warp3d.library,
 * versions 1 to 4) that the Warp3D back end of softgl uses: the types,
 * the values and the calls. The calls go through backends/amiga/
 * warp3d_stubs.s and have the prefix sgl_. This file is not the Warp3D
 * SDK; it gives only the interface facts that the back end needs.
 *
 * All structures use the 68k alignment of 2 bytes.
 *
 * Part of clang-retro softgl, a software implementation of OpenGL.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#ifndef SGL_WARP3D_API_H
#define SGL_WARP3D_API_H

#include <exec/lists.h>
#include <exec/types.h>
#include <utility/tagitem.h>

struct BitMap;

typedef short W3D_Bool;
typedef float W3D_Float;
typedef double W3D_Double;

typedef struct {
  W3D_Float r, g, b, a;
} W3D_Color;

typedef struct {
  W3D_Float r, g, b;
} W3D_ColorRGB;

/* A vertex: window coordinates (y down), z 0 to 1, 1 / w, the texture
 * coordinates in texels, the color and the specular color. */
typedef struct {
  W3D_Float x, y;
  W3D_Double z;
  W3D_Float w;
  W3D_Float u, v, tex3d;
  W3D_Color color;
  W3D_ColorRGB spec;
  W3D_Float l;
} W3D_Vertex;

/* A texture object. The back end does not read its fields. */
typedef struct W3D_Texture W3D_Texture;

typedef struct {
  W3D_Vertex v1;
  W3D_Texture *tex;
  W3D_Float pointsize;
} W3D_Point;

typedef struct {
  W3D_Vertex v1, v2;
  W3D_Texture *tex;
  W3D_Float linewidth;
  W3D_Bool st_enable;
  unsigned short st_pattern;
  int st_factor;
} W3D_Line;

typedef struct {
  W3D_Vertex v1, v2, v3;
  W3D_Texture *tex;
  unsigned char *st_pattern;
} W3D_Triangle;

typedef struct {
  int left, top, width, height;
} W3D_Scissor;

typedef struct {
  W3D_Float fog_start, fog_end, fog_density;
  W3D_ColorRGB fog_color;
} W3D_Fog;

/* The first fields of the context. Warp3D lets programs read format,
 * bprow, width, height, depth and drawmem; the fields before them give
 * their offsets. */
typedef struct {
  void *driver, *gfxdriver;
  int drivertype;
  void *regbase, *vmembase, *zbuffer, *stencilbuffer;
  ULONG state;
  struct BitMap *drawregion;
  ULONG supportedfmt, format;
  int yoffset, bprow, width, height, depth;
  W3D_Bool chunky, destalpha, zbufferalloc, stbufferalloc, HWlocked, w3dbitmap, zbufferlost, reserved3;
  struct MinList restex, tex;
  int maxtexwidth, maxtexheight, maxtexwidthp, maxtexheightp;
  W3D_Scissor scissor;
  W3D_Fog fog;
  ULONG envsupmask;
  void *queue;
  void *drawmem;
} W3D_Context;

/* The tags of W3D_CreateContext. */
#define W3D_CC_TAGS (TAG_USER + 0x200000)
#define W3D_CC_BITMAP (W3D_CC_TAGS + 0)
#define W3D_CC_YOFFSET (W3D_CC_TAGS + 1)
#define W3D_CC_DRIVERTYPE (W3D_CC_TAGS + 2)

/* The driver types (W3D_CheckDriver, W3D_CC_DRIVERTYPE). */
#define W3D_DRIVER_UNAVAILABLE (1 << 0)
#define W3D_DRIVER_BEST (1 << 1)
#define W3D_DRIVER_3DHW (1 << 2)
#define W3D_DRIVER_CPU (1 << 3)

/* The tags of W3D_AllocTexObj. */
#define W3D_ATO_TAGS (TAG_USER + 0x201000)
#define W3D_ATO_IMAGE (W3D_ATO_TAGS + 0)
#define W3D_ATO_FORMAT (W3D_ATO_TAGS + 1)
#define W3D_ATO_WIDTH (W3D_ATO_TAGS + 2)
#define W3D_ATO_HEIGHT (W3D_ATO_TAGS + 3)
#define W3D_ATO_MIPMAP (W3D_ATO_TAGS + 4)

/* The texture format of the back end. */
#define W3D_A8R8G8B8 6

/* The formats of the drawing region (W3D_Context.format). */
#define W3D_FMT_R5G5B5 (1 << 1)
#define W3D_FMT_R5G5B5PC (1 << 3)
#define W3D_FMT_R5G6B5 (1 << 5)
#define W3D_FMT_R5G6B5PC (1 << 7)
#define W3D_FMT_A8R8G8B8 (1 << 11)
#define W3D_FMT_B8G8R8A8 (1 << 14)

/* The states. */
#define W3D_TEXMAPPING (1 << 8)
#define W3D_PERSPECTIVE (1 << 9)
#define W3D_GOURAUD (1 << 10)
#define W3D_ZBUFFER (1 << 11)
#define W3D_ZBUFFERUPDATE (1 << 12)
#define W3D_BLENDING (1 << 13)
#define W3D_DITHERING (1 << 19)
#define W3D_ALPHATEST (1 << 22)
#define W3D_SPECULAR (1 << 23)
#define W3D_SCISSOR (1 << 25)
#define W3D_ENABLE 1
#define W3D_DISABLE 2

/* The queries. */
#define W3D_Q_DRAW_POINT 1
#define W3D_Q_DRAW_LINE 2
#define W3D_Q_DRAW_TRIANGLE 3
#define W3D_Q_TEXMAPPING 11
#define W3D_Q_PERSPECTIVE 17
#define W3D_Q_SPECULAR 25
#define W3D_Q_GOURAUDSHADING 32
#define W3D_Q_ZBUFFER 41
#define W3D_Q_ALPHATEST 51
#define W3D_Q_BLENDING 61
#define W3D_Q_SCISSOR 101
#define W3D_Q_MAXTEXWIDTH 111
#define W3D_Q_MAXTEXHEIGHT 112
#define W3D_FULLY_SUPPORTED 3
#define W3D_PARTIALLY_SUPPORTED 4
#define W3D_NOT_SUPPORTED 5

/* The blend factors. */
#define W3D_ZERO 1
#define W3D_ONE 2
#define W3D_SRC_COLOR 3
#define W3D_DST_COLOR 4
#define W3D_ONE_MINUS_SRC_COLOR 5
#define W3D_ONE_MINUS_DST_COLOR 6
#define W3D_SRC_ALPHA 7
#define W3D_ONE_MINUS_SRC_ALPHA 8
#define W3D_DST_ALPHA 9
#define W3D_ONE_MINUS_DST_ALPHA 10
#define W3D_SRC_ALPHA_SATURATE 11

/* The filters. */
#define W3D_NEAREST 1
#define W3D_LINEAR 2
#define W3D_NEAREST_MIP_NEAREST 3
#define W3D_NEAREST_MIP_LINEAR 4
#define W3D_LINEAR_MIP_NEAREST 5
#define W3D_LINEAR_MIP_LINEAR 6

/* The texture environments. */
#define W3D_REPLACE 1
#define W3D_DECAL 2
#define W3D_MODULATE 3
#define W3D_BLEND 4

/* The wrap modes. */
#define W3D_REPEAT 1
#define W3D_CLAMP 2

/* The alpha and Z compare modes (the same values). */
#define W3D_A_NEVER 1
#define W3D_A_LESS 2
#define W3D_A_GEQUAL 3
#define W3D_A_LEQUAL 4
#define W3D_A_GREATER 5
#define W3D_A_NOTEQUAL 6
#define W3D_A_EQUAL 7
#define W3D_A_ALWAYS 8
#define W3D_Z_NEVER 1
#define W3D_Z_ALWAYS 8

/* The results. */
#define W3D_SUCCESS 0

/* The calls (backends/amiga/warp3d_stubs.s). */
extern struct Library *sgl_Warp3DBase;
W3D_Context *sgl_W3D_CreateContext(ULONG *error, struct TagItem *tags);
void sgl_W3D_DestroyContext(W3D_Context *c);
ULONG sgl_W3D_SetState(W3D_Context *c, ULONG state, ULONG action);
ULONG sgl_W3D_CheckDriver(void);
ULONG sgl_W3D_LockHardware(W3D_Context *c);
void sgl_W3D_UnLockHardware(W3D_Context *c);
void sgl_W3D_WaitIdle(W3D_Context *c);
ULONG sgl_W3D_Query(W3D_Context *c, ULONG query, ULONG destfmt);
W3D_Texture *sgl_W3D_AllocTexObj(W3D_Context *c, ULONG *error, struct TagItem *tags);
void sgl_W3D_FreeTexObj(W3D_Context *c, W3D_Texture *t);
ULONG sgl_W3D_SetFilter(W3D_Context *c, W3D_Texture *t, ULONG min, ULONG mag);
ULONG sgl_W3D_SetTexEnv(W3D_Context *c, W3D_Texture *t, ULONG env, W3D_Color *color);
ULONG sgl_W3D_SetWrapMode(W3D_Context *c, W3D_Texture *t, ULONG s, ULONG tt, W3D_Color *border);
ULONG sgl_W3D_DrawLine(W3D_Context *c, W3D_Line *l);
ULONG sgl_W3D_DrawPoint(W3D_Context *c, W3D_Point *p);
ULONG sgl_W3D_DrawTriangle(W3D_Context *c, W3D_Triangle *t);
ULONG sgl_W3D_SetAlphaMode(W3D_Context *c, ULONG mode, W3D_Float *ref);
ULONG sgl_W3D_SetBlendMode(W3D_Context *c, ULONG src, ULONG dst);
ULONG sgl_W3D_SetDrawRegion(W3D_Context *c, struct BitMap *bm, int yoffset, W3D_Scissor *s);
ULONG sgl_W3D_AllocZBuffer(W3D_Context *c);
ULONG sgl_W3D_FreeZBuffer(W3D_Context *c);
ULONG sgl_W3D_ClearZBuffer(W3D_Context *c, W3D_Double *value);
void sgl_W3D_ReadZSpan(W3D_Context *c, ULONG x, ULONG y, ULONG n, W3D_Double *z);
void sgl_W3D_SetZCompareMode(W3D_Context *c, ULONG mode);
void sgl_W3D_WriteZSpan(W3D_Context *c, ULONG x, ULONG y, ULONG n, W3D_Double *z, UBYTE *mask);
void sgl_W3D_SetScissor(W3D_Context *c, W3D_Scissor *s);
ULONG sgl_W3D_FlushFrame(W3D_Context *c);
ULONG sgl_W3D_ClearDrawRegion(W3D_Context *c, ULONG color);

#endif
