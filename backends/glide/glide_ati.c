/* glide_ati.c - the Glide 2 functions of the Glide back end, on the 3D
 * engine of an ATI Rage Pro card, for 32-bit DOS. sgl_glide_ati fills
 * an sgl_glide_api, and the Glide back end (glide.c) uses it as it uses
 * a Voodoo. The cards are the 3D Rage Pro, the Rage LT Pro, the Rage XL
 * and XC, and the Rage Mobility (the mach64 chips with the triangle
 * setup engine).
 *
 * The setup engine draws a triangle from its three vertices: the driver
 * writes the position, Z, the color, the fog and the texture
 * coordinates (S, T and W) of each vertex, then one over the area of
 * the triangle. The engine calculates the edges and the gradients.
 *
 * The Rage Pro draws almost all that a Voodoo draws. The driver sets
 * the limits of the table (SGL_GLIDE_LIMIT_), and softgl then draws in
 * software what the card cannot do:
 *
 * - The fog and the blend use the same unit: no fog with a blend.
 * - One texture unit.
 *
 * The screen is a VBE mode of 16-bit color (5:6:5) with its linear
 * frame buffer. Video memory holds three color buffers (two when the
 * memory is small), the 16-bit Z buffer, and then the textures with
 * their mipmaps.
 *
 * The register facts come from the mach64 register guides of ATI, from
 * the open mach64 drivers, and from the mach64 cards of the 86Box
 * emulator, where this driver is tested.
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

/* The registers of block 0 (byte offsets in the block). */
enum {
  R_BUS_CNTL = 0x0A0,
  R_DST_OFF_PITCH = 0x100,
  R_DST_Y_X = 0x10C,
  R_DST_HEIGHT_WIDTH = 0x118,
  R_DST_CNTL = 0x130,
  R_Z_OFF_PITCH = 0x148,
  R_Z_CNTL = 0x14C,
  R_ALPHA_TST_CNTL = 0x150,
  R_SRC_CNTL = 0x1B4,
  R_TEX_0_OFF = 0x1C0,
  R_SCALE_3D_CNTL = 0x1FC,
  R_SC_LEFT_RIGHT = 0x2A8,
  R_SC_TOP_BOTTOM = 0x2B4,
  R_DP_BKGD_CLR = 0x2C0,
  R_DP_FRGD_CLR = 0x2C4,
  R_DP_WRITE_MASK = 0x2C8,
  R_DP_PIX_WIDTH = 0x2D0,
  R_DP_MIX = 0x2D4,
  R_DP_SRC = 0x2D8,
  R_CLR_CMP_CNTL = 0x308,
  R_FIFO_STAT = 0x310,
  R_GUI_TRAJ_CNTL = 0x330,
  R_GUI_STAT = 0x338,
  R_TEX_SIZE_PITCH = 0x370,
  R_TEX_CNTL = 0x374
};

/* The registers of block 1, the setup engine (byte offsets in the
 * block). Vertex n (0 to 2) has 8 registers from R_VERTEX + 32 n: S,
 * T, W, the specular color and fog, Z, the color, X and Y, and then
 * one over the area. */
enum { R_VERTEX = 0x240, R_ONE_OVER_AREA = 0x29C, R_SETUP_CNTL = 0x304 };
enum { V_S = 0, V_T = 4, V_W = 8, V_SPEC = 12, V_Z = 16, V_ARGB = 20, V_XY = 24 };

/* The bits of SCALE_3D_CNTL. */
#define S3D_PIX_EXPAND (1u << 0) /* expand 5 and 6 bits to 8 bits correctly */
#define S3D_DITHER_ORDERED (1u << 1)
#define S3D_DITHER_EN (1u << 2)
#define S3D_ROUND_EN (1u << 4)
#define S3D_FCN_TEXTURE (2u << 6)
#define S3D_FCN_SHADE (3u << 6)
#define S3D_BLEND (1u << 11) /* ALPHA_FOG_EN: blend */
#define S3D_FOG (2u << 11)   /* ALPHA_FOG_EN: fog */
#define S3D_BLEND_SRC_SHIFT 16
#define S3D_BLEND_DST_SHIFT 19
#define S3D_LIGHT_MODULATE (1u << 22)
#define S3D_LIGHT_DECAL (2u << 22)
#define S3D_MIP_MAP_DISABLE (1u << 24)
#define S3D_BILINEAR_MAG (1u << 25)
#define S3D_BILINEAR_MIN (2u << 26) /* TEX_BLEND_FCN: bilinear in the nearest mipmap */
#define S3D_TEX_ALPHA (1u << 30)    /* TEX_MAP_AEN: the alpha of the texels */

/* The bits of TEX_CNTL. */
#define TEX_CLAMP_S (1u << 17)
#define TEX_CLAMP_T (1u << 18)
#define TEX_ST_DIRECT (1u << 19) /* S and T come multiplied by W */
#define TEX_CACHE_FLUSH (1u << 23)

/* The bits of Z_CNTL and ALPHA_TST_CNTL. */
#define Z_EN 1u
#define Z_WRITE (1u << 8)
#define ALPHA_TEST_EN 1u
#define ALPHA_TEST_SRC (1u << 12) /* test the alpha after the texture and the light */

/* The pixel types of DP_PIX_WIDTH. */
#define PIX_565 4u
#define PIX_4444 15u

/* The color combines of glide.c, as the driver knows them. */
enum { COMBINE_COLOR, COMBINE_REPLACE, COMBINE_MODULATE, COMBINE_DECAL };

/* The state of the driver. */
static struct {
  sgl_pci_device pci;
  volatile uint8_t *lfb;  /* video memory */
  volatile uint8_t *regs; /* the register window: block 1, then block 0 */
  int regs_size;          /* the size of the mapping of regs */
  uint32_t lfb_phys, regs_phys, memory;
  int open, width, height, pitch, origin;
  uint32_t buffer[3]; /* the offsets of the color buffers */
  int buffers;        /* 3, or 2 when video memory is small */
  int back;           /* the color buffer that is drawn */
  int front;          /* the color buffer that is shown */
  uint32_t zbuffer, tex_min;
  /* The state that Glide sets. */
  int depth, depth_func, depth_mask, color_mask, fog, dither, color_combine, alpha_texture;
  int blend_src, blend_dst, alpha_func, alpha_ref;
  uint8_t fog_table[GR_FOG_TABLE_SIZE];
  float fog_w[GR_FOG_TABLE_SIZE];
  uint32_t fog_color;
  int clip[4]; /* minx, miny, maxx, maxy (Glide coordinates) */
  /* The texture that TexSource selected. */
  int textured, tex_wlog, tex_hlog, tex_large, tex_small, tex_mipmap, tex_min_linear, tex_mag_linear;
  uint32_t tex_base, tex_format, tex_clamp, tex_flush;
  /* The registers that the driver wrote last (0xFFFFFFFF: unknown). */
  uint32_t last_s3d, last_z, last_tex, last_alpha, last_pix;
  uint16_t *shadow; /* the copy of a locked buffer */
  int lock_origin;
} at;

/* The PCI IDs of the mach64 chips with the setup engine: the Rage Pro
 * (GB, GD, GI, GP, GQ), the Rage LT Pro (LB, LD, LI, LP, LQ), the Rage
 * XL and XC (GL, GM, GN, GO, GR, GS) and the Rage Mobility (LM, LN, LR,
 * LS). */
static const sgl_pci_id ati_ids[] = {
    {0x1002, 0x4742}, {0x1002, 0x4744}, {0x1002, 0x4749}, {0x1002, 0x4750}, {0x1002, 0x4751}, {0x1002, 0x4C42},
    {0x1002, 0x4C44}, {0x1002, 0x4C49}, {0x1002, 0x4C50}, {0x1002, 0x4C51}, {0x1002, 0x474C}, {0x1002, 0x474D},
    {0x1002, 0x474E}, {0x1002, 0x474F}, {0x1002, 0x4752}, {0x1002, 0x4753}, {0x1002, 0x4C4D}, {0x1002, 0x4C4E},
    {0x1002, 0x4C52}, {0x1002, 0x4C53}, {0, 0}};

/* Writes a 32-bit register of block 0. */
static void wr(uint32_t reg, uint32_t value) { *(volatile uint32_t *)(at.regs + 0x400 + reg) = value; }

/* Reads a 32-bit register of block 0. */
static uint32_t rd(uint32_t reg) { return *(volatile uint32_t *)(at.regs + 0x400 + reg); }

/* Writes a 32-bit register of block 1. */
static void wr1(uint32_t reg, uint32_t value) { *(volatile uint32_t *)(at.regs + reg) = value; }

/* Gives the number of bits that are set in v. */
static int bits(uint32_t v) {
  int n = 0;
  for (; v; v &= v - 1) n++;
  return n;
}

/* Waits until the FIFO of the engine has n free entries of 16. Each
 * entry in use sets one bit of FIFO_STAT. Stops after about one
 * second. */
static void slots(int n) {
  uint32_t start = sgl_dos_ms();
  while (bits(rd(R_FIFO_STAT) & 0xFFFFu) > 16 - n)
    if (sgl_dos_ms() - start > 1000) break;
}

/* Waits until the engine is idle (GUI_STAT bit 0, GUI_ACTIVE). */
static void idle(void) {
  uint32_t start = sgl_dos_ms();
  if (!at.regs) return;
  slots(16);
  while (rd(R_GUI_STAT) & 1u)
    if (sgl_dos_ms() - start > 1000) break;
}

/* Gives the screen row of a Glide y. */
static float screen_y(float y) { return at.origin == GR_ORIGIN_LOWER_LEFT ? (float)at.height - y : y; }

/* grGlideInit: finds the card and maps its registers. The register
 * window is BAR 2 (block 1, then block 0) when the BIOS gave it an
 * address; else it is the last 2 KB of the first 8 MB of the linear
 * aperture (BAR 0), which BUS_CNTL bit 27 (BUS_EXT_REG_EN) opens. */
static void SGL_GLIDE_CALL gr_glide_init(void) {
  memset(&at, 0, sizeof at);
  if (!sgl_pci_find(ati_ids, 0, &at.pci)) return;
  at.lfb_phys = sgl_pci_bar(&at.pci, 0);
  if (!at.lfb_phys) return;
  sgl_pci_write16(&at.pci, 0x04, (uint16_t)(sgl_pci_read16(&at.pci, 0x04) | 0x0002));
  at.regs_phys = sgl_pci_bar(&at.pci, 2);
  if (at.regs_phys) {
    at.regs_size = 0x1000;
    at.regs = (volatile uint8_t *)sgl_map_physical(at.regs_phys, 0x1000ul);
  } else {
    at.regs_size = 0x800;
    at.regs = (volatile uint8_t *)sgl_map_physical(at.lfb_phys + 0x7FF800ul, 0x800ul);
    if (at.regs) wr(R_BUS_CNTL, rd(R_BUS_CNTL) | (1u << 27));
  }
}

/* grGlideShutdown: closes the screen and frees the mappings. */
static void SGL_GLIDE_CALL gr_glide_shutdown(void);

/* grSstQueryHardware: one board with one TMU, as a Voodoo Graphics
 * (GrHwConfiguration as 32-bit words). */
static int SGL_GLIDE_CALL gr_sst_query_hardware(void *config) {
  int32_t *hw = (int32_t *)config;
  if (!at.regs) return 0;
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

/* Gives the value of DST_OFF_PITCH or Z_OFF_PITCH for a buffer at the
 * byte offset off: the offset in units of 8 bytes, and the pitch in
 * units of 8 pixels. */
static uint32_t off_pitch(uint32_t off) { return (off >> 3) | ((uint32_t)(at.pitch / 8) << 22); }

/* Writes the clip window to the scissor registers (the last pixel is
 * in the scissor). */
static void scissor(void) {
  int top = at.origin == GR_ORIGIN_LOWER_LEFT ? at.height - at.clip[3] : at.clip[1];
  int bottom = top + (at.clip[3] - at.clip[1]) - 1;
  int left = at.clip[0], right = at.clip[2] - 1;
  if (right < left || bottom < top) left = 1, right = 0, top = 1, bottom = 0;
  slots(2);
  wr(R_SC_LEFT_RIGHT, ((uint32_t)right << 16) | (uint32_t)left);
  wr(R_SC_TOP_BOTTOM, ((uint32_t)bottom << 16) | (uint32_t)top);
}

/* Sets the drawing target to the back buffer. */
static void target(void) {
  slots(1);
  wr(R_DST_OFF_PITCH, off_pitch(at.buffer[at.back]));
}

/* Forgets the registers that the driver wrote last. */
static void forget(void) { at.last_s3d = at.last_z = at.last_tex = at.last_alpha = at.last_pix = 0xFFFFFFFFu; }

/* Writes a register of block 0 when its value changes. last is the
 * value that the driver wrote last. */
static void wr_changed(uint32_t reg, uint32_t value, uint32_t *last) {
  if (*last == value) return;
  *last = value;
  slots(1);
  wr(reg, value);
}

/* grClipWindow: the clip rectangle (the maxima are not in it). */
static void SGL_GLIDE_CALL gr_clip_window(unsigned long minx, unsigned long miny, unsigned long maxx,
                                          unsigned long maxy) {
  at.clip[0] = (int)minx, at.clip[1] = (int)miny, at.clip[2] = (int)maxx, at.clip[3] = (int)maxy;
  if (at.open) scissor();
}

/* grSstWinOpen: sets the VBE mode, maps the frame buffer, divides
 * video memory and sets the engine. 16-bit color only; the other
 * arguments are not used. */
static int SGL_GLIDE_CALL gr_sst_win_open(unsigned long hwnd, int resolution, int refresh, int color_format,
                                          int origin, int color_buffers, int aux_buffers) {
  sgl_vbe_mode info;
  uint32_t frame, need;
  int mode, w, h, i;
  (void)hwnd, (void)refresh, (void)color_format, (void)color_buffers, (void)aux_buffers;
  if (!at.regs || at.open) return 0;
  mode = vbe_mode(resolution, &w, &h);
  if (mode < 0 || !sgl_vbe_mode_info(mode, &info) || !(info.attributes & 1) || info.bits != 16) return 0;
  /* The engine needs a pitch of a multiple of 8 pixels. */
  if (info.stride % 16u) return 0;
  at.memory = sgl_vbe_memory();
  if (!at.memory) at.memory = 0x200000ul;
  /* The registers can be in the last 2 KB of the first 8 MB. */
  if (at.memory > 0x7FF800ul) at.memory = 0x7FF800ul;
  frame = (uint32_t)info.stride * (uint32_t)h;
  need = 3 * frame + 0x20000ul;
  if (need > at.memory) return 0;
  if (!(info.attributes & 0x80) || !sgl_vbe_set_mode(mode, 1)) return 0;
  if (info.lfb) at.lfb_phys = info.lfb;
  at.lfb = (volatile uint8_t *)sgl_map_physical(at.lfb_phys, at.memory);
  if (!at.lfb) {
    sgl_vbe_text_mode();
    return 0;
  }
  at.width = w, at.height = h, at.pitch = info.stride / 2, at.origin = origin;
  /* Three color buffers when the textures keep 1 MB: the card draws
   * into the third while the monitor can still show the old front. */
  at.buffers = need + frame + 0xC0000ul <= at.memory ? 3 : 2;
  for (i = 0; i < at.buffers; i++) at.buffer[i] = (uint32_t)i * frame;
  at.zbuffer = at.buffer[at.buffers - 1] + frame;
  at.tex_min = (at.zbuffer + frame + 63u) & ~63u;
  at.front = 0;
  at.back = 1;
  at.clip[0] = 0, at.clip[1] = 0, at.clip[2] = w, at.clip[3] = h;
  at.depth_func = GR_CMP_LESS;
  at.depth_mask = 1;
  at.color_mask = 1;
  at.dither = 1;
  at.blend_src = GR_BLEND_ONE, at.blend_dst = GR_BLEND_ZERO;
  at.alpha_func = GR_CMP_ALWAYS;
  at.open = 1;
  forget();
  idle();
  slots(12);
  wr(R_CLR_CMP_CNTL, 0);
  wr(R_SRC_CNTL, 0);
  wr(R_DP_MIX, 0x00070003u); /* the foreground is the source, the background the destination */
  wr(R_DP_SRC, 0x00000505u); /* the 3D engine is the source of the colors */
  wr(R_DP_WRITE_MASK, 0xFFFFFFFFu);
  wr(R_DP_FRGD_CLR, at.fog_color);
  wr(R_DP_BKGD_CLR, 0);
  wr(R_GUI_TRAJ_CNTL, 3u); /* left to right, top to bottom */
  wr(R_Z_OFF_PITCH, off_pitch(at.zbuffer));
  wr1(R_SETUP_CNTL, 0); /* a write of ONE_OVER_AREA starts the triangle */
  target();
  scissor();
  sgl_vbe_display_start(0, 0);
  return 1;
}

/* grSstWinClose: sets the text mode again and frees the frame buffer. */
static void SGL_GLIDE_CALL gr_sst_win_close(void) {
  if (!at.open) return;
  idle();
  sgl_vbe_text_mode();
  sgl_unmap_physical(at.lfb);
  at.lfb = NULL;
  free(at.shadow);
  at.shadow = NULL;
  at.open = 0;
}

/* grGlideShutdown: closes the screen and frees the mappings. */
static void SGL_GLIDE_CALL gr_glide_shutdown(void) {
  gr_sst_win_close();
  if (at.regs) sgl_unmap_physical(at.regs);
  memset(&at, 0, sizeof at);
}

/* grSstIdle: waits until the card has drawn all. */
static void SGL_GLIDE_CALL gr_sst_idle(void) { idle(); }

/* Fills the clip window of the buffer at the byte offset off with the
 * 16-bit value value, with the rectangle fill of the 2D engine. */
static void fill(uint32_t off, uint16_t value) {
  int top = at.origin == GR_ORIGIN_LOWER_LEFT ? at.height - at.clip[3] : at.clip[1];
  int w = at.clip[2] - at.clip[0], h = at.clip[3] - at.clip[1];
  if (w <= 0 || h <= 0) return;
  wr_changed(R_DP_PIX_WIDTH, 0x01040404u, &at.last_pix);
  slots(7);
  wr(R_DST_OFF_PITCH, off_pitch(off));
  wr(R_DP_SRC, 0x00000100u); /* the foreground color */
  wr(R_DP_FRGD_CLR, value);
  wr(R_DP_WRITE_MASK, 0xFFFFFFFFu);
  wr(R_DST_CNTL, 3u);
  wr(R_DST_Y_X, ((uint32_t)at.clip[0] << 16) | (uint32_t)top);
  wr(R_DST_HEIGHT_WIDTH, ((uint32_t)w << 16) | (uint32_t)h);
}

/* Converts a Glide color (0xAARRGGBB) to a 5:6:5 pixel. */
static uint16_t pixel565(unsigned long c) {
  return (uint16_t)(((c >> 8) & 0xF800u) | ((c >> 5) & 0x07E0u) | ((c >> 3) & 0x001Fu));
}

/* grBufferClear: fills the clip window of the back buffer with color
 * when the color writes are on, and of the Z buffer with depth when
 * the depth writes are on. */
static void SGL_GLIDE_CALL gr_buffer_clear(unsigned long color, unsigned alpha, unsigned depth) {
  (void)alpha;
  if (!at.open) return;
  if (at.color_mask) fill(at.buffer[at.back], pixel565(color));
  if (at.depth && at.depth_mask) fill(at.zbuffer, (uint16_t)depth);
  slots(4);
  wr(R_DP_SRC, 0x00000505u);
  wr(R_DP_FRGD_CLR, at.fog_color);
  wr(R_DP_WRITE_MASK, at.color_mask ? 0xFFFFFFFFu : 0u);
  wr(R_DST_OFF_PITCH, off_pitch(at.buffer[at.back]));
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
  int old = at.front;
  if (!at.open) return;
  idle();
  sgl_vbe_display_start((int)(at.buffer[at.back] / ((uint32_t)at.pitch * 2u)), interval > 0);
  at.front = at.back;
  if (at.buffers == 3) {
    at.back = 3 - at.front - old;
  } else {
    if (interval > 0) next_frame();
    at.back = old;
  }
  target();
}

/* Gives the fog visibility of a vertex (255 is no fog) from the Glide
 * fog table, by the eye depth w = 1 / oow. */
static float fog_visibility(float oow) {
  float w = oow > 0.0f ? 1.0f / oow : 1e30f, f;
  int i;
  if (w <= at.fog_w[0]) return 255.0f - at.fog_table[0];
  for (i = 1; i < GR_FOG_TABLE_SIZE; i++)
    if (w < at.fog_w[i]) break;
  if (i == GR_FOG_TABLE_SIZE) return 255.0f - at.fog_table[GR_FOG_TABLE_SIZE - 1];
  f = (w - at.fog_w[i - 1]) / (at.fog_w[i] - at.fog_w[i - 1]);
  return 255.0f - ((float)at.fog_table[i - 1] + f * ((float)at.fog_table[i] - (float)at.fog_table[i - 1]));
}

/* Gives v limited to lo to hi. */
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

/* Gives a color channel (0 to 255) as an 8-bit value. */
static uint32_t byte(float v) { return (uint32_t)lrintf(clampf(v, 0.0f, 255.0f)); }

/* Gives the 32 bits of the IEEE single float f. */
static uint32_t float_bits(float f) {
  uint32_t u;
  memcpy(&u, &f, sizeof u);
  return u;
}

/* The Glide compare functions as the compare codes of the engine: 0
 * never, 1 less, 2 less or equal, 3 equal, 4 greater or equal, 5
 * greater, 6 not equal, 7 always. */
static const uint8_t cmp_code[8] = {0, 1, 3, 2, 5, 6, 4, 7};

/* The Glide blend factors as the blend codes of the engine: 0 zero, 1
 * one, 2 the other color, 3 one minus the other color, 4 source alpha,
 * 5 one minus source alpha, 6 destination alpha, 7 one minus
 * destination alpha. */
static const uint8_t blend_code[8] = {0, 4, 2, 6, 1, 5, 3, 7};

/* Gives SCALE_3D_CNTL for a triangle (the fog only without a blend),
 * and sets the texture registers when the triangle has a texture. */
static uint32_t triangle_state(int textured, int blend, int fog) {
  uint32_t s3d = S3D_PIX_EXPAND | (at.dither ? S3D_DITHER_EN | S3D_DITHER_ORDERED : S3D_ROUND_EN);
  if (blend) {
    s3d |= S3D_BLEND | ((uint32_t)blend_code[at.blend_src & 7] << S3D_BLEND_SRC_SHIFT) |
           ((uint32_t)blend_code[at.blend_dst & 7] << S3D_BLEND_DST_SHIFT);
  } else if (fog) {
    s3d |= S3D_FOG;
  }
  if (!textured) return s3d | S3D_FCN_SHADE;
  s3d |= S3D_FCN_TEXTURE;
  switch (at.color_combine) {
    case COMBINE_MODULATE: s3d |= S3D_LIGHT_MODULATE | S3D_TEX_ALPHA; break;
    case COMBINE_DECAL: s3d |= S3D_LIGHT_DECAL | S3D_TEX_ALPHA; break;
    default:
      if (at.alpha_texture) s3d |= S3D_TEX_ALPHA;
      break;
  }
  if (!at.tex_mipmap) s3d |= S3D_MIP_MAP_DISABLE;
  if (at.tex_min_linear) s3d |= S3D_BILINEAR_MIN;
  if (at.tex_mag_linear) s3d |= S3D_BILINEAR_MAG;
  {
    uint32_t pix = (at.tex_format << 28) | 0x01040404u;
    uint32_t tex = TEX_ST_DIRECT | at.tex_clamp;
    if (at.tex_flush) {
      at.last_tex = 0xFFFFFFFFu;
      tex |= TEX_CACHE_FLUSH;
      at.tex_flush = 0;
    }
    wr_changed(R_DP_PIX_WIDTH, pix, &at.last_pix);
    wr_changed(R_TEX_CNTL, tex, &at.last_tex);
    if (tex & TEX_CACHE_FLUSH) at.last_tex = tex & ~TEX_CACHE_FLUSH;
  }
  return s3d;
}

/* grDrawTriangle: writes the three vertices to the setup engine, then
 * one over the area, which starts the triangle. */
static void SGL_GLIDE_CALL gr_draw_triangle(const sgl_gr_vertex *a, const sgl_gr_vertex *b,
                                            const sgl_gr_vertex *c) {
  const sgl_gr_vertex *g[3];
  int32_t qx[3], qy[3];
  int64_t cross;
  int i, textured, blend, fog;
  uint32_t s3d, zc, alpha;
  float ss, ts;
  if (!at.open || (at.depth && at.depth_func == GR_CMP_NEVER) || at.alpha_func == GR_CMP_NEVER) return;
  g[0] = a, g[1] = b, g[2] = c;
  for (i = 0; i < 3; i++) {
    float x = g[i]->x, y = screen_y(g[i]->y);
    /* X and Y are 16-bit values in quarter pixels. */
    if (!(x > -4096.0f && x < 4096.0f && y > -4096.0f && y < 4096.0f)) return;
    qx[i] = (int32_t)lrintf(x * 4.0f);
    qy[i] = (int32_t)lrintf(y * 4.0f);
  }
  cross = (int64_t)(qx[0] - qx[2]) * (qy[1] - qy[2]) - (int64_t)(qy[0] - qy[2]) * (qx[1] - qx[2]);
  if (cross == 0) return;
  textured = at.textured && at.color_combine != COMBINE_COLOR;
  blend = !(at.blend_src == GR_BLEND_ONE && at.blend_dst == GR_BLEND_ZERO);
  fog = at.fog && !blend;
  s3d = triangle_state(textured, blend, fog);
  zc = at.depth ? Z_EN | ((uint32_t)cmp_code[at.depth_func & 7] << 4) | (at.depth_mask ? Z_WRITE : 0u) : 0u;
  alpha = 0;
  if (at.alpha_func != GR_CMP_ALWAYS)
    alpha = ALPHA_TEST_EN | ALPHA_TEST_SRC | ((uint32_t)cmp_code[at.alpha_func & 7] << 4) |
            ((uint32_t)(at.alpha_ref & 0xFF) << 16);
  wr_changed(R_SCALE_3D_CNTL, s3d, &at.last_s3d);
  wr_changed(R_Z_CNTL, zc, &at.last_z);
  wr_changed(R_ALPHA_TST_CNTL, alpha, &at.last_alpha);
  /* S and T go from 0 to 1 across the texture. In Glide, 256 is the
   * long side. */
  ss = (float)(1 << at.tex_large) / ((float)(1 << at.tex_wlog) * 256.0f);
  ts = (float)(1 << at.tex_large) / ((float)(1 << at.tex_hlog) * 256.0f);
  for (i = 0; i < 3; i++) {
    const sgl_gr_vertex *v = g[i];
    uint32_t r = R_VERTEX + (uint32_t)i * 32u;
    uint32_t vis = fog ? byte(fog_visibility(v->oow)) : 255u;
    slots(textured ? 7 : 4);
    if (textured) {
      wr1(r + V_S, float_bits(v->tmu[0].sow * ss));
      wr1(r + V_T, float_bits(v->tmu[0].tow * ts));
      wr1(r + V_W, float_bits(v->tmu[0].oow));
    }
    wr1(r + V_SPEC, vis << 24);
    /* Z is 16.16: lrintf would overflow 31 bits. */
    wr1(r + V_Z, (uint32_t)(clampf(v->ooz, 0.0f, 65535.0f) * 65536.0f + 0.5f));
    wr1(r + V_ARGB, (byte(v->a) << 24) | (byte(v->r) << 16) | (byte(v->g) << 8) | byte(v->b));
    wr1(r + V_XY, ((uint32_t)(qx[i] & 0xFFFF) << 16) | (uint32_t)(qy[i] & 0xFFFF));
  }
  slots(1);
  wr1(R_ONE_OVER_AREA, float_bits((float)(16.0 / (double)cross)));
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
  if ((dx < 0 ? -dx : dx) >= (dy < 0 ? -dy : dy))
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
static void SGL_GLIDE_CALL gr_depth_buffer_mode(int mode) { at.depth = mode != GR_DEPTHBUFFER_DISABLE; }

/* grDepthBufferFunction: the Z compare. */
static void SGL_GLIDE_CALL gr_depth_buffer_function(int func) { at.depth_func = func; }

/* grDepthMask: the Z writes. */
static void SGL_GLIDE_CALL gr_depth_mask(int on) { at.depth_mask = on != 0; }

/* grColorMask: the color writes on or off (DP_WRITE_MASK). */
static void SGL_GLIDE_CALL gr_color_mask(int rgb, int alpha) {
  (void)alpha;
  at.color_mask = rgb != 0;
  if (!at.open) return;
  slots(1);
  wr(R_DP_WRITE_MASK, at.color_mask ? 0xFFFFFFFFu : 0u);
}

/* grColorCombine: the combines of glide.c: the vertex color, the
 * texture (GL_REPLACE), the texture times the vertex color
 * (GL_MODULATE), or the vertex color and the texture mixed by the
 * texture alpha (GL_DECAL). */
static void SGL_GLIDE_CALL gr_color_combine(int func, int factor, int local, int other, int invert) {
  (void)local, (void)invert;
  if (other != GR_COMBINE_OTHER_TEXTURE || func == GR_COMBINE_FUNCTION_LOCAL)
    at.color_combine = COMBINE_COLOR;
  else if (func == GR_COMBINE_FUNCTION_BLEND)
    at.color_combine = COMBINE_DECAL;
  else if (func == GR_COMBINE_FUNCTION_SCALE_OTHER && factor == GR_COMBINE_FACTOR_LOCAL)
    at.color_combine = COMBINE_MODULATE;
  else
    at.color_combine = COMBINE_REPLACE;
}

/* grAlphaCombine: the alpha is the vertex alpha (LOCAL), or comes from
 * the texture (SCALE_OTHER). */
static void SGL_GLIDE_CALL gr_alpha_combine(int func, int factor, int local, int other, int invert) {
  (void)factor, (void)local, (void)other, (void)invert;
  at.alpha_texture = func == GR_COMBINE_FUNCTION_SCALE_OTHER;
}

/* grTexCombine: the card has one texture unit. */
static void SGL_GLIDE_CALL gr_tex_combine(int tmu, int rgb_func, int rgb_factor, int alpha_func, int alpha_factor,
                                          int rgb_invert, int alpha_invert) {
  (void)tmu, (void)rgb_func, (void)rgb_factor, (void)alpha_func, (void)alpha_factor, (void)rgb_invert,
      (void)alpha_invert;
}

/* grAlphaBlendFunction: the blend factors of the color. */
static void SGL_GLIDE_CALL gr_alpha_blend_function(int rgb_src, int rgb_dst, int alpha_src, int alpha_dst) {
  (void)alpha_src, (void)alpha_dst;
  at.blend_src = rgb_src, at.blend_dst = rgb_dst;
}

/* grAlphaTestFunction: the alpha test. */
static void SGL_GLIDE_CALL gr_alpha_test_function(int func) { at.alpha_func = func; }

/* grAlphaTestReferenceValue: the alpha that the test compares with. */
static void SGL_GLIDE_CALL gr_alpha_test_reference_value(unsigned value) { at.alpha_ref = (int)value; }

/* grFogMode: the fog table on or off. */
static void SGL_GLIDE_CALL gr_fog_mode(int mode) { at.fog = mode != GR_FOG_DISABLE; }

/* grFogColorValue: the fog color (0xRRGGBB), in DP_FRGD_CLR. */
static void SGL_GLIDE_CALL gr_fog_color_value(unsigned long color) {
  at.fog_color = (uint32_t)color & 0xFFFFFFu;
  if (!at.open) return;
  slots(1);
  wr(R_DP_FRGD_CLR, at.fog_color);
}

/* grFogTable: keeps the table, and the eye depth of each entry: entry
 * i is at w = 2^(3 + i / 4) / (8 - i % 4), as on a Voodoo. */
static void SGL_GLIDE_CALL gr_fog_table(const unsigned char *table) {
  int i;
  for (i = 0; i < GR_FOG_TABLE_SIZE; i++) {
    at.fog_table[i] = table[i];
    at.fog_w[i] = (float)(1ul << (3 + (i >> 2))) / (float)(8 - (i & 3));
  }
}

/* grCullMode: glide.c culls in software. */
static void SGL_GLIDE_CALL gr_cull_mode(int mode) { (void)mode; }

/* grDitherMode: dither the 16-bit pixels, or round them. */
static void SGL_GLIDE_CALL gr_dither_mode(int mode) { at.dither = mode != GR_DITHER_DISABLE; }

/* grTexMinAddress: the textures start after the Z buffer. */
static unsigned long SGL_GLIDE_CALL gr_tex_min_address(int tmu) {
  (void)tmu;
  return at.tex_min;
}

/* grTexMaxAddress: the textures end at the end of video memory. */
static unsigned long SGL_GLIDE_CALL gr_tex_max_address(int tmu) {
  (void)tmu;
  return at.memory;
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

/* Gives the bytes of the level of a texture whose long side is 2^n
 * texels (16 bits for each texel), rounded up to 8 bytes. wlog and
 * hlog are the sizes of the largest level, whose long side is 2^large
 * texels. */
static unsigned long level_bytes(int n, int large, int wlog, int hlog) {
  int w = wlog - (large - n), h = hlog - (large - n);
  if (w < 0) w = 0;
  if (h < 0) h = 0;
  return ((1ul << (w + h)) * 2ul + 7ul) & ~7ul;
}

/* grTexTextureMemRequired: the bytes of all the levels, the largest
 * first. */
static unsigned long SGL_GLIDE_CALL gr_tex_texture_mem_required(unsigned long even_odd, sgl_gr_texinfo *info) {
  int wlog, hlog, large = 8 - info->large_lod, small = 8 - info->small_lod, n;
  unsigned long size = 0;
  (void)even_odd;
  tex_size(info, &wlog, &hlog);
  for (n = large; n >= small; n--) size += level_bytes(n, large, wlog, hlog);
  return (size + 63ul) & ~63ul;
}

/* grTexDownloadMipMap: writes all the levels at start, the largest
 * first. RGB 565 and ARGB 4444 are the formats of the card. */
static void SGL_GLIDE_CALL gr_tex_download_mip_map(int tmu, unsigned long start, unsigned long even_odd,
                                                   sgl_gr_texinfo *info) {
  const uint16_t *src = (const uint16_t *)info->data;
  int wlog, hlog, large = 8 - info->large_lod, small = 8 - info->small_lod, n;
  unsigned long off = start;
  (void)tmu, (void)even_odd;
  if (!at.open || start + gr_tex_texture_mem_required(0, info) > at.memory) return;
  tex_size(info, &wlog, &hlog);
  idle();
  for (n = large; n >= small; n--) {
    long i;
    int w = wlog - (large - n), h = hlog - (large - n);
    long texels = (1l << ((w < 0 ? 0 : w) + (h < 0 ? 0 : h)));
    volatile uint16_t *dst = (volatile uint16_t *)(at.lfb + off);
    for (i = 0; i < texels; i++) dst[i] = src[i];
    src += texels;
    off += level_bytes(n, large, wlog, hlog);
  }
  at.tex_flush = 1;
}

/* grTexSource: the texture that the triangles use. TEX_n_OFF is the
 * level whose long side is 2^n texels. */
static void SGL_GLIDE_CALL gr_tex_source(int tmu, unsigned long start, unsigned long even_odd, sgl_gr_texinfo *info) {
  int n;
  uint32_t off = (uint32_t)start, last = off;
  (void)tmu, (void)even_odd;
  at.textured = 1;
  at.tex_base = (uint32_t)start;
  at.tex_large = 8 - info->large_lod;
  at.tex_small = 8 - info->small_lod;
  tex_size(info, &at.tex_wlog, &at.tex_hlog);
  at.tex_format = info->format == GR_TEXFMT_ARGB_4444 ? PIX_4444 : PIX_565;
  if (!at.open) return;
  slots(12);
  for (n = at.tex_large; n >= at.tex_small; n--) {
    wr(R_TEX_0_OFF + (uint32_t)n * 4u, off);
    last = off;
    off += (uint32_t)level_bytes(n, at.tex_large, at.tex_wlog, at.tex_hlog);
  }
  /* The levels below the smallest are the smallest again. */
  for (n = at.tex_small - 1; n >= 0; n--) wr(R_TEX_0_OFF + (uint32_t)n * 4u, last);
  wr(R_TEX_SIZE_PITCH, (uint32_t)at.tex_wlog | ((uint32_t)at.tex_large << 4) | ((uint32_t)at.tex_hlog << 8) |
                           ((uint32_t)at.tex_small << 12));
  at.tex_flush = 1;
}

/* grTexFilterMode: the filters for the minification and for the
 * magnification. */
static void SGL_GLIDE_CALL gr_tex_filter_mode(int tmu, int min, int mag) {
  (void)tmu;
  at.tex_min_linear = min == GR_TEXTUREFILTER_BILINEAR;
  at.tex_mag_linear = mag == GR_TEXTUREFILTER_BILINEAR;
}

/* grTexClampMode: wrap or clamp, for s and for t. */
static void SGL_GLIDE_CALL gr_tex_clamp_mode(int tmu, int s, int t) {
  (void)tmu;
  at.tex_clamp = (s == GR_TEXTURECLAMP_CLAMP ? TEX_CLAMP_S : 0u) | (t == GR_TEXTURECLAMP_CLAMP ? TEX_CLAMP_T : 0u);
}

/* grTexMipMapMode: the nearest mipmap, or the largest level only. */
static void SGL_GLIDE_CALL gr_tex_mip_map_mode(int tmu, int mode, int lod_blend) {
  (void)tmu, (void)lod_blend;
  at.tex_mipmap = mode != GR_MIPMAP_DISABLE;
}

/* Gives the offset of the buffer of a lock: the front buffer, the back
 * buffer or the Z buffer. The rows have the same pitch. */
static uint32_t lock_buffer(int buffer) {
  if (buffer == GR_BUFFER_AUXBUFFER) return at.zbuffer;
  return at.buffer[buffer == GR_BUFFER_BACKBUFFER ? at.back : at.front];
}

/* grLfbLock: gives a copy of the buffer in system memory, in RGB 565
 * (or the Z values), rows in the order of origin. A write lock copies
 * the pixels to the card in grLfbUnlock. */
static int SGL_GLIDE_CALL gr_lfb_lock(int type, int buffer, int write_mode, int origin, int pixel_pipeline,
                                      sgl_gr_lfbinfo *info) {
  int x, y;
  uint32_t base;
  (void)pixel_pipeline;
  if (!at.open) return 0;
  if (!at.shadow) at.shadow = (uint16_t *)malloc((size_t)at.width * (size_t)at.height * 2u);
  if (!at.shadow) return 0;
  idle();
  at.lock_origin = origin;
  base = lock_buffer(buffer);
  if (type == GR_LFB_READ_ONLY) {
    for (y = 0; y < at.height; y++) {
      int row = origin == GR_ORIGIN_LOWER_LEFT ? at.height - 1 - y : y;
      const volatile uint16_t *p = (const volatile uint16_t *)(at.lfb + base) + (long)row * at.pitch;
      uint16_t *q = at.shadow + (long)y * at.width;
      for (x = 0; x < at.width; x++) q[x] = p[x];
    }
  }
  info->ptr = at.shadow;
  info->stride = (unsigned long)at.width * 2ul;
  info->write_mode = write_mode;
  info->origin = origin;
  return 1;
}

/* grLfbUnlock: copies the pixels of a write lock to the card. */
static int SGL_GLIDE_CALL gr_lfb_unlock(int type, int buffer) {
  int x, y;
  uint32_t base;
  if (!at.open || !at.shadow || type != GR_LFB_WRITE_ONLY) return 1;
  base = lock_buffer(buffer);
  for (y = 0; y < at.height; y++) {
    int row = at.lock_origin == GR_ORIGIN_LOWER_LEFT ? at.height - 1 - y : y;
    volatile uint16_t *p = (volatile uint16_t *)(at.lfb + base) + (long)row * at.pitch;
    const uint16_t *q = at.shadow + (long)y * at.width;
    for (x = 0; x < at.width; x++) p[x] = q[x];
  }
  return 1;
}

/* Fills api with the ATI driver (see softgl_glide.h). */
int sgl_glide_ati(sgl_glide_api *api) {
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
  api->limits = SGL_GLIDE_LIMIT_FOG_ALPHA;
  return 1;
}

#endif
