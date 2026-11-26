/* softgl_glide.h - the 3dfx Glide back end of softgl. A Voodoo card
 * (Voodoo Graphics, Voodoo Rush, Voodoo2, Banshee, Voodoo3 and later)
 * draws the triangles, lines and points through the Glide 2 interface.
 * softgl keeps the GL state, the transform, lighting and clipping, and
 * draws in software what the card cannot do. See docs/glide.md.
 *
 * The back end calls Glide through a table of functions:
 * - On Windows 95 and 98, sgl_glide_load fills the table from
 *   glide2x.dll.
 * - On a 32-bit DOS extender, the program links the Glide 2 library of
 *   the 3dfx SDK and fills the table with SGL_GLIDE_FILL.
 *
 * Part of clang-retro softgl, a software implementation of OpenGL.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#ifndef SOFTGL_GLIDE_H
#define SOFTGL_GLIDE_H

#include "softgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The calling convention of Glide: __stdcall on Windows, the C
 * convention on DOS. */
#if defined(_WIN32) && !defined(SGL_GLIDE_CALL)
#define SGL_GLIDE_CALL __stdcall
#elif !defined(SGL_GLIDE_CALL)
#define SGL_GLIDE_CALL
#endif

/* The texture coordinates of one texture unit (TMU) in a Glide vertex:
 * s / w, t / w and q / w. */
typedef struct sgl_gr_tmu_vertex {
  float sow, tow, oow;
} sgl_gr_tmu_vertex;

/* A Glide 2 vertex (GrVertex): the window coordinates, the color
 * (0 to 255), the depth (0 to 65535), 1 / w and three TMUs. */
typedef struct sgl_gr_vertex {
  float x, y, z;
  float r, g, b;
  float ooz;
  float a;
  float oow;
  sgl_gr_tmu_vertex tmu[3];
} sgl_gr_vertex;

/* A Glide 2 texture (GrTexInfo): the smallest and largest level, the
 * aspect ratio, the format and the texels of all the levels. */
typedef struct sgl_gr_texinfo {
  int small_lod, large_lod, aspect, format;
  void *data;
} sgl_gr_texinfo;

/* The result of grLfbLock (GrLfbInfo_t). Set size before the call. */
typedef struct sgl_gr_lfbinfo {
  int size;
  void *ptr;
  unsigned long stride;
  int write_mode, origin;
} sgl_gr_lfbinfo;

/* The Glide 2 functions that the back end calls. Each entry has the
 * name of the Glide function without the prefix "gr". */
typedef struct sgl_glide_api {
  void(SGL_GLIDE_CALL *GlideInit)(void);
  void(SGL_GLIDE_CALL *GlideShutdown)(void);
  int(SGL_GLIDE_CALL *SstQueryHardware)(void *config);
  void(SGL_GLIDE_CALL *SstSelect)(int which);
  int(SGL_GLIDE_CALL *SstWinOpen)(unsigned long hwnd, int resolution, int refresh, int color_format,
                                  int origin, int color_buffers, int aux_buffers);
  void(SGL_GLIDE_CALL *SstWinClose)(void);
  void(SGL_GLIDE_CALL *SstIdle)(void);
  void(SGL_GLIDE_CALL *BufferClear)(unsigned long color, unsigned alpha, unsigned depth);
  void(SGL_GLIDE_CALL *BufferSwap)(int interval);
  void(SGL_GLIDE_CALL *DrawTriangle)(const sgl_gr_vertex *a, const sgl_gr_vertex *b, const sgl_gr_vertex *c);
  void(SGL_GLIDE_CALL *DrawLine)(const sgl_gr_vertex *a, const sgl_gr_vertex *b);
  void(SGL_GLIDE_CALL *DrawPoint)(const sgl_gr_vertex *a);
  void(SGL_GLIDE_CALL *DepthBufferMode)(int mode);
  void(SGL_GLIDE_CALL *DepthBufferFunction)(int func);
  void(SGL_GLIDE_CALL *DepthMask)(int on);
  void(SGL_GLIDE_CALL *ColorMask)(int rgb, int alpha);
  void(SGL_GLIDE_CALL *ColorCombine)(int func, int factor, int local, int other, int invert);
  void(SGL_GLIDE_CALL *AlphaCombine)(int func, int factor, int local, int other, int invert);
  void(SGL_GLIDE_CALL *TexCombine)(int tmu, int rgb_func, int rgb_factor, int alpha_func, int alpha_factor,
                                   int rgb_invert, int alpha_invert);
  void(SGL_GLIDE_CALL *AlphaBlendFunction)(int rgb_src, int rgb_dst, int alpha_src, int alpha_dst);
  void(SGL_GLIDE_CALL *AlphaTestFunction)(int func);
  void(SGL_GLIDE_CALL *AlphaTestReferenceValue)(unsigned value);
  void(SGL_GLIDE_CALL *FogMode)(int mode);
  void(SGL_GLIDE_CALL *FogColorValue)(unsigned long color);
  void(SGL_GLIDE_CALL *FogTable)(const unsigned char *table);
  void(SGL_GLIDE_CALL *ClipWindow)(unsigned long minx, unsigned long miny, unsigned long maxx,
                                   unsigned long maxy);
  void(SGL_GLIDE_CALL *CullMode)(int mode);
  void(SGL_GLIDE_CALL *DitherMode)(int mode);
  unsigned long(SGL_GLIDE_CALL *TexMinAddress)(int tmu);
  unsigned long(SGL_GLIDE_CALL *TexMaxAddress)(int tmu);
  unsigned long(SGL_GLIDE_CALL *TexTextureMemRequired)(unsigned long even_odd, sgl_gr_texinfo *info);
  void(SGL_GLIDE_CALL *TexDownloadMipMap)(int tmu, unsigned long start, unsigned long even_odd,
                                          sgl_gr_texinfo *info);
  void(SGL_GLIDE_CALL *TexSource)(int tmu, unsigned long start, unsigned long even_odd, sgl_gr_texinfo *info);
  void(SGL_GLIDE_CALL *TexFilterMode)(int tmu, int min, int mag);
  void(SGL_GLIDE_CALL *TexClampMode)(int tmu, int s, int t);
  void(SGL_GLIDE_CALL *TexMipMapMode)(int tmu, int mode, int lod_blend);
  int(SGL_GLIDE_CALL *LfbLock)(int type, int buffer, int write_mode, int origin, int pixel_pipeline,
                               sgl_gr_lfbinfo *info);
  int(SGL_GLIDE_CALL *LfbUnlock)(int type, int buffer);
  void *library;           /* for sgl_glide_unload: the DLL, or NULL */
  unsigned limits;         /* SGL_GLIDE_LIMIT_ flags; 0 for a Voodoo */
} sgl_glide_api;

/* The flags of sgl_glide_api.limits. A driver that is not a Voodoo (for
 * example the S3 ViRGE driver) sets them for the Glide functions that it
 * cannot do. The back end then lets softgl draw these primitives. */
#define SGL_GLIDE_LIMIT_ALPHA_TEST 0x01u  /* no alpha test */
#define SGL_GLIDE_LIMIT_BLEND 0x02u       /* only the blend (src alpha, 1 - src alpha) */
#define SGL_GLIDE_LIMIT_COLOR_MASK 0x04u  /* the color writes cannot be stopped */
#define SGL_GLIDE_LIMIT_FOG_ALPHA 0x08u   /* the fog uses the vertex alpha: no fog with a blend */
#define SGL_GLIDE_LIMIT_SQUARE 0x10u      /* only square textures */
#define SGL_GLIDE_LIMIT_ONE_ALPHA 0x20u   /* the alpha is the texture alpha or the vertex alpha, not a product */
#define SGL_GLIDE_LIMIT_NO_BLEND 0x40u    /* no blend */
#define SGL_GLIDE_LIMIT_NO_FOG 0x80u      /* no fog */
#define SGL_GLIDE_LIMIT_NO_CLAMP 0x100u   /* the textures wrap: no clamp */
#define SGL_GLIDE_LIMIT_ONE_HUE 0x200u    /* with a texture or a blend, one vertex color times an intensity */
#define SGL_GLIDE_LIMIT_DECAL 0x400u      /* no GL_DECAL of an RGBA texture */

/* Fills the table api from the Glide 2 functions that the program links
 * (the 3dfx SDK: glide.h and the Glide 2 library). Use it where
 * sgl_glide_load is not available, for example on a DOS extender. */
#define SGL_GLIDE_FILL(api)                                                    \
  do {                                                                         \
    *(void **)&(api)->GlideInit = (void *)grGlideInit;                         \
    *(void **)&(api)->GlideShutdown = (void *)grGlideShutdown;                 \
    *(void **)&(api)->SstQueryHardware = (void *)grSstQueryHardware;           \
    *(void **)&(api)->SstSelect = (void *)grSstSelect;                         \
    *(void **)&(api)->SstWinOpen = (void *)grSstWinOpen;                       \
    *(void **)&(api)->SstWinClose = (void *)grSstWinClose;                     \
    *(void **)&(api)->SstIdle = (void *)grSstIdle;                             \
    *(void **)&(api)->BufferClear = (void *)grBufferClear;                     \
    *(void **)&(api)->BufferSwap = (void *)grBufferSwap;                       \
    *(void **)&(api)->DrawTriangle = (void *)grDrawTriangle;                   \
    *(void **)&(api)->DrawLine = (void *)grDrawLine;                           \
    *(void **)&(api)->DrawPoint = (void *)grDrawPoint;                         \
    *(void **)&(api)->DepthBufferMode = (void *)grDepthBufferMode;             \
    *(void **)&(api)->DepthBufferFunction = (void *)grDepthBufferFunction;     \
    *(void **)&(api)->DepthMask = (void *)grDepthMask;                         \
    *(void **)&(api)->ColorMask = (void *)grColorMask;                         \
    *(void **)&(api)->ColorCombine = (void *)grColorCombine;                   \
    *(void **)&(api)->AlphaCombine = (void *)grAlphaCombine;                   \
    *(void **)&(api)->TexCombine = (void *)grTexCombine;                       \
    *(void **)&(api)->AlphaBlendFunction = (void *)grAlphaBlendFunction;       \
    *(void **)&(api)->AlphaTestFunction = (void *)grAlphaTestFunction;         \
    *(void **)&(api)->AlphaTestReferenceValue = (void *)grAlphaTestReferenceValue; \
    *(void **)&(api)->FogMode = (void *)grFogMode;                             \
    *(void **)&(api)->FogColorValue = (void *)grFogColorValue;                 \
    *(void **)&(api)->FogTable = (void *)grFogTable;                           \
    *(void **)&(api)->ClipWindow = (void *)grClipWindow;                       \
    *(void **)&(api)->CullMode = (void *)grCullMode;                           \
    *(void **)&(api)->DitherMode = (void *)grDitherMode;                       \
    *(void **)&(api)->TexMinAddress = (void *)grTexMinAddress;                 \
    *(void **)&(api)->TexMaxAddress = (void *)grTexMaxAddress;                 \
    *(void **)&(api)->TexTextureMemRequired = (void *)grTexTextureMemRequired; \
    *(void **)&(api)->TexDownloadMipMap = (void *)grTexDownloadMipMap;         \
    *(void **)&(api)->TexSource = (void *)grTexSource;                         \
    *(void **)&(api)->TexFilterMode = (void *)grTexFilterMode;                 \
    *(void **)&(api)->TexClampMode = (void *)grTexClampMode;                   \
    *(void **)&(api)->TexMipMapMode = (void *)grTexMipMapMode;                 \
    *(void **)&(api)->LfbLock = (void *)grLfbLock;                             \
    *(void **)&(api)->LfbUnlock = (void *)grLfbUnlock;                         \
    (api)->library = 0;                                                        \
    (api)->limits = 0;                                                         \
  } while (0)

/* Fills api from glide2x.dll (Windows only). Gives 1 on success, and 0
 * when the DLL or one of its functions is not there. */
int sgl_glide_load(sgl_glide_api *api);

/* Frees the DLL of sgl_glide_load. */
void sgl_glide_unload(sgl_glide_api *api);

/* Fills api with the built-in driver for the 3dfx Voodoo Graphics (SST-1),
 * Voodoo2, Banshee and Voodoo3 on 32-bit DOS (clang-retro i386-pc-msdos). The driver writes the
 * registers of the card directly, and does not need a Glide library.
 * Gives 1 on success. The code is in backends/glide/glide_sst1.c. */
int sgl_glide_sst1(sgl_glide_api *api);

/* Fills api with the built-in driver for the S3 ViRGE cards on 32-bit
 * DOS. The driver uses the 3D engine of the card and the VBE BIOS, and
 * sets the limits of api. Gives 1 on success. The code is in
 * backends/glide/glide_virge.c. */
int sgl_glide_virge(sgl_glide_api *api);

/* Fills api with the built-in driver for a Matrox Mystique, Mystique
 * 220, Millennium II or Productiva G100 card, for 32-bit DOS. It also
 * accepts the G200, G400, G450 and G550 (not tested). The driver
 * uses the drawing engine of the card and the VBE BIOS, and sets the
 * limits of api. Gives 1 on success. The code is in
 * backends/glide/glide_mga.c. */
int sgl_glide_mga(sgl_glide_api *api);

/* Fills api with the built-in driver for an ATI 3D Rage, Rage II, Rage
 * Pro, Rage LT Pro, Rage XL or Rage Mobility card, for 32-bit DOS. The
 * driver uses the 3D engine of the card and the VBE BIOS, and sets the
 * limits of api for the card that it finds. Gives 1 on success. The code is in
 * backends/glide/glide_ati.c. */
int sgl_glide_ati(sgl_glide_api *api);

/* Fills api with the built-in driver for an ATI Rage 128, Rage 128
 * Pro, Rage 128 Ultra or Rage Mobility M3 or M4 card, for 32-bit DOS.
 * The driver uses the command engine (the CCE) of the card and the VBE
 * BIOS. On a real card it loads the CCE microcode from the file
 * R128CCE.BIN. Gives 1 on success. The code is in
 * backends/glide/glide_r128.c. */
int sgl_glide_r128(sgl_glide_api *api);

/* Fills api with the built-in driver for the graphics of a SiS 530 or
 * SiS 620 chipset (SiS 6306), for 32-bit DOS. The driver uses the 3D
 * engine and the VBE BIOS, and sets the limits of api. Gives 1 on
 * success. The code is in backends/glide/glide_sis.c. */
int sgl_glide_sis(sgl_glide_api *api);

/* Fills api with the built-in driver for a PowerVR PCX2 card (Matrox
 * m3D, VideoLogic Apocalypse 3Dx or 5D), for 32-bit DOS. The card draws
 * into the frame buffer of the VGA card, in a VBE mode. The driver sets
 * the limits of api. Gives 1 on success. The code is in
 * backends/glide/glide_pvr.c. */
int sgl_glide_pvr(sgl_glide_api *api);

/* Fills api with the built-in driver of the first 3D card that is in
 * the computer (32-bit DOS). With name ("voodoo", "powervr", "virge",
 * "matrox", "ati", "rage128" or "sis"), only that driver is tried. Gives the name of the driver, or NULL when there is
 * no card. The code is in backends/glide/glide_dos.c. */
const char *sgl_glide_dos(sgl_glide_api *api, const char *name);

/* A flag of sgl_glide: grBufferSwap does not wait for the vertical
 * retrace. */
#define SGL_GLIDE_NO_VSYNC 1u

/* The Glide back end. Fill api, width, height and flags, then call
 * sgl_glide_attach. */
typedef struct sgl_glide {
  const sgl_glide_api *api;
  int width, height;       /* the screen: 640 x 480, 800 x 600, ... */
  int refresh;             /* the Glide refresh value (0 is 60 Hz) */
  unsigned long hwnd;      /* the window (Windows), or 0 for the full screen */
  unsigned flags;          /* SGL_GLIDE_NO_VSYNC */
  void *state;             /* for the back end */
} sgl_glide;

/* Opens the Voodoo screen and gives the Glide back end to ctx. Call it
 * after sgl_set_buffer with a color buffer of width x height. Gives 0
 * when there is no card, the size is not a Glide resolution, or Glide
 * does not open the screen; then the context stays in software.
 * sgl_set_backend(ctx, NULL, NULL) or sgl_destroy_context closes the
 * screen. */
int sgl_glide_attach(sgl_glide *g, sgl_context *ctx);

#ifdef __cplusplus
}
#endif

#endif
