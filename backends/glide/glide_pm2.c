/* glide_pm2.c - the Glide 2 functions of the Glide back end, on the 3D
 * engine of a 3Dlabs Permedia 2 or Permedia 2v card (TI TVP4020), for
 * 32-bit DOS. sgl_glide_pm2 fills an sgl_glide_api, and the Glide back
 * end (glide.c) uses it as it uses a Voodoo.
 *
 * The Permedia 2 has the Delta set-up unit in the chip. The driver
 * writes the three vertices of a triangle to the vertex store of the
 * Delta unit as IEEE floats, then the DrawTriangle command. The Delta
 * unit calculates the edges and the gradients, and the rasterizer draws
 * the triangle.
 *
 * The driver sets the limits of the table (SGL_GLIDE_LIMIT_), and
 * softgl then draws in software what the card cannot do:
 *
 * - No alpha test.
 * - The only blend is (source alpha, 1 - source alpha).
 * - No mipmaps: the card uses the largest level of a texture.
 * - One texture unit.
 *
 * The screen is a VBE mode of 16-bit color (5:6:5) with its linear
 * frame buffer. Video memory holds three color buffers (two when the
 * memory is small), the 16-bit Z buffer (the "localbuffer"), and then
 * the textures. The width of the screen must be a width that the
 * partial products of the card can make (640, 800 and 1024 are).
 *
 * The register facts come from the TVP4020 PERMEDIA 2 Programmer's
 * Reference Manual of Texas Instruments. 86Box does not emulate the
 * card, and the driver is not tested.
 *
 * Part of clang-retro softgl, a software implementation of OpenGL.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#if defined(__MSDOS__) && defined(__i386__)

#include <dos.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "glide_api.h"
#include "softgl_dos.h"
#include "softgl_glide.h"

/* The control registers (byte offsets in PCI region 0). */
enum {
  R_IN_FIFO_SPACE = 0x0018,  /* the free entries of the input FIFO */
  R_OUT_FIFO_WORDS = 0x0020, /* the words in the output FIFO */
  R_FIFO_DISCON = 0x0068,    /* bit 0: PCI disconnect when the FIFO is full */
  R_OUT_FIFO = 0x2000
};

/* The graphics registers, by their tags. The register of tag t is at
 * the byte offset 0x8000 + 8 t in region 0. */
enum {
  T_RENDER = 0x007,
  T_RASTERIZER_MODE = 0x014,
  T_WAIT_FOR_COMPLETION = 0x017,
  T_RECTANGLE_ORIGIN = 0x01A,
  T_RECTANGLE_SIZE = 0x01B,
  T_SCISSOR_MODE = 0x030,
  T_SCISSOR_MIN = 0x031,
  T_SCISSOR_MAX = 0x032,
  T_SCREEN_SIZE = 0x033,
  T_AREA_STIPPLE_MODE = 0x034,
  T_WINDOW_ORIGIN = 0x039,
  T_TEXTURE_ADDRESS_MODE = 0x070,
  T_TEXTURE_BASE = 0x0B0,
  T_TEXTURE_MAP_FORMAT = 0x0B1,
  T_TEXTURE_DATA_FORMAT = 0x0B2,
  T_TEXTURE_READ_MODE = 0x0CE,
  T_TEXEL_LUT_MODE = 0x0CF,
  T_TEXTURE_COLOR_MODE = 0x0D0,
  T_FOG_MODE = 0x0D2,
  T_FOG_COLOR = 0x0D3,
  T_COLOR_DDA_MODE = 0x0FC,
  T_ALPHA_BLEND_MODE = 0x102,
  T_DITHER_MODE = 0x103,
  T_FB_SOFTWARE_WRITE_MASK = 0x104,
  T_LOGICAL_OP_MODE = 0x105,
  T_FB_WRITE_DATA = 0x106,
  T_LB_READ_MODE = 0x110,
  T_LB_READ_FORMAT = 0x111,
  T_LB_SOURCE_OFFSET = 0x112,
  T_LB_WINDOW_BASE = 0x117,
  T_LB_WRITE_MODE = 0x118,
  T_LB_WRITE_FORMAT = 0x119,
  T_WINDOW = 0x130,
  T_STENCIL_MODE = 0x131,
  T_DEPTH_MODE = 0x134,
  T_FB_READ_MODE = 0x150,
  T_FB_SOURCE_OFFSET = 0x151,
  T_FB_PIXEL_OFFSET = 0x152,
  T_FB_WINDOW_BASE = 0x156,
  T_FB_WRITE_MODE = 0x157,
  T_FB_HARDWARE_WRITE_MASK = 0x158,
  T_FB_READ_PIXEL = 0x15A,
  T_FILTER_MODE = 0x180,
  T_STATISTIC_MODE = 0x181,
  T_SYNC = 0x188,
  T_YUV_MODE = 0x1E0,
  T_V0_FLOAT = 0x230, /* vertex n at T_V0_FLOAT + 16 n */
  T_DELTA_MODE = 0x260,
  T_DRAW_TRIANGLE = 0x261
};

/* The parameters of a vertex in the vertex store of the Delta unit. */
enum { P_S = 0, P_T = 1, P_Q = 2, P_R = 5, P_G = 6, P_B = 7, P_A = 8, P_F = 9, P_X = 10, P_Y = 11, P_Z = 12 };

/* The Render and DrawTriangle commands. */
#define RENDER_TRAPEZOID (1u << 6)
#define RENDER_RECTANGLE (3u << 6)
#define RENDER_TEXTURE (1u << 13)
#define RENDER_FOG (1u << 14)
#define RENDER_SUBPIXEL (1u << 16)
#define RENDER_INCREASE_X (1u << 21)
#define RENDER_INCREASE_Y (1u << 22)

/* DeltaMode: 16-bit depth, fog, texture, Gouraud shading, depth, sub
 * pixel correction, the clamp of the parameters, and the normalization
 * of S, T and Q. */
#define DELTA_MODE ((1u << 2) | (1u << 4) | (1u << 5) | (1u << 6) | (1u << 7) | (1u << 10) | (1u << 13) | (2u << 14))

/* The color format 5:6:5 (format 16: the extension bit and 0) in the
 * RGB order, without an alpha buffer, for AlphaBlendMode and for
 * DitherMode. */
#define BLEND_FORMAT_565 ((1u << 16) | (1u << 12) | (1u << 13))
#define BLEND_SRC_ALPHA_ONE_MINUS_SRC_ALPHA (84u << 1)
#define DITHER_FORMAT_565 ((1u << 16) | (1u << 10))

/* The texture formats of TextureDataFormat, in the RGB order. */
#define TEX_DATA_565 ((1u << 6) | (1u << 5) | (1u << 4))
#define TEX_DATA_4444 (2u | (1u << 5))
#define TEX_DATA_NO_ALPHA (1u << 4)

/* The application modes of TextureColorMode (the OpenGL type). */
enum { APPLY_MODULATE = 0, APPLY_DECAL = 1, APPLY_COPY = 3 };

/* The color combines of glide.c, as the driver knows them. */
enum { COMBINE_COLOR, COMBINE_REPLACE, COMBINE_MODULATE, COMBINE_DECAL };

/* The alpha of a textured triangle. */
enum { ALPHA_VERTEX, ALPHA_TEXTURE, ALPHA_PRODUCT };

/* The state registers that the driver keeps, in the order of last[]. */
enum {
  S_SCISSOR_MIN,
  S_SCISSOR_MAX,
  S_FB_WINDOW_BASE,
  S_FB_READ_MODE,
  S_FB_WRITE_MODE,
  S_LB_READ_MODE,
  S_LB_WRITE_MODE,
  S_WINDOW,
  S_DEPTH_MODE,
  S_COLOR_DDA_MODE,
  S_ALPHA_BLEND_MODE,
  S_DITHER_MODE,
  S_LOGICAL_OP_MODE,
  S_FOG_MODE,
  S_FOG_COLOR,
  S_TEXTURE_ADDRESS_MODE,
  S_TEXTURE_READ_MODE,
  S_TEXTURE_DATA_FORMAT,
  S_TEXTURE_COLOR_MODE,
  S_COUNT
};
static const uint16_t state_tag[S_COUNT] = {
    T_SCISSOR_MIN,   T_SCISSOR_MAX,     T_FB_WINDOW_BASE,       T_FB_READ_MODE,      T_FB_WRITE_MODE,
    T_LB_READ_MODE,  T_LB_WRITE_MODE,   T_WINDOW,               T_DEPTH_MODE,        T_COLOR_DDA_MODE,
    T_ALPHA_BLEND_MODE, T_DITHER_MODE,  T_LOGICAL_OP_MODE,      T_FOG_MODE,          T_FOG_COLOR,
    T_TEXTURE_ADDRESS_MODE, T_TEXTURE_READ_MODE, T_TEXTURE_DATA_FORMAT, T_TEXTURE_COLOR_MODE};

/* The state of the driver. */
static struct {
  sgl_pci_device pci;
  volatile uint8_t *lfb;  /* video memory (region 1) */
  volatile uint8_t *regs; /* the registers (region 0, 128 KB) */
  uint32_t lfb_phys, memory;
  int open, width, height, stride, origin;
  uint32_t pp;        /* the partial products of the screen width */
  uint32_t buffer[3]; /* the byte offsets of the color buffers */
  int buffers;        /* 3, or 2 when video memory is small */
  int back;           /* the color buffer that is drawn */
  int front;          /* the color buffer that is shown */
  uint32_t zbuffer, tex_min;
  /* The state that Glide sets. */
  int depth, depth_func, depth_mask, color_mask, fog, dither, color_combine, alpha_mode;
  int blend_src, blend_dst;
  uint8_t fog_table[GR_FOG_TABLE_SIZE];
  float fog_w[GR_FOG_TABLE_SIZE];
  uint32_t fog_color; /* in the order of FogColor: red in bits 0 to 7 */
  int clip[4];        /* minx, miny, maxx, maxy (Glide coordinates) */
  /* The texture that TexSource selected. */
  int textured, tex_wlog, tex_hlog, tex_large, tex_alpha, tex_linear;
  uint32_t tex_wrap;
  /* The values that the driver wrote last to the state registers
   * (0xFFFFFFFF: unknown). */
  uint32_t last[S_COUNT];
  uint16_t *shadow; /* the copy of a locked buffer */
  int lock_origin;
} pm;

/* The PCI IDs of the Permedia 2 (TI and 3Dlabs) and of the Permedia 2v. */
static const sgl_pci_id pm2_ids[] = {{0x104C, 0x3D07}, {0x3D3D, 0x0007}, {0x3D3D, 0x0009}, {0, 0}};

/* Reads a 32-bit control register. */
static uint32_t rd(uint32_t reg) { return *(volatile uint32_t *)(pm.regs + reg); }

/* Writes a 32-bit control register. */
static void wr(uint32_t reg, uint32_t value) { *(volatile uint32_t *)(pm.regs + reg) = value; }

/* Writes the graphics register of tag t. The write goes into the input
 * FIFO of the card. */
static void gr(uint32_t t, uint32_t value) { *(volatile uint32_t *)(pm.regs + 0x8000u + t * 8u) = value; }

/* Waits until the input FIFO has n free entries. Stops after about one
 * second. */
static void fifo(unsigned n) {
  uint32_t start = sgl_dos_ms();
  while (rd(R_IN_FIFO_SPACE) < n && sgl_dos_ms() - start < 1000) {
  }
}

/* Waits until the card has done all the commands in the input FIFO: it
 * sends a Sync command and waits for the Sync tag at the output FIFO.
 * Stops after about one second. */
static void idle(void) {
  uint32_t start = sgl_dos_ms();
  if (!pm.regs) return;
  fifo(2);
  gr(T_FILTER_MODE, 0xC00u); /* the tag and the data of Sync only */
  gr(T_SYNC, 0);
  while (sgl_dos_ms() - start < 1000) {
    if (!rd(R_OUT_FIFO_WORDS)) continue;
    if (rd(R_OUT_FIFO) == T_SYNC) {
      /* The data word of the Sync tag. */
      while (!rd(R_OUT_FIFO_WORDS) && sgl_dos_ms() - start < 1000) {
      }
      (void)rd(R_OUT_FIFO);
      break;
    }
  }
}

/* Gives the screen row of a Glide y. */
static float screen_y(float y) { return pm.origin == GR_ORIGIN_LOWER_LEFT ? (float)pm.height - y : y; }

/* Gives the partial products (PP0, PP1 and PP2 in bits 0 to 8) for a
 * width in pixels, or 0xFFFFFFFF when the card cannot make the width
 * (Appendix C of the manual). A partial product n is 2^(n + 4) pixels. */
static uint32_t partial_products(int width) {
  static const uint16_t widths[] = {32,  64,  96,  128, 160, 192, 224,  256,  288,  320,  384,
                                    416, 448, 512, 544, 576, 640, 768,  800,  832,  896,  1024,
                                    1056, 1088, 1152, 1280, 1536, 1568, 1600, 1664, 1792, 2048};
  static const uint8_t pp[][3] = {{1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {2, 1, 1}, {2, 2, 1}, {2, 2, 2}, {3, 2, 1}, {3, 2, 2},
                                  {3, 3, 1}, {3, 3, 2}, {3, 3, 3}, {4, 3, 1}, {4, 3, 2}, {4, 3, 3}, {4, 4, 1}, {4, 4, 2},
                                  {4, 4, 3}, {4, 4, 4}, {5, 4, 1}, {5, 4, 2}, {5, 4, 3}, {5, 4, 4}, {5, 5, 1}, {5, 5, 2},
                                  {5, 5, 3}, {5, 5, 4}, {5, 5, 5}, {6, 5, 1}, {6, 5, 2}, {6, 5, 3}, {6, 5, 4}, {6, 5, 5}};
  unsigned i;
  for (i = 0; i < sizeof widths / sizeof widths[0]; i++)
    if (widths[i] == width) return (uint32_t)pp[i][0] | ((uint32_t)pp[i][1] << 3) | ((uint32_t)pp[i][2] << 6);
  return 0xFFFFFFFFu;
}

/* grGlideInit: finds the card, maps the registers (region 0, 128 KB)
 * and sets the PCI disconnect, thus a write to a full FIFO waits. */
static void SGL_GLIDE_CALL gr_glide_init(void) {
  uint32_t regs_phys;
  memset(&pm, 0, sizeof pm);
  if (!sgl_pci_find(pm2_ids, 0, &pm.pci)) return;
  regs_phys = sgl_pci_bar(&pm.pci, 0);
  pm.lfb_phys = sgl_pci_bar(&pm.pci, 1);
  if (!regs_phys || !pm.lfb_phys) return;
  sgl_pci_write16(&pm.pci, 0x04, (uint16_t)(sgl_pci_read16(&pm.pci, 0x04) | 0x0002));
  pm.regs = (volatile uint8_t *)sgl_map_physical(regs_phys, 0x20000ul);
  if (pm.regs) wr(R_FIFO_DISCON, 1);
}

/* grGlideShutdown: closes the screen and frees the mappings. */
static void SGL_GLIDE_CALL gr_glide_shutdown(void);

/* grSstQueryHardware: one board with one TMU, as a Voodoo Graphics
 * (GrHwConfiguration as 32-bit words). */
static int SGL_GLIDE_CALL gr_sst_query_hardware(void *config) {
  int32_t *hw = (int32_t *)config;
  if (!pm.regs) return 0;
  hw[0] = 1;
  hw[1] = GR_SSTTYPE_VOODOO;
  hw[2] = 4;
  hw[3] = 2;
  hw[4] = 1;
  return 1;
}

/* grSstSelect: there is one board. */
static void SGL_GLIDE_CALL gr_sst_select(int which) { (void)which; }

/* Gives the VBE mode of 16-bit color for a Glide resolution, and its
 * size in *w and *h, or -1. */
static int vbe_mode(int resolution, int *w, int *h) {
  switch (resolution) {
    case GR_RESOLUTION_320x200: *w = 320, *h = 200; return 0x10E;
    case GR_RESOLUTION_640x480: *w = 640, *h = 480; return 0x111;
    case GR_RESOLUTION_800x600: *w = 800, *h = 600; return 0x114;
    case GR_RESOLUTION_1024x768: *w = 1024, *h = 768; return 0x117;
    default: return -1;
  }
}

/* Forgets the values of the state registers. */
static void forget(void) {
  int i;
  for (i = 0; i < S_COUNT; i++) pm.last[i] = 0xFFFFFFFFu;
}

/* Writes the state register i when its value changes. Gives 1 when it
 * wrote the register. */
static int state(int i, uint32_t value) {
  if (pm.last[i] == value) return 0;
  pm.last[i] = value;
  gr(state_tag[i], value);
  return 1;
}

/* grClipWindow: the clip rectangle (the maxima are not in it). */
static void SGL_GLIDE_CALL gr_clip_window(unsigned long minx, unsigned long miny, unsigned long maxx,
                                          unsigned long maxy) {
  pm.clip[0] = (int)minx, pm.clip[1] = (int)miny, pm.clip[2] = (int)maxx, pm.clip[3] = (int)maxy;
}

/* Sets the registers that do not change while the screen is open: the
 * screen scissor, the pixel size, the formats of the Z buffer, the
 * Delta unit, and the units that the driver does not use. */
static void setup(void) {
  fifo(40);
  gr(T_RASTERIZER_MODE, 0); /* no bias: the Delta unit gives the coordinates */
  gr(T_SCISSOR_MODE, 3);    /* the user scissor and the screen scissor */
  gr(T_SCREEN_SIZE, ((uint32_t)pm.height << 16) | (uint32_t)pm.width);
  gr(T_WINDOW_ORIGIN, 0);
  gr(T_AREA_STIPPLE_MODE, 0);
  gr(T_STENCIL_MODE, 0);
  gr(T_TEXEL_LUT_MODE, 0);
  gr(T_YUV_MODE, 0);
  gr(T_STATISTIC_MODE, 0);
  gr(T_FILTER_MODE, 0xC00u);
  gr(T_FB_READ_PIXEL, 1); /* 16-bit pixels */
  gr(T_FB_SOURCE_OFFSET, 0);
  gr(T_FB_PIXEL_OFFSET, 0);
  gr(T_FB_SOFTWARE_WRITE_MASK, 0xFFFFFFFFu);
  gr(T_FB_HARDWARE_WRITE_MASK, 0xFFFFFFFFu);
  gr(T_LB_READ_FORMAT, 0); /* 16-bit depth, no stencil */
  gr(T_LB_WRITE_FORMAT, 0);
  gr(T_LB_SOURCE_OFFSET, 0);
  gr(T_LB_WINDOW_BASE, pm.zbuffer / 2u);
  gr(T_TEXTURE_MAP_FORMAT, 0);
  gr(T_DELTA_MODE, DELTA_MODE);
}

/* grSstWinOpen: sets the VBE mode, maps the frame buffer, divides
 * video memory and sets the engine. 16-bit color only; the other
 * arguments are not used. */
static int SGL_GLIDE_CALL gr_sst_win_open(unsigned long hwnd, int resolution, int refresh, int color_format,
                                          int origin, int color_buffers, int aux_buffers) {
  sgl_vbe_mode info;
  uint32_t frame, need, pp;
  int mode, w, h, i;
  (void)hwnd, (void)refresh, (void)color_format, (void)color_buffers, (void)aux_buffers;
  if (!pm.regs || pm.open) return 0;
  mode = vbe_mode(resolution, &w, &h);
  if (mode < 0 || !sgl_vbe_mode_info(mode, &info) || !(info.attributes & 1) || info.bits != 16) return 0;
  /* The card addresses a buffer with the partial products of its width
   * in pixels: the row pitch must be such a width. */
  pp = partial_products(info.stride / 2);
  if (pp == 0xFFFFFFFFu || info.stride & 1) return 0;
  frame = ((uint32_t)info.stride * (uint32_t)h + 63u) & ~63u;
  pm.memory = sgl_vbe_memory();
  if (!pm.memory) pm.memory = 0x400000ul;
  need = 3 * frame + 0x20000ul;
  if (need > pm.memory) return 0;
  if (!(info.attributes & 0x80) || !sgl_vbe_set_mode(mode, 1)) return 0;
  if (info.lfb) pm.lfb_phys = info.lfb;
  pm.lfb = (volatile uint8_t *)sgl_map_physical(pm.lfb_phys, pm.memory);
  if (!pm.lfb) {
    sgl_vbe_text_mode();
    return 0;
  }
  pm.width = w, pm.height = h, pm.stride = info.stride, pm.origin = origin, pm.pp = pp;
  /* Three color buffers when the textures keep 1 MB: the card draws
   * into the third while the monitor can still show the old front. */
  pm.buffers = need + frame + 0xC0000ul <= pm.memory ? 3 : 2;
  for (i = 0; i < pm.buffers; i++) pm.buffer[i] = (uint32_t)i * (uint32_t)info.stride * (uint32_t)h;
  pm.zbuffer = (pm.buffer[pm.buffers - 1] + (uint32_t)info.stride * (uint32_t)h + 63u) & ~63u;
  pm.tex_min = pm.zbuffer + frame;
  pm.front = 0;
  pm.back = 1;
  pm.clip[0] = 0, pm.clip[1] = 0, pm.clip[2] = w, pm.clip[3] = h;
  pm.depth_func = GR_CMP_LESS;
  pm.depth_mask = 1;
  pm.color_mask = 1;
  pm.dither = 1;
  pm.blend_src = GR_BLEND_ONE, pm.blend_dst = GR_BLEND_ZERO;
  pm.open = 1;
  forget();
  setup();
  idle();
  sgl_vbe_display_start(0, 0);
  return 1;
}

/* grSstWinClose: sets the text mode again and frees the frame buffer. */
static void SGL_GLIDE_CALL gr_sst_win_close(void) {
  if (!pm.open) return;
  idle();
  sgl_vbe_text_mode();
  sgl_unmap_physical(pm.lfb);
  pm.lfb = NULL;
  free(pm.shadow);
  pm.shadow = NULL;
  pm.open = 0;
}

/* grGlideShutdown: closes the screen and frees the mappings. */
static void SGL_GLIDE_CALL gr_glide_shutdown(void) {
  gr_sst_win_close();
  if (pm.regs) sgl_unmap_physical(pm.regs);
  memset(&pm, 0, sizeof pm);
}

/* grSstIdle: waits until the card has drawn all. */
static void SGL_GLIDE_CALL gr_sst_idle(void) { idle(); }

/* Writes the user scissor: the clip window, in screen rows. */
static void scissor(void) {
  int top = pm.origin == GR_ORIGIN_LOWER_LEFT ? pm.height - pm.clip[3] : pm.clip[1];
  int bottom = top + (pm.clip[3] - pm.clip[1]);
  state(S_SCISSOR_MIN, ((uint32_t)(top & 0xFFF) << 16) | (uint32_t)(pm.clip[0] & 0xFFF));
  state(S_SCISSOR_MAX, ((uint32_t)(bottom & 0xFFF) << 16) | (uint32_t)(pm.clip[2] & 0xFFF));
}

/* Fills the clip window of the buffer at the byte offset off with the
 * 16-bit value value. The card writes the constant value (the raw
 * pixel) to each pixel of a rectangle, with all the other units off.
 * This works with SGRAM and with SDRAM. */
static void fill(uint32_t off, uint16_t value) {
  int top = pm.origin == GR_ORIGIN_LOWER_LEFT ? pm.height - pm.clip[3] : pm.clip[1];
  int w = pm.clip[2] - pm.clip[0], h = pm.clip[3] - pm.clip[1];
  if (w <= 0 || h <= 0) return;
  fifo(30);
  scissor();
  state(S_FB_WINDOW_BASE, off / 2u);
  state(S_FB_READ_MODE, pm.pp);
  state(S_FB_WRITE_MODE, 1);
  state(S_LB_READ_MODE, pm.pp);
  state(S_LB_WRITE_MODE, 0);
  state(S_WINDOW, (1u << 4) | (1u << 18)); /* no localbuffer update */
  state(S_DEPTH_MODE, 0);
  state(S_COLOR_DDA_MODE, 1); /* flat */
  state(S_ALPHA_BLEND_MODE, BLEND_FORMAT_565);
  state(S_DITHER_MODE, 1 | DITHER_FORMAT_565); /* no dither */
  state(S_FOG_MODE, 0);
  state(S_TEXTURE_ADDRESS_MODE, 0);
  state(S_TEXTURE_READ_MODE, 0);
  state(S_TEXTURE_COLOR_MODE, 0);
  gr(T_FB_WRITE_DATA, (uint32_t)value | ((uint32_t)value << 16));
  state(S_LOGICAL_OP_MODE, 1u << 5); /* the constant FBWriteData */
  gr(T_RECTANGLE_ORIGIN, ((uint32_t)top << 16) | (uint32_t)pm.clip[0]);
  gr(T_RECTANGLE_SIZE, ((uint32_t)h << 16) | (uint32_t)w);
  gr(T_RENDER, RENDER_RECTANGLE | RENDER_INCREASE_X | RENDER_INCREASE_Y);
}

/* Converts a Glide color (0xAARRGGBB) to a 5:6:5 pixel. */
static uint16_t pixel565(unsigned long c) {
  return (uint16_t)(((c >> 8) & 0xF800u) | ((c >> 5) & 0x07E0u) | ((c >> 3) & 0x001Fu));
}

/* grBufferClear: fills the clip window of the back buffer with color,
 * and of the Z buffer with depth when the depth writes are on. The Z
 * buffer is in the same memory as the color buffers, and the fill
 * writes it as a buffer of 16-bit pixels. */
static void SGL_GLIDE_CALL gr_buffer_clear(unsigned long color, unsigned alpha, unsigned depth) {
  (void)alpha;
  if (!pm.open) return;
  if (pm.color_mask) fill(pm.buffer[pm.back], pixel565(color));
  if (pm.depth && pm.depth_mask) fill(pm.zbuffer, (uint16_t)(depth & 0xFFFFu));
}

/* Waits until the monitor starts the next frame: the end of the
 * vertical retrace (bit 3 of port 3DAh), then the next retrace. The
 * card reads a new display start at the retrace. */
static void next_frame(void) {
  uint32_t start = sgl_dos_ms();
  while ((inportb(0x3DA) & 8) && sgl_dos_ms() - start < 100) {
  }
  while (!(inportb(0x3DA) & 8) && sgl_dos_ms() - start < 100) {
  }
}

/* grBufferSwap: shows the back buffer. With interval 1 or more, the
 * change waits for the vertical retrace. The next back buffer is a
 * buffer that the monitor does not show: with three buffers the one
 * that is not the old front; with two, the old front after the
 * monitor starts the new frame. */
static void SGL_GLIDE_CALL gr_buffer_swap(int interval) {
  int old = pm.front;
  if (!pm.open) return;
  idle();
  sgl_vbe_display_start((int)(pm.buffer[pm.back] / (uint32_t)pm.stride), interval > 0);
  pm.front = pm.back;
  if (pm.buffers == 3) {
    pm.back = 3 - pm.front - old;
  } else {
    if (interval > 0) next_frame();
    pm.back = old;
  }
}

/* Gives the fog visibility of a vertex (255 is no fog) from the Glide
 * fog table, by the eye depth w = 1 / oow. */
static float fog_visibility(float oow) {
  float w = oow > 0.0f ? 1.0f / oow : 1e30f, f;
  int i;
  if (w <= pm.fog_w[0]) return 255.0f - pm.fog_table[0];
  for (i = 1; i < GR_FOG_TABLE_SIZE; i++)
    if (w < pm.fog_w[i]) break;
  if (i == GR_FOG_TABLE_SIZE) return 255.0f - pm.fog_table[GR_FOG_TABLE_SIZE - 1];
  f = (w - pm.fog_w[i - 1]) / (pm.fog_w[i] - pm.fog_w[i - 1]);
  return 255.0f - ((float)pm.fog_table[i - 1] + f * ((float)pm.fog_table[i] - (float)pm.fog_table[i - 1]));
}

/* Gives v limited to lo to hi. */
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

/* Gives the 32 bits of the IEEE single float f. */
static uint32_t float_bits(float f) {
  uint32_t u;
  memcpy(&u, &f, sizeof u);
  return u;
}

/* Gives the application mode of the texture, and in *no_alpha 1 when
 * the card must use 1 as the texture alpha. GL_REPLACE with the
 * texture alpha is the copy; GL_REPLACE with the vertex alpha (an RGB
 * texture) is the decal of a texture without alpha. */
static uint32_t apply_mode(int *no_alpha) {
  *no_alpha = !pm.tex_alpha;
  switch (pm.color_combine) {
    case COMBINE_MODULATE:
      if (pm.alpha_mode == ALPHA_VERTEX) *no_alpha = 1;
      return APPLY_MODULATE;
    case COMBINE_DECAL: return APPLY_DECAL;
    default:
      if (pm.alpha_mode == ALPHA_TEXTURE && pm.tex_alpha) return APPLY_COPY;
      *no_alpha = 1;
      return APPLY_DECAL;
  }
}

/* Writes the state of a triangle: the target, the clip window, the Z
 * buffer, the texture, the fog and the blend. Gives the bits of the
 * DrawTriangle command. */
static uint32_t triangle_state(int textured) {
  int blend = pm.blend_src == GR_BLEND_SRC_ALPHA && pm.blend_dst == GR_BLEND_ONE_MINUS_SRC_ALPHA;
  uint32_t cmd = RENDER_TRAPEZOID | RENDER_SUBPIXEL;
  uint32_t lb_read = pm.pp | (pm.depth ? 1u << 10 : 0u);
  uint32_t fb_read = pm.pp | (blend ? 1u << 10 : 0u);
  int reads = 0;
  scissor();
  state(S_FB_WINDOW_BASE, pm.buffer[pm.back] / 2u);
  reads |= state(S_FB_READ_MODE, fb_read);
  state(S_FB_WRITE_MODE, pm.color_mask ? 1u : 0u);
  reads |= state(S_LB_READ_MODE, lb_read);
  state(S_LB_WRITE_MODE, pm.depth && pm.depth_mask ? 1u : 0u);
  state(S_WINDOW, (1u << 4) | (pm.depth && pm.depth_mask ? 0u : 1u << 18));
  state(S_DEPTH_MODE,
        pm.depth ? 1u | (pm.depth_mask ? 2u : 0u) | ((uint32_t)(pm.depth_func & 7) << 4) : 0u);
  state(S_COLOR_DDA_MODE, 3); /* Gouraud */
  state(S_ALPHA_BLEND_MODE, BLEND_FORMAT_565 | (blend ? 1u | BLEND_SRC_ALPHA_ONE_MINUS_SRC_ALPHA : 0u));
  state(S_DITHER_MODE, 1u | DITHER_FORMAT_565 | (pm.dither ? 2u : 0u));
  state(S_LOGICAL_OP_MODE, 0);
  state(S_FOG_MODE, pm.fog ? 1u : 0u);
  state(S_FOG_COLOR, pm.fog_color);
  if (pm.fog) cmd |= RENDER_FOG;
  if (textured) {
    int no_alpha;
    uint32_t apply = apply_mode(&no_alpha);
    uint32_t data = pm.tex_alpha ? TEX_DATA_4444 : TEX_DATA_565;
    if (no_alpha) data |= TEX_DATA_NO_ALPHA;
    state(S_TEXTURE_ADDRESS_MODE, 3); /* on, with the perspective correction */
    state(S_TEXTURE_READ_MODE, 1u | pm.tex_wrap | ((uint32_t)pm.tex_wlog << 9) | ((uint32_t)pm.tex_hlog << 13) |
                                   (pm.tex_linear ? 1u << 17 : 0u));
    state(S_TEXTURE_DATA_FORMAT, data);
    state(S_TEXTURE_COLOR_MODE, 1u | (apply << 1));
    cmd |= RENDER_TEXTURE;
  } else {
    state(S_TEXTURE_ADDRESS_MODE, 0);
    state(S_TEXTURE_READ_MODE, 0);
    state(S_TEXTURE_COLOR_MODE, 0);
  }
  /* The manual: wait for the end of the earlier primitives when the
   * reads of a buffer start. */
  if (reads) gr(T_WAIT_FOR_COMPLETION, 0);
  return cmd;
}

/* grDrawTriangle: writes the state and the three vertices, then the
 * DrawTriangle command. The Delta unit sorts the vertices. S and T are
 * in texels (S/Q and T/Q are the texel address; the S register is a
 * 12.18 value), times Q = 1/W. The Delta unit normalizes S, T and Q
 * before the set-up, which does not change S/Q and T/Q. */
static void SGL_GLIDE_CALL gr_draw_triangle(const sgl_gr_vertex *a, const sgl_gr_vertex *b,
                                            const sgl_gr_vertex *c) {
  const sgl_gr_vertex *g[3];
  int i, textured;
  uint32_t cmd;
  float scale = 0.0f;
  if (!pm.open || (pm.depth && pm.depth_func == GR_CMP_NEVER)) return;
  g[0] = a, g[1] = b, g[2] = c;
  for (i = 0; i < 3; i++) {
    float x = g[i]->x, y = g[i]->y;
    if (!(x > -2048.0f && x < 2048.0f && y > -2048.0f && y < 2048.0f)) return;
  }
  textured = pm.textured && pm.color_combine != COMBINE_COLOR;
  /* In Glide, 256 is the long side of the texture, which has
   * 2^tex_large texels. */
  if (textured) scale = (float)(1 << pm.tex_large) / 256.0f;
  fifo(32);
  cmd = triangle_state(textured);
  fifo(3 * 11 + 1);
  for (i = 0; i < 3; i++) {
    const sgl_gr_vertex *v = g[i];
    uint32_t t = T_V0_FLOAT + (uint32_t)i * 16u;
    gr(t + P_X, float_bits(v->x));
    gr(t + P_Y, float_bits(screen_y(v->y)));
    gr(t + P_Z, float_bits(clampf(v->ooz, 0.0f, 65535.0f) / 65535.0f));
    gr(t + P_R, float_bits(clampf(v->r, 0.0f, 255.0f) / 255.0f));
    gr(t + P_G, float_bits(clampf(v->g, 0.0f, 255.0f) / 255.0f));
    gr(t + P_B, float_bits(clampf(v->b, 0.0f, 255.0f) / 255.0f));
    gr(t + P_A, float_bits(clampf(v->a, 0.0f, 255.0f) / 255.0f));
    if (pm.fog) gr(t + P_F, float_bits(fog_visibility(v->oow) / 255.0f));
    if (textured) {
      gr(t + P_S, float_bits(v->tmu[0].sow * scale));
      gr(t + P_T, float_bits(v->tmu[0].tow * scale));
      gr(t + P_Q, float_bits(v->tmu[0].oow));
    }
  }
  gr(T_DRAW_TRIANGLE, cmd);
}

/* Draws the quad q[0..3] as two triangles. */
static void quad(const sgl_gr_vertex *q) {
  gr_draw_triangle(&q[0], &q[1], &q[2]);
  gr_draw_triangle(&q[0], &q[2], &q[3]);
}

/* grDrawLine: a line of one pixel, as a thin quad. */
static void SGL_GLIDE_CALL gr_draw_line(const sgl_gr_vertex *a, const sgl_gr_vertex *b) {
  sgl_gr_vertex q[4];
  float dx = b->x - a->x, dy = b->y - a->y, ox = 0.0f, oy = 0.0f;
  if (fabsf(dx) >= fabsf(dy))
    oy = 0.5f;
  else
    ox = 0.5f;
  q[0] = *a, q[1] = *b, q[2] = *b, q[3] = *a;
  q[0].x -= ox, q[0].y -= oy, q[1].x -= ox, q[1].y -= oy;
  q[2].x += ox, q[2].y += oy, q[3].x += ox, q[3].y += oy;
  quad(q);
}

/* grDrawPoint: a point of one pixel, as a square. */
static void SGL_GLIDE_CALL gr_draw_point(const sgl_gr_vertex *a) {
  sgl_gr_vertex q[4];
  q[0] = q[1] = q[2] = q[3] = *a;
  q[0].x -= 0.5f, q[0].y -= 0.5f, q[1].x += 0.5f, q[1].y -= 0.5f;
  q[2].x += 0.5f, q[2].y += 0.5f, q[3].x -= 0.5f, q[3].y += 0.5f;
  quad(q);
}

/* grDepthBufferMode: the Z buffer on or off. */
static void SGL_GLIDE_CALL gr_depth_buffer_mode(int mode) { pm.depth = mode != GR_DEPTHBUFFER_DISABLE; }

/* grDepthBufferFunction: the Z compare. The Glide compare codes are
 * the codes of DepthMode. */
static void SGL_GLIDE_CALL gr_depth_buffer_function(int func) { pm.depth_func = func; }

/* grDepthMask: the Z writes. */
static void SGL_GLIDE_CALL gr_depth_mask(int on) { pm.depth_mask = on != 0; }

/* grColorMask: the color writes on or off (the card has no alpha
 * buffer). */
static void SGL_GLIDE_CALL gr_color_mask(int rgb, int alpha) {
  (void)alpha;
  pm.color_mask = rgb != 0;
}

/* grColorCombine: the combines of glide.c: the vertex color, the
 * texture (GL_REPLACE), the texture times the vertex color
 * (GL_MODULATE), or GL_DECAL. */
static void SGL_GLIDE_CALL gr_color_combine(int func, int factor, int local, int other, int invert) {
  (void)local, (void)invert;
  if (other != GR_COMBINE_OTHER_TEXTURE || func == GR_COMBINE_FUNCTION_LOCAL)
    pm.color_combine = COMBINE_COLOR;
  else if (func == GR_COMBINE_FUNCTION_BLEND)
    pm.color_combine = COMBINE_DECAL;
  else if (func == GR_COMBINE_FUNCTION_SCALE_OTHER && factor == GR_COMBINE_FACTOR_LOCAL)
    pm.color_combine = COMBINE_MODULATE;
  else
    pm.color_combine = COMBINE_REPLACE;
}

/* grAlphaCombine: the alpha is the vertex alpha (LOCAL), the texture
 * alpha (SCALE_OTHER by ONE), or the texture alpha times the vertex
 * alpha (SCALE_OTHER by LOCAL). */
static void SGL_GLIDE_CALL gr_alpha_combine(int func, int factor, int local, int other, int invert) {
  (void)local, (void)other, (void)invert;
  if (func != GR_COMBINE_FUNCTION_SCALE_OTHER)
    pm.alpha_mode = ALPHA_VERTEX;
  else if (factor == GR_COMBINE_FACTOR_LOCAL)
    pm.alpha_mode = ALPHA_PRODUCT;
  else
    pm.alpha_mode = ALPHA_TEXTURE;
}

/* grTexCombine: the card has one texture unit. */
static void SGL_GLIDE_CALL gr_tex_combine(int tmu, int rgb_func, int rgb_factor, int alpha_func, int alpha_factor,
                                          int rgb_invert, int alpha_invert) {
  (void)tmu, (void)rgb_func, (void)rgb_factor, (void)alpha_func, (void)alpha_factor, (void)rgb_invert,
      (void)alpha_invert;
}

/* grAlphaBlendFunction: the blend factors of the color. The card does
 * only (source alpha, 1 - source alpha) (see the limits). */
static void SGL_GLIDE_CALL gr_alpha_blend_function(int rgb_src, int rgb_dst, int alpha_src, int alpha_dst) {
  (void)alpha_src, (void)alpha_dst;
  pm.blend_src = rgb_src, pm.blend_dst = rgb_dst;
}

/* grAlphaTestFunction: the card has no alpha test (see the limits). */
static void SGL_GLIDE_CALL gr_alpha_test_function(int func) { (void)func; }

/* grAlphaTestReferenceValue: the card has no alpha test. */
static void SGL_GLIDE_CALL gr_alpha_test_reference_value(unsigned value) { (void)value; }

/* grFogMode: the fog table on or off. */
static void SGL_GLIDE_CALL gr_fog_mode(int mode) { pm.fog = mode != GR_FOG_DISABLE; }

/* grFogColorValue: the fog color (0xRRGGBB). FogColor has red in bits
 * 0 to 7 and blue in bits 16 to 23. */
static void SGL_GLIDE_CALL gr_fog_color_value(unsigned long color) {
  pm.fog_color = (uint32_t)(((color >> 16) & 0xFFu) | (color & 0xFF00u) | ((color & 0xFFu) << 16));
}

/* grFogTable: keeps the table, and the eye depth of each entry: entry
 * i is at w = 2^(3 + i / 4) / (8 - i % 4), as on a Voodoo. */
static void SGL_GLIDE_CALL gr_fog_table(const unsigned char *table) {
  int i;
  for (i = 0; i < GR_FOG_TABLE_SIZE; i++) {
    pm.fog_table[i] = table[i];
    pm.fog_w[i] = (float)(1ul << (3 + (i >> 2))) / (float)(8 - (i & 3));
  }
}

/* grCullMode: glide.c culls in software. */
static void SGL_GLIDE_CALL gr_cull_mode(int mode) { (void)mode; }

/* grDitherMode: the dither of the 16-bit color on or off. */
static void SGL_GLIDE_CALL gr_dither_mode(int mode) { pm.dither = mode != GR_DITHER_DISABLE; }

/* grTexMinAddress: the textures start after the Z buffer. */
static unsigned long SGL_GLIDE_CALL gr_tex_min_address(int tmu) {
  (void)tmu;
  return pm.tex_min;
}

/* grTexMaxAddress: the textures end at the end of video memory. */
static unsigned long SGL_GLIDE_CALL gr_tex_max_address(int tmu) {
  (void)tmu;
  return pm.memory;
}

/* Gives the log2 of the width and of the height of the largest level
 * of a texture in *wlog and *hlog. */
static void tex_size(const sgl_gr_texinfo *info, int *wlog, int *hlog) {
  int large = 8 - info->large_lod, aspect = info->aspect - GR_ASPECT_1x1;
  *wlog = aspect > 0 ? large - aspect : large;
  *hlog = aspect < 0 ? large + aspect : large;
  if (*wlog < 0) *wlog = 0;
  if (*hlog < 0) *hlog = 0;
}

/* Gives the row pitch of the largest level in texels: the width, at
 * least 32 texels (the smallest width of the partial products). */
static int tex_pitch(int wlog) { return wlog < 5 ? 32 : 1 << wlog; }

/* grTexTextureMemRequired: the bytes of the largest level, the only
 * level that the card uses. */
static unsigned long SGL_GLIDE_CALL gr_tex_texture_mem_required(unsigned long even_odd, sgl_gr_texinfo *info) {
  int wlog, hlog;
  (void)even_odd;
  tex_size(info, &wlog, &hlog);
  return ((unsigned long)tex_pitch(wlog) * 2ul * (1ul << hlog) + 63ul) & ~63ul;
}

/* grTexDownloadMipMap: writes the largest level at start (16 bits for
 * each texel). RGB 565 and ARGB 4444 are formats of the card. */
static void SGL_GLIDE_CALL gr_tex_download_mip_map(int tmu, unsigned long start, unsigned long even_odd,
                                                   sgl_gr_texinfo *info) {
  const uint16_t *src = (const uint16_t *)info->data;
  int wlog, hlog, x, y, pitch;
  (void)tmu, (void)even_odd;
  if (!pm.open || start + gr_tex_texture_mem_required(0, info) > pm.memory) return;
  tex_size(info, &wlog, &hlog);
  pitch = tex_pitch(wlog);
  idle();
  for (y = 0; y < 1 << hlog; y++) {
    volatile uint16_t *dst = (volatile uint16_t *)(pm.lfb + start + (unsigned long)y * (unsigned long)pitch * 2ul);
    for (x = 0; x < 1 << wlog; x++) dst[x] = *src++;
  }
}

/* grTexSource: the texture that the triangles use. The base address
 * is in texels from the start of video memory. */
static void SGL_GLIDE_CALL gr_tex_source(int tmu, unsigned long start, unsigned long even_odd, sgl_gr_texinfo *info) {
  (void)tmu, (void)even_odd;
  pm.textured = 1;
  pm.tex_large = 8 - info->large_lod;
  tex_size(info, &pm.tex_wlog, &pm.tex_hlog);
  pm.tex_alpha = info->format == GR_TEXFMT_ARGB_4444;
  if (!pm.open) return;
  fifo(2);
  gr(T_TEXTURE_BASE, (uint32_t)(start / 2ul));
  gr(T_TEXTURE_MAP_FORMAT, partial_products(tex_pitch(pm.tex_wlog)) | (1u << 19)); /* 16-bit texels */
}

/* grTexFilterMode: the bilinear filter when the magnification or the
 * minification asks for it. */
static void SGL_GLIDE_CALL gr_tex_filter_mode(int tmu, int min, int mag) {
  (void)tmu;
  pm.tex_linear = min == GR_TEXTUREFILTER_BILINEAR || mag == GR_TEXTUREFILTER_BILINEAR;
}

/* grTexClampMode: repeat or clamp, for s (bits 1 and 2 of
 * TextureReadMode) and for t (bits 3 and 4). */
static void SGL_GLIDE_CALL gr_tex_clamp_mode(int tmu, int s, int t) {
  (void)tmu;
  pm.tex_wrap = (s == GR_TEXTURECLAMP_CLAMP ? 0u : 1u << 1) | (t == GR_TEXTURECLAMP_CLAMP ? 0u : 1u << 3);
}

/* grTexMipMapMode: the card uses the largest level only. */
static void SGL_GLIDE_CALL gr_tex_mip_map_mode(int tmu, int mode, int lod_blend) {
  (void)tmu, (void)mode, (void)lod_blend;
}

/* Gives the offset of the buffer of a lock: the front buffer, the back
 * buffer or the Z buffer. The rows have the same pitch. */
static uint32_t lock_buffer(int buffer) {
  if (buffer == GR_BUFFER_AUXBUFFER) return pm.zbuffer;
  return pm.buffer[buffer == GR_BUFFER_BACKBUFFER ? pm.back : pm.front];
}

/* grLfbLock: gives a copy of the buffer in system memory, in RGB 565
 * (or the Z values of 16 bits), rows in the order of origin. A write
 * lock copies the pixels to the card in grLfbUnlock. */
static int SGL_GLIDE_CALL gr_lfb_lock(int type, int buffer, int write_mode, int origin, int pixel_pipeline,
                                      sgl_gr_lfbinfo *info) {
  int x, y;
  uint32_t base;
  (void)pixel_pipeline;
  if (!pm.open) return 0;
  if (!pm.shadow) pm.shadow = (uint16_t *)malloc((size_t)pm.width * (size_t)pm.height * 2u);
  if (!pm.shadow) return 0;
  idle();
  pm.lock_origin = origin;
  base = lock_buffer(buffer);
  if (type == GR_LFB_READ_ONLY) {
    for (y = 0; y < pm.height; y++) {
      int row = origin == GR_ORIGIN_LOWER_LEFT ? pm.height - 1 - y : y;
      const volatile uint16_t *p = (const volatile uint16_t *)(pm.lfb + base + (unsigned long)row * pm.stride);
      uint16_t *q = pm.shadow + (long)y * pm.width;
      for (x = 0; x < pm.width; x++) q[x] = p[x];
    }
  }
  info->ptr = pm.shadow;
  info->stride = (unsigned long)pm.width * 2ul;
  info->write_mode = write_mode;
  info->origin = origin;
  return 1;
}

/* grLfbUnlock: copies the pixels of a write lock to the card. */
static int SGL_GLIDE_CALL gr_lfb_unlock(int type, int buffer) {
  int x, y;
  uint32_t base;
  if (!pm.open || !pm.shadow || type != GR_LFB_WRITE_ONLY) return 1;
  base = lock_buffer(buffer);
  for (y = 0; y < pm.height; y++) {
    int row = pm.lock_origin == GR_ORIGIN_LOWER_LEFT ? pm.height - 1 - y : y;
    volatile uint16_t *p = (volatile uint16_t *)(pm.lfb + base + (unsigned long)row * pm.stride);
    const uint16_t *q = pm.shadow + (long)y * pm.width;
    for (x = 0; x < pm.width; x++) p[x] = q[x];
  }
  return 1;
}

/* Fills api with the Permedia 2 driver (see softgl_glide.h). */
int sgl_glide_pm2(sgl_glide_api *api) {
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
  api->TexDownloadMipMap = gr_tex_download_mip_map;
  api->TexSource = gr_tex_source;
  api->TexFilterMode = gr_tex_filter_mode;
  api->TexClampMode = gr_tex_clamp_mode;
  api->TexMipMapMode = gr_tex_mip_map_mode;
  api->LfbLock = gr_lfb_lock;
  api->LfbUnlock = gr_lfb_unlock;
  api->limits = SGL_GLIDE_LIMIT_ALPHA_TEST | SGL_GLIDE_LIMIT_BLEND;
  return 1;
}

#endif
