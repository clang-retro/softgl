/* glext.h - the OpenGL extension tokens and functions. wglGetProcAddress
 * gives the functions of the extensions and of OpenGL 1.2 and later
 * that softgl has (see docs/software-opengl.md).
 *
 * The extension sections are a copy of include/GL/glext.h of
 * clang-retro/mac-sdk, which tools/gen-gl.py makes from the Khronos
 * OpenGL API registry (gl.xml, Copyright 2013-2026 The Khronos Group
 * Inc., SPDX-License-Identifier: Apache-2.0). The GL_VERSION sections at
 * the end are added from the same registry.
 */
#ifndef __glext_h_
#define __glext_h_

#include <GL/gl.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GL_GLEXT_VERSION 1

#ifndef GL_ARB_multitexture
#define GL_ARB_multitexture 1
#define GL_TEXTURE0_ARB 0x84C0
#define GL_TEXTURE1_ARB 0x84C1
#define GL_TEXTURE2_ARB 0x84C2
#define GL_TEXTURE3_ARB 0x84C3
#define GL_TEXTURE4_ARB 0x84C4
#define GL_TEXTURE5_ARB 0x84C5
#define GL_TEXTURE6_ARB 0x84C6
#define GL_TEXTURE7_ARB 0x84C7
#define GL_TEXTURE8_ARB 0x84C8
#define GL_TEXTURE9_ARB 0x84C9
#define GL_TEXTURE10_ARB 0x84CA
#define GL_TEXTURE11_ARB 0x84CB
#define GL_TEXTURE12_ARB 0x84CC
#define GL_TEXTURE13_ARB 0x84CD
#define GL_TEXTURE14_ARB 0x84CE
#define GL_TEXTURE15_ARB 0x84CF
#define GL_TEXTURE16_ARB 0x84D0
#define GL_TEXTURE17_ARB 0x84D1
#define GL_TEXTURE18_ARB 0x84D2
#define GL_TEXTURE19_ARB 0x84D3
#define GL_TEXTURE20_ARB 0x84D4
#define GL_TEXTURE21_ARB 0x84D5
#define GL_TEXTURE22_ARB 0x84D6
#define GL_TEXTURE23_ARB 0x84D7
#define GL_TEXTURE24_ARB 0x84D8
#define GL_TEXTURE25_ARB 0x84D9
#define GL_TEXTURE26_ARB 0x84DA
#define GL_TEXTURE27_ARB 0x84DB
#define GL_TEXTURE28_ARB 0x84DC
#define GL_TEXTURE29_ARB 0x84DD
#define GL_TEXTURE30_ARB 0x84DE
#define GL_TEXTURE31_ARB 0x84DF
#define GL_ACTIVE_TEXTURE_ARB 0x84E0
#define GL_CLIENT_ACTIVE_TEXTURE_ARB 0x84E1
#define GL_MAX_TEXTURE_UNITS_ARB 0x84E2
GLAPI void APIENTRY glActiveTextureARB(GLenum texture);
GLAPI void APIENTRY glClientActiveTextureARB(GLenum texture);
GLAPI void APIENTRY glMultiTexCoord1dARB(GLenum target, GLdouble s);
GLAPI void APIENTRY glMultiTexCoord1dvARB(GLenum target, const GLdouble *v);
GLAPI void APIENTRY glMultiTexCoord1fARB(GLenum target, GLfloat s);
GLAPI void APIENTRY glMultiTexCoord1fvARB(GLenum target, const GLfloat *v);
GLAPI void APIENTRY glMultiTexCoord1iARB(GLenum target, GLint s);
GLAPI void APIENTRY glMultiTexCoord1ivARB(GLenum target, const GLint *v);
GLAPI void APIENTRY glMultiTexCoord1sARB(GLenum target, GLshort s);
GLAPI void APIENTRY glMultiTexCoord1svARB(GLenum target, const GLshort *v);
GLAPI void APIENTRY glMultiTexCoord2dARB(GLenum target, GLdouble s, GLdouble t);
GLAPI void APIENTRY glMultiTexCoord2dvARB(GLenum target, const GLdouble *v);
GLAPI void APIENTRY glMultiTexCoord2fARB(GLenum target, GLfloat s, GLfloat t);
GLAPI void APIENTRY glMultiTexCoord2fvARB(GLenum target, const GLfloat *v);
GLAPI void APIENTRY glMultiTexCoord2iARB(GLenum target, GLint s, GLint t);
GLAPI void APIENTRY glMultiTexCoord2ivARB(GLenum target, const GLint *v);
GLAPI void APIENTRY glMultiTexCoord2sARB(GLenum target, GLshort s, GLshort t);
GLAPI void APIENTRY glMultiTexCoord2svARB(GLenum target, const GLshort *v);
GLAPI void APIENTRY glMultiTexCoord3dARB(GLenum target, GLdouble s, GLdouble t, GLdouble r);
GLAPI void APIENTRY glMultiTexCoord3dvARB(GLenum target, const GLdouble *v);
GLAPI void APIENTRY glMultiTexCoord3fARB(GLenum target, GLfloat s, GLfloat t, GLfloat r);
GLAPI void APIENTRY glMultiTexCoord3fvARB(GLenum target, const GLfloat *v);
GLAPI void APIENTRY glMultiTexCoord3iARB(GLenum target, GLint s, GLint t, GLint r);
GLAPI void APIENTRY glMultiTexCoord3ivARB(GLenum target, const GLint *v);
GLAPI void APIENTRY glMultiTexCoord3sARB(GLenum target, GLshort s, GLshort t, GLshort r);
GLAPI void APIENTRY glMultiTexCoord3svARB(GLenum target, const GLshort *v);
GLAPI void APIENTRY glMultiTexCoord4dARB(GLenum target, GLdouble s, GLdouble t, GLdouble r, GLdouble q);
GLAPI void APIENTRY glMultiTexCoord4dvARB(GLenum target, const GLdouble *v);
GLAPI void APIENTRY glMultiTexCoord4fARB(GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q);
GLAPI void APIENTRY glMultiTexCoord4fvARB(GLenum target, const GLfloat *v);
GLAPI void APIENTRY glMultiTexCoord4iARB(GLenum target, GLint s, GLint t, GLint r, GLint q);
GLAPI void APIENTRY glMultiTexCoord4ivARB(GLenum target, const GLint *v);
GLAPI void APIENTRY glMultiTexCoord4sARB(GLenum target, GLshort s, GLshort t, GLshort r, GLshort q);
GLAPI void APIENTRY glMultiTexCoord4svARB(GLenum target, const GLshort *v);
typedef void (APIENTRY *PFNGLACTIVETEXTUREARBPROC)(GLenum texture);
typedef void (APIENTRY *PFNGLCLIENTACTIVETEXTUREARBPROC)(GLenum texture);
typedef void (APIENTRY *PFNGLMULTITEXCOORD1DARBPROC)(GLenum target, GLdouble s);
typedef void (APIENTRY *PFNGLMULTITEXCOORD1DVARBPROC)(GLenum target, const GLdouble *v);
typedef void (APIENTRY *PFNGLMULTITEXCOORD1FARBPROC)(GLenum target, GLfloat s);
typedef void (APIENTRY *PFNGLMULTITEXCOORD1FVARBPROC)(GLenum target, const GLfloat *v);
typedef void (APIENTRY *PFNGLMULTITEXCOORD1IARBPROC)(GLenum target, GLint s);
typedef void (APIENTRY *PFNGLMULTITEXCOORD1IVARBPROC)(GLenum target, const GLint *v);
typedef void (APIENTRY *PFNGLMULTITEXCOORD1SARBPROC)(GLenum target, GLshort s);
typedef void (APIENTRY *PFNGLMULTITEXCOORD1SVARBPROC)(GLenum target, const GLshort *v);
typedef void (APIENTRY *PFNGLMULTITEXCOORD2DARBPROC)(GLenum target, GLdouble s, GLdouble t);
typedef void (APIENTRY *PFNGLMULTITEXCOORD2DVARBPROC)(GLenum target, const GLdouble *v);
typedef void (APIENTRY *PFNGLMULTITEXCOORD2FARBPROC)(GLenum target, GLfloat s, GLfloat t);
typedef void (APIENTRY *PFNGLMULTITEXCOORD2FVARBPROC)(GLenum target, const GLfloat *v);
typedef void (APIENTRY *PFNGLMULTITEXCOORD2IARBPROC)(GLenum target, GLint s, GLint t);
typedef void (APIENTRY *PFNGLMULTITEXCOORD2IVARBPROC)(GLenum target, const GLint *v);
typedef void (APIENTRY *PFNGLMULTITEXCOORD2SARBPROC)(GLenum target, GLshort s, GLshort t);
typedef void (APIENTRY *PFNGLMULTITEXCOORD2SVARBPROC)(GLenum target, const GLshort *v);
typedef void (APIENTRY *PFNGLMULTITEXCOORD3DARBPROC)(GLenum target, GLdouble s, GLdouble t, GLdouble r);
typedef void (APIENTRY *PFNGLMULTITEXCOORD3DVARBPROC)(GLenum target, const GLdouble *v);
typedef void (APIENTRY *PFNGLMULTITEXCOORD3FARBPROC)(GLenum target, GLfloat s, GLfloat t, GLfloat r);
typedef void (APIENTRY *PFNGLMULTITEXCOORD3FVARBPROC)(GLenum target, const GLfloat *v);
typedef void (APIENTRY *PFNGLMULTITEXCOORD3IARBPROC)(GLenum target, GLint s, GLint t, GLint r);
typedef void (APIENTRY *PFNGLMULTITEXCOORD3IVARBPROC)(GLenum target, const GLint *v);
typedef void (APIENTRY *PFNGLMULTITEXCOORD3SARBPROC)(GLenum target, GLshort s, GLshort t, GLshort r);
typedef void (APIENTRY *PFNGLMULTITEXCOORD3SVARBPROC)(GLenum target, const GLshort *v);
typedef void (APIENTRY *PFNGLMULTITEXCOORD4DARBPROC)(GLenum target, GLdouble s, GLdouble t, GLdouble r, GLdouble q);
typedef void (APIENTRY *PFNGLMULTITEXCOORD4DVARBPROC)(GLenum target, const GLdouble *v);
typedef void (APIENTRY *PFNGLMULTITEXCOORD4FARBPROC)(GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q);
typedef void (APIENTRY *PFNGLMULTITEXCOORD4FVARBPROC)(GLenum target, const GLfloat *v);
typedef void (APIENTRY *PFNGLMULTITEXCOORD4IARBPROC)(GLenum target, GLint s, GLint t, GLint r, GLint q);
typedef void (APIENTRY *PFNGLMULTITEXCOORD4IVARBPROC)(GLenum target, const GLint *v);
typedef void (APIENTRY *PFNGLMULTITEXCOORD4SARBPROC)(GLenum target, GLshort s, GLshort t, GLshort r, GLshort q);
typedef void (APIENTRY *PFNGLMULTITEXCOORD4SVARBPROC)(GLenum target, const GLshort *v);
#endif /* GL_ARB_multitexture */

#ifndef GL_ARB_transpose_matrix
#define GL_ARB_transpose_matrix 1
#define GL_TRANSPOSE_MODELVIEW_MATRIX_ARB 0x84E3
#define GL_TRANSPOSE_PROJECTION_MATRIX_ARB 0x84E4
#define GL_TRANSPOSE_TEXTURE_MATRIX_ARB 0x84E5
#define GL_TRANSPOSE_COLOR_MATRIX_ARB 0x84E6
GLAPI void APIENTRY glLoadTransposeMatrixfARB(const GLfloat *m);
GLAPI void APIENTRY glLoadTransposeMatrixdARB(const GLdouble *m);
GLAPI void APIENTRY glMultTransposeMatrixfARB(const GLfloat *m);
GLAPI void APIENTRY glMultTransposeMatrixdARB(const GLdouble *m);
typedef void (APIENTRY *PFNGLLOADTRANSPOSEMATRIXFARBPROC)(const GLfloat *m);
typedef void (APIENTRY *PFNGLLOADTRANSPOSEMATRIXDARBPROC)(const GLdouble *m);
typedef void (APIENTRY *PFNGLMULTTRANSPOSEMATRIXFARBPROC)(const GLfloat *m);
typedef void (APIENTRY *PFNGLMULTTRANSPOSEMATRIXDARBPROC)(const GLdouble *m);
#endif /* GL_ARB_transpose_matrix */

#ifndef GL_EXT_compiled_vertex_array
#define GL_EXT_compiled_vertex_array 1
#define GL_ARRAY_ELEMENT_LOCK_FIRST_EXT 0x81A8
#define GL_ARRAY_ELEMENT_LOCK_COUNT_EXT 0x81A9
GLAPI void APIENTRY glLockArraysEXT(GLint first, GLsizei count);
GLAPI void APIENTRY glUnlockArraysEXT(void);
typedef void (APIENTRY *PFNGLLOCKARRAYSEXTPROC)(GLint first, GLsizei count);
typedef void (APIENTRY *PFNGLUNLOCKARRAYSEXTPROC)(void);
#endif /* GL_EXT_compiled_vertex_array */

#ifndef GL_EXT_abgr
#define GL_EXT_abgr 1
#define GL_ABGR_EXT 0x8000
#endif /* GL_EXT_abgr */

#ifndef GL_EXT_bgra
#define GL_EXT_bgra 1
#define GL_BGR_EXT 0x80E0
#define GL_BGRA_EXT 0x80E1
#endif /* GL_EXT_bgra */

#ifndef GL_EXT_vertex_array
#define GL_EXT_vertex_array 1
#define GL_VERTEX_ARRAY_EXT 0x8074
#define GL_NORMAL_ARRAY_EXT 0x8075
#define GL_COLOR_ARRAY_EXT 0x8076
#define GL_INDEX_ARRAY_EXT 0x8077
#define GL_TEXTURE_COORD_ARRAY_EXT 0x8078
#define GL_EDGE_FLAG_ARRAY_EXT 0x8079
#define GL_VERTEX_ARRAY_SIZE_EXT 0x807A
#define GL_VERTEX_ARRAY_TYPE_EXT 0x807B
#define GL_VERTEX_ARRAY_STRIDE_EXT 0x807C
#define GL_VERTEX_ARRAY_COUNT_EXT 0x807D
#define GL_NORMAL_ARRAY_TYPE_EXT 0x807E
#define GL_NORMAL_ARRAY_STRIDE_EXT 0x807F
#define GL_NORMAL_ARRAY_COUNT_EXT 0x8080
#define GL_COLOR_ARRAY_SIZE_EXT 0x8081
#define GL_COLOR_ARRAY_TYPE_EXT 0x8082
#define GL_COLOR_ARRAY_STRIDE_EXT 0x8083
#define GL_COLOR_ARRAY_COUNT_EXT 0x8084
#define GL_INDEX_ARRAY_TYPE_EXT 0x8085
#define GL_INDEX_ARRAY_STRIDE_EXT 0x8086
#define GL_INDEX_ARRAY_COUNT_EXT 0x8087
#define GL_TEXTURE_COORD_ARRAY_SIZE_EXT 0x8088
#define GL_TEXTURE_COORD_ARRAY_TYPE_EXT 0x8089
#define GL_TEXTURE_COORD_ARRAY_STRIDE_EXT 0x808A
#define GL_TEXTURE_COORD_ARRAY_COUNT_EXT 0x808B
#define GL_EDGE_FLAG_ARRAY_STRIDE_EXT 0x808C
#define GL_EDGE_FLAG_ARRAY_COUNT_EXT 0x808D
#define GL_VERTEX_ARRAY_POINTER_EXT 0x808E
#define GL_NORMAL_ARRAY_POINTER_EXT 0x808F
#define GL_COLOR_ARRAY_POINTER_EXT 0x8090
#define GL_INDEX_ARRAY_POINTER_EXT 0x8091
#define GL_TEXTURE_COORD_ARRAY_POINTER_EXT 0x8092
#define GL_EDGE_FLAG_ARRAY_POINTER_EXT 0x8093
GLAPI void APIENTRY glArrayElementEXT(GLint i);
GLAPI void APIENTRY glColorPointerEXT(GLint size, GLenum type, GLsizei stride, GLsizei count, const void *pointer);
GLAPI void APIENTRY glDrawArraysEXT(GLenum mode, GLint first, GLsizei count);
GLAPI void APIENTRY glEdgeFlagPointerEXT(GLsizei stride, GLsizei count, const GLboolean *pointer);
GLAPI void APIENTRY glGetPointervEXT(GLenum pname, void **params);
GLAPI void APIENTRY glIndexPointerEXT(GLenum type, GLsizei stride, GLsizei count, const void *pointer);
GLAPI void APIENTRY glNormalPointerEXT(GLenum type, GLsizei stride, GLsizei count, const void *pointer);
GLAPI void APIENTRY glTexCoordPointerEXT(GLint size, GLenum type, GLsizei stride, GLsizei count, const void *pointer);
GLAPI void APIENTRY glVertexPointerEXT(GLint size, GLenum type, GLsizei stride, GLsizei count, const void *pointer);
typedef void (APIENTRY *PFNGLARRAYELEMENTEXTPROC)(GLint i);
typedef void (APIENTRY *PFNGLCOLORPOINTEREXTPROC)(GLint size, GLenum type, GLsizei stride, GLsizei count, const void *pointer);
typedef void (APIENTRY *PFNGLDRAWARRAYSEXTPROC)(GLenum mode, GLint first, GLsizei count);
typedef void (APIENTRY *PFNGLEDGEFLAGPOINTEREXTPROC)(GLsizei stride, GLsizei count, const GLboolean *pointer);
typedef void (APIENTRY *PFNGLGETPOINTERVEXTPROC)(GLenum pname, void **params);
typedef void (APIENTRY *PFNGLINDEXPOINTEREXTPROC)(GLenum type, GLsizei stride, GLsizei count, const void *pointer);
typedef void (APIENTRY *PFNGLNORMALPOINTEREXTPROC)(GLenum type, GLsizei stride, GLsizei count, const void *pointer);
typedef void (APIENTRY *PFNGLTEXCOORDPOINTEREXTPROC)(GLint size, GLenum type, GLsizei stride, GLsizei count, const void *pointer);
typedef void (APIENTRY *PFNGLVERTEXPOINTEREXTPROC)(GLint size, GLenum type, GLsizei stride, GLsizei count, const void *pointer);
#endif /* GL_EXT_vertex_array */

#ifndef GL_EXT_paletted_texture
#define GL_EXT_paletted_texture 1
#define GL_COLOR_INDEX1_EXT 0x80E2
#define GL_COLOR_INDEX2_EXT 0x80E3
#define GL_COLOR_INDEX4_EXT 0x80E4
#define GL_COLOR_INDEX8_EXT 0x80E5
#define GL_COLOR_INDEX12_EXT 0x80E6
#define GL_COLOR_INDEX16_EXT 0x80E7
#define GL_TEXTURE_INDEX_SIZE_EXT 0x80ED
GLAPI void APIENTRY glColorTableEXT(GLenum target, GLenum internalFormat, GLsizei width, GLenum format, GLenum type, const void *table);
GLAPI void APIENTRY glGetColorTableEXT(GLenum target, GLenum format, GLenum type, void *data);
GLAPI void APIENTRY glGetColorTableParameterivEXT(GLenum target, GLenum pname, GLint *params);
GLAPI void APIENTRY glGetColorTableParameterfvEXT(GLenum target, GLenum pname, GLfloat *params);
typedef void (APIENTRY *PFNGLCOLORTABLEEXTPROC)(GLenum target, GLenum internalFormat, GLsizei width, GLenum format, GLenum type, const void *table);
typedef void (APIENTRY *PFNGLGETCOLORTABLEEXTPROC)(GLenum target, GLenum format, GLenum type, void *data);
typedef void (APIENTRY *PFNGLGETCOLORTABLEPARAMETERIVEXTPROC)(GLenum target, GLenum pname, GLint *params);
typedef void (APIENTRY *PFNGLGETCOLORTABLEPARAMETERFVEXTPROC)(GLenum target, GLenum pname, GLfloat *params);
#endif /* GL_EXT_paletted_texture */

#ifndef GL_APPLE_transform_hint
#define GL_APPLE_transform_hint 1
#define GL_TRANSFORM_HINT_APPLE 0x85B1
#endif /* GL_APPLE_transform_hint */

#ifndef GL_ARB_texture_env_add
#define GL_ARB_texture_env_add 1
#endif /* GL_ARB_texture_env_add */

#ifndef GL_ARB_texture_cube_map
#define GL_ARB_texture_cube_map 1
#define GL_NORMAL_MAP_ARB 0x8511
#define GL_REFLECTION_MAP_ARB 0x8512
#define GL_TEXTURE_CUBE_MAP_ARB 0x8513
#define GL_TEXTURE_BINDING_CUBE_MAP_ARB 0x8514
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X_ARB 0x8515
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_X_ARB 0x8516
#define GL_TEXTURE_CUBE_MAP_POSITIVE_Y_ARB 0x8517
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_Y_ARB 0x8518
#define GL_TEXTURE_CUBE_MAP_POSITIVE_Z_ARB 0x8519
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_Z_ARB 0x851A
#define GL_PROXY_TEXTURE_CUBE_MAP_ARB 0x851B
#define GL_MAX_CUBE_MAP_TEXTURE_SIZE_ARB 0x851C
#endif /* GL_ARB_texture_cube_map */

#ifndef GL_EXT_blend_color
#define GL_EXT_blend_color 1
#define GL_CONSTANT_COLOR_EXT 0x8001
#define GL_ONE_MINUS_CONSTANT_COLOR_EXT 0x8002
#define GL_CONSTANT_ALPHA_EXT 0x8003
#define GL_ONE_MINUS_CONSTANT_ALPHA_EXT 0x8004
#define GL_BLEND_COLOR_EXT 0x8005
GLAPI void APIENTRY glBlendColorEXT(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
typedef void (APIENTRY *PFNGLBLENDCOLOREXTPROC)(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
#endif /* GL_EXT_blend_color */

#ifndef GL_EXT_blend_minmax
#define GL_EXT_blend_minmax 1
#define GL_MIN_EXT 0x8007
#define GL_MAX_EXT 0x8008
#define GL_FUNC_ADD_EXT 0x8006
#define GL_BLEND_EQUATION_EXT 0x8009
GLAPI void APIENTRY glBlendEquationEXT(GLenum mode);
typedef void (APIENTRY *PFNGLBLENDEQUATIONEXTPROC)(GLenum mode);
#endif /* GL_EXT_blend_minmax */

#ifndef GL_EXT_blend_subtract
#define GL_EXT_blend_subtract 1
#define GL_FUNC_SUBTRACT_EXT 0x800A
#define GL_FUNC_REVERSE_SUBTRACT_EXT 0x800B
#endif /* GL_EXT_blend_subtract */

#ifndef GL_EXT_texture_lod_bias
#define GL_EXT_texture_lod_bias 1
#define GL_MAX_TEXTURE_LOD_BIAS_EXT 0x84FD
#define GL_TEXTURE_FILTER_CONTROL_EXT 0x8500
#define GL_TEXTURE_LOD_BIAS_EXT 0x8501
#endif /* GL_EXT_texture_lod_bias */

#ifndef GL_EXT_texture_filter_anisotropic
#define GL_EXT_texture_filter_anisotropic 1
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif /* GL_EXT_texture_filter_anisotropic */

#ifndef GL_SGIS_generate_mipmap
#define GL_SGIS_generate_mipmap 1
#define GL_GENERATE_MIPMAP_SGIS 0x8191
#define GL_GENERATE_MIPMAP_HINT_SGIS 0x8192
#endif /* GL_SGIS_generate_mipmap */

#ifndef GL_SGIS_texture_edge_clamp
#define GL_SGIS_texture_edge_clamp 1
#define GL_CLAMP_TO_EDGE_SGIS 0x812F
#endif /* GL_SGIS_texture_edge_clamp */

#ifndef GL_VERSION_1_3
#define GL_VERSION_1_3 1
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE1 0x84C1
#define GL_TEXTURE2 0x84C2
#define GL_TEXTURE3 0x84C3
#define GL_TEXTURE4 0x84C4
#define GL_TEXTURE5 0x84C5
#define GL_TEXTURE6 0x84C6
#define GL_TEXTURE7 0x84C7
#define GL_TEXTURE8 0x84C8
#define GL_TEXTURE9 0x84C9
#define GL_TEXTURE10 0x84CA
#define GL_TEXTURE11 0x84CB
#define GL_TEXTURE12 0x84CC
#define GL_TEXTURE13 0x84CD
#define GL_TEXTURE14 0x84CE
#define GL_TEXTURE15 0x84CF
#define GL_TEXTURE16 0x84D0
#define GL_TEXTURE17 0x84D1
#define GL_TEXTURE18 0x84D2
#define GL_TEXTURE19 0x84D3
#define GL_TEXTURE20 0x84D4
#define GL_TEXTURE21 0x84D5
#define GL_TEXTURE22 0x84D6
#define GL_TEXTURE23 0x84D7
#define GL_TEXTURE24 0x84D8
#define GL_TEXTURE25 0x84D9
#define GL_TEXTURE26 0x84DA
#define GL_TEXTURE27 0x84DB
#define GL_TEXTURE28 0x84DC
#define GL_TEXTURE29 0x84DD
#define GL_TEXTURE30 0x84DE
#define GL_TEXTURE31 0x84DF
#define GL_ACTIVE_TEXTURE 0x84E0
#define GL_CLIENT_ACTIVE_TEXTURE 0x84E1
#define GL_MAX_TEXTURE_UNITS 0x84E2
#define GL_TRANSPOSE_MODELVIEW_MATRIX 0x84E3
#define GL_TRANSPOSE_PROJECTION_MATRIX 0x84E4
#define GL_TRANSPOSE_TEXTURE_MATRIX 0x84E5
#define GL_TRANSPOSE_COLOR_MATRIX 0x84E6
#define GL_MULTISAMPLE 0x809D
#define GL_SAMPLE_ALPHA_TO_COVERAGE 0x809E
#define GL_SAMPLE_ALPHA_TO_ONE 0x809F
#define GL_SAMPLE_COVERAGE 0x80A0
#define GL_SAMPLE_BUFFERS 0x80A8
#define GL_SAMPLES 0x80A9
#define GL_SAMPLE_COVERAGE_VALUE 0x80AA
#define GL_SAMPLE_COVERAGE_INVERT 0x80AB
#define GL_MULTISAMPLE_BIT 0x20000000
#define GL_NORMAL_MAP 0x8511
#define GL_REFLECTION_MAP 0x8512
#define GL_TEXTURE_CUBE_MAP 0x8513
#define GL_TEXTURE_BINDING_CUBE_MAP 0x8514
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X 0x8515
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_X 0x8516
#define GL_TEXTURE_CUBE_MAP_POSITIVE_Y 0x8517
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_Y 0x8518
#define GL_TEXTURE_CUBE_MAP_POSITIVE_Z 0x8519
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_Z 0x851A
#define GL_PROXY_TEXTURE_CUBE_MAP 0x851B
#define GL_MAX_CUBE_MAP_TEXTURE_SIZE 0x851C
#define GL_COMPRESSED_ALPHA 0x84E9
#define GL_COMPRESSED_LUMINANCE 0x84EA
#define GL_COMPRESSED_LUMINANCE_ALPHA 0x84EB
#define GL_COMPRESSED_INTENSITY 0x84EC
#define GL_COMPRESSED_RGB 0x84ED
#define GL_COMPRESSED_RGBA 0x84EE
#define GL_TEXTURE_COMPRESSION_HINT 0x84EF
#define GL_TEXTURE_COMPRESSED_IMAGE_SIZE 0x86A0
#define GL_TEXTURE_COMPRESSED 0x86A1
#define GL_NUM_COMPRESSED_TEXTURE_FORMATS 0x86A2
#define GL_COMPRESSED_TEXTURE_FORMATS 0x86A3
#define GL_CLAMP_TO_BORDER 0x812D
#define GL_COMBINE 0x8570
#define GL_COMBINE_RGB 0x8571
#define GL_COMBINE_ALPHA 0x8572
#define GL_SOURCE0_RGB 0x8580
#define GL_SOURCE1_RGB 0x8581
#define GL_SOURCE2_RGB 0x8582
#define GL_SOURCE0_ALPHA 0x8588
#define GL_SOURCE1_ALPHA 0x8589
#define GL_SOURCE2_ALPHA 0x858A
#define GL_OPERAND0_RGB 0x8590
#define GL_OPERAND1_RGB 0x8591
#define GL_OPERAND2_RGB 0x8592
#define GL_OPERAND0_ALPHA 0x8598
#define GL_OPERAND1_ALPHA 0x8599
#define GL_OPERAND2_ALPHA 0x859A
#define GL_RGB_SCALE 0x8573
#define GL_ADD_SIGNED 0x8574
#define GL_INTERPOLATE 0x8575
#define GL_SUBTRACT 0x84E7
#define GL_CONSTANT 0x8576
#define GL_PRIMARY_COLOR 0x8577
#define GL_PREVIOUS 0x8578
#define GL_DOT3_RGB 0x86AE
#define GL_DOT3_RGBA 0x86AF
GLAPI void APIENTRY glActiveTexture(GLenum texture);
GLAPI void APIENTRY glClientActiveTexture(GLenum texture);
GLAPI void APIENTRY glMultiTexCoord1s(GLenum target, GLshort s);
GLAPI void APIENTRY glMultiTexCoord1sv(GLenum target, const GLshort *v);
GLAPI void APIENTRY glMultiTexCoord1i(GLenum target, GLint s);
GLAPI void APIENTRY glMultiTexCoord1iv(GLenum target, const GLint *v);
GLAPI void APIENTRY glMultiTexCoord1f(GLenum target, GLfloat s);
GLAPI void APIENTRY glMultiTexCoord1fv(GLenum target, const GLfloat *v);
GLAPI void APIENTRY glMultiTexCoord1d(GLenum target, GLdouble s);
GLAPI void APIENTRY glMultiTexCoord1dv(GLenum target, const GLdouble *v);
GLAPI void APIENTRY glMultiTexCoord2s(GLenum target, GLshort s, GLshort t);
GLAPI void APIENTRY glMultiTexCoord2sv(GLenum target, const GLshort *v);
GLAPI void APIENTRY glMultiTexCoord2i(GLenum target, GLint s, GLint t);
GLAPI void APIENTRY glMultiTexCoord2iv(GLenum target, const GLint *v);
GLAPI void APIENTRY glMultiTexCoord2f(GLenum target, GLfloat s, GLfloat t);
GLAPI void APIENTRY glMultiTexCoord2fv(GLenum target, const GLfloat *v);
GLAPI void APIENTRY glMultiTexCoord2d(GLenum target, GLdouble s, GLdouble t);
GLAPI void APIENTRY glMultiTexCoord2dv(GLenum target, const GLdouble *v);
GLAPI void APIENTRY glMultiTexCoord3s(GLenum target, GLshort s, GLshort t, GLshort r);
GLAPI void APIENTRY glMultiTexCoord3sv(GLenum target, const GLshort *v);
GLAPI void APIENTRY glMultiTexCoord3i(GLenum target, GLint s, GLint t, GLint r);
GLAPI void APIENTRY glMultiTexCoord3iv(GLenum target, const GLint *v);
GLAPI void APIENTRY glMultiTexCoord3f(GLenum target, GLfloat s, GLfloat t, GLfloat r);
GLAPI void APIENTRY glMultiTexCoord3fv(GLenum target, const GLfloat *v);
GLAPI void APIENTRY glMultiTexCoord3d(GLenum target, GLdouble s, GLdouble t, GLdouble r);
GLAPI void APIENTRY glMultiTexCoord3dv(GLenum target, const GLdouble *v);
GLAPI void APIENTRY glMultiTexCoord4s(GLenum target, GLshort s, GLshort t, GLshort r, GLshort q);
GLAPI void APIENTRY glMultiTexCoord4sv(GLenum target, const GLshort *v);
GLAPI void APIENTRY glMultiTexCoord4i(GLenum target, GLint s, GLint t, GLint r, GLint q);
GLAPI void APIENTRY glMultiTexCoord4iv(GLenum target, const GLint *v);
GLAPI void APIENTRY glMultiTexCoord4f(GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q);
GLAPI void APIENTRY glMultiTexCoord4fv(GLenum target, const GLfloat *v);
GLAPI void APIENTRY glMultiTexCoord4d(GLenum target, GLdouble s, GLdouble t, GLdouble r, GLdouble q);
GLAPI void APIENTRY glMultiTexCoord4dv(GLenum target, const GLdouble *v);
GLAPI void APIENTRY glLoadTransposeMatrixf(const GLfloat *m);
GLAPI void APIENTRY glLoadTransposeMatrixd(const GLdouble *m);
GLAPI void APIENTRY glMultTransposeMatrixf(const GLfloat *m);
GLAPI void APIENTRY glMultTransposeMatrixd(const GLdouble *m);
GLAPI void APIENTRY glSampleCoverage(GLfloat value, GLboolean invert);
GLAPI void APIENTRY glCompressedTexImage1D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLint border, GLsizei imageSize, const void *data);
GLAPI void APIENTRY glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const void *data);
GLAPI void APIENTRY glCompressedTexImage3D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLsizei imageSize, const void *data);
GLAPI void APIENTRY glCompressedTexSubImage1D(GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLsizei imageSize, const void *data);
GLAPI void APIENTRY glCompressedTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const void *data);
GLAPI void APIENTRY glCompressedTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLsizei imageSize, const void *data);
GLAPI void APIENTRY glGetCompressedTexImage(GLenum target, GLint level, void *img);
#endif /* GL_VERSION_1_3 */

#ifndef GL_VERSION_1_4
#define GL_VERSION_1_4 1
#define GL_BLEND_DST_RGB 0x80C8
#define GL_BLEND_SRC_RGB 0x80C9
#define GL_BLEND_DST_ALPHA 0x80CA
#define GL_BLEND_SRC_ALPHA 0x80CB
#define GL_POINT_FADE_THRESHOLD_SIZE 0x8128
#define GL_DEPTH_COMPONENT16 0x81A5
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_DEPTH_COMPONENT32 0x81A7
#define GL_MIRRORED_REPEAT 0x8370
#define GL_MAX_TEXTURE_LOD_BIAS 0x84FD
#define GL_TEXTURE_LOD_BIAS 0x8501
#define GL_INCR_WRAP 0x8507
#define GL_DECR_WRAP 0x8508
#define GL_TEXTURE_DEPTH_SIZE 0x884A
#define GL_TEXTURE_COMPARE_MODE 0x884C
#define GL_TEXTURE_COMPARE_FUNC 0x884D
#define GL_POINT_SIZE_MIN 0x8126
#define GL_POINT_SIZE_MAX 0x8127
#define GL_POINT_DISTANCE_ATTENUATION 0x8129
#define GL_GENERATE_MIPMAP 0x8191
#define GL_GENERATE_MIPMAP_HINT 0x8192
#define GL_FOG_COORDINATE_SOURCE 0x8450
#define GL_FOG_COORDINATE 0x8451
#define GL_FRAGMENT_DEPTH 0x8452
#define GL_CURRENT_FOG_COORDINATE 0x8453
#define GL_FOG_COORDINATE_ARRAY_TYPE 0x8454
#define GL_FOG_COORDINATE_ARRAY_STRIDE 0x8455
#define GL_FOG_COORDINATE_ARRAY_POINTER 0x8456
#define GL_FOG_COORDINATE_ARRAY 0x8457
#define GL_COLOR_SUM 0x8458
#define GL_CURRENT_SECONDARY_COLOR 0x8459
#define GL_SECONDARY_COLOR_ARRAY_SIZE 0x845A
#define GL_SECONDARY_COLOR_ARRAY_TYPE 0x845B
#define GL_SECONDARY_COLOR_ARRAY_STRIDE 0x845C
#define GL_SECONDARY_COLOR_ARRAY_POINTER 0x845D
#define GL_SECONDARY_COLOR_ARRAY 0x845E
#define GL_TEXTURE_FILTER_CONTROL 0x8500
#define GL_DEPTH_TEXTURE_MODE 0x884B
#define GL_COMPARE_R_TO_TEXTURE 0x884E
#define GL_FUNC_ADD 0x8006
#define GL_FUNC_SUBTRACT 0x800A
#define GL_FUNC_REVERSE_SUBTRACT 0x800B
#define GL_MIN 0x8007
#define GL_MAX 0x8008
#define GL_CONSTANT_COLOR 0x8001
#define GL_ONE_MINUS_CONSTANT_COLOR 0x8002
#define GL_CONSTANT_ALPHA 0x8003
#define GL_ONE_MINUS_CONSTANT_ALPHA 0x8004
#define GL_BLEND_COLOR 0x8005
#define GL_BLEND_EQUATION 0x8009
#endif /* GL_VERSION_1_4 */

#ifndef GL_EXT_texture_compression_s3tc
#define GL_EXT_texture_compression_s3tc 1
#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT 0x83F0
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif /* GL_EXT_texture_compression_s3tc */

#ifdef __cplusplus
}
#endif

#endif /* __glext_h_ */
