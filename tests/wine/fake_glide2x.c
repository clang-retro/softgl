/* fake_glide2x.c - a glide2x.dll for the test of the Glide back end
 * under Wine (tools/test-glide-wine.sh). It has no card: it counts the
 * calls, examines the vertices and gives a frame buffer in memory. At
 * grGlideShutdown it writes one line:
 *
 *     fake glide: opens 1 triangles 360 swaps 30 ... bad 0
 *
 * The functions use __stdcall, and the script exports them with the
 * names of the 3dfx DLL ("_grDrawTriangle@12").
 *
 * Part of clang-retro softgl, a software implementation of OpenGL.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include <stdio.h>

#define API __stdcall

/* A Glide 2 vertex (as sgl_gr_vertex). */
typedef struct {
  float x, y, z, r, g, b, ooz, a, oow, tmu[3][3];
} vertex;

/* A frame buffer lock (as sgl_gr_lfbinfo). */
typedef struct {
  int size;
  void *ptr;
  unsigned long stride;
  int write_mode, origin;
} lfbinfo;

static int opens, triangles, lines, points, swaps, locks, clears, downloads, bad;
static unsigned short color[480][1024], aux[480][1024];

/* Counts a vertex that is not on the screen of 640 x 480, or has a bad
 * color or depth. */
static void check(const vertex *v) {
  if (!(v->x >= -1.0f && v->x <= 641.0f && v->y >= -1.0f && v->y <= 481.0f)) bad++;
  if (!(v->r >= 0.0f && v->r <= 255.0f && v->a >= 0.0f && v->a <= 255.0f)) bad++;
  if (!(v->ooz >= 0.0f && v->ooz <= 65535.0f && v->oow > 0.0f)) bad++;
}

/* Does nothing. */
void API grGlideInit(void) {}
/* Writes the counts. */
void API grGlideShutdown(void) {
  printf("fake glide: opens %d triangles %d lines %d points %d swaps %d locks %d clears %d downloads %d bad %d\n",
         opens, triangles, lines, points, swaps, locks, clears, downloads, bad);
  fflush(stdout);
}
/* Gives one Voodoo2 with two TMUs. */
int API grSstQueryHardware(int *hw) {
  hw[0] = 1, hw[1] = 3, hw[2] = 4, hw[3] = 2, hw[4] = 2;
  return 1;
}
/* Does nothing. */
void API grSstSelect(int which) { (void)which; }
/* Counts the open; gives 1 for 640 x 480, the lower left origin, two
 * color buffers and one depth buffer. */
int API grSstWinOpen(unsigned long hwnd, int res, int refresh, int fmt, int origin, int ncolor, int naux) {
  (void)hwnd, (void)refresh, (void)fmt;
  opens++;
  return res == 7 && origin == 1 && ncolor == 2 && naux == 1;
}
/* Does nothing. */
void API grSstWinClose(void) {}
/* Does nothing. */
void API grSstIdle(void) {}
/* Counts the clear. */
void API grBufferClear(unsigned long c, unsigned a, unsigned d) { (void)c, (void)a, (void)d, clears++; }
/* Counts the swap. */
void API grBufferSwap(int interval) { (void)interval, swaps++; }
/* Counts and examines the triangle. */
void API grDrawTriangle(const vertex *a, const vertex *b, const vertex *c) {
  triangles++;
  check(a), check(b), check(c);
}
/* Counts and examines the line. */
void API grDrawLine(const vertex *a, const vertex *b) {
  lines++;
  check(a), check(b);
}
/* Counts and examines the point. */
void API grDrawPoint(const vertex *a) {
  points++;
  check(a);
}
/* Does nothing. */
void API grDepthBufferMode(int m) { (void)m; }
/* Does nothing. */
void API grDepthBufferFunction(int f) { (void)f; }
/* Does nothing. */
void API grDepthMask(int m) { (void)m; }
/* Does nothing. */
void API grColorMask(int rgb, int a) { (void)rgb, (void)a; }
/* Does nothing. */
void API grColorCombine(int f, int fac, int l, int o, int inv) { (void)f, (void)fac, (void)l, (void)o, (void)inv; }
/* Does nothing. */
void API grAlphaCombine(int f, int fac, int l, int o, int inv) { (void)f, (void)fac, (void)l, (void)o, (void)inv; }
/* Does nothing. */
void API grTexCombine(int t, int a, int b, int c, int d, int e, int f) {
  (void)t, (void)a, (void)b, (void)c, (void)d, (void)e, (void)f;
}
/* Does nothing. */
void API grAlphaBlendFunction(int a, int b, int c, int d) { (void)a, (void)b, (void)c, (void)d; }
/* Does nothing. */
void API grAlphaTestFunction(int f) { (void)f; }
/* Does nothing. */
void API grAlphaTestReferenceValue(unsigned v) { (void)v; }
/* Does nothing. */
void API grFogMode(int m) { (void)m; }
/* Does nothing. */
void API grFogColorValue(unsigned long c) { (void)c; }
/* Does nothing. */
void API grFogTable(const unsigned char *t) { (void)t; }
/* Does nothing. */
void API grClipWindow(unsigned long a, unsigned long b, unsigned long c, unsigned long d) {
  (void)a, (void)b, (void)c, (void)d;
}
/* Does nothing. */
void API grCullMode(int m) { (void)m; }
/* Does nothing. */
void API grDitherMode(int m) { (void)m; }
/* Gives the start of the texture memory. */
unsigned long API grTexMinAddress(int tmu) { return (void)tmu, 0ul; }
/* Gives the end of the texture memory: 2 MB. */
unsigned long API grTexMaxAddress(int tmu) { return (void)tmu, 0x200000ul; }
/* Gives the size of a texture: 64 KB, the size of 256 x 128. */
unsigned long API grTexTextureMemRequired(unsigned long eo, void *info) { return (void)eo, (void)info, 65536ul; }
/* Counts the download. */
void API grTexDownloadMipMap(int t, unsigned long s, unsigned long eo, void *i) {
  (void)t, (void)s, (void)eo, (void)i, downloads++;
}
/* Does nothing. */
void API grTexSource(int t, unsigned long s, unsigned long eo, void *i) { (void)t, (void)s, (void)eo, (void)i; }
/* Does nothing. */
void API grTexFilterMode(int t, int a, int b) { (void)t, (void)a, (void)b; }
/* Does nothing. */
void API grTexClampMode(int t, int a, int b) { (void)t, (void)a, (void)b; }
/* Does nothing. */
void API grTexMipMapMode(int t, int a, int b) { (void)t, (void)a, (void)b; }
/* Counts the lock and gives a buffer of 1024 x 480 pixels. */
int API grLfbLock(int type, int buf, int wm, int origin, int pipe, lfbinfo *info) {
  (void)type, (void)wm, (void)origin, (void)pipe;
  locks++;
  info->ptr = buf == 2 ? (void *)aux : (void *)color;
  info->stride = 2048;
  return 1;
}
/* Does nothing. */
int API grLfbUnlock(int type, int buf) { return (void)type, (void)buf, 1; }
