/* glide_sst1.c - the Glide 2 functions of the Glide back end, on the
 * registers of a 3dfx Voodoo Graphics (SST-1), Voodoo2, Voodoo Banshee
 * or Voodoo3 card, for 32-bit DOS. With these functions a DOS program needs no Glide
 * library: sgl_glide_sst1 fills an sgl_glide_api, and the Glide back end
 * (glide.c) uses it as it uses glide2x.dll on Windows.
 *
 * The functions do only what glide.c needs: one board, one TMU, 16-bit
 * color with a 16-bit depth buffer, two color buffers, the texture
 * formats RGB 565 and ARGB 4444, and the combine functions of
 * glide_api.h.
 *
 * The register facts come from the SST-1 specification of 3dfx. The
 * layout of the card:
 *
 *   base + 0x000000   registers (bits 10 to 13 select the chips: 1 the
 *                     FBI, 2 TMU 0, 4 TMU 1; 0 is all)
 *   base + 0x400000   the linear frame buffer (2048 bytes for each row)
 *   base + 0x800000   texture memory (writes only)
 *
 * The Banshee and the Voodoo3 have a VGA and one memory for the screen,
 * the 3D buffers and the textures. The driver sets a VBE mode, and the
 * 3D engine draws into the buffers that colBufferAddr and auxBufferAddr
 * give. The layout of BAR 0 of these cards:
 *
 *   base + 0x000000   the I/O registers (miscInit0 at 0x10)
 *   base + 0x200000   the 3D registers, as on the SST-1
 *   base + 0x600000   texture download (an offset from texBaseAddr)
 *
 * Part of clang-retro softgl, a software implementation of OpenGL.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#if defined(__MSDOS__) && defined(__i386__)

#include <dos.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "glide_api.h"
#include "softgl_dos.h"
#include "softgl_glide.h"

static void sst1_close(void);

/* The registers (byte offsets). */
enum {
  R_STATUS = 0x000,
  R_FVERTEXAX = 0x088,
  R_FSTARTR = 0x0A0,
  R_FDRDX = 0x0C0,
  R_FDRDY = 0x0E0,
  R_FTRIANGLECMD = 0x100,
  R_FBZCOLORPATH = 0x104,
  R_FOGMODE = 0x108,
  R_ALPHAMODE = 0x10C,
  R_FBZMODE = 0x110,
  R_LFBMODE = 0x114,
  R_CLIPLEFTRIGHT = 0x118,
  R_CLIPLOWYHIGHY = 0x11C,
  R_NOPCMD = 0x120,
  R_FASTFILLCMD = 0x124,
  R_SWAPBUFFERCMD = 0x128,
  R_FOGCOLOR = 0x12C,
  R_ZACOLOR = 0x130,
  R_COLOR0 = 0x144,
  R_COLOR1 = 0x148,
  R_FOGTABLE = 0x160,
  R_FBIINIT4 = 0x200,
  R_BACKPORCH = 0x208,
  R_VIDEODIMENSIONS = 0x20C,
  R_FBIINIT0 = 0x210,
  R_FBIINIT1 = 0x214,
  R_FBIINIT2 = 0x218,
  R_FBIINIT3 = 0x21C,
  R_HSYNC = 0x220,
  R_VSYNC = 0x224,
  R_TEXTUREMODE = 0x300,
  R_TLOD = 0x304,
  R_TDETAIL = 0x308,
  R_TEXBASEADDR = 0x30C,
  R_TREXINIT0 = 0x31C,
  R_TREXINIT1 = 0x320,
  /* The Banshee and the Voodoo3 only. */
  R_COLBUFFERADDR = 0x1EC,
  R_COLBUFFERSTRIDE = 0x1F0,
  R_AUXBUFFERADDR = 0x1F4,
  R_AUXBUFFERSTRIDE = 0x1F8
};

/* The Banshee and the Voodoo3: the 3D registers and the texture download
 * in BAR 0, and the I/O register miscInit0 (bits 18 to 29: the Y origin
 * of the 3D engine, the height minus 1). */
#define H3_3D 0x200000ul
#define H3_TEXTURE 0x600000ul
#define H3_MISCINIT0 0x10u
#define H3_YORIGIN_SHIFT 18
#define H3_YORIGIN_MASK (0xFFFu << H3_YORIGIN_SHIFT)

/* The parameters of the float triangle registers, in the order of the
 * start, dX and dY register blocks: R, G, B, Z, A, S, T, W. */
enum { P_R, P_G, P_B, P_Z, P_A, P_S, P_T, P_W, P_COUNT };

/* The chip fields of a register address. */
#define CHIP_FBI (1u << 10)
#define CHIP_TMU0 (2u << 10)

/* The PCI configuration registers of the card. */
#define PCI_INIT_ENABLE 0x40
#define PCI_VCLK_ENABLE 0xC0
#define PCI_VCLK_DISABLE 0xE0

/* The bits of fbzMode. */
#define FBZ_CLIP 0x00000001u
#define FBZ_DEPTH 0x00000010u
#define FBZ_DITHER 0x00000100u
#define FBZ_RGB_WRITE 0x00000200u
#define FBZ_AUX_WRITE 0x00000400u
#define FBZ_DRAW_BACK 0x00004000u
#define FBZ_Y_ORIGIN 0x00020000u

/* The fbzColorPath bit that moves the start values from vertex A to the
 * center of the first pixel, and the bit that turns on the texture. */
#define CP_PARMADJUST 0x04000000u
#define CP_TEXTURE 0x08000000u

/* The texture memory of a TMU that this file uses. */
#define TMU_MEMORY 0x200000ul

/* The state of the card. */
static struct {
  sgl_pci_device pci;
  volatile uint8_t *base;  /* the 16 MB of the card (BAR 0) */
  volatile uint8_t *regs;  /* the 3D registers */
  uint32_t phys;
  int type;                /* GR_SSTTYPE_VOODOO or GR_SSTTYPE_VOODOO2 */
  int h3;                  /* a Banshee or a Voodoo3 */
  /* The Banshee and the Voodoo3: the frame buffer, the buffers (byte
   * offsets in video memory) and the first byte of the textures. */
  volatile uint8_t *lfb;
  uint32_t memory, pitch, buffer[3], aux, tex_min, misc0;
  int buffers, front, back, lock_origin;
  uint16_t *shadow;
  int open;
  int width, height;
  int origin;              /* GR_ORIGIN_LOWER_LEFT or GR_ORIGIN_UPPER_LEFT */
  uint32_t fbzmode, colorpath, alphamode, fogmode, texmode, tlod;
  int cc_texture, ac_texture; /* the combines use the texture */
  int cull;
} sst;

/* Writes the register at byte offset reg (with its chip field). */
static void wr(uint32_t reg, uint32_t value) { *(volatile uint32_t *)(sst.regs + reg) = value; }

/* Writes a float to the register at byte offset reg. */
static void wrf(uint32_t reg, float value) {
  union {
    float f;
    uint32_t u;
  } v;
  v.f = value;
  wr(reg, v.u);
}

/* Reads the register at byte offset reg. */
static uint32_t rd(uint32_t reg) { return *(volatile uint32_t *)(sst.regs + reg); }

/* Waits until the card is not busy: a NOP goes through the FIFO, and the
 * busy bit (bit 9 of status) must be off in three reads, as the SST-1
 * specification tells. It stops after about one second. */
static void idle(void) {
  uint32_t start = sgl_dos_ms();
  int clear = 0;
  wr(R_NOPCMD, 0);
  while (clear < 3) {
    if (rd(R_STATUS) & (1u << 9))
      clear = 0;
    else
      clear++;
    if (sgl_dos_ms() - start > 1000) break;
  }
}

/* The PCI IDs of the cards: the Voodoo Graphics, the Voodoo2, the
 * Banshee and the Voodoo3. */
static const sgl_pci_id sst_ids[] = {{0x121A, 0x0001}, {0x121A, 0x0002}, {0x121A, 0x0003}, {0x121A, 0x0005}, {0, 0}};

/* grGlideInit: finds the first Voodoo card and maps its memory. */
static void SGL_GLIDE_CALL gr_glide_init(void) {
  if (sst.base) return;
  memset(&sst, 0, sizeof sst);
  if (!sgl_pci_find(sst_ids, 0, &sst.pci)) return;
  sst.h3 = sst.pci.device == 0x0003 || sst.pci.device == 0x0005;
  sst.type = sst.pci.device == 0x0001 ? GR_SSTTYPE_VOODOO : GR_SSTTYPE_VOODOO2;
  sst.phys = sgl_pci_bar(&sst.pci, 0);
  if (!sst.phys) return;
  /* The memory space of the card must be on (bit 1 of the command). */
  sgl_pci_write16(&sst.pci, 0x04, (uint16_t)(sgl_pci_read16(&sst.pci, 0x04) | 0x0002));
  sst.base = (volatile uint8_t *)sgl_map_physical(sst.phys, sst.h3 ? 0x800000ul : 0x1000000ul);
  sst.regs = sst.base && sst.h3 ? sst.base + H3_3D : sst.base;
}

/* grGlideShutdown: closes the screen and frees the mapping. */
static void SGL_GLIDE_CALL gr_glide_shutdown(void) {
  if (sst.open) {
    sst1_close();
  }
  if (sst.base) sgl_unmap_physical(sst.base);
  free(sst.shadow);
  memset(&sst, 0, sizeof sst);
}

/* grSstQueryHardware: one board, with its type, 4 MB of frame buffer
 * memory, revision 2 and one TMU (GrHwConfiguration as 32-bit words). */
static int SGL_GLIDE_CALL gr_sst_query_hardware(void *config) {
  int32_t *hw = (int32_t *)config;
  if (!sst.base) return 0;
  hw[0] = 1;
  hw[1] = sst.type;
  hw[2] = 4;
  hw[3] = 2;
  hw[4] = 1;
  return 1;
}

/* grSstSelect: there is one board. */
static void SGL_GLIDE_CALL gr_sst_select(int which) { (void)which; }

/* The video timing of a screen: the visible size, the back porches, the
 * sync widths and the sync-to-sync totals. The values are the VESA
 * timings at 60 Hz (72 Hz for 320 wide modes is not used). */
typedef struct {
  int res, w, h;
  int hbp, vbp, hsync_on, hsync_off, vsync_on, vsync_off;
} sst_timing;

static const sst_timing timings[] = {
    {GR_RESOLUTION_512x384, 512, 384, 120, 25, 23, 640, 3, 409},
    {GR_RESOLUTION_640x400, 640, 400, 47, 34, 95, 704, 2, 447},
    {GR_RESOLUTION_640x480, 640, 480, 47, 33, 95, 704, 2, 523},
    {GR_RESOLUTION_800x600, 800, 600, 86, 23, 127, 928, 4, 624},
};

/* Programs the video registers for t. The FBI makes the sync signals
 * from these values. */
static void set_timing(const sst_timing *t) {
  wr(R_HSYNC, ((uint32_t)t->hsync_off << 16) | (uint32_t)t->hsync_on);
  wr(R_VSYNC, ((uint32_t)t->vsync_off << 16) | (uint32_t)t->vsync_on);
  wr(R_BACKPORCH, ((uint32_t)t->vbp << 16) | (uint32_t)t->hbp);
  wr(R_VIDEODIMENSIONS, ((uint32_t)(t->h - 1) << 16) | (uint32_t)(t->w - 1));
}

/* Sets the rendering state of grSstWinOpen: the fbzMode for origin, the
 * color path, no alpha, no fog, a perspective texture, and the clip
 * window of the whole screen. */
static void initial_state(int origin) {
  wr(CHIP_TMU0 | R_TDETAIL, 0);
  sst.fbzmode = FBZ_CLIP | FBZ_RGB_WRITE | FBZ_DRAW_BACK | (origin == GR_ORIGIN_LOWER_LEFT ? FBZ_Y_ORIGIN : 0);
  sst.colorpath = CP_PARMADJUST;
  sst.alphamode = 0;
  sst.fogmode = 0;
  sst.texmode = 1; /* perspective */
  sst.tlod = 0;
  wr(R_FBZMODE, sst.fbzmode);
  wr(R_FBZCOLORPATH, sst.colorpath);
  wr(R_ALPHAMODE, sst.alphamode);
  wr(R_FOGMODE, sst.fogmode);
  wr(CHIP_TMU0 | R_TEXTUREMODE, sst.texmode);
  wr(R_CLIPLEFTRIGHT, (uint32_t)sst.width);
  wr(R_CLIPLOWYHIGHY, (uint32_t)sst.height);
  wr(R_LFBMODE, 0);
}

/* Gives the VBE mode of 16-bit color (5:6:5) for a Glide resolution,
 * and its size in *w and *h. Gives -1 for other resolutions. */
static int vbe_mode(int resolution, int *w, int *h) {
  switch (resolution) {
    case GR_RESOLUTION_640x480: *w = 640, *h = 480; return 0x111;
    case GR_RESOLUTION_800x600: *w = 800, *h = 600; return 0x114;
    case GR_RESOLUTION_1024x768: *w = 1024, *h = 768; return 0x117;
    default: return -1;
  }
}

/* Tells the 3D engine of a Banshee or a Voodoo3 to draw into the back
 * buffer. */
static void h3_target(void) { wr(R_COLBUFFERADDR, sst.buffer[sst.back]); }

/* grSstWinOpen for the Banshee and the Voodoo3: sets a VBE mode with
 * the linear frame buffer, and divides video memory into the color
 * buffers (three when the textures keep 2 MB, else two), the depth
 * buffer and the textures. */
static int h3_open(int resolution, int origin) {
  sgl_vbe_mode info;
  uint32_t frame;
  int mode, w, h, i;
  mode = vbe_mode(resolution, &w, &h);
  if (mode < 0 || !sgl_vbe_mode_info(mode, &info) || !(info.attributes & 0x81) || info.bits != 16 || !info.lfb)
    return 0;
  sst.memory = sgl_vbe_memory();
  if (!sst.memory) sst.memory = 0x400000ul;
  frame = ((uint32_t)info.stride * (uint32_t)h + 4095u) & ~4095u;
  if (3 * frame + 0x80000ul > sst.memory || !sgl_vbe_set_mode(mode, 1)) return 0;
  sst.lfb = (volatile uint8_t *)sgl_map_physical(info.lfb, sst.memory);
  if (!sst.lfb) {
    sgl_vbe_text_mode();
    return 0;
  }
  sst.width = w, sst.height = h, sst.origin = origin, sst.pitch = info.stride;
  sst.buffers = 4 * frame + 0x200000ul <= sst.memory ? 3 : 2;
  for (i = 0; i < sst.buffers; i++) sst.buffer[i] = (uint32_t)i * frame;
  sst.aux = (uint32_t)sst.buffers * frame;
  sst.tex_min = sst.aux + frame;
  sst.front = 0, sst.back = 1;
  /* The Y origin of the 3D engine: the last row of the screen. */
  sst.misc0 = *(volatile uint32_t *)(sst.base + H3_MISCINIT0);
  *(volatile uint32_t *)(sst.base + H3_MISCINIT0) =
      (sst.misc0 & ~H3_YORIGIN_MASK) | ((uint32_t)(h - 1) << H3_YORIGIN_SHIFT);
  idle();
  wr(R_COLBUFFERSTRIDE, sst.pitch);
  wr(R_AUXBUFFERSTRIDE, sst.pitch);
  wr(R_AUXBUFFERADDR, sst.aux);
  h3_target();
  initial_state(origin);
  sgl_vbe_display_start(0, 0);
  sst.open = 1;
  return 1;
}

/* grSstWinOpen: resets the card, sets the video mode, and turns on the
 * output of the card (the card then shows its screen instead of the VGA
 * screen). The color format must be ARGB; there must be 2 color buffers
 * and 1 depth buffer. */
static int SGL_GLIDE_CALL gr_sst_win_open(unsigned long hwnd, int resolution, int refresh, int color_format,
                                          int origin, int color_buffers, int aux_buffers) {
  const sst_timing *t = NULL;
  uint32_t tiles, pages;
  size_t i;
  (void)hwnd, (void)refresh;
  if (!sst.base || sst.open || color_format != GR_COLORFORMAT_ARGB || color_buffers != 2 || aux_buffers != 1)
    return 0;
  if (sst.h3) return h3_open(resolution, origin);
  for (i = 0; i < sizeof timings / sizeof timings[0]; i++)
    if (timings[i].res == resolution) t = &timings[i];
  if (!t) return 0;
  sst.width = t->w;
  sst.height = t->h;
  sst.origin = origin;
  /* Let the program write fbiInit0 to fbiInit4, with the PCI FIFO on. */
  sgl_pci_write32(&sst.pci, PCI_INIT_ENABLE, 0x3);
  /* Stop the video clock, and reset the video timing, the graphics
   * engine and the FIFO. */
  sgl_pci_write32(&sst.pci, PCI_VCLK_DISABLE, 0);
  wr(R_FBIINIT1, rd(R_FBIINIT1) | (1u << 8));
  wr(R_FBIINIT0, rd(R_FBIINIT0) | (1u << 1) | (1u << 2));
  idle();
  /* The memory: the rows are tiles of 64 pixels, two color buffers
   * of 4 KB pages, then the depth buffer. Refresh on, with the load
   * value of the specification. */
  tiles = (uint32_t)(t->w + 63) / 64;
  pages = (uint32_t)((unsigned long)tiles * 64 * t->h * 2 + 4095) / 4096;
  wr(R_FBIINIT1, (tiles << 4) | (1u << 3) | (1u << 8) | (1u << 13) | (1u << 14) | (1u << 15) | (1u << 16));
  wr(R_FBIINIT2, (pages << 11) | (1u << 22) | (0x30u << 23) | (1u << 21));
  wr(R_FBIINIT3, (uint32_t)(t->h - 1) << 22);
  wr(R_FBIINIT4, (1u << 0) | (1u << 1));
  set_timing(t);
  /* End the resets, give the output to the card and start the clock. */
  wr(R_FBIINIT1, rd(R_FBIINIT1) & ~(1u << 8));
  wr(R_FBIINIT0, (rd(R_FBIINIT0) & ~((1u << 1) | (1u << 2))) | (1u << 0));
  sgl_pci_write32(&sst.pci, PCI_VCLK_ENABLE, 0);
  idle();
  initial_state(origin);
  sst.open = 1;
  return 1;
}

/* grSstWinClose: gives the screen back to the VGA card. */
static void sst1_close(void) {
  if (!sst.open) return;
  idle();
  if (sst.h3) {
    *(volatile uint32_t *)(sst.base + H3_MISCINIT0) = sst.misc0;
    sgl_vbe_text_mode();
    sgl_unmap_physical(sst.lfb);
    sst.lfb = NULL;
    sst.open = 0;
    return;
  }
  wr(R_FBIINIT0, rd(R_FBIINIT0) & ~1u);
  sgl_pci_write32(&sst.pci, PCI_VCLK_DISABLE, 0);
  sgl_pci_write32(&sst.pci, PCI_INIT_ENABLE, 0);
  sst.open = 0;
}

/* grSstWinClose, for the table. */
static void SGL_GLIDE_CALL gr_sst_win_close(void) { sst1_close(); }

/* grSstIdle: waits until the card is not busy. */
static void SGL_GLIDE_CALL gr_sst_idle(void) {
  if (sst.base) idle();
}

/* grBufferClear: fills the clip window of the draw buffer with the color
 * (0xAARRGGBB) and the depth buffer with depth. The color mask and the
 * depth mask (fbzMode) select the buffers, as in Glide. */
static void SGL_GLIDE_CALL gr_buffer_clear(unsigned long color, unsigned alpha, unsigned depth) {
  wr(R_COLOR1, (uint32_t)color);
  wr(R_ZACOLOR, ((uint32_t)(alpha & 0xFF) << 24) | (depth & 0xFFFF));
  wr(R_FASTFILLCMD, 0);
}

/* Waits for the start of the next vertical retrace (bit 3 of port
 * 3DAh). If the retrace is active, it waits for its end first. */
static void next_retrace(void) {
  uint32_t start = sgl_dos_ms();
  while ((inportb(0x3DA) & 8) && sgl_dos_ms() - start < 100) {
  }
  while (!(inportb(0x3DA) & 8) && sgl_dos_ms() - start < 100) {
  }
}

/* grBufferSwap for the Banshee and the Voodoo3: shows the back buffer
 * through VBE function 4F07h. With interval 1 or more, the change
 * occurs in the vertical retrace. The next back buffer is one that
 * the monitor does not show: with three buffers the one that is not
 * the old front, with two the old front. */
static void h3_swap(int interval) {
  int old = sst.front;
  if (!sst.open) return;
  idle();
  if (interval > 0) next_retrace();
  sgl_vbe_display_start((int)(sst.buffer[sst.back] / sst.pitch), 0);
  sst.front = sst.back;
  sst.back = sst.buffers == 3 ? 3 - sst.front - old : old;
  h3_target();
}

/* grBufferSwap: shows the back buffer after interval vertical retraces
 * (0: at once). */
static void SGL_GLIDE_CALL gr_buffer_swap(int interval) {
  if (sst.h3) {
    h3_swap(interval);
    return;
  }
  if (interval < 0) interval = 0;
  if (interval > 255) interval = 255;
  wr(R_SWAPBUFFERCMD, interval ? (((uint32_t)(interval - 1) << 1) | 1u) : 0u);
}

/* Gives the value of parameter p of vertex v. */
static float param(const sgl_gr_vertex *v, int p) {
  switch (p) {
    case P_R: return v->r;
    case P_G: return v->g;
    case P_B: return v->b;
    case P_Z: return v->ooz;
    case P_A: return v->a;
    case P_S: return v->tmu[0].sow;
    case P_T: return v->tmu[0].tow;
    default: return v->oow;
  }
}

/* grDrawTriangle: the triangle setup of Glide. The vertices go in the
 * order of y (vertex A has the smallest y). Each parameter gets its
 * value at A and its gradients in x and y; the sign of the area tells
 * the card the side of the long edge. The FBI gets W of the vertex and
 * TMU 0 gets W of its texture coordinates. */
static void SGL_GLIDE_CALL gr_draw_triangle(const sgl_gr_vertex *a, const sgl_gr_vertex *b,
                                            const sgl_gr_vertex *c) {
  const sgl_gr_vertex *v[3], *t;
  float dxab, dxbc, dyab, dybc, area, ooa;
  int p;
  v[0] = a, v[1] = b, v[2] = c;
  if (v[1]->y < v[0]->y) t = v[0], v[0] = v[1], v[1] = t;
  if (v[2]->y < v[1]->y) t = v[1], v[1] = v[2], v[2] = t;
  if (v[1]->y < v[0]->y) t = v[0], v[0] = v[1], v[1] = t;
  dxab = v[0]->x - v[1]->x, dxbc = v[1]->x - v[2]->x;
  dyab = v[0]->y - v[1]->y, dybc = v[1]->y - v[2]->y;
  area = dxab * dybc - dxbc * dyab;
  if (area == 0.0f) return;
  if (sst.cull && ((sst.cull == 1) == (area < 0.0f))) return;
  ooa = 1.0f / area;
  wrf(R_FVERTEXAX + 0, v[0]->x);
  wrf(R_FVERTEXAX + 4, v[0]->y);
  wrf(R_FVERTEXAX + 8, v[1]->x);
  wrf(R_FVERTEXAX + 12, v[1]->y);
  wrf(R_FVERTEXAX + 16, v[2]->x);
  wrf(R_FVERTEXAX + 20, v[2]->y);
  for (p = 0; p < P_COUNT; p++) {
    float pa = param(v[0], p), dab = pa - param(v[1], p), dbc = param(v[1], p) - param(v[2], p);
    float ddx = (dab * dybc - dbc * dyab) * ooa, ddy = (dbc * dxab - dab * dxbc) * ooa;
    uint32_t chip = 0;
    if (p == P_W) {
      /* W of the FBI (fog), then W of the texture. */
      wrf(CHIP_FBI | (R_FSTARTR + 4 * p), pa);
      wrf(CHIP_FBI | (R_FDRDX + 4 * p), ddx);
      wrf(CHIP_FBI | (R_FDRDY + 4 * p), ddy);
      pa = v[0]->tmu[0].oow, dab = pa - v[1]->tmu[0].oow, dbc = v[1]->tmu[0].oow - v[2]->tmu[0].oow;
      ddx = (dab * dybc - dbc * dyab) * ooa, ddy = (dbc * dxab - dab * dxbc) * ooa;
      chip = CHIP_TMU0;
    }
    wrf(chip | (R_FSTARTR + 4 * p), pa);
    wrf(chip | (R_FDRDX + 4 * p), ddx);
    wrf(chip | (R_FDRDY + 4 * p), ddy);
  }
  wrf(R_FTRIANGLECMD, area);
}

/* grDrawLine: a line of one pixel is a thin quad of two triangles,
 * across its minor axis. */
static void SGL_GLIDE_CALL gr_draw_line(const sgl_gr_vertex *a, const sgl_gr_vertex *b) {
  sgl_gr_vertex q[4];
  float dx = b->x - a->x, dy = b->y - a->y;
  int i;
  q[0] = *a, q[1] = *b, q[2] = *b, q[3] = *a;
  for (i = 0; i < 4; i++) {
    float o = (i == 0 || i == 1) ? -0.5f : 0.5f;
    if ((dx < 0 ? -dx : dx) >= (dy < 0 ? -dy : dy))
      q[i].y += o;
    else
      q[i].x += o;
  }
  gr_draw_triangle(&q[0], &q[1], &q[2]);
  gr_draw_triangle(&q[0], &q[2], &q[3]);
}

/* grDrawPoint: a point is a square of one pixel. */
static void SGL_GLIDE_CALL gr_draw_point(const sgl_gr_vertex *a) {
  sgl_gr_vertex q[4];
  int i;
  for (i = 0; i < 4; i++) {
    q[i] = *a;
    q[i].x += (i == 1 || i == 2) ? 0.5f : -0.5f;
    q[i].y += (i >= 2) ? 0.5f : -0.5f;
  }
  gr_draw_triangle(&q[0], &q[1], &q[2]);
  gr_draw_triangle(&q[0], &q[2], &q[3]);
}

/* Writes fbzMode after a change of one of its fields. */
static void set_fbzmode(uint32_t clear, uint32_t set) {
  sst.fbzmode = (sst.fbzmode & ~clear) | set;
  wr(R_FBZMODE, sst.fbzmode);
}

/* grDepthBufferMode: the Z buffer on or off. */
static void SGL_GLIDE_CALL gr_depth_buffer_mode(int mode) {
  set_fbzmode(FBZ_DEPTH, mode == GR_DEPTHBUFFER_ZBUFFER ? FBZ_DEPTH : 0);
}

/* grDepthBufferFunction: the depth compare (the Glide and the card
 * codes are the same). */
static void SGL_GLIDE_CALL gr_depth_buffer_function(int func) {
  set_fbzmode(7u << 5, (uint32_t)(func & 7) << 5);
}

/* grDepthMask: writes to the depth buffer on or off. */
static void SGL_GLIDE_CALL gr_depth_mask(int on) { set_fbzmode(FBZ_AUX_WRITE, on ? FBZ_AUX_WRITE : 0); }

/* grColorMask: writes to the color buffer on or off. The card has no
 * alpha buffer here. */
static void SGL_GLIDE_CALL gr_color_mask(int rgb, int alpha) {
  (void)alpha;
  set_fbzmode(FBZ_RGB_WRITE, rgb ? FBZ_RGB_WRITE : 0);
}

/* Gives the bits of a Glide combine function: zero other (bit 0),
 * subtract local (bit 1) and the add field (bits 2 and 3). */
static uint32_t func_bits(int func) {
  switch (func) {
    case GR_COMBINE_FUNCTION_ZERO: return 1;
    case GR_COMBINE_FUNCTION_LOCAL: return 1 | (1 << 2);
    case 0x2: return 1 | (2 << 2);                   /* LOCAL_ALPHA */
    case GR_COMBINE_FUNCTION_SCALE_OTHER: return 0;
    case 0x4: return 1 << 2;                         /* SCALE_OTHER_ADD_LOCAL */
    case 0x5: return 2 << 2;                         /* ..._ADD_LOCAL_ALPHA */
    case 0x6: return 2;                              /* SCALE_OTHER_MINUS_LOCAL */
    case GR_COMBINE_FUNCTION_BLEND: return 2 | (1 << 2);
    case 0x8: return 2 | (2 << 2);                   /* ..._ADD_LOCAL_ALPHA */
    case 0x9: return 1 | 2 | (1 << 2);               /* SCALE_MINUS_LOCAL_ADD_LOCAL */
    default: return 1 | 2 | (2 << 2);                /* ..._ADD_LOCAL_ALPHA */
  }
}

/* Gives the combine fields of a function, a factor and invert, in the
 * layout of the RGB fields of fbzColorPath (from bit 8): zero other,
 * subtract local, the factor (3 bits), reverse blend, add (2 bits) and
 * invert. The alpha fields and the fields of textureMode have the same
 * layout at other positions. A factor without bit 3 is used as is; the
 * card inverts the factor when reverse blend is off. */
static uint32_t combine(int func, int factor, int invert) {
  uint32_t f = func_bits(func);
  return (f & 1) | (f & 2) | ((uint32_t)(factor & 7) << 2) | ((factor & 8) ? 0 : (1u << 5)) |
         (((f >> 2) & 3) << 6) | (invert ? (1u << 8) : 0);
}

/* Gives 1 when a factor reads the texture alpha. */
static int factor_uses_texture(int factor) { return (factor & 7) == 4; }

/* Writes fbzColorPath and turns the texture on when a combine uses it. */
static void write_colorpath(void) {
  sst.colorpath &= ~CP_TEXTURE;
  if (sst.cc_texture || sst.ac_texture) sst.colorpath |= CP_TEXTURE;
  wr(R_FBZCOLORPATH, sst.colorpath);
}

/* grColorCombine: the RGB of the FBI. local is the iterated or the
 * constant color (color0); other is the iterated color, the texture or
 * the constant color (color1). */
static void SGL_GLIDE_CALL gr_color_combine(int func, int factor, int local, int other, int invert) {
  uint32_t keep = sst.colorpath & ~(0x3u | 0x10u | 0x1FF00u);
  uint32_t sel = other == GR_COMBINE_OTHER_TEXTURE ? 1u : other == 2 ? 2u : 0u;
  sst.colorpath = keep | sel | (local == GR_COMBINE_LOCAL_CONSTANT ? 0x10u : 0u) | (combine(func, factor, invert) << 8);
  sst.cc_texture = other == GR_COMBINE_OTHER_TEXTURE || factor_uses_texture(factor);
  write_colorpath();
}

/* grAlphaCombine: the alpha of the FBI, as gr_color_combine. */
static void SGL_GLIDE_CALL gr_alpha_combine(int func, int factor, int local, int other, int invert) {
  uint32_t keep = sst.colorpath & ~(0xCu | 0x60u | 0x3FE0000u);
  uint32_t sel = other == GR_COMBINE_OTHER_TEXTURE ? 1u : other == 2 ? 2u : 0u;
  sst.colorpath = keep | (sel << 2) | (local == GR_COMBINE_LOCAL_CONSTANT ? 0x20u : 0u) |
                  (combine(func, factor, invert) << 17);
  sst.ac_texture = other == GR_COMBINE_OTHER_TEXTURE || factor_uses_texture(factor);
  write_colorpath();
}

/* grTexCombine: the combine of TMU 0. Its local value is the texel and
 * its other value is the output of the TMU before it (none here). */
static void SGL_GLIDE_CALL gr_tex_combine(int tmu, int rgb_func, int rgb_factor, int alpha_func, int alpha_factor,
                                          int rgb_invert, int alpha_invert) {
  if (tmu != GR_TMU0) return;
  sst.texmode = (sst.texmode & ~0x3FFFF000u) | (combine(rgb_func, rgb_factor, rgb_invert) << 12) |
                (combine(alpha_func, alpha_factor, alpha_invert) << 21);
  wr(CHIP_TMU0 | R_TEXTUREMODE, sst.texmode);
}

/* grAlphaBlendFunction: the blend factors (the Glide and the card codes
 * are the same). ONE, ZERO for RGB turns the blend off. */
static void SGL_GLIDE_CALL gr_alpha_blend_function(int rgb_src, int rgb_dst, int alpha_src, int alpha_dst) {
  int on = !(rgb_src == GR_BLEND_ONE && rgb_dst == GR_BLEND_ZERO);
  sst.alphamode = (sst.alphamode & ~0x00FFFF10u) | (on ? 0x10u : 0u) | ((uint32_t)(rgb_src & 15) << 8) |
                  ((uint32_t)(rgb_dst & 15) << 12) | ((uint32_t)(alpha_src & 15) << 16) |
                  ((uint32_t)(alpha_dst & 15) << 20);
  wr(R_ALPHAMODE, sst.alphamode);
}

/* grAlphaTestFunction: the alpha test; ALWAYS turns it off. */
static void SGL_GLIDE_CALL gr_alpha_test_function(int func) {
  sst.alphamode = (sst.alphamode & ~0xFu) | (func != GR_CMP_ALWAYS ? 1u : 0u) | ((uint32_t)(func & 7) << 1);
  wr(R_ALPHAMODE, sst.alphamode);
}

/* grAlphaTestReferenceValue: the reference of the alpha test. */
static void SGL_GLIDE_CALL gr_alpha_test_reference_value(unsigned value) {
  sst.alphamode = (sst.alphamode & 0x00FFFFFFu) | ((uint32_t)(value & 0xFF) << 24);
  wr(R_ALPHAMODE, sst.alphamode);
}

/* grFogMode: fog with the table of W, or no fog. */
static void SGL_GLIDE_CALL gr_fog_mode(int mode) {
  sst.fogmode = mode == GR_FOG_WITH_TABLE ? 1u : 0u;
  wr(R_FOGMODE, sst.fogmode);
}

/* grFogColorValue: the fog color (0x00RRGGBB). */
static void SGL_GLIDE_CALL gr_fog_color_value(unsigned long color) { wr(R_FOGCOLOR, (uint32_t)color & 0xFFFFFF); }

/* grFogTable: the 64 fog values. Each register has two entries; each
 * entry has the value and the step to the next value (times 4, as the
 * card multiplies it by the fraction of W). */
static void SGL_GLIDE_CALL gr_fog_table(const unsigned char *table) {
  int i;
  for (i = 0; i < GR_FOG_TABLE_SIZE; i += 2) {
    unsigned d0, d1, n0 = table[i + 1], n1 = i + 2 < GR_FOG_TABLE_SIZE ? table[i + 2] : table[i + 1];
    int s0 = ((int)n0 - (int)table[i]) * 4, s1 = ((int)n1 - (int)table[i + 1]) * 4;
    d0 = (unsigned)(s0 < 0 ? 0 : s0 > 255 ? 255 : s0);
    d1 = (unsigned)(s1 < 0 ? 0 : s1 > 255 ? 255 : s1);
    wr(R_FOGTABLE + (uint32_t)i * 2,
       ((uint32_t)table[i + 1] << 24) | ((uint32_t)d1 << 16) | ((uint32_t)table[i] << 8) | d0);
  }
}

/* grClipWindow: the pixels from minx to maxx - 1 and from miny to
 * maxy - 1 are drawn. */
static void SGL_GLIDE_CALL gr_clip_window(unsigned long minx, unsigned long miny, unsigned long maxx,
                                          unsigned long maxy) {
  wr(R_CLIPLEFTRIGHT, ((uint32_t)minx << 16) | (uint32_t)maxx);
  wr(R_CLIPLOWYHIGHY, ((uint32_t)miny << 16) | (uint32_t)maxy);
}

/* grCullMode: 0 none, 1 negative area, 2 positive area. */
static void SGL_GLIDE_CALL gr_cull_mode(int mode) { sst.cull = mode; }

/* grDitherMode: the 4 x 4 dither, the 2 x 2 dither, or none. */
static void SGL_GLIDE_CALL gr_dither_mode(int mode) {
  uint32_t set = mode == GR_DITHER_DISABLE ? 0 : mode == GR_DITHER_4x4 ? FBZ_DITHER : (FBZ_DITHER | (1u << 11));
  set_fbzmode(FBZ_DITHER | (1u << 11), set);
}

/* grTexMinAddress: texture memory starts at 0 (on a Banshee or a
 * Voodoo3, after the depth buffer). */
static unsigned long SGL_GLIDE_CALL gr_tex_min_address(int tmu) {
  (void)tmu;
  return sst.h3 ? sst.tex_min : 0;
}

/* grTexMaxAddress: the last address for a texture. */
static unsigned long SGL_GLIDE_CALL gr_tex_max_address(int tmu) {
  (void)tmu;
  return (sst.h3 ? sst.memory : TMU_MEMORY) - 8;
}

/* Gives the width and the height of level lod (GR_LOD_256 is 0) of a
 * texture with a Glide aspect: 0 is 8 x 1, 3 is 1 x 1, 6 is 1 x 8. */
static void level_size(int lod, int aspect, int *w, int *h) {
  int big = 256 >> lod, small;
  int shift = aspect < GR_ASPECT_1x1 ? GR_ASPECT_1x1 - aspect : aspect - GR_ASPECT_1x1;
  small = big >> shift;
  if (small < 1) small = 1;
  if (aspect <= GR_ASPECT_1x1)
    *w = big, *h = small;
  else
    *w = small, *h = big;
}

/* Gives the bytes of the levels from lod_from to lod_to - 1 (16-bit
 * texels). The SST-1 and the Voodoo2 keep each level of fewer than 4
 * texels as 4. */
static unsigned long levels_bytes(int lod_from, int lod_to, int aspect) {
  unsigned long n = 0;
  int lod, w, h;
  for (lod = lod_from; lod < lod_to; lod++) {
    unsigned long texels;
    level_size(lod, aspect, &w, &h);
    texels = (unsigned long)w * (unsigned long)h;
    if (!sst.h3 && lod >= 4 && texels < 4) texels = 4;
    n += texels * 2;
  }
  return n;
}

/* grTexTextureMemRequired: the bytes of the levels of info, on 8-byte
 * steps. */
static unsigned long SGL_GLIDE_CALL gr_tex_texture_mem_required(unsigned long even_odd, sgl_gr_texinfo *info) {
  (void)even_odd;
  return (levels_bytes(info->large_lod, info->small_lod + 1, info->aspect) + 7) & ~7ul;
}

/* Gives the byte address in texture memory of the level 0 of a texture
 * at start: the card finds level n after the levels 0 to n - 1, so the
 * base goes back by the bytes of the levels before large_lod. */
static unsigned long texture_base(unsigned long start, const sgl_gr_texinfo *info) {
  unsigned long base = start - levels_bytes(0, info->large_lod, info->aspect);
  return sst.h3 ? base & 0xFFFFF0ul : base & (TMU_MEMORY * 2 - 1);
}

/* Writes texBaseAddr and tLOD for a texture at start. texBaseAddr is in
 * units of 8 bytes on the SST-1 and the Voodoo2, in bytes on the
 * Banshee and the Voodoo3. */
static void set_texture_base(unsigned long start, const sgl_gr_texinfo *info, int small_lod) {
  unsigned long base = texture_base(start, info);
  uint32_t aspect = info->aspect <= GR_ASPECT_1x1 ? (uint32_t)(GR_ASPECT_1x1 - info->aspect)
                                                  : (uint32_t)(info->aspect - GR_ASPECT_1x1);
  sst.tlod = ((uint32_t)info->large_lod * 4) | ((uint32_t)small_lod * 4 << 6) | (aspect << 21) |
             (info->aspect <= GR_ASPECT_1x1 ? (1u << 20) : 0u);
  wr(CHIP_TMU0 | R_TEXBASEADDR, (uint32_t)(sst.h3 ? base : base >> 3));
  wr(CHIP_TMU0 | R_TLOD, sst.tlod);
}

/* Writes the texture format into textureMode. */
static void set_texture_format(int format) {
  sst.texmode = (sst.texmode & ~0xF00u) | ((uint32_t)(format & 15) << 8);
  wr(CHIP_TMU0 | R_TEXTUREMODE, sst.texmode);
}

/* grTexDownloadMipMap: writes the levels of info (from large_lod to
 * small_lod, each w x h 16-bit texels) at start. The card takes two
 * texels in each write. On the SST-1 and the Voodoo2, the address
 * holds the level, the row and the column. On the Banshee and the
 * Voodoo3, it is the byte offset from texBaseAddr. */
static void SGL_GLIDE_CALL gr_tex_download_mipmap(int tmu, unsigned long start, unsigned long even_odd,
                                                  sgl_gr_texinfo *info) {
  const uint16_t *src = (const uint16_t *)info->data;
  uint32_t texmode = sst.texmode, tlod = sst.tlod;
  int lod, x, y, w, h;
  (void)even_odd;
  if (tmu != GR_TMU0) return;
  set_texture_format(info->format);
  set_texture_base(start, info, info->small_lod);
  if (sst.h3) {
    uint32_t off = (uint32_t)(start - texture_base(start, info));
    volatile uint32_t *port = (volatile uint32_t *)(sst.base + H3_TEXTURE);
    for (lod = info->large_lod; lod <= info->small_lod; lod++) {
      level_size(lod, info->aspect, &w, &h);
      /* Only the last level (1 x 1) has an odd number of texels. */
      for (x = 0; x < w * h; x += 2, off += 4)
        port[off >> 2] = src[x] | (x + 1 < w * h ? (uint32_t)src[x + 1] << 16 : 0u);
      src += w * h;
    }
  }
  for (lod = sst.h3 ? info->small_lod + 1 : info->large_lod; lod <= info->small_lod; lod++) {
    level_size(lod, info->aspect, &w, &h);
    for (y = 0; y < h; y++) {
      volatile uint32_t *row =
          (volatile uint32_t *)(sst.base + 0x800000ul + ((uint32_t)lod << 17) + ((uint32_t)y << 9));
      for (x = 0; x < w; x += 2) {
        uint32_t t0 = src[x], t1 = x + 1 < w ? src[x + 1] : 0;
        row[x >> 1] = t0 | (t1 << 16);
      }
      src += w;
    }
  }
  /* The next draw sets the texture again. */
  sst.texmode = texmode, sst.tlod = tlod;
  wr(CHIP_TMU0 | R_TEXTUREMODE, sst.texmode);
  wr(CHIP_TMU0 | R_TLOD, sst.tlod);
}

/* The levels that the last grTexSource gave, for grTexMipMapMode. */
static sgl_gr_texinfo source_info;
static unsigned long source_start;
static int mipmaps;

/* grTexSource: the texture of TMU 0 for the next triangles. */
static void SGL_GLIDE_CALL gr_tex_source(int tmu, unsigned long start, unsigned long even_odd, sgl_gr_texinfo *info) {
  (void)even_odd;
  if (tmu != GR_TMU0) return;
  source_info = *info;
  source_start = start;
  set_texture_format(info->format);
  set_texture_base(start, info, mipmaps ? info->small_lod : info->large_lod);
}

/* grTexFilterMode: point or bilinear for minification and magnification
 * (bits 1 and 2 of textureMode). */
static void SGL_GLIDE_CALL gr_tex_filter_mode(int tmu, int min, int mag) {
  if (tmu != GR_TMU0) return;
  sst.texmode = (sst.texmode & ~6u) | (min == GR_TEXTUREFILTER_BILINEAR ? 2u : 0u) |
                (mag == GR_TEXTUREFILTER_BILINEAR ? 4u : 0u);
  wr(CHIP_TMU0 | R_TEXTUREMODE, sst.texmode);
}

/* grTexClampMode: wrap or clamp in s and t (bits 6 and 7). */
static void SGL_GLIDE_CALL gr_tex_clamp_mode(int tmu, int s, int t) {
  if (tmu != GR_TMU0) return;
  sst.texmode = (sst.texmode & ~0xC0u) | (s == GR_TEXTURECLAMP_CLAMP ? 0x40u : 0u) |
                (t == GR_TEXTURECLAMP_CLAMP ? 0x80u : 0u);
  wr(CHIP_TMU0 | R_TEXTUREMODE, sst.texmode);
}

/* grTexMipMapMode: with GR_MIPMAP_DISABLE only the large level is
 * used; else all levels. */
static void SGL_GLIDE_CALL gr_tex_mipmap_mode(int tmu, int mode, int lod_blend) {
  (void)lod_blend;
  if (tmu != GR_TMU0) return;
  mipmaps = mode != GR_MIPMAP_DISABLE;
  if (source_info.data || source_info.large_lod || source_info.small_lod)
    set_texture_base(source_start, &source_info, mipmaps ? source_info.small_lod : source_info.large_lod);
}

/* Gives the byte offset in video memory of a buffer of a Banshee or a
 * Voodoo3. */
static uint32_t h3_buffer(int buffer) {
  if (buffer == GR_BUFFER_AUXBUFFER) return sst.aux;
  return sst.buffer[buffer == GR_BUFFER_BACKBUFFER ? sst.back : sst.front];
}

/* Copies the rows of a buffer of a Banshee or a Voodoo3 to the shadow
 * copy (to_card 0) or back (to_card 1), in the row order of origin. */
static void h3_copy(int buffer, int origin, int to_card) {
  uint32_t base = h3_buffer(buffer);
  int x, y;
  for (y = 0; y < sst.height; y++) {
    int row = origin == GR_ORIGIN_LOWER_LEFT ? sst.height - 1 - y : y;
    volatile uint16_t *p = (volatile uint16_t *)(sst.lfb + base + (uint32_t)row * sst.pitch);
    uint16_t *q = sst.shadow + (long)y * sst.width;
    if (to_card)
      for (x = 0; x < sst.width; x++) p[x] = q[x];
    else
      for (x = 0; x < sst.width; x++) q[x] = p[x];
  }
}

/* grLfbLock for the Banshee and the Voodoo3: gives a copy of the buffer
 * in system memory (RGB 565 or the depth values). A write lock copies
 * the pixels to the card in grLfbUnlock. */
static int h3_lfb_lock(int type, int buffer, int write_mode, int origin, sgl_gr_lfbinfo *info) {
  if (!sst.shadow) sst.shadow = (uint16_t *)malloc((size_t)sst.width * (size_t)sst.height * 2u);
  if (!sst.shadow) return 0;
  idle();
  sst.lock_origin = origin;
  if (type == GR_LFB_READ_ONLY) h3_copy(buffer, origin, 0);
  info->ptr = sst.shadow;
  info->stride = (unsigned long)sst.width * 2ul;
  info->write_mode = write_mode;
  info->origin = origin;
  return 1;
}

/* grLfbLock: gives a pointer to a buffer of the card. Reads give 16-bit
 * RGB 565 (the color buffers) or 16-bit depth (the aux buffer). Writes
 * take RGB 565 for a color buffer, or depth (ZA16) for the aux buffer.
 * The origin of the pointer is the upper left corner. */
static int SGL_GLIDE_CALL gr_lfb_lock(int type, int buffer, int write_mode, int origin, int pixel_pipeline,
                                      sgl_gr_lfbinfo *info) {
  uint32_t mode;
  int sel = buffer == GR_BUFFER_AUXBUFFER ? 2 : buffer == GR_BUFFER_BACKBUFFER ? 1 : 0;
  (void)pixel_pipeline;
  if (!sst.open) return 0;
  if (sst.h3) return h3_lfb_lock(type, buffer, write_mode, origin, info);
  idle();
  mode = origin == GR_ORIGIN_LOWER_LEFT ? (1u << 13) : 0u;
  if (type == GR_LFB_WRITE_ONLY) {
    if (buffer == GR_BUFFER_AUXBUFFER)
      mode |= GR_LFBWRITEMODE_ZA16 | (1u << 4);
    else
      mode |= (write_mode == GR_LFBWRITEMODE_ANY ? GR_LFBWRITEMODE_565 : (uint32_t)write_mode) | ((uint32_t)sel << 4);
  } else {
    mode |= (uint32_t)sel << 6;
  }
  wr(R_LFBMODE, mode);
  info->ptr = (void *)(sst.base + 0x400000ul);
  info->stride = 2048;
  info->write_mode = write_mode;
  info->origin = origin;
  return 1;
}

/* grLfbUnlock: waits until the writes are in the buffer. On a Banshee
 * or a Voodoo3, it copies the pixels of a write lock to the card. */
static int SGL_GLIDE_CALL gr_lfb_unlock(int type, int buffer) {
  if (sst.h3) {
    if (sst.open && sst.shadow && type == GR_LFB_WRITE_ONLY) h3_copy(buffer, sst.lock_origin, 1);
    return 1;
  }
  idle();
  return 1;
}

/* Fills api with the functions of this file. Gives 0 when no Voodoo
 * card is on the PCI bus. */
int sgl_glide_sst1(sgl_glide_api *api) {
  sgl_pci_device d;
  if (!api || !sgl_pci_find(sst_ids, 0, &d)) return 0;
  memset(api, 0, sizeof *api);
  api->GlideInit = gr_glide_init;
  api->GlideShutdown = gr_glide_shutdown;
  api->SstQueryHardware = gr_sst_query_hardware;
  api->SstSelect = gr_sst_select;
  api->SstWinOpen = gr_sst_win_open;
  api->SstWinClose = gr_sst_win_close;
  api->SstIdle = gr_sst_idle;
  api->BufferClear = gr_buffer_clear;
  api->BufferSwap = gr_buffer_swap;
  api->DrawTriangle = gr_draw_triangle;
  api->DrawLine = gr_draw_line;
  api->DrawPoint = gr_draw_point;
  api->DepthBufferMode = gr_depth_buffer_mode;
  api->DepthBufferFunction = gr_depth_buffer_function;
  api->DepthMask = gr_depth_mask;
  api->ColorMask = gr_color_mask;
  api->ColorCombine = gr_color_combine;
  api->AlphaCombine = gr_alpha_combine;
  api->TexCombine = gr_tex_combine;
  api->AlphaBlendFunction = gr_alpha_blend_function;
  api->AlphaTestFunction = gr_alpha_test_function;
  api->AlphaTestReferenceValue = gr_alpha_test_reference_value;
  api->FogMode = gr_fog_mode;
  api->FogColorValue = gr_fog_color_value;
  api->FogTable = gr_fog_table;
  api->ClipWindow = gr_clip_window;
  api->CullMode = gr_cull_mode;
  api->DitherMode = gr_dither_mode;
  api->TexMinAddress = gr_tex_min_address;
  api->TexMaxAddress = gr_tex_max_address;
  api->TexTextureMemRequired = gr_tex_texture_mem_required;
  api->TexDownloadMipMap = gr_tex_download_mipmap;
  api->TexSource = gr_tex_source;
  api->TexFilterMode = gr_tex_filter_mode;
  api->TexClampMode = gr_tex_clamp_mode;
  api->TexMipMapMode = gr_tex_mipmap_mode;
  api->LfbLock = gr_lfb_lock;
  api->LfbUnlock = gr_lfb_unlock;
  api->library = NULL;
  return 1;
}

#endif
