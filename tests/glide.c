/* glide.c - the tests of the 3dfx Glide back end (backends/glide/
 * glide.c): a Glide table in the test records the calls and keeps a
 * frame buffer and a depth buffer in memory. The tests examine the
 * vertices, the state, the textures, the copies between softgl and the
 * card, and the clears.
 *
 * Part of clang-retro softgl, a software implementation of OpenGL.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include <stdio.h>

#include "../backends/glide/glide.c"

#define SW 320
#define SH 240

static int failures;

/* Records a failure named what when cond is 0. */
static void check(const char *what, int cond) {
  if (!cond) {
    printf("FAIL %s\n", what);
    failures++;
  }
}

/* What the Glide table of the test saw. The frame buffer and the depth
 * buffer have row 0 at the top. */
static struct {
  int inits, shutdowns, opens, closes, swaps, idles, clears, triangles, lines, points;
  int open_res, open_origin, swap_interval;
  int locks_read, locks_write;
  sgl_gr_vertex tri[3];
  unsigned long clear_color;
  unsigned clear_depth;
  int clip[4];
  int depth_mode, depth_func, depth_mask, color_mask;
  int color_combine[5], tex_combine[2][7];
  int blend[4];
  int fog_mode;
  unsigned char fog_table[GR_FOG_TABLE_SIZE];
  int downloads;
  sgl_gr_texinfo download;
  uint16_t first_texel;
  int filter[2][2], mipmap_mode[2];
  uint16_t color[SH][SW], aux[SH][SW];
  int ntmu;
} gr;

/* Records grGlideInit. */
static void f_init(void) { gr.inits++; }
/* Records grGlideShutdown. */
static void f_shutdown(void) { gr.shutdowns++; }
/* Records grSstQueryHardware: board 0 is a Voodoo2 with gr.ntmu TMUs. */
static int f_query(void *config) {
  int32_t *hw = (int32_t *)config;
  hw[0] = 1;
  hw[1] = GR_SSTTYPE_VOODOO2;
  hw[2] = 4;
  hw[3] = 2;
  hw[4] = gr.ntmu;
  return 1;
}
/* Records grSstSelect. */
static void f_select(int w) { (void)w; }
/* Records grSstWinOpen. */
static int f_open(unsigned long hwnd, int res, int refresh, int fmt, int origin, int nc, int na) {
  (void)hwnd, (void)refresh, (void)fmt, (void)nc, (void)na;
  gr.opens++;
  gr.open_res = res;
  gr.open_origin = origin;
  return 1;
}
/* Records grSstWinClose. */
static void f_close(void) { gr.closes++; }
/* Records grSstIdle. */
static void f_idle(void) { gr.idles++; }
/* Records grBufferClear. */
static void f_clear(unsigned long color, unsigned alpha, unsigned depth) {
  (void)alpha;
  gr.clears++;
  gr.clear_color = color;
  gr.clear_depth = depth;
}
/* Records grBufferSwap. */
static void f_swap(int interval) {
  gr.swaps++;
  gr.swap_interval = interval;
}
/* Records grDrawTriangle and its vertices. */
static void f_tri(const sgl_gr_vertex *a, const sgl_gr_vertex *b, const sgl_gr_vertex *c) {
  gr.triangles++;
  gr.tri[0] = *a, gr.tri[1] = *b, gr.tri[2] = *c;
}
/* Records grDrawLine. */
static void f_line(const sgl_gr_vertex *a, const sgl_gr_vertex *b) {
  (void)a, (void)b;
  gr.lines++;
}
/* Records grDrawPoint. */
static void f_point(const sgl_gr_vertex *a) {
  (void)a;
  gr.points++;
}
/* Records grDepthBufferMode. */
static void f_dmode(int m) { gr.depth_mode = m; }
/* Records grDepthBufferFunction. */
static void f_dfunc(int f) { gr.depth_func = f; }
/* Records grDepthMask. */
static void f_dmask(int m) { gr.depth_mask = m; }
/* Records grColorMask. */
static void f_cmask(int rgb, int a) {
  (void)a;
  gr.color_mask = rgb;
}
/* Records grColorCombine. */
static void f_ccombine(int f, int fac, int l, int o, int inv) {
  gr.color_combine[0] = f, gr.color_combine[1] = fac, gr.color_combine[2] = l, gr.color_combine[3] = o;
  gr.color_combine[4] = inv;
}
/* Records grAlphaCombine. */
static void f_acombine(int f, int fac, int l, int o, int inv) { (void)f, (void)fac, (void)l, (void)o, (void)inv; }
/* Records grTexCombine. */
static void f_tcombine(int tmu, int rf, int rfac, int af, int afac, int ri, int ai) {
  int *k = gr.tex_combine[tmu & 1];
  k[0] = tmu, k[1] = rf, k[2] = rfac, k[3] = af, k[4] = afac, k[5] = ri, k[6] = ai;
}
/* Records grAlphaBlendFunction. */
static void f_blend(int rs, int rd, int as, int ad) { gr.blend[0] = rs, gr.blend[1] = rd, gr.blend[2] = as, gr.blend[3] = ad; }
/* Records grAlphaTestFunction. */
static void f_afunc(int f) { (void)f; }
/* Records grAlphaTestReferenceValue. */
static void f_aref(unsigned v) { (void)v; }
/* Records grFogMode. */
static void f_fogmode(int m) { gr.fog_mode = m; }
/* Records grFogColorValue. */
static void f_fogcolor(unsigned long c) { (void)c; }
/* Records grFogTable. */
static void f_fogtable(const unsigned char *t) { memcpy(gr.fog_table, t, sizeof gr.fog_table); }
/* Records grClipWindow. */
static void f_clip(unsigned long x0, unsigned long y0, unsigned long x1, unsigned long y1) {
  gr.clip[0] = (int)x0, gr.clip[1] = (int)y0, gr.clip[2] = (int)x1, gr.clip[3] = (int)y1;
}
/* Records grCullMode. */
static void f_cull(int m) { (void)m; }
/* Records grDitherMode. */
static void f_dither(int m) { (void)m; }
/* Records grTexMinAddress. */
static unsigned long f_texmin(int tmu) { return tmu ? 0x10000ul : 0; }
/* Records grTexMaxAddress. */
static unsigned long f_texmax(int tmu) { return (void)tmu, 0x200000ul; }
/* Gives the memory of a texture: 2 bytes for each texel of all the
 * levels. */
static unsigned long f_texmem(unsigned long eo, sgl_gr_texinfo *info) {
  int lod, n = 0, aspect = info->aspect - GR_ASPECT_1x1;
  (void)eo;
  for (lod = info->large_lod; lod <= info->small_lod; lod++) {
    int big = 256 >> lod, w = aspect <= 0 ? big : big >> aspect, h = aspect >= 0 ? big : big >> -aspect;
    n += (w ? w : 1) * (h ? h : 1);
  }
  return (unsigned long)n * 2;
}
/* Records grTexDownloadMipMap and its first texel. */
static void f_download(int tmu, unsigned long start, unsigned long eo, sgl_gr_texinfo *info) {
  (void)tmu, (void)start, (void)eo;
  gr.downloads++;
  gr.download = *info;
  gr.first_texel = ((uint16_t *)info->data)[0];
}
/* Records grTexSource. */
static void f_source(int tmu, unsigned long start, unsigned long eo, sgl_gr_texinfo *info) {
  (void)tmu, (void)start, (void)eo, (void)info;
}
/* Records grTexFilterMode. */
static void f_filter(int tmu, int mn, int mg) { gr.filter[tmu & 1][0] = mn, gr.filter[tmu & 1][1] = mg; }
/* Records grTexClampMode. */
static void f_clamp(int tmu, int s, int t) { (void)tmu, (void)s, (void)t; }
/* Records grTexMipMapMode. */
static void f_mipmap(int tmu, int m, int b) {
  (void)b;
  gr.mipmap_mode[tmu & 1] = m;
}
/* Records grLfbLock: gives the buffers of the test. */
static int f_lock(int type, int buffer, int wm, int origin, int pipe, sgl_gr_lfbinfo *info) {
  (void)wm, (void)pipe;
  check("lock origin", origin == GR_ORIGIN_UPPER_LEFT);
  if (type == GR_LFB_READ_ONLY)
    gr.locks_read++;
  else
    gr.locks_write++;
  info->ptr = buffer == GR_BUFFER_AUXBUFFER ? (void *)gr.aux : (void *)gr.color;
  info->stride = SW * 2;
  return 1;
}
/* Records grLfbUnlock. */
static int f_unlock(int type, int buffer) {
  (void)type, (void)buffer;
  return 1;
}

/* The Glide table of the test. */
static sgl_glide_api api = {
    f_init,     f_shutdown, f_query,    f_select,  f_open,     f_close,    f_idle,   f_clear,
    f_swap,     f_tri,      f_line,     f_point,   f_dmode,    f_dfunc,    f_dmask,  f_cmask,
    f_ccombine, f_acombine, f_tcombine, f_blend,   f_afunc,    f_aref,     f_fogmode, f_fogcolor,
    f_fogtable, f_clip,     f_cull,     f_dither,  f_texmin,   f_texmax,   f_texmem, f_download,
    f_source,   f_filter,   f_clamp,    f_mipmap,  f_lock,     f_unlock,   NULL};

static uint32_t pixels[SW * SH];

/* Makes a context of SW x SH with the Glide back end and ntmu TMUs. */
static sgl_context *make(sgl_glide *g, int ntmu) {
  sgl_context *c = sgl_create_context(NULL);
  memset(&gr, 0, sizeof gr);
  gr.ntmu = ntmu;
  sgl_make_current(c);
  sgl_set_buffer(c, SW, SH, pixels, SW);
  memset(g, 0, sizeof *g);
  g->api = &api;
  g->width = SW;
  g->height = SH;
  check("attach", sgl_glide_attach(g, c) == 1);
  glViewport(0, 0, SW, SH);
  return c;
}

/* Draws a triangle with three colors. */
static void triangle(void) {
  glBegin(GL_TRIANGLES);
  glColor3f(1, 0, 0);
  glVertex3f(-0.5f, -0.5f, 0.0f);
  glColor3f(0, 1, 0);
  glVertex3f(0.5f, -0.5f, 0.0f);
  glColor3f(0, 0, 1);
  glVertex3f(0.0f, 0.5f, 0.0f);
  glEnd();
}

/* Tests the screen, the vertices and the state of a plain triangle. */
static void test_triangle(void) {
  sgl_glide g;
  sgl_context *c = make(&g, 1);
  float x;
  int i, ok = 1;
  check("init and open", gr.inits == 1 && gr.opens == 1 && gr.open_res == GR_RESOLUTION_320x240 &&
                             gr.open_origin == GR_ORIGIN_LOWER_LEFT);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  triangle();
  check("triangle on the card", gr.triangles == 1);
  check("depth state", gr.depth_mode == GR_DEPTHBUFFER_ZBUFFER && gr.depth_func == GR_CMP_LEQUAL && gr.depth_mask);
  check("no texture", gr.color_combine[0] == GR_COMBINE_FUNCTION_LOCAL &&
                          gr.color_combine[2] == GR_COMBINE_LOCAL_ITERATED);
  check("colors", gr.tri[0].r == 255.0f && gr.tri[0].g == 0.0f && gr.tri[1].g == 255.0f && gr.tri[2].b == 255.0f);
  /* x = -0.5 is window x 80; y = -0.5 is window y 60 (y up). */
  check("window coordinates", gr.tri[0].x == 80.0f && gr.tri[0].y == 60.0f && gr.tri[2].y == 180.0f);
  check("depth", gr.tri[0].ooz > 32700.0f && gr.tri[0].ooz < 32800.0f && gr.tri[0].oow == 1.0f);
  for (i = 0; i < 3; i++) {
    x = gr.tri[i].x * 16.0f;
    if (x != (float)(long)x) ok = 0;
  }
  check("vertices on the grid of 1/16 pixel", ok);
  /* The first card draw copies the buffers of softgl to the card. */
  check("first copy to the card", gr.locks_write == 2);
  sgl_present(c);
  check("swap", gr.swaps == 1 && gr.swap_interval == 1);
  sgl_destroy_context(c);
  check("close", gr.closes == 1 && gr.shutdowns == 1);
}

/* Tests the copies between the card and softgl: a stencil test sends
 * the triangle to softgl, which reads the frame of the card first. */
static void test_copies(void) {
  sgl_glide g;
  sgl_context *c = make(&g, 1);
  uint32_t px = 0;
  triangle();
  /* The card "drew" a red pixel at the top left. */
  gr.color[0][0] = 0xF800;
  glEnable(GL_STENCIL_TEST);
  glStencilFunc(GL_ALWAYS, 1, 0xFF);
  gr.locks_read = 0;
  triangle();
  check("stencil stays in softgl", gr.triangles == 1);
  check("read of the frame", gr.locks_read >= 1 && gr.idles >= 1);
  glReadPixels(0, SH - 1, 1, 1, GL_BGRA, GL_UNSIGNED_BYTE, &px);
  check("red pixel read back", (px & 0xFFFFFF) == 0xFF0000);
  /* softgl drew the triangle: the next card draw copies it back. */
  glDisable(GL_STENCIL_TEST);
  gr.locks_write = 0;
  gr.color[SH / 2][SW / 2] = 0;
  triangle();
  check("copy back to the card", gr.locks_write >= 1 && gr.color[SH / 2][SW / 2] != 0);
  sgl_destroy_context(c);
}

/* Tests a texture: the format, the size, the levels and the texture
 * coordinates. */
static void test_texture(void) {
  sgl_glide g;
  sgl_context *c = make(&g, 1);
  static const uint8_t texels[8 * 3] = {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255,
                                        0, 0, 0, 255, 255, 0, 0, 255, 255, 255, 0, 255};
  GLuint t;
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 4, 2, 0, GL_RGB, GL_UNSIGNED_BYTE, texels);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glEnable(GL_TEXTURE_2D);
  glBegin(GL_TRIANGLES);
  glTexCoord2f(0, 0);
  glVertex2f(-0.5f, -0.5f);
  glTexCoord2f(1, 0);
  glVertex2f(0.5f, -0.5f);
  glTexCoord2f(1, 1);
  glVertex2f(0.0f, 0.5f);
  glEnd();
  check("textured triangle on the card", gr.triangles == 1 && gr.downloads == 1);
  check("4 x 2 texture", gr.download.large_lod == 6 && gr.download.small_lod == 6 && gr.download.aspect == 2 &&
                             gr.download.format == GR_TEXFMT_RGB_565);
  check("565 texel", gr.first_texel == 0xF800);
  check("modulate", gr.color_combine[0] == GR_COMBINE_FUNCTION_SCALE_OTHER &&
                        gr.color_combine[1] == GR_COMBINE_FACTOR_LOCAL &&
                        gr.color_combine[3] == GR_COMBINE_OTHER_TEXTURE);
  check("bilinear", gr.filter[0][0] == GR_TEXTUREFILTER_BILINEAR);
  /* s 1 is 256 (the long side), t 1 is 128. */
  check("texture coordinates", gr.tri[1].tmu[0].sow == 256.0f && gr.tri[2].tmu[0].tow == 128.0f &&
                                   gr.tri[2].tmu[0].oow == 1.0f);
  /* The same texture again: no new download. */
  glBegin(GL_TRIANGLES);
  glVertex2f(-0.5f, -0.5f);
  glVertex2f(0.5f, -0.5f);
  glVertex2f(0.0f, 0.5f);
  glEnd();
  check("texture cache", gr.triangles == 2 && gr.downloads == 1);
  /* Mipmaps: all the levels go to the card. */
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
  glTexImage2D(GL_TEXTURE_2D, 1, GL_RGB, 2, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, texels);
  glTexImage2D(GL_TEXTURE_2D, 2, GL_RGB, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, texels);
  glBegin(GL_TRIANGLES);
  glVertex2f(-0.5f, -0.5f);
  glVertex2f(0.5f, -0.5f);
  glVertex2f(0.0f, 0.5f);
  glEnd();
  check("mipmaps", gr.downloads == 2 && gr.download.small_lod == GR_LOD_1 && gr.mipmap_mode[0] == GR_MIPMAP_NEAREST);
  /* GL_BLEND is not on the card. */
  glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_BLEND);
  triangle();
  check("GL_BLEND stays in softgl", gr.triangles == 3);
  sgl_destroy_context(c);
}

/* Tests two texture units on a card with two TMUs, and one TMU. */
static void test_units(void) {
  static const uint8_t white[4 * 4 * 4] = {0};
  int ntmu;
  for (ntmu = 1; ntmu <= 2; ntmu++) {
    sgl_glide g;
    sgl_context *c = make(&g, ntmu);
    GLuint t[2];
    int u;
    glGenTextures(2, t);
    for (u = 0; u < 2; u++) {
      glActiveTexture(GL_TEXTURE0 + u);
      glBindTexture(GL_TEXTURE_2D, t[u]);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glEnable(GL_TEXTURE_2D);
    }
    triangle();
    if (ntmu == 2) {
      check("two units on two TMUs", gr.triangles == 1 && gr.downloads == 2);
      check("TMU 0 multiplies TMU 1", gr.tex_combine[0][1] == GR_COMBINE_FUNCTION_SCALE_OTHER &&
                                          gr.tex_combine[0][2] == GR_COMBINE_FACTOR_LOCAL &&
                                          gr.tex_combine[1][1] == GR_COMBINE_FUNCTION_LOCAL);
      check("4444 format", gr.download.format == GR_TEXFMT_ARGB_4444);
    } else {
      check("two units on one TMU stay in softgl", gr.triangles == 0);
    }
    sgl_destroy_context(c);
  }
}

/* Tests the fog table, and fog with an orthographic projection. */
static void test_fog(void) {
  sgl_glide g;
  sgl_context *c = make(&g, 1);
  glEnable(GL_FOG);
  glFogi(GL_FOG_MODE, GL_LINEAR);
  glFogf(GL_FOG_START, 2.0f);
  glFogf(GL_FOG_END, 100.0f);
  triangle();
  check("fog with an orthographic projection stays in softgl", gr.triangles == 0);
  glMatrixMode(GL_PROJECTION);
  glFrustum(-1, 1, -1, 1, 1, 1000);
  glMatrixMode(GL_MODELVIEW);
  glTranslatef(0, 0, -5);
  triangle();
  check("fog with a perspective projection on the card", gr.triangles == 1 && gr.fog_mode == GR_FOG_WITH_TABLE);
  /* Entry 0 is w 1 (no fog); entry 63 is w 52428 (all fog). */
  check("fog table", gr.fog_table[0] == 0 && gr.fog_table[GR_FOG_TABLE_SIZE - 1] == 255);
  sgl_destroy_context(c);
}

/* Tests the clears, blending, wide lines and large points. */
static void test_misc(void) {
  sgl_glide g;
  sgl_context *c = make(&g, 1);
  glClearColor(1, 0, 0, 1);
  glClearDepth(1.0);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  check("clear on the card", gr.clears == 1 && (gr.clear_color & 0xFFFFFF) == 0xFF0000 && gr.clear_depth == 0xFFFF);
  check("clear of the screen", gr.clip[0] == 0 && gr.clip[1] == 0 && gr.clip[2] == SW && gr.clip[3] == SH);
  glEnable(GL_SCISSOR_TEST);
  glScissor(10, 20, 30, 40);
  glClear(GL_COLOR_BUFFER_BIT);
  check("clear of the scissor box", gr.clears == 2 && gr.clip[0] == 10 && gr.clip[1] == 20 && gr.clip[2] == 40 &&
                                        gr.clip[3] == 60);
  glDisable(GL_SCISSOR_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  triangle();
  check("blend", gr.triangles == 1 && gr.blend[0] == GR_BLEND_SRC_ALPHA && gr.blend[1] == GR_BLEND_ONE_MINUS_SRC_ALPHA);
  glBlendFunc(GL_ONE, GL_DST_ALPHA);
  triangle();
  check("destination alpha stays in softgl", gr.triangles == 1);
  glDisable(GL_BLEND);
  glLineWidth(4.0f);
  glBegin(GL_LINES);
  glVertex2f(-0.5f, 0.0f);
  glVertex2f(0.5f, 0.1f);
  glEnd();
  check("wide line as two triangles", gr.triangles == 3 && gr.lines == 0);
  glLineWidth(1.0f);
  glBegin(GL_LINES);
  glVertex2f(-0.5f, 0.0f);
  glVertex2f(0.5f, 0.1f);
  glEnd();
  check("line", gr.lines == 1);
  glPointSize(1.0f);
  glBegin(GL_POINTS);
  glVertex2f(0, 0);
  glEnd();
  check("point", gr.points == 1);
  glPointSize(5.0f);
  glBegin(GL_POINTS);
  glVertex2f(0, 0);
  glEnd();
  check("large point as two triangles", gr.points == 1 && gr.triangles == 5);
  sgl_destroy_context(c);
}

/* Runs the tests. */
int main(void) {
  test_triangle();
  test_copies();
  test_texture();
  test_units();
  test_fog();
  test_misc();
  if (failures) return 1;
  printf("PASS softgl glide\n");
  return 0;
}
