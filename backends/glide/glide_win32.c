/* glide_win32.c - fills the Glide table of softgl_glide.h from
 * glide2x.dll on Windows 95 and 98. The DLL exports the __stdcall names
 * of Glide: "_grDrawTriangle@12" is grDrawTriangle with 12 bytes of
 * arguments.
 *
 * Part of clang-retro softgl, a software implementation of OpenGL.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include <stddef.h>
#include <string.h>

#include <windows.h>

#include "softgl_glide.h"

/* The exported names of the functions and their places in the table. */
static const struct {
  const char *name;
  size_t offset;
} names[] = {
    {"_grGlideInit@0", offsetof(sgl_glide_api, GlideInit)},
    {"_grGlideShutdown@0", offsetof(sgl_glide_api, GlideShutdown)},
    {"_grSstQueryHardware@4", offsetof(sgl_glide_api, SstQueryHardware)},
    {"_grSstSelect@4", offsetof(sgl_glide_api, SstSelect)},
    {"_grSstWinOpen@28", offsetof(sgl_glide_api, SstWinOpen)},
    {"_grSstWinClose@0", offsetof(sgl_glide_api, SstWinClose)},
    {"_grSstIdle@0", offsetof(sgl_glide_api, SstIdle)},
    {"_grBufferClear@12", offsetof(sgl_glide_api, BufferClear)},
    {"_grBufferSwap@4", offsetof(sgl_glide_api, BufferSwap)},
    {"_grDrawTriangle@12", offsetof(sgl_glide_api, DrawTriangle)},
    {"_grDrawLine@8", offsetof(sgl_glide_api, DrawLine)},
    {"_grDrawPoint@4", offsetof(sgl_glide_api, DrawPoint)},
    {"_grDepthBufferMode@4", offsetof(sgl_glide_api, DepthBufferMode)},
    {"_grDepthBufferFunction@4", offsetof(sgl_glide_api, DepthBufferFunction)},
    {"_grDepthMask@4", offsetof(sgl_glide_api, DepthMask)},
    {"_grColorMask@8", offsetof(sgl_glide_api, ColorMask)},
    {"_grColorCombine@20", offsetof(sgl_glide_api, ColorCombine)},
    {"_grAlphaCombine@20", offsetof(sgl_glide_api, AlphaCombine)},
    {"_grTexCombine@28", offsetof(sgl_glide_api, TexCombine)},
    {"_grAlphaBlendFunction@16", offsetof(sgl_glide_api, AlphaBlendFunction)},
    {"_grAlphaTestFunction@4", offsetof(sgl_glide_api, AlphaTestFunction)},
    {"_grAlphaTestReferenceValue@4", offsetof(sgl_glide_api, AlphaTestReferenceValue)},
    {"_grFogMode@4", offsetof(sgl_glide_api, FogMode)},
    {"_grFogColorValue@4", offsetof(sgl_glide_api, FogColorValue)},
    {"_grFogTable@4", offsetof(sgl_glide_api, FogTable)},
    {"_grClipWindow@16", offsetof(sgl_glide_api, ClipWindow)},
    {"_grCullMode@4", offsetof(sgl_glide_api, CullMode)},
    {"_grDitherMode@4", offsetof(sgl_glide_api, DitherMode)},
    {"_grTexMinAddress@4", offsetof(sgl_glide_api, TexMinAddress)},
    {"_grTexMaxAddress@4", offsetof(sgl_glide_api, TexMaxAddress)},
    {"_grTexTextureMemRequired@8", offsetof(sgl_glide_api, TexTextureMemRequired)},
    {"_grTexDownloadMipMap@16", offsetof(sgl_glide_api, TexDownloadMipMap)},
    {"_grTexSource@16", offsetof(sgl_glide_api, TexSource)},
    {"_grTexFilterMode@12", offsetof(sgl_glide_api, TexFilterMode)},
    {"_grTexClampMode@12", offsetof(sgl_glide_api, TexClampMode)},
    {"_grTexMipMapMode@12", offsetof(sgl_glide_api, TexMipMapMode)},
    {"_grLfbLock@24", offsetof(sgl_glide_api, LfbLock)},
    {"_grLfbUnlock@8", offsetof(sgl_glide_api, LfbUnlock)},
};

/* Fills api from glide2x.dll (see softgl_glide.h). */
int sgl_glide_load(sgl_glide_api *api) {
  HMODULE dll;
  size_t i;
  if (!api) return 0;
  memset(api, 0, sizeof *api);
  dll = LoadLibraryA("glide2x.dll");
  if (!dll) return 0;
  for (i = 0; i < sizeof names / sizeof names[0]; i++) {
    FARPROC f = GetProcAddress(dll, names[i].name);
    if (!f) {
      FreeLibrary(dll);
      memset(api, 0, sizeof *api);
      return 0;
    }
    memcpy((char *)api + names[i].offset, &f, sizeof f);
  }
  api->library = (void *)dll;
  return 1;
}

/* Frees the DLL of sgl_glide_load (see softgl_glide.h). */
void sgl_glide_unload(sgl_glide_api *api) {
  if (!api || !api->library) return;
  FreeLibrary((HMODULE)api->library);
  memset(api, 0, sizeof *api);
}
