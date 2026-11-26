/* buffer.c - the buffer objects and the occlusion queries of the
 * software OpenGL (OpenGL 1.5, GL_ARB_vertex_buffer_object and
 * GL_ARB_occlusion_query).
 *
 * A buffer object is memory that the vertex arrays and glDrawElements
 * read. The renderer draws from memory, thus a buffer is a block of
 * memory from malloc, and glMapBuffer gives its address.
 *
 * Part of clang-retro softgl, a software implementation of OpenGL.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include <stdlib.h>

#include "sgl.h"

/* Lets go of a reference to buffer b (or NULL), and frees it after the
 * last one. */
void sgl_buffer_release(sgl_buffer *b) {
  if (!b || --b->refs > 0) return;
  free(b->data);
  free(b);
}

/* Sets the binding *slot to buffer b (or NULL), with the references. */
void sgl_buffer_set(sgl_buffer **slot, sgl_buffer *b) {
  if (*slot == b) return;
  if (b) b->refs++;
  sgl_buffer_release(*slot);
  *slot = b;
}

/* Gives the arrays of a client state, for the loops over them. */
static int client_arrays(sgl_client *cl, sgl_array **out) {
  int n = 0, u;
  out[n++] = &cl->vertex;
  out[n++] = &cl->normal;
  out[n++] = &cl->color;
  out[n++] = &cl->index;
  out[n++] = &cl->edge;
  out[n++] = &cl->color2;
  out[n++] = &cl->fog;
  for (u = 0; u < SGL_MAX_TEXTURE_UNITS; u++) out[n++] = &cl->tex[u];
  return n;
}

/* Adds delta (1 or -1) to the references of all the buffers that the
 * vertex array state of cl binds: glPushClientAttrib copies them, and
 * glPopClientAttrib and the end of a context let go of them. */
void sgl_client_refs(sgl_client *cl, int delta) {
  sgl_array *arr[7 + SGL_MAX_TEXTURE_UNITS];
  sgl_buffer *b[2 + 7 + SGL_MAX_TEXTURE_UNITS];
  int i, n = client_arrays(cl, arr), m = 0;
  b[m++] = cl->array_buffer;
  b[m++] = cl->element_buffer;
  for (i = 0; i < n; i++) b[m++] = arr[i]->buf;
  for (i = 0; i < m; i++) {
    if (!b[i]) continue;
    if (delta > 0)
      b[i]->refs++;
    else
      sgl_buffer_release(b[i]);
  }
}

/* Frees the name table of the buffers of a set of shared objects. */
void sgl_buffer_free_all(sgl_shared *s) {
  int i;
  for (i = 0; i < 64; i++) {
    sgl_buffer *b = s->buffers[i];
    while (b) {
      sgl_buffer *next = b->next;
      sgl_buffer_release(b);
      b = next;
    }
    s->buffers[i] = NULL;
  }
}

/* Gives the address of element 0 of array a: the pointer, or the
 * offset in the buffer of the array. Gives NULL for an empty buffer. */
const uint8_t *sgl_array_base(const sgl_array *a) {
  if (!a->buf) return (const uint8_t *)a->pointer;
  if (!a->buf->data) return NULL;
  return a->buf->data + (size_t)(uintptr_t)a->pointer;
}

/* Gives the buffer of a name, or NULL. */
static sgl_buffer *find_buffer(sgl_shared *s, GLuint name) {
  sgl_buffer *b;
  for (b = s->buffers[name & 63]; b; b = b->next)
    if (b->name == name) return b;
  return NULL;
}

/* Gives the binding of a buffer target, or NULL for an invalid one. */
static sgl_buffer **binding(sgl_context *c, GLenum target) {
  if (target == GL_ARRAY_BUFFER) return &c->c.array_buffer;
  if (target == GL_ELEMENT_ARRAY_BUFFER) return &c->c.element_buffer;
  return NULL;
}

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

/* Gives the buffer bound to target. Records GL_INVALID_ENUM for an
 * invalid target and GL_INVALID_OPERATION when no buffer is bound. */
static sgl_buffer *bound_buffer(GLenum target) {
  sgl_context *c = outside();
  sgl_buffer **slot;
  if (!c) return NULL;
  slot = binding(c, target);
  if (!slot) {
    sgl_error(GL_INVALID_ENUM);
    return NULL;
  }
  if (!*slot) sgl_error(GL_INVALID_OPERATION);
  return *slot;
}

/* glGenBuffers: gives n names that no buffer has. */
void APIENTRY sgl_GenBuffers(GLsizei n, GLuint *buffers) {
  sgl_context *c = outside();
  GLsizei i;
  if (!c) return;
  if (n < 0) {
    sgl_error(GL_INVALID_VALUE);
    return;
  }
  for (i = 0; i < n; i++) {
    GLuint name;
    do {
      name = c->shared->next_buffer++;
    } while (name == 0 || find_buffer(c->shared, name));
    buffers[i] = name;
  }
}

/* glBindBuffer: binds the buffer of a name to target. A name that has
 * no buffer gets a new, empty buffer. Name 0 binds no buffer. */
void APIENTRY sgl_BindBuffer(GLenum target, GLuint name) {
  sgl_context *c = outside();
  sgl_buffer **slot, *b = NULL;
  if (!c) return;
  slot = binding(c, target);
  if (!slot) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  if (name) {
    b = find_buffer(c->shared, name);
    if (!b) {
      b = (sgl_buffer *)calloc(1, sizeof *b);
      if (!b) {
        sgl_error(GL_OUT_OF_MEMORY);
        return;
      }
      b->name = name;
      b->refs = 1; /* the reference of the name table */
      b->usage = GL_STATIC_DRAW;
      b->access = GL_READ_WRITE;
      b->next = c->shared->buffers[name & 63];
      c->shared->buffers[name & 63] = b;
    }
  }
  sgl_buffer_set(slot, b);
}

/* glDeleteBuffers: deletes buffers. The bindings of the current context
 * to them become 0. */
void APIENTRY sgl_DeleteBuffers(GLsizei n, const GLuint *buffers) {
  sgl_context *c = outside();
  GLsizei i;
  if (!c) return;
  if (n < 0) {
    sgl_error(GL_INVALID_VALUE);
    return;
  }
  for (i = 0; i < n; i++) {
    sgl_buffer *b = buffers[i] ? find_buffer(c->shared, buffers[i]) : NULL, **pp;
    sgl_array *arr[7 + SGL_MAX_TEXTURE_UNITS];
    int k, m;
    if (!b) continue;
    if (c->c.array_buffer == b) sgl_buffer_set(&c->c.array_buffer, NULL);
    if (c->c.element_buffer == b) sgl_buffer_set(&c->c.element_buffer, NULL);
    m = client_arrays(&c->c, arr);
    for (k = 0; k < m; k++)
      if (arr[k]->buf == b) sgl_buffer_set(&arr[k]->buf, NULL);
    for (pp = &c->shared->buffers[b->name & 63]; *pp; pp = &(*pp)->next)
      if (*pp == b) {
        *pp = b->next;
        break;
      }
    b->next = NULL;
    sgl_buffer_release(b);
  }
}

/* glIsBuffer: tells if a name has a buffer. */
GLboolean APIENTRY sgl_IsBuffer(GLuint name) {
  sgl_context *c = outside();
  if (!c || !name) return GL_FALSE;
  return find_buffer(c->shared, name) ? GL_TRUE : GL_FALSE;
}

/* Tells if usage is a usage of glBufferData. */
static int valid_usage(GLenum usage) {
  switch (usage) {
    case GL_STREAM_DRAW: case GL_STREAM_READ: case GL_STREAM_COPY:
    case GL_STATIC_DRAW: case GL_STATIC_READ: case GL_STATIC_COPY:
    case GL_DYNAMIC_DRAW: case GL_DYNAMIC_READ: case GL_DYNAMIC_COPY:
      return 1;
    default:
      return 0;
  }
}

/* glBufferData: makes the memory of the buffer bound to target size
 * bytes, and copies data into it when data is not NULL. */
void APIENTRY sgl_BufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage) {
  sgl_buffer *b;
  uint8_t *p = NULL;
  if (size < 0) {
    sgl_error(GL_INVALID_VALUE);
    return;
  }
  if (!valid_usage(usage)) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  b = bound_buffer(target);
  if (!b) return;
  if (size > 0) {
    p = (uint8_t *)malloc((size_t)size);
    if (!p) {
      sgl_error(GL_OUT_OF_MEMORY);
      return;
    }
    if (data) memcpy(p, data, (size_t)size);
  }
  free(b->data);
  b->data = p;
  b->size = (size_t)size;
  b->usage = usage;
  b->mapped = GL_FALSE;
  b->access = GL_READ_WRITE;
}

/* Gives the buffer bound to target when [offset, offset + size) is in
 * it and it is not mapped, or NULL with the error. */
static sgl_buffer *range_buffer(GLenum target, GLintptr offset, GLsizeiptr size) {
  sgl_buffer *b = bound_buffer(target);
  if (!b) return NULL;
  if (offset < 0 || size < 0 || (size_t)offset + (size_t)size > b->size) {
    sgl_error(GL_INVALID_VALUE);
    return NULL;
  }
  if (b->mapped) {
    sgl_error(GL_INVALID_OPERATION);
    return NULL;
  }
  return b;
}

/* glBufferSubData: copies size bytes of data into the buffer bound to
 * target, at offset. */
void APIENTRY sgl_BufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void *data) {
  sgl_buffer *b = range_buffer(target, offset, size);
  if (b && size > 0) memcpy(b->data + offset, data, (size_t)size);
}

/* glGetBufferSubData: copies size bytes of the buffer bound to target,
 * at offset, into data. */
void APIENTRY sgl_GetBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, void *data) {
  sgl_buffer *b = range_buffer(target, offset, size);
  if (b && size > 0) memcpy(data, b->data + offset, (size_t)size);
}

/* glMapBuffer: gives the address of the memory of the buffer bound to
 * target. The renderer does not check that the program obeys access. */
void *APIENTRY sgl_MapBuffer(GLenum target, GLenum access) {
  sgl_buffer *b;
  if (access != GL_READ_ONLY && access != GL_WRITE_ONLY && access != GL_READ_WRITE) {
    sgl_error(GL_INVALID_ENUM);
    return NULL;
  }
  b = bound_buffer(target);
  if (!b) return NULL;
  if (b->mapped) {
    sgl_error(GL_INVALID_OPERATION);
    return NULL;
  }
  b->mapped = GL_TRUE;
  b->access = access;
  return b->data;
}

/* glUnmapBuffer: ends the map of the buffer bound to target. The data
 * cannot become bad, thus it gives GL_TRUE. */
GLboolean APIENTRY sgl_UnmapBuffer(GLenum target) {
  sgl_buffer *b = bound_buffer(target);
  if (!b) return GL_FALSE;
  if (!b->mapped) {
    sgl_error(GL_INVALID_OPERATION);
    return GL_FALSE;
  }
  b->mapped = GL_FALSE;
  return GL_TRUE;
}

/* glGetBufferParameteriv: gives the size, the usage, the access or the
 * map state of the buffer bound to target. */
void APIENTRY sgl_GetBufferParameteriv(GLenum target, GLenum pname, GLint *params) {
  sgl_buffer *b = bound_buffer(target);
  if (!b) return;
  switch (pname) {
    case GL_BUFFER_SIZE: params[0] = (GLint)b->size; break;
    case GL_BUFFER_USAGE: params[0] = (GLint)b->usage; break;
    case GL_BUFFER_ACCESS: params[0] = (GLint)b->access; break;
    case GL_BUFFER_MAPPED: params[0] = b->mapped; break;
    default: sgl_error(GL_INVALID_ENUM); break;
  }
}

/* glGetBufferPointerv: gives the address of the map of the buffer bound
 * to target, or NULL when it is not mapped. */
void APIENTRY sgl_GetBufferPointerv(GLenum target, GLenum pname, void **params) {
  sgl_buffer *b = bound_buffer(target);
  if (!b) return;
  if (pname != GL_BUFFER_MAP_POINTER) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  params[0] = b->mapped ? (void *)b->data : NULL;
}

/* Gives the query of a name in context c, or NULL. */
static sgl_query *find_query(sgl_context *c, GLuint name) {
  sgl_query *q;
  for (q = c->queries[name & 15]; q; q = q->next)
    if (q->name == name) return q;
  return NULL;
}

/* Frees the queries of a context. */
void sgl_query_free_all(sgl_context *c) {
  int i;
  for (i = 0; i < 16; i++) {
    sgl_query *q = c->queries[i];
    while (q) {
      sgl_query *next = q->next;
      free(q);
      q = next;
    }
    c->queries[i] = NULL;
  }
  c->query = NULL;
}

/* glGenQueries: gives n names that no query has. */
void APIENTRY sgl_GenQueries(GLsizei n, GLuint *ids) {
  sgl_context *c = outside();
  GLsizei i;
  if (!c) return;
  if (n < 0) {
    sgl_error(GL_INVALID_VALUE);
    return;
  }
  for (i = 0; i < n; i++) {
    GLuint name;
    do {
      name = ++c->next_query;
    } while (name == 0 || find_query(c, name));
    ids[i] = name;
  }
}

/* glDeleteQueries: deletes queries. An active query ends. */
void APIENTRY sgl_DeleteQueries(GLsizei n, const GLuint *ids) {
  sgl_context *c = outside();
  GLsizei i;
  if (!c) return;
  if (n < 0) {
    sgl_error(GL_INVALID_VALUE);
    return;
  }
  for (i = 0; i < n; i++) {
    sgl_query **pp;
    if (!ids[i]) continue;
    for (pp = &c->queries[ids[i] & 15]; *pp; pp = &(*pp)->next)
      if ((*pp)->name == ids[i]) {
        sgl_query *q = *pp;
        *pp = q->next;
        if (c->query == q) c->query = NULL;
        free(q);
        break;
      }
  }
}

/* glIsQuery: tells if a name has a query. */
GLboolean APIENTRY sgl_IsQuery(GLuint id) {
  sgl_context *c = outside();
  if (!c || !id) return GL_FALSE;
  return find_query(c, id) ? GL_TRUE : GL_FALSE;
}

/* glBeginQuery: starts to count the samples that pass the depth test
 * into query id. */
void APIENTRY sgl_BeginQuery(GLenum target, GLuint id) {
  sgl_context *c = outside();
  sgl_query *q;
  if (!c) return;
  if (target != GL_SAMPLES_PASSED) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  if (c->query || id == 0) {
    sgl_error(GL_INVALID_OPERATION);
    return;
  }
  q = find_query(c, id);
  if (!q) {
    q = (sgl_query *)calloc(1, sizeof *q);
    if (!q) {
      sgl_error(GL_OUT_OF_MEMORY);
      return;
    }
    q->name = id;
    q->next = c->queries[id & 15];
    c->queries[id & 15] = q;
  }
  q->result = 0;
  c->query = q;
}

/* glEndQuery: stops the active query. Its result is available at once:
 * the renderer draws before each call returns. */
void APIENTRY sgl_EndQuery(GLenum target) {
  sgl_context *c = outside();
  if (!c) return;
  if (target != GL_SAMPLES_PASSED) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  if (!c->query) {
    sgl_error(GL_INVALID_OPERATION);
    return;
  }
  c->query = NULL;
}

/* glGetQueryiv: gives the active query or the bits of the counter. */
void APIENTRY sgl_GetQueryiv(GLenum target, GLenum pname, GLint *params) {
  sgl_context *c = outside();
  if (!c) return;
  if (target != GL_SAMPLES_PASSED) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  switch (pname) {
    case GL_CURRENT_QUERY: params[0] = c->query ? (GLint)c->query->name : 0; break;
    case GL_QUERY_COUNTER_BITS: params[0] = 32; break;
    default: sgl_error(GL_INVALID_ENUM); break;
  }
}

/* glGetQueryObjectuiv: gives the result of a query, or if it is
 * available (always). */
void APIENTRY sgl_GetQueryObjectuiv(GLuint id, GLenum pname, GLuint *params) {
  sgl_context *c = outside();
  sgl_query *q;
  if (!c) return;
  q = id ? find_query(c, id) : NULL;
  if (!q || q == c->query) {
    sgl_error(GL_INVALID_OPERATION);
    return;
  }
  switch (pname) {
    case GL_QUERY_RESULT: params[0] = q->result; break;
    case GL_QUERY_RESULT_AVAILABLE: params[0] = GL_TRUE; break;
    default: sgl_error(GL_INVALID_ENUM); break;
  }
}

/* glGetQueryObjectiv: as glGetQueryObjectuiv, as integers. */
void APIENTRY sgl_GetQueryObjectiv(GLuint id, GLenum pname, GLint *params) {
  GLuint v = 0;
  GLenum before = sgl_ctx->error;
  sgl_ctx->error = GL_NO_ERROR;
  sgl_GetQueryObjectuiv(id, pname, &v);
  if (sgl_ctx->error == GL_NO_ERROR) params[0] = v > 0x7FFFFFFFu ? 0x7FFFFFFF : (GLint)v;
  if (before != GL_NO_ERROR) sgl_ctx->error = before;
}
