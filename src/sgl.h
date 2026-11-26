/* sgl.h - the internal state of the software OpenGL (softgl).
 *
 * The renderer is the generic implementation of OpenGL 1.1, as GDI
 * Generic is on Windows. It draws into a buffer in memory that the
 * window system gives (see softgl.h). All the arithmetic of the geometry
 * is in float. The rasterizer steps in integers (16.16 fixed point).
 *
 * Part of clang-retro softgl, a software implementation of OpenGL.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#ifndef SGL_H
#define SGL_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <GL/gl.h>
#include <GL/glext.h>

#include "softgl.h"

#ifndef APIENTRY
#define APIENTRY
#endif

/* The names of GL_EXT_paletted_texture, when the headers do not have
 * them. */
#ifndef GL_COLOR_TABLE_FORMAT_EXT
#define GL_COLOR_TABLE_FORMAT_EXT 0x80D8
#define GL_COLOR_TABLE_WIDTH_EXT 0x80D9
#define GL_COLOR_TABLE_RED_SIZE_EXT 0x80DA
#define GL_COLOR_TABLE_GREEN_SIZE_EXT 0x80DB
#define GL_COLOR_TABLE_BLUE_SIZE_EXT 0x80DC
#define GL_COLOR_TABLE_ALPHA_SIZE_EXT 0x80DD
#define GL_COLOR_TABLE_LUMINANCE_SIZE_EXT 0x80DE
#define GL_COLOR_TABLE_INTENSITY_SIZE_EXT 0x80DF
#endif
#ifndef GL_COLOR_INDEX1_EXT
#define GL_COLOR_INDEX1_EXT 0x80E2
#define GL_COLOR_INDEX2_EXT 0x80E3
#define GL_COLOR_INDEX4_EXT 0x80E4
#define GL_COLOR_INDEX8_EXT 0x80E5
#define GL_COLOR_INDEX12_EXT 0x80E6
#define GL_COLOR_INDEX16_EXT 0x80E7
#endif

/* A thread-local variable on systems with threads. The classic Mac OS
 * has one thread for the renderer. */
#if defined(macintosh) || defined(__MSDOS__) || defined(SGL_NO_TLS)
#define SGL_THREAD
#else
#define SGL_THREAD _Thread_local
#endif

/* The size of the depth buffer elements: 16 bits on the 68k and other
 * small machines, 32 bits elsewhere. */
#ifndef SGL_DEPTH_BITS
#if defined(__m68k__) || defined(__MSDOS__) || defined(__i86__)
#define SGL_DEPTH_BITS 16
#else
#define SGL_DEPTH_BITS 32
#endif
#endif
#if SGL_DEPTH_BITS == 16
typedef uint16_t sgl_depth_t;
#define SGL_DEPTH_SHIFT 15 /* from the 31-bit z of the rasterizer */
#else
typedef uint32_t sgl_depth_t;
#define SGL_DEPTH_SHIFT 0
#endif
/* Multiplies two values that fit in 16 bits with sign. The 68000 has
 * no 32-bit multiply: this is one muls instruction there, not a call to
 * the runtime library. */
#define SGL_MUL16(a, b) ((int32_t)(int16_t)(a) * (int32_t)(int16_t)(b))

/* The value of sgl_context.prim outside glBegin and glEnd. GL_NONE is
 * not correct: it is the same value as GL_POINTS. */
#define SGL_NO_PRIM 0xFFFFu

/* The z of the rasterizer: [0, 1] is [0, SGL_Z_MAX]. */
#define SGL_Z_MAX 0x7FFFFFFF

/* The limits. */
#define SGL_MAX_SIZE 32767 /* the width and height of a buffer */
#define SGL_MAX_LIGHTS 8
#define SGL_MAX_CLIP_PLANES 6
#define SGL_MODELVIEW_DEPTH 32
#define SGL_PROJECTION_DEPTH 4
#define SGL_TEXTURE_DEPTH 4
#define SGL_ATTRIB_DEPTH 16
#define SGL_NAME_DEPTH 64
#define SGL_LIST_DEPTH 64
#define SGL_MAX_TEXTURE_LEVELS 11
#define SGL_MAX_TEXTURE_SIZE 1024
/* The largest 3D texture: 64 x 64 x 64 texels are 1 MB. */
/* The largest face of a cube map: 6 faces of 256 x 256 texels are 1.5 MB. */
#ifndef SGL_MAX_CUBE_MAP_SIZE
#define SGL_MAX_CUBE_MAP_SIZE 256
#endif
#ifndef SGL_MAX_3D_TEXTURE_SIZE
#define SGL_MAX_3D_TEXTURE_SIZE 64
#endif
#define SGL_MAX_EVAL_ORDER 16
#define SGL_MAX_LOD_BIAS 11.0f /* GL_MAX_TEXTURE_LOD_BIAS: the levels of a mipmap */
#define SGL_MAX_POINT_SIZE 64.0f
/* The largest depth of a texel of a depth texture (30 bits). */
#define SGL_TEX_DEPTH_MAX 0x3FFFFFFFu
#define SGL_SHINE_TABLE 256
#define SGL_SHINE_CACHE 4
/* The texture units (GL_ARB_multitexture). Each unit costs memory in
 * each vertex and time in each textured pixel: a build for a small
 * machine can set 1. */
#ifndef SGL_MAX_TEXTURE_UNITS
#define SGL_MAX_TEXTURE_UNITS 2
#endif
#if SGL_MAX_TEXTURE_UNITS < 1 || SGL_MAX_TEXTURE_UNITS > 32
#error "SGL_MAX_TEXTURE_UNITS must be 1 to 32"
#endif

/* One argument of a command in a display list. */
typedef union sgl_cell {
  GLfloat f;
  GLint i;
  GLuint u;
  GLdouble d;
  void *p;
  void (*fn)(const union sgl_cell *);
} sgl_cell;

/* An extension function by name. */
struct sgl_proc {
  const char *name;
  void (*fn)(void);
};

#include "dispatch.h"

/* A 4 x 4 matrix, in column order as OpenGL keeps it. */
typedef struct sgl_matrix {
  GLfloat m[16];
} sgl_matrix;

/* A stack of matrices. */
typedef struct sgl_stack {
  sgl_matrix *top;
  sgl_matrix *base;
  int depth, max;
} sgl_stack;

/* A vertex after transformation and lighting. */
typedef struct sgl_vertex {
  GLfloat clip[4];     /* clip coordinates */
  GLfloat win[4];      /* window x, y and z (0 to 1), and 1 / w */
  GLfloat color[2][4]; /* front and back color, 0 to 1 */
  GLfloat tex[SGL_MAX_TEXTURE_UNITS][4]; /* texture coordinates of each unit,
                                          * after the texture matrix */
  GLfloat spec[2][3];  /* front and back secondary color (the separate
                        * specular color), 0 to 1 */
  GLfloat fog;         /* fog factor: 1 is no fog */
  GLfloat eye[4];      /* eye coordinates (for clipping by the user planes) */
  unsigned clipmask;   /* the planes that it is outside of */
  GLboolean edge;      /* the edge flag */
} sgl_vertex;

/* A light. */
typedef struct sgl_light {
  GLfloat ambient[4], diffuse[4], specular[4];
  GLfloat position[4];     /* in eye coordinates */
  GLfloat spot_dir[3];     /* in eye coordinates */
  GLfloat spot_exponent, spot_cutoff;
  GLfloat attenuation[3];  /* constant, linear, quadratic */
  /* Computed when the light is changed: */
  GLfloat cos_cutoff;
  GLfloat vp_unit[3];      /* the unit direction of a directional light */
  GLfloat half_inf[3];     /* the half vector for an infinite viewer */
} sgl_light;

/* A material (one side). */
typedef struct sgl_material {
  GLfloat ambient[4], diffuse[4], specular[4], emission[4];
  GLfloat shininess;
  GLfloat indexes[3];      /* ambient, diffuse and specular color indices */
  /* Tables of x^shininess for x in [0, 1], for the last shininess
   * values: a program that changes between a few materials does not
   * compute a table again. */
  GLfloat shine_table[SGL_SHINE_CACHE][SGL_SHINE_TABLE + 1];
  GLfloat shine_of[SGL_SHINE_CACHE]; /* the shininess of each table */
  int shine_cur;           /* the table of the current shininess */
  int shine_next;          /* the table to replace next */
} sgl_material;

/* One level of a texture image. The texels are 0xAARRGGBB. */
typedef struct sgl_level {
  uint32_t *texels;
  uint8_t *indices;        /* for a paletted texture: the indices */
  int width, height, depth; /* depth is 1 for a 1D or 2D texture */
  int wlog, hlog, dlog;    /* log2 of the size, or -1 */
  GLenum internal;         /* the internal format that was asked for */
  GLenum base;             /* GL_RGBA, GL_RGB, GL_ALPHA, GL_LUMINANCE, ... */
  uint8_t *compressed;     /* an S3TC level: the blocks as given, for glGetCompressedTexImage */
  size_t compressed_size;
  GLint border;
} sgl_level;

/* A texture object. */
typedef struct sgl_texture {
  GLuint name;
  int refs;
  GLenum target;
  sgl_level level[SGL_MAX_TEXTURE_LEVELS]; /* a cube map: the face +X */
  /* A cube map: the faces -X, +Y, -Y, +Z and -Z, or NULL. */
  sgl_level (*faces)[SGL_MAX_TEXTURE_LEVELS];
  GLenum min_filter, mag_filter, wrap_s, wrap_t, wrap_r;
  GLfloat border_color[4];
  GLint base_level, max_level; /* OpenGL 1.2: the levels that are used */
  GLfloat min_lod, max_lod;
  GLfloat priority;
  GLfloat lod_bias;        /* OpenGL 1.4: GL_TEXTURE_LOD_BIAS of the texture */
  GLboolean generate_mipmap; /* OpenGL 1.4: GL_GENERATE_MIPMAP */
  GLenum compare_mode, compare_func, depth_mode; /* OpenGL 1.4: depth textures */
  uint32_t palette[256];   /* GL_EXT_paletted_texture */
  int palette_size;
  GLenum palette_format;
  int complete;            /* all the mipmap levels from base_level are there */
  struct sgl_texture *next;
} sgl_texture;

/* A command of a display list. */
typedef struct sgl_node {
  struct sgl_node *next;
  int op;
  int count;
  sgl_cell *args;          /* count cells */
  void *data;              /* memory that the node owns, or NULL */
} sgl_node;

/* A display list. */
typedef struct sgl_list {
  GLuint name;
  sgl_node *first, *last;
  struct sgl_list *next;
} sgl_list;

/* The objects that contexts can share (wglShareLists): display lists
 * and texture objects. */
typedef struct sgl_shared {
  int refs;
  sgl_list *lists[64];       /* a hash table by name */
  sgl_texture *textures[64]; /* a hash table by name */
  GLuint next_list, next_texture;
} sgl_shared;

/* A map of an evaluator. */
typedef struct sgl_map {
  int order_u, order_v;
  GLfloat u1, u2, v1, v2;
  int k;                   /* the number of values in each point */
  GLfloat *points;         /* order_u * order_v * k values */
} sgl_map;

/* The capabilities of each texture unit, in the order of their bits. */
enum sgl_unit_cap {
  SGL_U_TEXTURE_1D, SGL_U_TEXTURE_2D, SGL_U_GEN_S, SGL_U_GEN_T, SGL_U_GEN_R, SGL_U_GEN_Q,
  SGL_U_TEXTURE_3D, SGL_U_TEXTURE_CUBE_MAP, SGL_UNIT_CAP_COUNT
};

/* The bits of the enables, by capability. */
enum sgl_cap {
  SGL_ALPHA_TEST, SGL_AUTO_NORMAL, SGL_BLEND, SGL_COLOR_MATERIAL,
  SGL_CULL_FACE, SGL_DEPTH_TEST, SGL_DITHER, SGL_FOG, SGL_LIGHTING,
  SGL_LINE_SMOOTH, SGL_LINE_STIPPLE, SGL_COLOR_LOGIC_OP, SGL_NORMALIZE,
  SGL_POINT_SMOOTH, SGL_POLYGON_SMOOTH, SGL_POLYGON_STIPPLE,
  SGL_SCISSOR_TEST, SGL_STENCIL_TEST, SGL_POLYGON_OFFSET_FILL, SGL_POLYGON_OFFSET_LINE,
  SGL_POLYGON_OFFSET_POINT, SGL_MAP1_VERTEX_3, SGL_MAP1_VERTEX_4,
  SGL_MAP1_NORMAL, SGL_MAP1_COLOR_4, SGL_MAP1_TEXTURE_COORD_1,
  SGL_MAP1_TEXTURE_COORD_2, SGL_MAP1_TEXTURE_COORD_3,
  SGL_MAP1_TEXTURE_COORD_4, SGL_MAP1_INDEX, SGL_MAP2_VERTEX_3,
  SGL_MAP2_VERTEX_4, SGL_MAP2_NORMAL, SGL_MAP2_COLOR_4,
  SGL_MAP2_TEXTURE_COORD_1, SGL_MAP2_TEXTURE_COORD_2,
  SGL_MAP2_TEXTURE_COORD_3, SGL_MAP2_TEXTURE_COORD_4, SGL_MAP2_INDEX,
  SGL_INDEX_LOGIC_OP, SGL_RESCALE_NORMAL,
  SGL_MULTISAMPLE, SGL_SAMPLE_ALPHA_TO_COVERAGE, SGL_SAMPLE_ALPHA_TO_ONE, SGL_SAMPLE_COVERAGE,
  SGL_COLOR_SUM,
  SGL_LIGHT0, /* to SGL_LIGHT0 + 7 */
  SGL_CLIP_PLANE0 = SGL_LIGHT0 + SGL_MAX_LIGHTS, /* to + 5 */
  SGL_UNIT_CAPS = SGL_CLIP_PLANE0 + SGL_MAX_CLIP_PLANES, /* the caps of the texture units */
  SGL_CAP_COUNT = SGL_UNIT_CAPS + SGL_MAX_TEXTURE_UNITS * SGL_UNIT_CAP_COUNT
};

/* Gives the index in sgl_attrib.enabled of capability k of texture
 * unit u. */
#define SGL_UCAP(u, k) (SGL_UNIT_CAPS + (u) * SGL_UNIT_CAP_COUNT + (k))

/* The state of one texture unit that glPushAttrib saves with
 * GL_TEXTURE_BIT. */
typedef struct sgl_unit_attrib {
  GLenum env_mode;
  GLfloat env_color[4];
  GLenum gen_mode[4];
  GLfloat gen_object[4][4], gen_eye[4][4];
  /* GL_COMBINE: the functions, the sources and the operands of the three
   * arguments, and the scales (1, 2 or 4). */
  GLenum combine_rgb, combine_alpha;
  GLenum src_rgb[3], src_alpha[3], op_rgb[3], op_alpha[3];
  GLfloat rgb_scale, alpha_scale;
  GLfloat lod_bias;        /* GL_TEXTURE_LOD_BIAS of GL_TEXTURE_FILTER_CONTROL */
  GLuint bound_1d, bound_2d, bound_3d, bound_cube; /* the bound names, at glPushAttrib */
} sgl_unit_attrib;

/* The evaluator maps, in the order of the SGL_MAP1_ and SGL_MAP2_ caps. */
#define SGL_MAP_TYPES 9

/* The state that glPushAttrib saves, in groups. */
typedef struct sgl_attrib {
  GLbitfield mask;         /* the groups that are saved */
  uint8_t enabled[SGL_CAP_COUNT];
  /* GL_CURRENT_BIT */
  GLfloat color[4], normal[3], index;
  GLfloat color2[4];       /* OpenGL 1.4: the secondary color */
  GLfloat fog_coord;       /* OpenGL 1.4: the fog coordinate */
  GLfloat tex[SGL_MAX_TEXTURE_UNITS][4];
  GLfloat raster_pos[4], raster_color[4];
  GLfloat raster_tex[SGL_MAX_TEXTURE_UNITS][4];
  GLfloat raster_distance, raster_index;
  GLboolean raster_valid, edge;
  /* GL_POINT_BIT, GL_LINE_BIT */
  GLfloat point_size, line_width;
  /* OpenGL 1.4: the point parameters. */
  GLfloat point_min, point_max, point_fade, point_atten[3];
  GLint line_repeat;
  GLushort line_pattern;
  /* GL_POLYGON_BIT */
  GLenum cull_face, front_face, polygon_mode[2];
  GLfloat offset_factor, offset_units;
  /* GL_POLYGON_STIPPLE_BIT */
  uint32_t stipple[32];
  /* GL_PIXEL_MODE_BIT */
  GLfloat zoom_x, zoom_y;
  GLfloat red_scale, red_bias, green_scale, green_bias;
  GLfloat blue_scale, blue_bias, alpha_scale, alpha_bias;
  GLfloat depth_scale, depth_bias;
  GLint index_shift, index_offset;
  GLboolean map_color, map_stencil;
  GLenum read_buffer;
  /* GL_LIGHTING_BIT */
  sgl_light light[SGL_MAX_LIGHTS];
  sgl_material material[2];
  GLfloat model_ambient[4];
  GLboolean local_viewer, two_side;
  GLenum color_control;    /* GL_SINGLE_COLOR or GL_SEPARATE_SPECULAR_COLOR */
  GLenum shade_model, color_material_face, color_material_mode;
  /* GL_FOG_BIT */
  GLenum fog_mode;
  GLfloat fog_color[4], fog_density, fog_start, fog_end, fog_index;
  GLenum fog_source;       /* GL_FOG_COORDINATE or GL_FRAGMENT_DEPTH */
  /* GL_DEPTH_BUFFER_BIT */
  GLenum depth_func;
  GLboolean depth_mask;
  GLclampd clear_depth;
  /* GL_COLOR_BUFFER_BIT */
  GLenum alpha_func, blend_src, blend_dst, logic_op, draw_buffer;
  GLclampf alpha_ref;
  /* OpenGL 1.4: the alpha factors (blend_src and blend_dst are those of
   * RGB), the constant color and the equation. */
  GLenum blend_src_alpha, blend_dst_alpha, blend_eq;
  GLfloat blend_color[4];
  GLfloat clear_color[4], clear_index;
  GLboolean color_mask[4];
  GLuint index_mask;
  /* GL_STENCIL_BUFFER_BIT */
  GLenum stencil_func, stencil_fail, stencil_zfail, stencil_zpass;
  GLint stencil_ref, stencil_clear;
  GLuint stencil_mask, stencil_writemask;
  /* GL_VIEWPORT_BIT */
  GLint viewport[4];
  GLclampd depth_near, depth_far;
  /* GL_TRANSFORM_BIT */
  GLenum matrix_mode;
  GLdouble clip_plane[SGL_MAX_CLIP_PLANES][4]; /* in eye coordinates */
  /* GL_TEXTURE_BIT */
  sgl_unit_attrib unit[SGL_MAX_TEXTURE_UNITS];
  int active_texture;      /* the unit of glActiveTexture, from 0 */
  /* GL_HINT_BIT */
  GLenum hint_perspective, hint_point, hint_line, hint_polygon, hint_fog;
  GLenum hint_compression; /* GL_TEXTURE_COMPRESSION_HINT */
  GLenum hint_mipmap;      /* GL_GENERATE_MIPMAP_HINT */
  /* GL_MULTISAMPLE_BIT: there is no multisample buffer, so these have
   * no effect. */
  GLfloat sample_coverage;
  GLboolean sample_invert;
  /* GL_SCISSOR_BIT */
  GLint scissor[4];
  /* GL_EVAL_BIT */
  GLint grid1_n, grid2_un, grid2_vn;
  GLfloat grid1_u1, grid1_u2, grid2_u1, grid2_u2, grid2_v1, grid2_v2;
  /* GL_LIST_BIT */
  GLuint list_base;
  /* GL_ACCUM_BUFFER_BIT */
  GLfloat clear_accum[4];
} sgl_attrib;

/* A vertex array. */
typedef struct sgl_array {
  GLboolean enabled;
  GLint size;
  GLenum type;
  GLsizei stride;
  const void *pointer;
} sgl_array;

/* The pixel store of packing or unpacking. */
typedef struct sgl_pixelstore {
  GLboolean swap_bytes, lsb_first;
  GLint row_length, skip_rows, skip_pixels, alignment;
  GLint image_height, skip_images; /* OpenGL 1.2: for the 3D images */
} sgl_pixelstore;

/* The state of the client: arrays and pixel store, which
 * glPushClientAttrib saves. */
typedef struct sgl_client {
  GLbitfield mask;
  sgl_array vertex, normal, color, index, edge;
  sgl_array color2, fog;   /* OpenGL 1.4: the secondary color and fog coordinate */
  sgl_array tex[SGL_MAX_TEXTURE_UNITS];
  int client_active;       /* the unit of glClientActiveTexture, from 0 */
  sgl_pixelstore pack, unpack;
} sgl_client;

/* A rendering context. */
struct sgl_context {
  /* The buffer that it draws into (sgl_set_buffer). */
  uint32_t *color;
  int index_mode;          /* color index mode: the pixels are indices */
  int index_bits;          /* the bits of an index in color index mode */
  uint32_t index_max;      /* (1 << index_bits) - 1 */
  long rowpixels;          /* pixels from one row to the next */
  int width, height;
  sgl_depth_t *depth;
  uint8_t *stencil;
  GLfloat *accum;
  size_t depth_size;

  sgl_shared *shared;
  const struct sgl_dispatch *table; /* the table for execution or saving */
  GLenum error;

  sgl_attrib a;            /* the attribute state */
  sgl_attrib attrib_stack[SGL_ATTRIB_DEPTH];
  int attrib_depth;
  sgl_client c;
  sgl_client client_stack[SGL_ATTRIB_DEPTH];
  int client_depth;

  /* The matrices. */
  sgl_stack modelview, projection;
  sgl_stack texture[SGL_MAX_TEXTURE_UNITS];
  sgl_stack *stack;        /* the stack of the matrix mode */
  sgl_matrix mvp;          /* projection * modelview */
  sgl_matrix inv_modelview;/* for the normals and the eye planes */
  int mvp_dirty, inv_dirty;
  int texture_identity[SGL_MAX_TEXTURE_UNITS]; /* the texture matrix is the identity */

  /* glBegin and glEnd. */
  GLenum prim;             /* the primitive, or SGL_NO_PRIM outside glBegin */
  sgl_vertex *verts;       /* the vertices of the primitive */
  int nverts, maxverts;
  int lit;                 /* lighting is on for this primitive */
  int need_eye;            /* the vertices need eye coordinates */
  int need_normal;         /* the vertices need eye normals (texgen) */
  int clip_user;           /* the user clip planes that are on (bits) */
  unsigned tex_units;      /* the texture units that are on (bits) */
  unsigned tex_coords;     /* the units whose coordinates the vertices have */
  int sec_on;              /* the vertices have a secondary color to add */
  int point_atten;         /* the point size changes with the distance */
  const GLfloat *flat_spec;/* the secondary color of flat shading */
  sgl_vertex *clipverts;   /* the vertices that clipping makes */
  int nclip, maxclip;

  /* The lighting, computed again when a light or material changes. */
  int light_dirty;
  int nlights;
  int light_index[SGL_MAX_LIGHTS];
  GLfloat light_amb[2][SGL_MAX_LIGHTS][3];  /* light * material, by side */
  GLfloat light_diff[2][SGL_MAX_LIGHTS][3];
  GLfloat light_spec[2][SGL_MAX_LIGHTS][3];
  GLfloat base_color[2][4];                 /* emission + model ambient */
  GLfloat light_ci[SGL_MAX_LIGHTS][2];      /* color index mode: diffuse and specular weights */

  /* Display lists. */
  sgl_list *compiling;     /* the list that glNewList started, or NULL */
  GLenum list_mode;
  int call_depth;

  /* Textures. */
  sgl_texture default_1d, default_2d, default_3d, default_cube;
  sgl_texture *tex_1d[SGL_MAX_TEXTURE_UNITS], *tex_2d[SGL_MAX_TEXTURE_UNITS];
  sgl_texture *tex_3d[SGL_MAX_TEXTURE_UNITS], *tex_cube[SGL_MAX_TEXTURE_UNITS];

  /* Evaluators. */
  sgl_map map1[SGL_MAP_TYPES], map2[SGL_MAP_TYPES];

  /* Selection and feedback (select.c). */
  GLenum render_mode;
  GLuint *select_buffer;
  GLsizei select_size;
  GLsizei select_count;    /* the values that the hits need */
  GLint hits;              /* the hit records */
  int hit;                 /* a primitive hit since the last record */
  GLfloat hit_min, hit_max;/* the window z range of the hit */
  GLuint names[SGL_NAME_DEPTH];
  int name_depth;
  GLfloat *feedback_buffer;
  GLsizei feedback_size;
  GLsizei feedback_count;  /* the values that the feedback needs */
  GLenum feedback_type;
  int line_reset;          /* the next line starts a new stipple */

  /* Pixel maps (only stored). */
  GLfloat *pixel_map[10];
  GLint pixel_map_size[10];

  /* The paletted texture of GL_PROXY_TEXTURE_2D. */
  int proxy_palette_size;
};

/* The current context and the current table of this thread. */
extern SGL_THREAD sgl_context *sgl_ctx;
extern SGL_THREAD const struct sgl_dispatch *sgl_cur;
#define SGL_DISPATCH sgl_cur

/* context.c */
void sgl_error(GLenum e);
int sgl_pname_count(GLenum pname);
int sgl_pname_is_color(GLenum pname);
int sgl_cap_index(GLenum cap);
#define SGL_ON(c, cap) ((c)->a.enabled[cap])

/* matrix.c */
void sgl_matrix_identity(sgl_matrix *m);
void sgl_matrix_mul(sgl_matrix *out, const sgl_matrix *a, const sgl_matrix *b);
int sgl_matrix_invert(sgl_matrix *out, const sgl_matrix *m);
void sgl_matrix_apply(const sgl_matrix *m, const GLfloat in[4], GLfloat out[4]);
void sgl_stack_init(sgl_stack *s, int max);
void sgl_stack_free(sgl_stack *s);
void sgl_update_matrices(sgl_context *c);
void sgl_update_inverse(sgl_context *c);

/* vertex.c */
void sgl_vertex_init(sgl_context *c);
void sgl_light_changed(sgl_light *l);
void sgl_material_changed(sgl_material *m);
void sgl_transform_vertex(sgl_context *c, const GLfloat obj[4], sgl_vertex *v);
void sgl_draw_polygon(sgl_context *c, sgl_vertex **v, int n, int provoking);
void sgl_draw_line(sgl_context *c, sgl_vertex *a, sgl_vertex *b, int provoking);
void sgl_draw_point(sgl_context *c, sgl_vertex *a);
void sgl_compute_window(sgl_context *c, sgl_vertex *v);

/* raster.c */
void sgl_raster_triangle(sgl_context *c, const sgl_vertex *a, const sgl_vertex *b,
                         const sgl_vertex *d, int side, const GLfloat *flat, GLfloat zoffset);
void sgl_raster_line(sgl_context *c, const sgl_vertex *a, const sgl_vertex *b, int side,
                     const GLfloat *flat, GLfloat zoffset);
void sgl_raster_point(sgl_context *c, const sgl_vertex *a, int side, GLfloat zoffset);
void sgl_write_fragment(sgl_context *c, int x, int y, uint32_t z, const GLfloat rgba[4]);
void sgl_clip_rect(sgl_context *c, int *x0, int *y0, int *x1, int *y1);

/* texture.c */
void sgl_texture_init(sgl_context *c);
void sgl_texture_free_all(sgl_shared *s);
void sgl_texture_release(sgl_context *c);
sgl_texture *sgl_unit_texture(sgl_context *c, int u);
unsigned sgl_texture_units(const sgl_context *c);
const sgl_level *sgl_base_level(const sgl_texture *t);
sgl_level *sgl_face_levels(sgl_texture *t, int face);

/* pixels.c */
size_t sgl_image_bytes(const sgl_pixelstore *ps, int w, int h, GLenum format, GLenum type);
int sgl_unpack_rgba8(const sgl_pixelstore *ps, int w, int h, GLenum format, GLenum type,
                     const void *pixels, uint8_t *out);
void sgl_pack_texels(sgl_context *c, int w, int h, const uint32_t *texels, GLenum format,
                     GLenum type, void *dst);
void sgl_unpack_index(const sgl_pixelstore *ps, int w, int h, GLenum type,
                      const void *pixels, uint8_t *out);
int sgl_format_components(GLenum format);
int sgl_unpack_depth(sgl_context *c, const sgl_pixelstore *ps, int w, int h, GLenum type,
                     const void *pixels, uint32_t *out, uint32_t max);
int sgl_need_stencil(sgl_context *c);
void sgl_read_depth(sgl_context *c, int x, int y, int w, int h, int32_t *out);
extern const sgl_pixelstore sgl_tight_store;

/* list.c */
sgl_cell *sgl_list_add(int op, int count);
void *sgl_list_copy(const void *p, size_t bytes);
int sgl_list_executes(void);
void sgl_list_free_all(sgl_shared *s);
sgl_node *sgl_list_add_hand(void (*play)(const sgl_cell *), int count, void *data);

/* select.c */
void sgl_select_hit(sgl_context *c, GLfloat z);
void sgl_feedback_token(sgl_context *c, GLfloat token);
void sgl_feedback_vertex(sgl_context *c, const sgl_vertex *v, const GLfloat *color);
void sgl_feedback_polygon(sgl_context *c, sgl_vertex **v, int n, int side, const GLfloat *flat,
                          GLenum mode);
void sgl_feedback_line(sgl_context *c, const sgl_vertex *a, const sgl_vertex *b, const GLfloat *flat);
void sgl_feedback_raster(sgl_context *c, GLfloat token);

/* eval.c */
void sgl_eval_init(sgl_context *c);
void sgl_eval_free(sgl_context *c);
int sgl_map_k(GLenum target);

/* extension.c: the extension procedures by name. */
extern const struct sgl_proc sgl_extension_procs[];

/* Gives the enum of a float parameter, or 0 when it is negative or too
 * large: the conversion of such a float to GLenum is undefined. */
static inline GLenum sgl_enum(GLfloat f) { return f >= 0.0f && f < 4294967040.0f ? (GLenum)f : 0; }

/* Clamps a float to [0, 1]. */
static inline GLfloat sgl_clamp01(GLfloat x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

#endif
