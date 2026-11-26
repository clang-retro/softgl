/* select.c - selection and feedback of the software OpenGL.
 *
 * In selection mode (GL_SELECT), a primitive that is not clipped away
 * and not culled sets the hit flag and the range of its window z. A
 * change of the name stack, or the end of selection mode, writes a hit
 * record: the number of names, the minimum and maximum z (as unsigned
 * integers, 0 to 2^32 - 1) and the names.
 *
 * In feedback mode (GL_FEEDBACK), each primitive writes a token and its
 * vertices after clipping, in window coordinates. The type of the
 * feedback buffer tells which values each vertex has.
 *
 * Nothing is drawn in these two modes.
 *
 * Part of clang-retro softgl, a software implementation of OpenGL.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include "sgl.h"

/* Gives the current context, and records GL_INVALID_OPERATION when the
 * call is between glBegin and glEnd. Gives NULL then. */
static sgl_context *outside(void) {
  sgl_context *c = sgl_ctx;
  if (c->prim != SGL_NO_PRIM) {
    sgl_error(GL_INVALID_OPERATION);
    return NULL;
  }
  return c;
}

/* Gives a window z (0 to 1) as an unsigned integer of a hit record. */
static GLuint hit_z(GLfloat z) {
  if (z <= 0.0f) return 0;
  if (z >= 1.0f) return 0xFFFFFFFFu;
  return (GLuint)((double)z * 4294967295.0);
}

/* Puts one value in the selection buffer. A value that does not fit is
 * counted, so that glRenderMode can tell the overflow. */
static void select_put(sgl_context *c, GLuint v) {
  if (c->select_count < c->select_size) c->select_buffer[c->select_count] = v;
  c->select_count++;
}

/* Writes the hit record of the primitives since the last record, if
 * there was a hit. */
static void write_hit(sgl_context *c) {
  int i;
  if (!c->hit) return;
  select_put(c, (GLuint)c->name_depth);
  select_put(c, hit_z(c->hit_min));
  select_put(c, hit_z(c->hit_max));
  for (i = 0; i < c->name_depth; i++) select_put(c, c->names[i]);
  c->hits++;
  c->hit = 0;
  c->hit_min = 1.0f;
  c->hit_max = 0.0f;
}

/* Records a hit at window z (0 to 1) in selection mode. */
void sgl_select_hit(sgl_context *c, GLfloat z) {
  if (z < c->hit_min) c->hit_min = z;
  if (z > c->hit_max) c->hit_max = z;
  c->hit = 1;
}

/* Puts one value in the feedback buffer. A value that does not fit is
 * counted, so that glRenderMode can tell the overflow. */
void sgl_feedback_token(sgl_context *c, GLfloat token) {
  if (c->feedback_count < c->feedback_size) c->feedback_buffer[c->feedback_count] = token;
  c->feedback_count++;
}

/* Puts a vertex in the feedback buffer, with the values that the type
 * of the buffer tells. color is the color of the vertex (four values in
 * RGBA mode, one index in color index mode). */
void sgl_feedback_vertex(sgl_context *c, const sgl_vertex *v, const GLfloat *color) {
  GLenum type = c->feedback_type;
  int i;
  sgl_feedback_token(c, v->win[0]);
  sgl_feedback_token(c, v->win[1]);
  if (type != GL_2D) sgl_feedback_token(c, v->win[2]);
  if (type == GL_4D_COLOR_TEXTURE) sgl_feedback_token(c, v->clip[3]);
  if (type == GL_3D_COLOR || type == GL_3D_COLOR_TEXTURE || type == GL_4D_COLOR_TEXTURE) {
    if (c->index_mode)
      sgl_feedback_token(c, color[0]);
    else
      for (i = 0; i < 4; i++) sgl_feedback_token(c, color[i]);
  }
  if (type == GL_3D_COLOR_TEXTURE || type == GL_4D_COLOR_TEXTURE)
    for (i = 0; i < 4; i++) sgl_feedback_token(c, v->tex[0][i]);
}

/* Records a polygon (after clipping and culling) in selection or
 * feedback mode. mode is the polygon mode of its side. */
void sgl_feedback_polygon(sgl_context *c, sgl_vertex **v, int n, int side, const GLfloat *flat,
                          GLenum mode) {
  int i;
  if (c->render_mode == GL_SELECT) {
    for (i = 0; i < n; i++) sgl_select_hit(c, v[i]->win[2]);
    return;
  }
  if (mode == GL_FILL) {
    sgl_feedback_token(c, (GLfloat)GL_POLYGON_TOKEN);
    sgl_feedback_token(c, (GLfloat)n);
    for (i = 0; i < n; i++) sgl_feedback_vertex(c, v[i], flat ? flat : v[i]->color[side]);
  } else if (mode == GL_LINE) {
    c->line_reset = 1;
    for (i = 0; i < n; i++) {
      const sgl_vertex *a = v[i], *b = v[(i + 1) % n];
      if (!a->edge) continue;
      sgl_feedback_token(c, (GLfloat)(c->line_reset ? GL_LINE_RESET_TOKEN : GL_LINE_TOKEN));
      c->line_reset = 0;
      sgl_feedback_vertex(c, a, flat ? flat : a->color[side]);
      sgl_feedback_vertex(c, b, flat ? flat : b->color[side]);
    }
  } else {
    for (i = 0; i < n; i++) {
      if (!v[i]->edge) continue;
      sgl_feedback_token(c, (GLfloat)GL_POINT_TOKEN);
      sgl_feedback_vertex(c, v[i], v[i]->color[side]);
    }
  }
}

/* Records a line (after clipping) in selection or feedback mode. */
void sgl_feedback_line(sgl_context *c, const sgl_vertex *a, const sgl_vertex *b, const GLfloat *flat) {
  if (c->render_mode == GL_SELECT) {
    sgl_select_hit(c, a->win[2]);
    sgl_select_hit(c, b->win[2]);
    return;
  }
  sgl_feedback_token(c, (GLfloat)(c->line_reset ? GL_LINE_RESET_TOKEN : GL_LINE_TOKEN));
  c->line_reset = 0;
  sgl_feedback_vertex(c, a, flat ? flat : a->color[0]);
  sgl_feedback_vertex(c, b, flat ? flat : b->color[0]);
}

/* Records glBitmap, glDrawPixels or glCopyPixels (token) at the current
 * raster position, in selection or feedback mode. */
void sgl_feedback_raster(sgl_context *c, GLfloat token) {
  sgl_vertex v;
  int i;
  if (!c->a.raster_valid) return;
  if (c->render_mode == GL_SELECT) {
    sgl_select_hit(c, c->a.raster_pos[2]);
    return;
  }
  for (i = 0; i < 3; i++) v.win[i] = c->a.raster_pos[i];
  v.clip[3] = c->a.raster_pos[3];
  for (i = 0; i < 4; i++) v.tex[0][i] = c->a.raster_tex[0][i];
  sgl_feedback_token(c, token);
  sgl_feedback_vertex(c, &v, c->index_mode ? &c->a.raster_index : c->a.raster_color);
}

/* glRenderMode: sets render, selection or feedback mode. Gives the
 * number of hit records (selection) or values (feedback) of the mode
 * that ends, or -1 when they did not fit in the buffer. */
GLint APIENTRY sgl_RenderMode(GLenum mode) {
  sgl_context *c = outside();
  GLint result = 0;
  if (!c) return 0;
  if (mode != GL_RENDER && mode != GL_SELECT && mode != GL_FEEDBACK) {
    sgl_error(GL_INVALID_ENUM);
    return 0;
  }
  if (mode == GL_SELECT && !c->select_buffer) {
    sgl_error(GL_INVALID_OPERATION);
    return 0;
  }
  if (mode == GL_FEEDBACK && !c->feedback_buffer) {
    sgl_error(GL_INVALID_OPERATION);
    return 0;
  }
  if (c->render_mode == GL_SELECT) {
    write_hit(c);
    result = c->select_count > c->select_size ? -1 : c->hits;
  } else if (c->render_mode == GL_FEEDBACK) {
    result = c->feedback_count > c->feedback_size ? -1 : c->feedback_count;
  }
  c->render_mode = mode;
  c->select_count = 0;
  c->hits = 0;
  c->hit = 0;
  c->hit_min = 1.0f;
  c->hit_max = 0.0f;
  c->feedback_count = 0;
  if (mode == GL_SELECT) c->name_depth = 0;
  return result;
}

/* glSelectBuffer: sets the buffer of the hit records. */
void APIENTRY sgl_SelectBuffer(GLsizei size, GLuint *buffer) {
  sgl_context *c = outside();
  if (!c) return;
  if (size < 0) {
    sgl_error(GL_INVALID_VALUE);
    return;
  }
  if (c->render_mode == GL_SELECT) {
    sgl_error(GL_INVALID_OPERATION);
    return;
  }
  c->select_buffer = buffer;
  c->select_size = size;
}

/* glFeedbackBuffer: sets the buffer of the feedback values and the
 * values of each vertex. */
void APIENTRY sgl_FeedbackBuffer(GLsizei size, GLenum type, GLfloat *buffer) {
  sgl_context *c = outside();
  if (!c) return;
  if (type != GL_2D && type != GL_3D && type != GL_3D_COLOR && type != GL_3D_COLOR_TEXTURE &&
      type != GL_4D_COLOR_TEXTURE) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  if (size < 0) {
    sgl_error(GL_INVALID_VALUE);
    return;
  }
  if (c->render_mode == GL_FEEDBACK) {
    sgl_error(GL_INVALID_OPERATION);
    return;
  }
  c->feedback_buffer = buffer;
  c->feedback_size = size;
  c->feedback_type = type;
}

/* glInitNames: empties the name stack. It has an effect in selection
 * mode only. */
void APIENTRY sgl_InitNames(void) {
  sgl_context *c = outside();
  if (!c || c->render_mode != GL_SELECT) return;
  write_hit(c);
  c->name_depth = 0;
}

/* glLoadName: replaces the top of the name stack (selection mode). */
void APIENTRY sgl_LoadName(GLuint name) {
  sgl_context *c = outside();
  if (!c || c->render_mode != GL_SELECT) return;
  if (c->name_depth == 0) {
    sgl_error(GL_INVALID_OPERATION);
    return;
  }
  write_hit(c);
  c->names[c->name_depth - 1] = name;
}

/* glPushName: pushes a name on the name stack (selection mode). */
void APIENTRY sgl_PushName(GLuint name) {
  sgl_context *c = outside();
  if (!c || c->render_mode != GL_SELECT) return;
  if (c->name_depth >= SGL_NAME_DEPTH) {
    sgl_error(GL_STACK_OVERFLOW);
    return;
  }
  write_hit(c);
  c->names[c->name_depth++] = name;
}

/* glPopName: pops a name off the name stack (selection mode). */
void APIENTRY sgl_PopName(void) {
  sgl_context *c = outside();
  if (!c || c->render_mode != GL_SELECT) return;
  if (c->name_depth == 0) {
    sgl_error(GL_STACK_UNDERFLOW);
    return;
  }
  write_hit(c);
  c->name_depth--;
}

/* glPassThrough: puts a marker in the feedback buffer (feedback mode). */
void APIENTRY sgl_PassThrough(GLfloat token) {
  sgl_context *c = outside();
  if (!c || c->render_mode != GL_FEEDBACK) return;
  sgl_feedback_token(c, (GLfloat)GL_PASS_THROUGH_TOKEN);
  sgl_feedback_token(c, token);
}
