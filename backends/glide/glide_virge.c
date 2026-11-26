/* glide_virge.c - the Glide 2 functions of the Glide back end, on the
 * 3D engine of an S3 ViRGE card, for 32-bit DOS. sgl_glide_virge fills
 * an sgl_glide_api, and the Glide back end (glide.c) uses it as it uses
 * a Voodoo. The cards are the ViRGE (86C325), ViRGE/VX (86C988),
 * ViRGE/DX and ViRGE/GX (86C375 and 86C385), ViRGE/GX2 (86C357) and
 * Trio3D/2X (86C362 and 86C368).
 *
 * The ViRGE draws less than a Voodoo. The driver sets the limits of
 * the table (SGL_GLIDE_LIMIT_), and softgl then draws in software what
 * the card cannot do:
 *
 * - The blend is (source alpha, 1 - source alpha) only.
 * - No alpha test and no color mask.
 * - The fog uses the vertex alpha, so no fog with a blend.
 * - The alpha is the texture alpha or the vertex alpha, not a product.
 * - Square textures only. The texture wraps; a clamped texture wraps
 *   too.
 *
 * The screen is a VBE mode of 15-bit color (5:5:5) with its linear
 * frame buffer. Video memory holds three color buffers (two when the
 * memory is small), the 16-bit Z buffer, and then the textures. The
 * registers are in the "new MMIO" window, 16 MB after the linear frame
 * buffer.
 *
 * The register facts come from the ViRGE documentation of S3 and from
 * the ViRGE of the 86Box emulator, where this driver is tested.
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

/* The registers (byte offsets in the MMIO window). */
enum {
  R_SUBSYS_STATUS = 0x8504,
  /* The 2D engine (rectangle fill). */
  R_2D_DEST_BASE = 0xA4D8,
  R_2D_CLIP_LR = 0xA4DC,
  R_2D_CLIP_TB = 0xA4E0,
  R_2D_STRIDE = 0xA4E4,
  R_2D_PAT_FG = 0xA4F4,
  R_2D_CMD = 0xA500,
  R_2D_SIZE = 0xA504,
  R_2D_DEST_XY = 0xA50C,
  /* The 3D engine (triangles). */
  R_Z_BASE = 0xB4D4,
  R_DEST_BASE = 0xB4D8,
  R_CLIP_LR = 0xB4DC,
  R_CLIP_TB = 0xB4E0,
  R_DEST_STRIDE = 0xB4E4,
  R_Z_STRIDE = 0xB4E8,
  R_TEX_BASE = 0xB4EC,
  R_TEX_BORDER = 0xB4F0,
  R_FOG_COLOR = 0xB4F4,
  R_CMD = 0xB500,
  R_TBV = 0xB504,
  R_TBU = 0xB508,
  R_WDX = 0xB50C,
  R_WDY = 0xB510,
  R_WS = 0xB514,
  R_DDX = 0xB518,
  R_VDX = 0xB51C,
  R_UDX = 0xB520,
  R_DDY = 0xB524,
  R_VDY = 0xB528,
  R_UDY = 0xB52C,
  R_DS = 0xB530,
  R_VS = 0xB534,
  R_US = 0xB538,
  R_GBDX = 0xB53C,
  R_ARDX = 0xB540,
  R_GBDY = 0xB544,
  R_ARDY = 0xB548,
  R_GBS = 0xB54C,
  R_ARS = 0xB550,
  R_ZDX = 0xB554,
  R_ZDY = 0xB558,
  R_ZS = 0xB55C,
  R_XDY12 = 0xB560,
  R_XEND12 = 0xB564,
  R_XDY01 = 0xB568,
  R_XEND01 = 0xB56C,
  R_XDY02 = 0xB570,
  R_XS = 0xB574,
  R_YS = 0xB578,
  R_YCOUNT = 0xB57C
};

/* The bits of the command register (R_CMD and R_2D_CMD). */
#define CMD_AE (1u << 0)          /* auto execute: the last register starts it */
#define CMD_HC (1u << 1)          /* clip to the clip registers */
#define CMD_16BPP (1u << 2)       /* 16-bit destination */
#define CMD_TEX_ARGB4444 (1u << 5)
#define CMD_TEX_ARGB1555 (2u << 5)
#define CMD_FILTER_MIP (0u << 12) /* mipmap, nearest texel */
#define CMD_FILTER_MIP_LINEAR (2u << 12)
#define CMD_FILTER_ONE (4u << 12) /* one level, nearest texel */
#define CMD_FILTER_ONE_LINEAR (6u << 12)
#define CMD_MODULATE (1u << 15)
#define CMD_FOG (1u << 17)
#define CMD_ALPHA_VERTEX (1u << 18) /* the blend uses the vertex alpha */
#define CMD_BLEND (1u << 19)
#define CMD_ZUP (1u << 23)        /* write Z */
#define CMD_NO_Z (3u << 24)
#define CMD_WRAP (1u << 26)
#define CMD_XP (1u << 25)         /* 2D: x goes up */
#define CMD_YP (1u << 26)         /* 2D: y goes up */
#define CMD_GOURAUD (0u << 27)
#define CMD_LIT_TEXTURE (5u << 27)   /* perspective texture times color */
#define CMD_UNLIT_TEXTURE (6u << 27) /* perspective texture */
#define CMD_RECTFILL (2u << 27)
#define CMD_ROP_PATCOPY (0xF0u << 17)

/* The color combines of glide.c, as the driver knows them. */
enum { COMBINE_COLOR, COMBINE_REPLACE, COMBINE_MODULATE };
/* The alpha sources. */
enum { ALPHA_VERTEX, ALPHA_TEXTURE, ALPHA_PRODUCT };

/* The state of the driver. */
static struct {
  sgl_pci_device pci;
  volatile uint8_t *lfb;   /* video memory */
  volatile uint8_t *mmio;  /* the registers */
  uint32_t lfb_phys, memory;
  int persp_shift;         /* 12 on the ViRGE and ViRGE/VX, 8 after them */
  int open, width, height, stride, origin;
  uint32_t buffer[3];      /* the offsets of the color buffers */
  int buffers;             /* 3, or 2 when video memory is small */
  int back;                /* the color buffer that is drawn */
  int front;               /* the color buffer that is shown */
  uint32_t zbuffer, tex_min;
  /* The state that Glide sets. */
  int depth, depth_func, depth_mask, blend, fog, color_combine, alpha_combine;
  uint8_t fog_table[GR_FOG_TABLE_SIZE];
  float fog_w[GR_FOG_TABLE_SIZE];
  int clip[4];             /* minx, miny, maxx, maxy (Glide coordinates) */
  /* The texture that TexSource selected. */
  int textured, tex_level, tex_alpha, tex_linear, tex_mipmap;
  uint32_t tex_base, tex_format;
  uint16_t *shadow;        /* the copy of a locked buffer */
  int lock_type, lock_buffer, lock_origin;
} vg;

/* The PCI IDs of the ViRGE cards. */
static const sgl_pci_id virge_ids[] = {
    {0x5333, 0x5631}, {0x5333, 0x883D}, {0x5333, 0x8A01}, {0x5333, 0x8A10}, {0x5333, 0x8A13}, {0, 0}};

/* Writes a 32-bit register. */
static void wr(uint32_t reg, uint32_t value) { *(volatile uint32_t *)(vg.mmio + reg) = value; }

/* Writes a 32-bit register with a signed value. */
static void wri(uint32_t reg, int32_t value) { wr(reg, (uint32_t)value); }

/* Reads the 16-bit subsystem status. */
static unsigned status(void) { return *(volatile uint16_t *)(vg.mmio + R_SUBSYS_STATUS); }

/* Waits until the command FIFO has n free slots (bits 8 to 12 of the
 * status). Stops after about one second. */
static void slots(unsigned n) {
  uint32_t start = sgl_dos_ms();
  while (((status() >> 8) & 0x1F) < n)
    if (sgl_dos_ms() - start > 1000) break;
}

/* Waits until the 3D engine is idle (bit 13 of the status). */
static void idle(void) {
  uint32_t start = sgl_dos_ms();
  if (!vg.mmio) return;
  while (!(status() & 0x2000))
    if (sgl_dos_ms() - start > 1000) break;
}

/* Writes the CRTC register index of the VGA. */
static void crtc(int index, uint8_t value) {
  outportb(0x3D4, (unsigned char)index);
  outportb(0x3D5, value);
}

/* Reads the CRTC register index of the VGA. */
static uint8_t crtc_read(int index) {
  outportb(0x3D4, (unsigned char)index);
  return inportb(0x3D5);
}

/* Converts a Glide color (0xAARRGGBB) to a 5:5:5 pixel. */
static uint16_t pixel555(unsigned long c) {
  return (uint16_t)(((c >> 9) & 0x7C00u) | ((c >> 6) & 0x03E0u) | ((c >> 3) & 0x001Fu));
}

/* Gives the screen row of a Glide y. */
static float screen_y(float y) { return vg.origin == GR_ORIGIN_LOWER_LEFT ? (float)vg.height - y : y; }

/* grGlideInit: finds the card and maps its registers. */
static void SGL_GLIDE_CALL gr_glide_init(void) {
  uint16_t dev;
  memset(&vg, 0, sizeof vg);
  if (!sgl_pci_find(virge_ids, 0, &vg.pci)) return;
  dev = vg.pci.device;
  vg.persp_shift = (dev == 0x5631 || dev == 0x883D) ? 12 : 8;
  vg.lfb_phys = sgl_pci_bar(&vg.pci, 0);
  if (!vg.lfb_phys) return;
  sgl_pci_write16(&vg.pci, 0x04, (uint16_t)(sgl_pci_read16(&vg.pci, 0x04) | 0x0002));
  vg.mmio = (volatile uint8_t *)sgl_map_physical(vg.lfb_phys + 0x1000000ul, 0x10000ul);
}

/* grGlideShutdown: closes the screen and frees the mappings. */
static void SGL_GLIDE_CALL gr_glide_shutdown(void);

/* grSstQueryHardware: one board with one TMU, as a Voodoo Graphics
 * (GrHwConfiguration as 32-bit words). */
static int SGL_GLIDE_CALL gr_sst_query_hardware(void *config) {
  int32_t *hw = (int32_t *)config;
  if (!vg.mmio) return 0;
  hw[0] = 1;
  hw[1] = GR_SSTTYPE_VOODOO;
  hw[2] = 4;
  hw[3] = 2;
  hw[4] = 1;
  return 1;
}

/* grSstSelect: there is one board. */
static void SGL_GLIDE_CALL gr_sst_select(int which) { (void)which; }

/* Gives the VBE mode of 15-bit color for a Glide resolution, and its
 * size in *w and *h, or -1. */
static int vbe_mode(int resolution, int *w, int *h) {
  switch (resolution) {
    case GR_RESOLUTION_320x200: *w = 320, *h = 200; return 0x10D;
    case GR_RESOLUTION_640x480: *w = 640, *h = 480; return 0x110;
    case GR_RESOLUTION_800x600: *w = 800, *h = 600; return 0x113;
    case GR_RESOLUTION_1024x768: *w = 1024, *h = 768; return 0x116;
    default: return -1;
  }
}

/* Sets the registers that stay the same for each triangle. */
static void set_buffers(void) {
  slots(8);
  wr(R_DEST_BASE, vg.buffer[vg.back]);
  wr(R_Z_BASE, vg.zbuffer);
  wr(R_DEST_STRIDE, ((uint32_t)vg.stride << 16) | (uint32_t)vg.stride);
  wr(R_Z_STRIDE, (uint32_t)vg.width * 2u);
  wr(R_TBU, 0);
  wr(R_TBV, 0);
  wr(R_TEX_BORDER, 0);
}

/* grClipWindow: the clip rectangle (the maxima are not in it). */
static void SGL_GLIDE_CALL gr_clip_window(unsigned long minx, unsigned long miny, unsigned long maxx,
                                          unsigned long maxy);

/* grSstWinOpen: sets the VBE mode, maps the frame buffer and divides
 * video memory. 16-bit color only; the other arguments are not used. */
static int SGL_GLIDE_CALL gr_sst_win_open(unsigned long hwnd, int resolution, int refresh, int color_format,
                                          int origin, int color_buffers, int aux_buffers) {
  sgl_vbe_mode info;
  uint32_t frame, need;
  int mode, w, h, i;
  (void)hwnd, (void)refresh, (void)color_format, (void)color_buffers, (void)aux_buffers;
  if (!vg.mmio || vg.open) return 0;
  mode = vbe_mode(resolution, &w, &h);
  if (mode < 0 || !sgl_vbe_mode_info(mode, &info) || !(info.attributes & 1) || info.bits != 15) return 0;
  vg.memory = sgl_vbe_memory();
  if (!vg.memory) vg.memory = 0x200000ul;
  frame = (uint32_t)info.stride * (uint32_t)h;
  need = 2 * frame + (uint32_t)w * 2u * (uint32_t)h + 0x40000ul;
  if (need > vg.memory) return 0;
  if (!((info.attributes & 0x80) && sgl_vbe_set_mode(mode, 1))) {
    /* A VBE 1.2 BIOS: set the mode, then the linear frame buffer at BAR
     * 0 (CR58 bit 4, the size in bits 0 to 2, the address in CR59 and
     * CR5A). */
    if (!sgl_vbe_set_mode(mode, 0)) return 0;
    crtc(0x38, 0x48);
    crtc(0x39, 0xA5);
    crtc(0x59, (uint8_t)(vg.lfb_phys >> 24));
    crtc(0x5A, (uint8_t)(vg.lfb_phys >> 16));
    crtc(0x58, (uint8_t)((crtc_read(0x58) & ~0x17u) | 0x13u));
  } else if (info.lfb) {
    vg.lfb_phys = info.lfb;
  }
  /* The mode set turns off the new MMIO window (CR53 bit 3). */
  crtc(0x38, 0x48);
  crtc(0x39, 0xA5);
  crtc(0x53, (uint8_t)(crtc_read(0x53) | 0x08u));
  vg.lfb = (volatile uint8_t *)sgl_map_physical(vg.lfb_phys, vg.memory);
  if (!vg.lfb) {
    sgl_vbe_text_mode();
    return 0;
  }
  vg.width = w, vg.height = h, vg.stride = info.stride, vg.origin = origin;
  /* Three color buffers when the textures keep 1 MB: the card draws
   * into the third while the monitor can still show the old front. */
  vg.buffers = need + frame + 0xC0000ul <= vg.memory ? 3 : 2;
  for (i = 0; i < vg.buffers; i++) vg.buffer[i] = (uint32_t)i * ((frame + 7u) & ~7u);
  vg.zbuffer = vg.buffer[vg.buffers - 1] + ((frame + 7u) & ~7u);
  vg.tex_min = (vg.zbuffer + (uint32_t)w * 2u * (uint32_t)h + 7u) & ~7u;
  vg.front = 0;
  vg.back = 1;
  vg.clip[0] = 0, vg.clip[1] = 0, vg.clip[2] = w, vg.clip[3] = h;
  vg.depth_func = GR_CMP_LESS;
  vg.depth_mask = 1;
  vg.open = 1;
  set_buffers();
  gr_clip_window(0, 0, (unsigned long)w, (unsigned long)h);
  sgl_vbe_display_start(0, 0);
  return 1;
}

/* grSstWinClose: sets the text mode again and frees the frame buffer. */
static void SGL_GLIDE_CALL gr_sst_win_close(void) {
  if (!vg.open) return;
  idle();
  sgl_vbe_text_mode();
  sgl_unmap_physical(vg.lfb);
  vg.lfb = NULL;
  free(vg.shadow);
  vg.shadow = NULL;
  vg.open = 0;
}

/* grGlideShutdown: closes the screen and frees the mappings. */
static void SGL_GLIDE_CALL gr_glide_shutdown(void) {
  gr_sst_win_close();
  if (vg.mmio) sgl_unmap_physical(vg.mmio);
  memset(&vg, 0, sizeof vg);
}

/* grSstIdle: waits until the card has drawn all. */
static void SGL_GLIDE_CALL gr_sst_idle(void) { idle(); }

/* Fills the rectangle (x, y, w, h) of the buffer at offset base, with
 * stride bytes in each row, with the 16-bit value value. The 2D engine
 * of the card does it. */
static void fill(uint32_t base, int stride, int x, int y, int w, int h, uint16_t value) {
  if (w <= 0 || h <= 0) return;
  slots(9);
  wr(R_2D_DEST_BASE, base);
  wr(R_2D_STRIDE, (uint32_t)stride << 16);
  wr(R_2D_CLIP_LR, (uint32_t)(vg.width - 1));
  wr(R_2D_CLIP_TB, (uint32_t)(vg.height - 1));
  wr(R_2D_PAT_FG, value);
  wr(R_2D_SIZE, ((uint32_t)(w - 1) << 16) | (uint32_t)h);
  wr(R_2D_DEST_XY, ((uint32_t)x << 16) | (uint32_t)y);
  wr(R_2D_CMD, CMD_RECTFILL | CMD_16BPP | CMD_ROP_PATCOPY | CMD_XP | CMD_YP | CMD_HC);
}

/* Gives the screen rectangle of the clip window in *x, *y, *w, *h. */
static void clip_rect(int *x, int *y, int *w, int *h) {
  int top = vg.origin == GR_ORIGIN_LOWER_LEFT ? vg.height - vg.clip[3] : vg.clip[1];
  *x = vg.clip[0];
  *y = top;
  *w = vg.clip[2] - vg.clip[0];
  *h = vg.clip[3] - vg.clip[1];
}

/* grBufferClear: fills the clip window of the back buffer with color,
 * and of the Z buffer with depth when the depth writes are on. The Z
 * buffer has the stride width * 2. */
static void SGL_GLIDE_CALL gr_buffer_clear(unsigned long color, unsigned alpha, unsigned depth) {
  int x, y, w, h;
  (void)alpha;
  if (!vg.open) return;
  clip_rect(&x, &y, &w, &h);
  fill(vg.buffer[vg.back], vg.stride, x, y, w, h, pixel555(color));
  if (vg.depth && vg.depth_mask) fill(vg.zbuffer, vg.width * 2, x, y, w, h, (uint16_t)depth);
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
  int old = vg.front;
  if (!vg.open) return;
  idle();
  sgl_vbe_display_start((int)(vg.buffer[vg.back] / (uint32_t)vg.stride), interval > 0);
  vg.front = vg.back;
  if (vg.buffers == 3) {
    vg.back = 3 - vg.front - old;
  } else {
    if (interval > 0) next_frame();
    vg.back = old;
  }
  slots(1);
  wr(R_DEST_BASE, vg.buffer[vg.back]);
}

/* Gives the fog alpha of a vertex (255 is no fog) from the Glide fog
 * table, by the eye depth w = 1 / oow. */
static float fog_alpha(float oow) {
  float w = oow > 0.0f ? 1.0f / oow : 1e30f, f;
  int i;
  if (w <= vg.fog_w[0]) return 255.0f - vg.fog_table[0];
  for (i = 1; i < GR_FOG_TABLE_SIZE; i++)
    if (w < vg.fog_w[i]) break;
  if (i == GR_FOG_TABLE_SIZE) return 255.0f - vg.fog_table[GR_FOG_TABLE_SIZE - 1];
  f = (w - vg.fog_w[i - 1]) / (vg.fog_w[i] - vg.fog_w[i - 1]);
  return 255.0f - ((float)vg.fog_table[i - 1] + f * ((float)vg.fog_table[i] - (float)vg.fog_table[i - 1]));
}

/* The values that the triangle setup interpolates. */
enum { A_R, A_G, A_B, A_A, A_Z, A_U, A_V, A_W, A_COUNT };

/* A vertex on the screen: x, y and the values. */
typedef struct {
  float x, y, v[A_COUNT];
} sv;

/* Converts a Glide vertex to a screen vertex. */
static void to_screen(const sgl_gr_vertex *g, sv *s) {
  /* The 2^(5 + level) (or 2^(9 + level)) and 2^24 scales make the
   * perspective divide of the card give the texture coordinate times
   * 2^27 (see the docs). S and T of Glide are 0 to 256 on a square
   * texture. */
  float uscale = ldexpf(1.0f, vg.persp_shift - 3 + vg.tex_level);
  s->x = g->x;
  s->y = screen_y(g->y);
  s->v[A_R] = g->r * 128.0f;
  s->v[A_G] = g->g * 128.0f;
  s->v[A_B] = g->b * 128.0f;
  s->v[A_A] = (vg.fog ? fog_alpha(g->oow) : g->a) * 128.0f;
  s->v[A_Z] = g->ooz * 32768.0f;
  s->v[A_U] = g->tmu[0].sow * uscale;
  s->v[A_V] = g->tmu[0].tow * uscale;
  s->v[A_W] = g->tmu[0].oow * 16777216.0f;
}

/* Gives v as an int32_t, limited to the range of int32_t. */
static int32_t fix(float v) {
  if (v >= 2147483520.0f) return 2147483647;
  if (v <= -2147483520.0f) return -2147483647;
  return (int32_t)lrintf(v);
}

/* Gives v as a 16-bit gradient of a color. */
static uint32_t fix16(float v) {
  int32_t i = fix(v);
  if (i > 32767) i = 32767;
  if (i < -32768) i = -32768;
  return (uint32_t)i & 0xFFFFu;
}

/* Gives v as a 16-bit start value of a color (0 to 0x7FFF). */
static uint32_t start16(float v) {
  int32_t i = fix(v);
  if (i < 0) i = 0;
  if (i > 0x7FFF) i = 0x7FFF;
  return (uint32_t)i;
}

/* Gives the command for a triangle from the state of Glide. */
static uint32_t command(void) {
  static const uint8_t zfunc[8] = {0, 4, 2, 6, 1, 5, 3, 7};
  uint32_t cmd = CMD_AE | CMD_HC | CMD_16BPP;
  if (vg.depth) {
    cmd |= (uint32_t)zfunc[vg.depth_func & 7] << 20;
    if (vg.depth_mask) cmd |= CMD_ZUP;
  } else {
    cmd |= CMD_NO_Z;
  }
  if (vg.fog) cmd |= CMD_FOG;
  if (vg.blend) cmd |= CMD_BLEND;
  if (vg.textured && vg.color_combine != COMBINE_COLOR) {
    cmd |= vg.color_combine == COMBINE_MODULATE ? CMD_LIT_TEXTURE | CMD_MODULATE : CMD_UNLIT_TEXTURE;
    cmd |= vg.tex_format | CMD_WRAP | ((uint32_t)vg.tex_level << 8);
    if (vg.tex_mipmap)
      cmd |= vg.tex_linear ? CMD_FILTER_MIP_LINEAR : CMD_FILTER_MIP;
    else
      cmd |= vg.tex_linear ? CMD_FILTER_ONE_LINEAR : CMD_FILTER_ONE;
    if (vg.alpha_combine == ALPHA_VERTEX || (vg.alpha_combine == ALPHA_PRODUCT && !vg.tex_alpha))
      cmd |= CMD_ALPHA_VERTEX;
  } else {
    cmd |= CMD_GOURAUD;
  }
  return cmd;
}

/* Gives the mipmap level offset of a triangle in the 5.27 format of
 * the card: the log2 of the texels for each pixel. p has the screen
 * vertices, area the screen area times 2. */
static int32_t lod(const sv *p, float area) {
  float s[3], t[3], tex, scale = ldexpf(1.0f, 19 - vg.persp_shift);
  int i;
  for (i = 0; i < 3; i++) {
    float w = p[i].v[A_W] != 0.0f ? p[i].v[A_W] : 1.0f;
    /* U / W is the texel coordinate times 2^(persp_shift - 19). */
    s[i] = p[i].v[A_U] / w * scale, t[i] = p[i].v[A_V] / w * scale;
  }
  tex = fabsf((s[1] - s[0]) * (t[2] - t[0]) - (s[2] - s[0]) * (t[1] - t[0]));
  if (tex <= area || area <= 0.0f) return 0;
  return fix(0.5f * log2f(tex / area) * 134217728.0f);
}

/* grDrawTriangle: calculates the start values and the gradients, and
 * gives them to the 3D engine. The card draws from the bottom row up:
 * vertex 0 is the lowest on the screen and vertex 2 the highest. A row
 * is drawn when its center is in the triangle; a pixel when its center
 * is right of the left edge, and not right of the right edge. */
static void SGL_GLIDE_CALL gr_draw_triangle(const sgl_gr_vertex *a, const sgl_gr_vertex *b,
                                            const sgl_gr_vertex *c) {
  sv p[3], tmp;
  float area, k02, k01, k12, yc, yc2, x02, x01, x12, dx, dy, base[A_COUNT], gx[A_COUNT], gy[A_COUNT];
  int r_bot, r_mid, r_top, n01, n12, left, i;
  uint32_t cmd;
  if (!vg.open) return;
  to_screen(a, &p[0]);
  to_screen(b, &p[1]);
  to_screen(c, &p[2]);
  /* Sort by y, the largest first. */
  if (p[0].y < p[1].y) tmp = p[0], p[0] = p[1], p[1] = tmp;
  if (p[1].y < p[2].y) tmp = p[1], p[1] = p[2], p[2] = tmp;
  if (p[0].y < p[1].y) tmp = p[0], p[0] = p[1], p[1] = tmp;
  r_bot = (int)ceilf(p[0].y - 0.5f);
  r_mid = (int)ceilf(p[1].y - 0.5f);
  r_top = (int)ceilf(p[2].y - 0.5f);
  if (r_bot <= r_top) return;
  area = (p[1].x - p[0].x) * (p[2].y - p[0].y) - (p[2].x - p[0].x) * (p[1].y - p[0].y);
  if (area == 0.0f) return;
  n01 = r_bot - r_mid;
  n12 = r_mid - r_top;
  if (n01 > 2047 || n12 > 2047) return;
  k02 = (p[2].x - p[0].x) / (p[2].y - p[0].y);
  k01 = p[1].y != p[0].y ? (p[1].x - p[0].x) / (p[1].y - p[0].y) : 0.0f;
  k12 = p[2].y != p[1].y ? (p[2].x - p[1].x) / (p[2].y - p[1].y) : 0.0f;
  yc = (float)r_bot - 0.5f;
  yc2 = (float)r_mid - 0.5f;
  x02 = p[0].x + (yc - p[0].y) * k02;
  x01 = p[0].x + (yc - p[0].y) * k01;
  x12 = p[1].x + (yc2 - p[1].y) * k12;
  /* The long edge 0-2 is on the left when it is left of vertex 1. */
  left = p[0].x + (p[1].y - p[0].y) * k02 < p[1].x;
  for (i = 0; i < A_COUNT; i++) {
    float d1 = p[1].v[i] - p[0].v[i], d2 = p[2].v[i] - p[0].v[i];
    gx[i] = (d1 * (p[2].y - p[0].y) - d2 * (p[1].y - p[0].y)) / area;
    gy[i] = (d2 * (p[1].x - p[0].x) - d1 * (p[2].x - p[0].x)) / area;
    /* The value at the long edge on the first row. */
    base[i] = p[0].v[i] + gx[i] * (x02 - p[0].x) + gy[i] * (yc - p[0].y);
  }
  cmd = command();
  /* Along x the card adds the gradient for each pixel in its direction;
   * along y it goes one row up and along the long edge. */
  dx = left ? 1.0f : -1.0f;
  dy = -k02;
  slots(16);
  wr(R_CMD, cmd);
  if (cmd & (CMD_UNLIT_TEXTURE | CMD_LIT_TEXTURE)) {
    wri(R_WDX, fix(gx[A_W] * dx));
    wri(R_WDY, fix(gx[A_W] * dy - gy[A_W]));
    wri(R_WS, fix(base[A_W]));
    wri(R_DDX, 0);
    wri(R_DDY, 0);
    wri(R_DS, vg.tex_mipmap ? lod(p, area < 0.0f ? -area : area) : 0);
    wri(R_VDX, fix(gx[A_V] * dx));
    wri(R_UDX, fix(gx[A_U] * dx));
    wri(R_VDY, fix(gx[A_V] * dy - gy[A_V]));
    wri(R_UDY, fix(gx[A_U] * dy - gy[A_U]));
    wri(R_VS, fix(base[A_V]));
    wri(R_US, fix(base[A_U]));
  }
  slots(16);
  wr(R_GBDX, (fix16(gx[A_G] * dx) << 16) | fix16(gx[A_B] * dx));
  wr(R_ARDX, (fix16(gx[A_A] * dx) << 16) | fix16(gx[A_R] * dx));
  wr(R_GBDY, (fix16(gx[A_G] * dy - gy[A_G]) << 16) | fix16(gx[A_B] * dy - gy[A_B]));
  wr(R_ARDY, (fix16(gx[A_A] * dy - gy[A_A]) << 16) | fix16(gx[A_R] * dy - gy[A_R]));
  wr(R_GBS, (start16(base[A_G]) << 16) | start16(base[A_B]));
  wr(R_ARS, (start16(base[A_A]) << 16) | start16(base[A_R]));
  wri(R_ZDX, fix(gx[A_Z] * dx));
  wri(R_ZDY, fix(gx[A_Z] * dy - gy[A_Z]));
  wri(R_ZS, base[A_Z] < 0.0f ? 0 : fix(base[A_Z]));
  /* The edges: 12.20 fixed point, on the grid of the pixel centers. */
  wri(R_XDY12, fix(-k12 * 1048576.0f));
  wri(R_XEND12, fix((x12 - 0.5f) * 1048576.0f));
  wri(R_XDY01, fix(-k01 * 1048576.0f));
  wri(R_XEND01, fix((x01 - 0.5f) * 1048576.0f));
  wri(R_XDY02, fix(dy * 1048576.0f));
  wri(R_XS, fix((x02 - 0.5f) * 1048576.0f));
  wri(R_YS, (uint32_t)(r_bot - 1) & 0x7FF);
  wr(R_YCOUNT, ((uint32_t)n01 << 16) | (uint32_t)n12 | (left ? 0x80000000u : 0u));
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
static void SGL_GLIDE_CALL gr_depth_buffer_mode(int mode) { vg.depth = mode != GR_DEPTHBUFFER_DISABLE; }

/* grDepthBufferFunction: the Z compare. */
static void SGL_GLIDE_CALL gr_depth_buffer_function(int func) { vg.depth_func = func; }

/* grDepthMask: the Z writes. */
static void SGL_GLIDE_CALL gr_depth_mask(int on) { vg.depth_mask = on != 0; }

/* grColorMask: the card always writes the color (see the limits). */
static void SGL_GLIDE_CALL gr_color_mask(int rgb, int alpha) { (void)rgb, (void)alpha; }

/* grColorCombine: the combines of glide.c: the vertex color, the
 * texture (GL_REPLACE and GL_DECAL), or the texture times the vertex
 * color (GL_MODULATE). */
static void SGL_GLIDE_CALL gr_color_combine(int func, int factor, int local, int other, int invert) {
  (void)local, (void)invert;
  if (other != GR_COMBINE_OTHER_TEXTURE || func == GR_COMBINE_FUNCTION_LOCAL)
    vg.color_combine = COMBINE_COLOR;
  else if (func == GR_COMBINE_FUNCTION_SCALE_OTHER && factor == GR_COMBINE_FACTOR_LOCAL)
    vg.color_combine = COMBINE_MODULATE;
  else
    vg.color_combine = COMBINE_REPLACE;
}

/* grAlphaCombine: the vertex alpha, the texture alpha, or the product
 * (the texture alpha of a texture with alpha, else the vertex alpha). */
static void SGL_GLIDE_CALL gr_alpha_combine(int func, int factor, int local, int other, int invert) {
  (void)local, (void)other, (void)invert;
  if (func == GR_COMBINE_FUNCTION_LOCAL)
    vg.alpha_combine = ALPHA_VERTEX;
  else if (factor == GR_COMBINE_FACTOR_LOCAL)
    vg.alpha_combine = ALPHA_PRODUCT;
  else
    vg.alpha_combine = ALPHA_TEXTURE;
}

/* grTexCombine: the card has one texture unit. */
static void SGL_GLIDE_CALL gr_tex_combine(int tmu, int rgb_func, int rgb_factor, int alpha_func, int alpha_factor,
                                          int rgb_invert, int alpha_invert) {
  (void)tmu, (void)rgb_func, (void)rgb_factor, (void)alpha_func, (void)alpha_factor, (void)rgb_invert,
      (void)alpha_invert;
}

/* grAlphaBlendFunction: (source alpha, 1 - source alpha) turns on the
 * blend; the other factors are (1, 0) (see the limits). */
static void SGL_GLIDE_CALL gr_alpha_blend_function(int rgb_src, int rgb_dst, int alpha_src, int alpha_dst) {
  (void)alpha_src, (void)alpha_dst;
  vg.blend = rgb_src == GR_BLEND_SRC_ALPHA && rgb_dst == GR_BLEND_ONE_MINUS_SRC_ALPHA;
}

/* grAlphaTestFunction: the card has no alpha test (see the limits). */
static void SGL_GLIDE_CALL gr_alpha_test_function(int func) { (void)func; }

/* grAlphaTestReferenceValue: the card has no alpha test. */
static void SGL_GLIDE_CALL gr_alpha_test_reference_value(unsigned value) { (void)value; }

/* grFogMode: the fog table on or off. */
static void SGL_GLIDE_CALL gr_fog_mode(int mode) { vg.fog = mode != GR_FOG_DISABLE; }

/* grFogColorValue: the fog color (0xRRGGBB). */
static void SGL_GLIDE_CALL gr_fog_color_value(unsigned long color) {
  if (!vg.open) return;
  slots(1);
  wr(R_FOG_COLOR, (uint32_t)color & 0xFFFFFFu);
}

/* grFogTable: keeps the table, and the eye depth of each entry: entry
 * i is at w = 2^(3 + i / 4) / (8 - i % 4), as on a Voodoo. */
static void SGL_GLIDE_CALL gr_fog_table(const unsigned char *table) {
  int i;
  for (i = 0; i < GR_FOG_TABLE_SIZE; i++) {
    vg.fog_table[i] = table[i];
    vg.fog_w[i] = (float)(1ul << (3 + (i >> 2))) / (float)(8 - (i & 3));
  }
}

/* grClipWindow: the clip rectangle (the maxima are not in it). */
static void SGL_GLIDE_CALL gr_clip_window(unsigned long minx, unsigned long miny, unsigned long maxx,
                                          unsigned long maxy) {
  int x, y, w, h;
  vg.clip[0] = (int)minx, vg.clip[1] = (int)miny, vg.clip[2] = (int)maxx, vg.clip[3] = (int)maxy;
  if (!vg.open) return;
  clip_rect(&x, &y, &w, &h);
  if (w <= 0 || h <= 0) x = 1, y = 1, w = 0, h = 0;
  slots(2);
  wr(R_CLIP_LR, ((uint32_t)x << 16) | (uint32_t)((x + w - 1) & 0x7FF));
  wr(R_CLIP_TB, ((uint32_t)y << 16) | (uint32_t)((y + h - 1) & 0x7FF));
}

/* grCullMode: glide.c culls in software. */
static void SGL_GLIDE_CALL gr_cull_mode(int mode) { (void)mode; }

/* grDitherMode: the card dithers each 16-bit pixel. */
static void SGL_GLIDE_CALL gr_dither_mode(int mode) { (void)mode; }

/* grTexMinAddress: the textures start after the Z buffer. */
static unsigned long SGL_GLIDE_CALL gr_tex_min_address(int tmu) {
  (void)tmu;
  return vg.tex_min;
}

/* grTexMaxAddress: the textures end at the end of video memory. */
static unsigned long SGL_GLIDE_CALL gr_tex_max_address(int tmu) {
  (void)tmu;
  return vg.memory;
}

/* Gives the side of a level of a texture (256 for GR_LOD_256). */
static int side(int lod) { return 256 >> lod; }

/* grTexTextureMemRequired: the bytes of the levels, 16 bits each. */
static unsigned long SGL_GLIDE_CALL gr_tex_texture_mem_required(unsigned long even_odd, sgl_gr_texinfo *info) {
  unsigned long n = 0;
  int l;
  (void)even_odd;
  for (l = info->large_lod; l <= info->small_lod; l++) n += (unsigned long)side(l) * (unsigned long)side(l) * 2ul;
  return (n + 7ul) & ~7ul;
}

/* grTexDownloadMipMap: writes the levels at start: the largest first,
 * each side x side texels. RGB 565 becomes ARGB 1555; ARGB 4444 stays.
 * The textures are square (see the limits). */
static void SGL_GLIDE_CALL gr_tex_download_mip_map(int tmu, unsigned long start, unsigned long even_odd,
                                                   sgl_gr_texinfo *info) {
  const uint16_t *src = (const uint16_t *)info->data;
  volatile uint16_t *dst;
  unsigned long i, n;
  int l;
  (void)tmu, (void)even_odd;
  if (!vg.open || start + gr_tex_texture_mem_required(0, info) > vg.memory) return;
  idle();
  dst = (volatile uint16_t *)(vg.lfb + start);
  for (l = info->large_lod; l <= info->small_lod; l++) {
    n = (unsigned long)side(l) * (unsigned long)side(l);
    if (info->format == GR_TEXFMT_RGB_565) {
      for (i = 0; i < n; i++) {
        unsigned v = src[i];
        dst[i] = (uint16_t)(0x8000u | ((v >> 1) & 0x7FE0u) | (v & 0x001Fu));
      }
    } else {
      for (i = 0; i < n; i++) dst[i] = src[i];
    }
    src += n, dst += n;
  }
}

/* grTexSource: the texture that the triangles use. */
static void SGL_GLIDE_CALL gr_tex_source(int tmu, unsigned long start, unsigned long even_odd, sgl_gr_texinfo *info) {
  (void)tmu, (void)even_odd;
  vg.textured = 1;
  vg.tex_base = (uint32_t)start;
  vg.tex_level = 8 - info->large_lod;
  vg.tex_alpha = info->format == GR_TEXFMT_ARGB_4444;
  vg.tex_format = vg.tex_alpha ? CMD_TEX_ARGB4444 : CMD_TEX_ARGB1555;
  if (!vg.open) return;
  slots(1);
  wr(R_TEX_BASE, vg.tex_base);
}

/* grTexFilterMode: one filter for both: bilinear when one of the two
 * is bilinear. */
static void SGL_GLIDE_CALL gr_tex_filter_mode(int tmu, int min, int mag) {
  (void)tmu;
  vg.tex_linear = min == GR_TEXTUREFILTER_BILINEAR || mag == GR_TEXTUREFILTER_BILINEAR;
}

/* grTexClampMode: the card wraps the textures (see the limits). */
static void SGL_GLIDE_CALL gr_tex_clamp_mode(int tmu, int s, int t) { (void)tmu, (void)s, (void)t; }

/* grTexMipMapMode: the mipmaps on or off. */
static void SGL_GLIDE_CALL gr_tex_mip_map_mode(int tmu, int mode, int lod_blend) {
  (void)tmu, (void)lod_blend;
  vg.tex_mipmap = mode != GR_MIPMAP_DISABLE;
}

/* Gives the offset of the buffer of a lock: the front buffer (0), the
 * back buffer (1) or the Z buffer (2), and its stride in *stride. */
static uint32_t lock_buffer(int buffer, int *stride) {
  *stride = vg.stride;
  if (buffer == GR_BUFFER_AUXBUFFER) {
    *stride = vg.width * 2;
    return vg.zbuffer;
  }
  return vg.buffer[buffer == GR_BUFFER_BACKBUFFER ? vg.back : vg.front];
}

/* grLfbLock: gives a copy of the buffer in system memory, in RGB 565
 * (or the Z values), rows in the order of origin. A write lock copies
 * the pixels to the card in grLfbUnlock. */
static int SGL_GLIDE_CALL gr_lfb_lock(int type, int buffer, int write_mode, int origin, int pixel_pipeline,
                                      sgl_gr_lfbinfo *info) {
  int x, y, stride;
  uint32_t base;
  (void)write_mode, (void)pixel_pipeline;
  if (!vg.open) return 0;
  if (!vg.shadow) vg.shadow = (uint16_t *)malloc((size_t)vg.width * (size_t)vg.height * 2u);
  if (!vg.shadow) return 0;
  idle();
  vg.lock_type = type, vg.lock_buffer = buffer, vg.lock_origin = origin;
  base = lock_buffer(buffer, &stride);
  if (type == GR_LFB_READ_ONLY) {
    for (y = 0; y < vg.height; y++) {
      int row = origin == GR_ORIGIN_LOWER_LEFT ? vg.height - 1 - y : y;
      const volatile uint16_t *p = (const volatile uint16_t *)(vg.lfb + base + (uint32_t)row * (uint32_t)stride);
      uint16_t *q = vg.shadow + (long)y * vg.width;
      if (buffer == GR_BUFFER_AUXBUFFER) {
        for (x = 0; x < vg.width; x++) q[x] = p[x];
      } else {
        for (x = 0; x < vg.width; x++) {
          unsigned v = p[x];
          q[x] = (uint16_t)(((v << 1) & 0xFFC0u) | ((v >> 4) & 0x0020u) | (v & 0x001Fu));
        }
      }
    }
  }
  info->ptr = vg.shadow;
  info->stride = (unsigned long)vg.width * 2ul;
  info->write_mode = write_mode;
  info->origin = origin;
  return 1;
}

/* grLfbUnlock: copies the pixels of a write lock to the card. */
static int SGL_GLIDE_CALL gr_lfb_unlock(int type, int buffer) {
  int x, y, stride;
  uint32_t base;
  if (!vg.open || !vg.shadow || type != GR_LFB_WRITE_ONLY) return 1;
  base = lock_buffer(buffer, &stride);
  for (y = 0; y < vg.height; y++) {
    int row = vg.lock_origin == GR_ORIGIN_LOWER_LEFT ? vg.height - 1 - y : y;
    volatile uint16_t *p = (volatile uint16_t *)(vg.lfb + base + (uint32_t)row * (uint32_t)stride);
    const uint16_t *q = vg.shadow + (long)y * vg.width;
    if (buffer == GR_BUFFER_AUXBUFFER) {
      for (x = 0; x < vg.width; x++) p[x] = q[x];
    } else {
      for (x = 0; x < vg.width; x++) {
        unsigned v = q[x];
        p[x] = (uint16_t)(((v >> 1) & 0x7FE0u) | (v & 0x001Fu));
      }
    }
  }
  return 1;
}

/* Fills api with the ViRGE driver (see softgl_glide.h). */
int sgl_glide_virge(sgl_glide_api *api) {
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
  api->limits = SGL_GLIDE_LIMIT_ALPHA_TEST | SGL_GLIDE_LIMIT_BLEND | SGL_GLIDE_LIMIT_COLOR_MASK |
                SGL_GLIDE_LIMIT_FOG_ALPHA | SGL_GLIDE_LIMIT_SQUARE | SGL_GLIDE_LIMIT_ONE_ALPHA;
  return 1;
}

#endif
