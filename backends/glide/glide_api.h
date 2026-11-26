/* glide_api.h - the values of the Glide 2 interface that the Glide back
 * end of softgl uses. This file is not the 3dfx SDK; it gives only the
 * interface facts that the back end needs. The types and the calls are
 * in include/softgl_glide.h.
 *
 * Part of clang-retro softgl, a software implementation of OpenGL.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#ifndef SGL_GLIDE_API_H
#define SGL_GLIDE_API_H

/* The screen (grSstWinOpen). */
#define GR_RESOLUTION_320x200 0x0
#define GR_RESOLUTION_320x240 0x1
#define GR_RESOLUTION_400x256 0x2
#define GR_RESOLUTION_512x384 0x3
#define GR_RESOLUTION_640x200 0x4
#define GR_RESOLUTION_640x350 0x5
#define GR_RESOLUTION_640x400 0x6
#define GR_RESOLUTION_640x480 0x7
#define GR_RESOLUTION_800x600 0x8
#define GR_RESOLUTION_960x720 0x9
#define GR_RESOLUTION_856x480 0xa
#define GR_RESOLUTION_512x256 0xb
#define GR_RESOLUTION_1024x768 0xc
#define GR_RESOLUTION_1280x1024 0xd
#define GR_RESOLUTION_1600x1200 0xe
#define GR_RESOLUTION_400x300 0xf
#define GR_COLORFORMAT_ARGB 0x0
#define GR_ORIGIN_UPPER_LEFT 0x0
#define GR_ORIGIN_LOWER_LEFT 0x1

/* The hardware (grSstQueryHardware): GrHwConfiguration starts with the
 * number of boards; each board has its type and its configuration. */
#define GR_SSTTYPE_VOODOO 0
#define GR_SSTTYPE_SST96 1
#define GR_SSTTYPE_AT3D 2
#define GR_SSTTYPE_VOODOO2 3

/* The texture units. */
#define GR_TMU0 0
#define GR_TMU1 1

/* The compare functions (depth and alpha test). */
#define GR_CMP_NEVER 0
#define GR_CMP_LESS 1
#define GR_CMP_EQUAL 2
#define GR_CMP_LEQUAL 3
#define GR_CMP_GREATER 4
#define GR_CMP_NOTEQUAL 5
#define GR_CMP_GEQUAL 6
#define GR_CMP_ALWAYS 7

/* The depth buffer modes. */
#define GR_DEPTHBUFFER_DISABLE 0x0
#define GR_DEPTHBUFFER_ZBUFFER 0x1

/* The combine functions, factors, local and other values. */
#define GR_COMBINE_FUNCTION_ZERO 0x0
#define GR_COMBINE_FUNCTION_LOCAL 0x1
#define GR_COMBINE_FUNCTION_SCALE_OTHER 0x3
#define GR_COMBINE_FUNCTION_BLEND 0x7
#define GR_COMBINE_FACTOR_NONE 0x0
#define GR_COMBINE_FACTOR_LOCAL 0x1
#define GR_COMBINE_FACTOR_TEXTURE_ALPHA 0x4
#define GR_COMBINE_FACTOR_ONE 0x8
#define GR_COMBINE_LOCAL_ITERATED 0x0
#define GR_COMBINE_LOCAL_CONSTANT 0x1
#define GR_COMBINE_OTHER_ITERATED 0x0
#define GR_COMBINE_OTHER_TEXTURE 0x1

/* The blend factors. As a source factor, 0x2 is the destination color;
 * as a destination factor, it is the source color. 0x6 is one minus
 * the same color. */
#define GR_BLEND_ZERO 0x0
#define GR_BLEND_SRC_ALPHA 0x1
#define GR_BLEND_OTHER_COLOR 0x2
#define GR_BLEND_ONE 0x4
#define GR_BLEND_ONE_MINUS_SRC_ALPHA 0x5
#define GR_BLEND_ONE_MINUS_OTHER_COLOR 0x6

/* Fog. */
#define GR_FOG_DISABLE 0x0
#define GR_FOG_WITH_TABLE 0x2
#define GR_FOG_TABLE_SIZE 64

/* The culling and the dither. */
#define GR_CULL_DISABLE 0x0
#define GR_DITHER_DISABLE 0x0
#define GR_DITHER_4x4 0x2

/* Textures. The levels go from GR_LOD_256 (0) to GR_LOD_1 (8). */
#define GR_LOD_256 0
#define GR_LOD_1 8
#define GR_ASPECT_8x1 0
#define GR_ASPECT_1x1 3
#define GR_ASPECT_1x8 6
#define GR_TEXFMT_RGB_565 0xa
#define GR_TEXFMT_ARGB_4444 0xc
#define GR_MIPMAPLEVELMASK_BOTH 0x3
#define GR_TEXTUREFILTER_POINT_SAMPLED 0x0
#define GR_TEXTUREFILTER_BILINEAR 0x1
#define GR_TEXTURECLAMP_WRAP 0x0
#define GR_TEXTURECLAMP_CLAMP 0x1
#define GR_MIPMAP_DISABLE 0x0
#define GR_MIPMAP_NEAREST 0x1

/* A texture must not cross a boundary of 2 MB of texture memory. */
#define GR_TEXTURE_BOUNDARY 0x200000ul

/* The frame buffer (grLfbLock). */
#define GR_BUFFER_BACKBUFFER 0x1
#define GR_BUFFER_AUXBUFFER 0x2
#define GR_LFB_READ_ONLY 0x00
#define GR_LFB_WRITE_ONLY 0x01
#define GR_LFBWRITEMODE_565 0x0
#define GR_LFBWRITEMODE_ZA16 0xf
#define GR_LFBWRITEMODE_ANY 0xff

#endif
