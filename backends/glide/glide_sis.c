/* glide_sis.c - the Glide 2 functions of the Glide back end, on the 3D
 * engine of a SiS 530 or SiS 620 chipset (the SiS 6306 graphics in the
 * north bridge), for 32-bit DOS. sgl_glide_sis fills an sgl_glide_api,
 * and the Glide back end (glide.c) uses it as it uses a Voodoo.
 *
 * The 3D engine draws a triangle from its three vertices. The driver
 * writes the 3D state registers, then the position, Z, the color, the
 * fog factor, and U, V and 1/W of each vertex as IEEE floats. A write of
 * TFIRE starts the triangle. The engine calculates the edges, the
 * gradients and the perspective division.
 *
 * The driver sets the limits of the table (SGL_GLIDE_LIMIT_), and
 * softgl then draws in software what the card cannot do:
 *
 * - No color mask.
 * - The texture combine is replace or modulate: no GL_DECAL of an RGBA
 *   texture.
 * - One texture unit.
 *
 * The screen is a VBE mode of 16-bit color (5:6:5) with its linear
 * frame buffer. Video memory (the shared memory of the chipset) holds
 * three color buffers (two when the memory is small), the Z buffer of
 * 15 bits in each 16-bit word, and then the textures with their
 * mipmaps.
 *
 * The register facts come from the SiS 6306 model of the 86Box
 * emulator, where this driver is tested. That model follows the SiS 530
 * data sheet and the Direct3D driver of SiS.
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

/* The registers of the 2D engine (byte offsets in the MMIO window). */
enum {
  R_DST_Y_X = 0x820C,    /* y in bits 0 to 15, x in bits 16 to 31 */
  R_DST_ADDR = 0x8210,
  R_DST_PITCH = 0x8214,  /* the pitch in bytes; the height of the buffer from bit 16 */
  R_RECT = 0x8218,       /* the width in bits 0 to 15, the height in bits 16 to 31 */
  R_PAT_FG = 0x821C,
  R_CMD = 0x823C,        /* a write starts the command */
  R_2D_STATUS = 0x8240   /* bits 29 to 31: idle; bits 0 to 12: free queue entries */
};

/* The registers of the 3D engine. Vertex n (0 to 2) has 8 registers
 * from R_VERTEX + 32 n. */
enum {
  R_VERTEX = 0x8800,
  R_PRIMITIVE = 0x89F8,
  R_FIRE = 0x89FC, /* a write starts the triangle; a read gives the status */
  R_ENABLE = 0x8A00,
  R_Z_CNTL = 0x8A04,
  R_Z_BASE = 0x8A08,
  R_ALPHA = 0x8A0C,
  R_DST_CNTL = 0x8A14,
  R_DST_BASE = 0x8A18,
  R_FOG = 0x8A20,
  R_BLEND = 0x8A28,
  R_CLIP_TB = 0x8A30,
  R_CLIP_LR = 0x8A34,
  R_TEX1 = 0x8A38,
  R_TEX2 = 0x8A3C,
  R_TEX3 = 0x8A40,
  R_TEX_BASE = 0x8A44,  /* level n at R_TEX_BASE + 4 n */
  R_TEX_PITCH = 0x8A6C, /* levels 2 n (bits 16 to 26) and 2 n + 1 (bits 0 to 10) at R_TEX_PITCH + 4 n */
  R_TEX_SIZE = 0x8A80
};
enum { V_FOG = 0x00, V_Z = 0x04, V_X = 0x08, V_Y = 0x0C, V_ARGB = 0x10, V_U = 0x14, V_V = 0x18, V_W = 0x1C };

/* The 2D fill: the pattern foreground color with the ROP P, left to
 * right and top to bottom. */
#define CMD_FILL ((2u << 4) | (0xF0u << 8) | (1u << 16) | (1u << 17))
#define STATUS_2D_IDLE 0xE0000000u

/* R_PRIMITIVE: a triangle with Gouraud shading; a write of R_FIRE
 * starts it. */
#define PRIMITIVE_TRIANGLE (2u | (4u << 18))

/* The bits of R_FIRE (status). */
#define STATUS_3D_IDLE 3u

/* The bits of R_ENABLE. */
#define EN_BLEND (1u << 2)
#define EN_FOG (1u << 3)
#define EN_PERSPECTIVE (1u << 9)
#define EN_TEXTURE (1u << 10)
#define EN_DRAW (1u << 11)
#define EN_ALPHA_TEST (1u << 17)
#define EN_Z_TEST (1u << 20)
#define EN_Z_WRITE (1u << 21)

/* The pixel types. */
#define DST_565 0x11u
#define DST_ROP_COPY 0x0Cu
#define Z_FORMAT_16 1u
#define TEX_565 0x51u
#define TEX_4444 0x53u

/* The bits of R_TEX1. */
#define TEX_MIN_LINEAR 1u
#define TEX_MIN_MIP_NEAREST 2u
#define TEX_MIN_LINEAR_MIP_NEAREST 4u
#define TEX_MAG_LINEAR (1u << 3)
#define TEX_LEVELS_SHIFT 8
#define TEX_WRAP_U (1u << 16)
#define TEX_WRAP_V (2u << 16)
#define TEX_CLAMP_U (0x10u << 16)
#define TEX_CLAMP_V (0x20u << 16)
#define TEX_FORMAT_SHIFT 24

/* The bits of R_TEX2: the color combine (bits 26 to 31) and the alpha
 * combine (bits 24 and 25). */
#define COMBINE_COLOR_REPLACE (0u << 26)
#define COMBINE_COLOR_MODULATE (2u << 26)
#define COMBINE_ALPHA_TEXTURE (0u << 24)
#define COMBINE_ALPHA_VERTEX (1u << 24)
#define COMBINE_ALPHA_PRODUCT (2u << 24)

/* The engine draws a pixel whose center is on an edge for the two
 * triangles of the edge (a blend then does it two times). The driver
 * moves all the vertices by a small offset, thus the edges of a
 * usual model (vertices on whole or half pixels) do not go through the
 * centers of the pixels. */
#define EDGE_X (1.0f / 256.0f)
#define EDGE_Y (1.0f / 512.0f)

/* The color combines of glide.c, as the driver knows them. */
enum { COMBINE_COLOR, COMBINE_REPLACE, COMBINE_MODULATE, COMBINE_DECAL };

/* The alpha of a textured triangle. */
enum { ALPHA_VERTEX, ALPHA_TEXTURE, ALPHA_PRODUCT };

/* The 3D state registers that the driver keeps, in the order of
 * last[]. */
enum { S_ENABLE, S_Z_CNTL, S_ALPHA, S_FOG, S_BLEND, S_CLIP_TB, S_CLIP_LR, S_DST, S_TEX1, S_TEX2, S_COUNT };
static const uint16_t state_reg[S_COUNT] = {R_ENABLE, R_Z_CNTL, R_ALPHA,   R_FOG,  R_BLEND,
                                            R_CLIP_TB, R_CLIP_LR, R_DST_BASE, R_TEX1, R_TEX2};

/* The state of the driver. */
static struct {
  sgl_pci_device pci;
  volatile uint8_t *lfb;  /* video memory */
  volatile uint8_t *regs; /* the MMIO window (64 KB) */
  uint32_t lfb_phys, memory;
  int open, width, height, stride, origin;
  uint32_t buffer[3]; /* the offsets of the color buffers */
  int buffers;        /* 3, or 2 when video memory is small */
  int back;           /* the color buffer that is drawn */
  int front;          /* the color buffer that is shown */
  uint32_t zbuffer, tex_min;
  /* The state that Glide sets. */
  int depth, depth_func, depth_mask, fog, color_combine, alpha_mode;
  int blend_src, blend_dst, alpha_func, alpha_ref;
  uint8_t fog_table[GR_FOG_TABLE_SIZE];
  float fog_w[GR_FOG_TABLE_SIZE];
  uint32_t fog_color;
  int clip[4]; /* minx, miny, maxx, maxy (Glide coordinates) */
  /* The texture that TexSource selected. */
  int textured, tex_wlog, tex_hlog, tex_large, tex_small, tex_mipmap, tex_min_linear, tex_mag_linear;
  uint32_t tex_format, tex_clamp;
  /* The values that the driver wrote last to the 3D state registers
   * (0xFFFFFFFF: unknown). */
  uint32_t last[S_COUNT];
  uint16_t *shadow; /* the copy of a locked buffer */
  int lock_origin;
} ss;

/* The PCI ID of the graphics of the SiS 530 and 620 chipsets. */
static const sgl_pci_id sis_ids[] = {{0x1039, 0x6306}, {0, 0}};

/* Writes a 32-bit register. */
static void wr(uint32_t reg, uint32_t value) { *(volatile uint32_t *)(ss.regs + reg) = value; }

/* Reads a 32-bit register. */
static uint32_t rd(uint32_t reg) { return *(volatile uint32_t *)(ss.regs + reg); }

/* Waits until the triangle queue of the 3D engine has a free entry.
 * Stops after about one second. */
static void slot(void) {
  uint32_t start = sgl_dos_ms();
  while (!(rd(R_FIRE) >> 16) && sgl_dos_ms() - start < 1000) {
  }
}

/* Waits until the 2D and the 3D engines are idle. Stops after about one
 * second. */
static void idle(void) {
  uint32_t start = sgl_dos_ms();
  if (!ss.regs) return;
  while ((rd(R_FIRE) & STATUS_3D_IDLE) != STATUS_3D_IDLE && sgl_dos_ms() - start < 1000) {
  }
  while ((rd(R_2D_STATUS) & STATUS_2D_IDLE) != STATUS_2D_IDLE && sgl_dos_ms() - start < 1000) {
  }
}

/* Gives the screen row of a Glide y. */
static float screen_y(float y) { return ss.origin == GR_ORIGIN_LOWER_LEFT ? (float)ss.height - y : y; }

/* grGlideInit: finds the chipset graphics and maps the MMIO window
 * (BAR 1, 64 KB). */
static void SGL_GLIDE_CALL gr_glide_init(void) {
  uint32_t regs_phys;
  memset(&ss, 0, sizeof ss);
  if (!sgl_pci_find(sis_ids, 0, &ss.pci)) return;
  ss.lfb_phys = sgl_pci_bar(&ss.pci, 0);
  regs_phys = sgl_pci_bar(&ss.pci, 1);
  if (!ss.lfb_phys || !regs_phys) return;
  sgl_pci_write16(&ss.pci, 0x04, (uint16_t)(sgl_pci_read16(&ss.pci, 0x04) | 0x0003));
  ss.regs = (volatile uint8_t *)sgl_map_physical(regs_phys, 0x10000ul);
}

/* grGlideShutdown: closes the screen and frees the mappings. */
static void SGL_GLIDE_CALL gr_glide_shutdown(void);

/* grSstQueryHardware: one board with one TMU, as a Voodoo Graphics
 * (GrHwConfiguration as 32-bit words). */
static int SGL_GLIDE_CALL gr_sst_query_hardware(void *config) {
  int32_t *hw = (int32_t *)config;
  if (!ss.regs) return 0;
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

/* Forgets the values of the 3D state registers. */
static void forget(void) {
  int i;
  for (i = 0; i < S_COUNT; i++) ss.last[i] = 0xFFFFFFFFu;
}

/* Writes the 3D state register i when its value changes. */
static void state(int i, uint32_t value) {
  if (ss.last[i] == value) return;
  ss.last[i] = value;
  wr(state_reg[i], value);
}

/* grClipWindow: the clip rectangle (the maxima are not in it). */
static void SGL_GLIDE_CALL gr_clip_window(unsigned long minx, unsigned long miny, unsigned long maxx,
                                          unsigned long maxy) {
  ss.clip[0] = (int)minx, ss.clip[1] = (int)miny, ss.clip[2] = (int)maxx, ss.clip[3] = (int)maxy;
}

/* grSstWinOpen: sets the VBE mode, maps the frame buffer, divides
 * video memory and sets the 3D engine. 16-bit color only; the other
 * arguments are not used. */
static int SGL_GLIDE_CALL gr_sst_win_open(unsigned long hwnd, int resolution, int refresh, int color_format,
                                          int origin, int color_buffers, int aux_buffers) {
  sgl_vbe_mode info;
  uint32_t frame, need;
  int mode, w, h, i;
  (void)hwnd, (void)refresh, (void)color_format, (void)color_buffers, (void)aux_buffers;
  if (!ss.regs || ss.open) return 0;
  mode = vbe_mode(resolution, &w, &h);
  if (mode < 0 || !sgl_vbe_mode_info(mode, &info) || !(info.attributes & 1) || info.bits != 16) return 0;
  frame = ((uint32_t)info.stride * (uint32_t)h + 63u) & ~63u;
  ss.memory = sgl_vbe_memory();
  if (!ss.memory) ss.memory = 0x200000ul;
  need = 3 * frame + 0x20000ul;
  if (need > ss.memory) return 0;
  if (!(info.attributes & 0x80) || !sgl_vbe_set_mode(mode, 1)) return 0;
  if (info.lfb) ss.lfb_phys = info.lfb;
  ss.lfb = (volatile uint8_t *)sgl_map_physical(ss.lfb_phys, ss.memory);
  if (!ss.lfb) {
    sgl_vbe_text_mode();
    return 0;
  }
  ss.width = w, ss.height = h, ss.stride = info.stride, ss.origin = origin;
  /* Three color buffers when the textures keep 1 MB: the card draws
   * into the third while the monitor can still show the old front.
   * The display start must be on a row, thus a frame is a number of
   * rows when there are three buffers. */
  ss.buffers = need + frame + 0xC0000ul <= ss.memory ? 3 : 2;
  for (i = 0; i < ss.buffers; i++) ss.buffer[i] = (uint32_t)i * (uint32_t)info.stride * (uint32_t)h;
  ss.zbuffer = (ss.buffer[ss.buffers - 1] + (uint32_t)info.stride * (uint32_t)h + 63u) & ~63u;
  ss.tex_min = ss.zbuffer + frame;
  ss.front = 0;
  ss.back = 1;
  ss.clip[0] = 0, ss.clip[1] = 0, ss.clip[2] = w, ss.clip[3] = h;
  ss.depth_func = GR_CMP_LESS;
  ss.depth_mask = 1;
  ss.blend_src = GR_BLEND_ONE, ss.blend_dst = GR_BLEND_ZERO;
  ss.alpha_func = GR_CMP_ALWAYS;
  ss.open = 1;
  forget();
  idle();
  wr(R_PRIMITIVE, PRIMITIVE_TRIANGLE);
  wr(R_Z_BASE, ss.zbuffer);
  wr(R_DST_CNTL, (uint32_t)ss.stride | (DST_565 << 16) | (DST_ROP_COPY << 24));
  wr(R_TEX3, 0);
  sgl_vbe_display_start(0, 0);
  return 1;
}

/* grSstWinClose: sets the text mode again and frees the frame buffer. */
static void SGL_GLIDE_CALL gr_sst_win_close(void) {
  if (!ss.open) return;
  idle();
  sgl_vbe_text_mode();
  sgl_unmap_physical(ss.lfb);
  ss.lfb = NULL;
  free(ss.shadow);
  ss.shadow = NULL;
  ss.open = 0;
}

/* grGlideShutdown: closes the screen and frees the mappings. */
static void SGL_GLIDE_CALL gr_glide_shutdown(void) {
  gr_sst_win_close();
  if (ss.regs) sgl_unmap_physical(ss.regs);
  memset(&ss, 0, sizeof ss);
}

/* grSstIdle: waits until the card has drawn all. */
static void SGL_GLIDE_CALL gr_sst_idle(void) { idle(); }

/* Fills the clip window of the buffer at the byte offset off with the
 * 16-bit value value, with the 2D engine, and waits for the end of the
 * fill. */
static void fill(uint32_t off, uint16_t value) {
  int top = ss.origin == GR_ORIGIN_LOWER_LEFT ? ss.height - ss.clip[3] : ss.clip[1];
  int w = ss.clip[2] - ss.clip[0], h = ss.clip[3] - ss.clip[1];
  if (w <= 0 || h <= 0) return;
  idle();
  wr(R_DST_ADDR, off);
  wr(R_DST_PITCH, (uint32_t)ss.stride | (0x1FFFu << 16));
  wr(R_DST_Y_X, ((uint32_t)ss.clip[0] << 16) | (uint32_t)top);
  wr(R_RECT, ((uint32_t)h << 16) | (uint32_t)w);
  wr(R_PAT_FG, value);
  wr(R_CMD, CMD_FILL);
  /* The 3D engine does not wait for the 2D engine. */
  idle();
}

/* Converts a Glide color (0xAARRGGBB) to a 5:6:5 pixel. */
static uint16_t pixel565(unsigned long c) {
  return (uint16_t)(((c >> 8) & 0xF800u) | ((c >> 5) & 0x07E0u) | ((c >> 3) & 0x001Fu));
}

/* grBufferClear: fills the clip window of the back buffer with color,
 * and of the Z buffer with depth when the depth writes are on. The Z
 * buffer has 15 bits. */
static void SGL_GLIDE_CALL gr_buffer_clear(unsigned long color, unsigned alpha, unsigned depth) {
  (void)alpha;
  if (!ss.open) return;
  fill(ss.buffer[ss.back], pixel565(color));
  if (ss.depth && ss.depth_mask) fill(ss.zbuffer, (uint16_t)((depth & 0xFFFFu) >> 1));
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
  int old = ss.front;
  if (!ss.open) return;
  idle();
  sgl_vbe_display_start((int)(ss.buffer[ss.back] / (uint32_t)ss.stride), interval > 0);
  ss.front = ss.back;
  if (ss.buffers == 3) {
    ss.back = 3 - ss.front - old;
  } else {
    if (interval > 0) next_frame();
    ss.back = old;
  }
}

/* Gives the fog visibility of a vertex (255 is no fog) from the Glide
 * fog table, by the eye depth w = 1 / oow. */
static float fog_visibility(float oow) {
  float w = oow > 0.0f ? 1.0f / oow : 1e30f, f;
  int i;
  if (w <= ss.fog_w[0]) return 255.0f - ss.fog_table[0];
  for (i = 1; i < GR_FOG_TABLE_SIZE; i++)
    if (w < ss.fog_w[i]) break;
  if (i == GR_FOG_TABLE_SIZE) return 255.0f - ss.fog_table[GR_FOG_TABLE_SIZE - 1];
  f = (w - ss.fog_w[i - 1]) / (ss.fog_w[i] - ss.fog_w[i - 1]);
  return 255.0f - ((float)ss.fog_table[i - 1] + f * ((float)ss.fog_table[i] - (float)ss.fog_table[i - 1]));
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

/* The Glide blend factors as the blend codes of the engine (the
 * Direct3D blend values less one), for the source and for the
 * destination. The codes are 0 zero, 1 one, 2 source color, 3 one
 * minus source color, 4 source alpha, 5 one minus source alpha, 6
 * destination alpha, 7 one minus destination alpha, 8 destination
 * color, 9 one minus destination color and 10 the saturated source
 * alpha. Glide factor 2 is the destination color for the source, and
 * the source color for the destination. */
static const uint8_t blend_src_code[8] = {0, 4, 8, 6, 1, 5, 9, 7};
static const uint8_t blend_dst_code[8] = {0, 4, 2, 6, 1, 5, 3, 7};

/* Gives the 13-bit clip value of v (a sign and 12 bits). */
static uint32_t clip13(int v) { return v < 0 ? 0x1000u | (uint32_t)(-v & 0xFFF) : (uint32_t)(v & 0xFFF); }

/* Writes the 3D state of a triangle: the target, the clip window, the
 * Z buffer, the texture, the fog, the blend and the alpha test. */
static void triangle_state(int textured) {
  int blend = !(ss.blend_src == GR_BLEND_ONE && ss.blend_dst == GR_BLEND_ZERO);
  int top = ss.origin == GR_ORIGIN_LOWER_LEFT ? ss.height - ss.clip[3] : ss.clip[1];
  int bottom = top + (ss.clip[3] - ss.clip[1]) - 1;
  uint32_t enable = EN_DRAW;
  uint32_t src = ss.blend_src == 0xF ? 10u : blend_src_code[ss.blend_src & 7]; /* 0xF: alpha saturate */
  if (ss.depth) enable |= EN_Z_TEST | (ss.depth_mask ? EN_Z_WRITE : 0u);
  if (blend) enable |= EN_BLEND;
  if (ss.fog) enable |= EN_FOG;
  if (ss.alpha_func != GR_CMP_ALWAYS) enable |= EN_ALPHA_TEST;
  if (textured) {
    uint32_t tex1 = ss.tex_clamp | (ss.tex_format << TEX_FORMAT_SHIFT) | (ss.tex_mag_linear ? TEX_MAG_LINEAR : 0u);
    uint32_t tex2;
    int alpha = ss.alpha_mode;
    enable |= EN_TEXTURE | EN_PERSPECTIVE;
    if (ss.tex_mipmap && ss.tex_small < ss.tex_large) {
      tex1 |= ss.tex_min_linear ? TEX_MIN_LINEAR_MIP_NEAREST : TEX_MIN_MIP_NEAREST;
      tex1 |= (uint32_t)(ss.tex_large - ss.tex_small) << TEX_LEVELS_SHIFT;
    } else if (ss.tex_min_linear) {
      tex1 |= TEX_MIN_LINEAR;
    }
    if (ss.color_combine == COMBINE_MODULATE) {
      tex2 = COMBINE_COLOR_MODULATE;
    } else {
      /* GL_DECAL of an RGB texture is the texture. */
      tex2 = COMBINE_COLOR_REPLACE;
      if (ss.color_combine == COMBINE_DECAL) alpha = ALPHA_VERTEX;
    }
    tex2 |= alpha == ALPHA_TEXTURE ? COMBINE_ALPHA_TEXTURE : alpha == ALPHA_PRODUCT ? COMBINE_ALPHA_PRODUCT
                                                                                  : COMBINE_ALPHA_VERTEX;
    state(S_TEX1, tex1);
    state(S_TEX2, tex2);
  }
  if (bottom < top || ss.clip[2] <= ss.clip[0]) top = 1, bottom = 0;
  state(S_ENABLE, enable);
  state(S_Z_CNTL, (uint32_t)ss.stride | ((uint32_t)(ss.depth_func & 7) << 16) | (Z_FORMAT_16 << 20));
  state(S_ALPHA, ((uint32_t)(ss.alpha_func & 7) << 24) | ((uint32_t)(ss.alpha_ref & 0xFF) << 16));
  state(S_FOG, ss.fog_color | (1u << 24));
  state(S_BLEND, (src << 24) | ((uint32_t)blend_dst_code[ss.blend_dst & 7] << 28));
  state(S_CLIP_TB, (clip13(top) << 13) | clip13(bottom));
  state(S_CLIP_LR, (clip13(ss.clip[0]) << 13) | clip13(ss.clip[2] - 1));
  state(S_DST, ss.buffer[ss.back]);
}

/* grDrawTriangle: writes the state and the three vertices, then TFIRE,
 * which starts the triangle. U and V go from 0 to 1 across the
 * texture; the engine multiplies them by 1/W. */
static void SGL_GLIDE_CALL gr_draw_triangle(const sgl_gr_vertex *a, const sgl_gr_vertex *b,
                                            const sgl_gr_vertex *c) {
  const sgl_gr_vertex *g[3];
  int i, textured;
  float us = 0.0f, vs = 0.0f;
  if (!ss.open || (ss.depth && ss.depth_func == GR_CMP_NEVER) || ss.alpha_func == GR_CMP_NEVER) return;
  g[0] = a, g[1] = b, g[2] = c;
  for (i = 0; i < 3; i++) {
    float x = g[i]->x, y = g[i]->y;
    if (!(x > -4096.0f && x < 4096.0f && y > -4096.0f && y < 4096.0f)) return;
  }
  textured = ss.textured && ss.color_combine != COMBINE_COLOR;
  if (textured) {
    /* In Glide, 256 is the long side. */
    us = (float)(1 << ss.tex_large) / ((float)(1 << ss.tex_wlog) * 256.0f);
    vs = (float)(1 << ss.tex_large) / ((float)(1 << ss.tex_hlog) * 256.0f);
  }
  slot();
  triangle_state(textured);
  for (i = 0; i < 3; i++) {
    const sgl_gr_vertex *v = g[i];
    uint32_t r = R_VERTEX + (uint32_t)i * 32u;
    uint32_t vis = ss.fog ? byte(fog_visibility(v->oow)) : 255u;
    wr(r + V_FOG, vis << 24);
    wr(r + V_Z, float_bits(clampf(v->ooz, 0.0f, 65535.0f) / 65535.0f));
    wr(r + V_X, float_bits(v->x + EDGE_X));
    wr(r + V_Y, float_bits(screen_y(v->y) + EDGE_Y));
    wr(r + V_ARGB, (byte(v->a) << 24) | (byte(v->r) << 16) | (byte(v->g) << 8) | byte(v->b));
    if (textured) {
      float w = v->tmu[0].oow;
      float inv = w != 0.0f ? 1.0f / w : 0.0f;
      wr(r + V_U, float_bits(v->tmu[0].sow * inv * us));
      wr(r + V_V, float_bits(v->tmu[0].tow * inv * vs));
      wr(r + V_W, float_bits(w));
    }
  }
  wr(R_FIRE, 0);
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
static void SGL_GLIDE_CALL gr_depth_buffer_mode(int mode) { ss.depth = mode != GR_DEPTHBUFFER_DISABLE; }

/* grDepthBufferFunction: the Z compare. The Glide compare codes are
 * the codes of the engine. */
static void SGL_GLIDE_CALL gr_depth_buffer_function(int func) { ss.depth_func = func; }

/* grDepthMask: the Z writes. */
static void SGL_GLIDE_CALL gr_depth_mask(int on) { ss.depth_mask = on != 0; }

/* grColorMask: the engine always writes the color (see the limits). */
static void SGL_GLIDE_CALL gr_color_mask(int rgb, int alpha) { (void)rgb, (void)alpha; }

/* grColorCombine: the combines of glide.c: the vertex color, the
 * texture (GL_REPLACE), the texture times the vertex color
 * (GL_MODULATE), or GL_DECAL (of an RGB texture only, see the
 * limits). */
static void SGL_GLIDE_CALL gr_color_combine(int func, int factor, int local, int other, int invert) {
  (void)local, (void)invert;
  if (other != GR_COMBINE_OTHER_TEXTURE || func == GR_COMBINE_FUNCTION_LOCAL)
    ss.color_combine = COMBINE_COLOR;
  else if (func == GR_COMBINE_FUNCTION_BLEND)
    ss.color_combine = COMBINE_DECAL;
  else if (func == GR_COMBINE_FUNCTION_SCALE_OTHER && factor == GR_COMBINE_FACTOR_LOCAL)
    ss.color_combine = COMBINE_MODULATE;
  else
    ss.color_combine = COMBINE_REPLACE;
}

/* grAlphaCombine: the alpha is the vertex alpha (LOCAL), the texture
 * alpha (SCALE_OTHER by ONE), or the texture alpha times the vertex
 * alpha (SCALE_OTHER by LOCAL). */
static void SGL_GLIDE_CALL gr_alpha_combine(int func, int factor, int local, int other, int invert) {
  (void)local, (void)other, (void)invert;
  if (func != GR_COMBINE_FUNCTION_SCALE_OTHER)
    ss.alpha_mode = ALPHA_VERTEX;
  else if (factor == GR_COMBINE_FACTOR_LOCAL)
    ss.alpha_mode = ALPHA_PRODUCT;
  else
    ss.alpha_mode = ALPHA_TEXTURE;
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
  ss.blend_src = rgb_src, ss.blend_dst = rgb_dst;
}

/* grAlphaTestFunction: the alpha test. The Glide compare codes are the
 * codes of the engine. */
static void SGL_GLIDE_CALL gr_alpha_test_function(int func) { ss.alpha_func = func; }

/* grAlphaTestReferenceValue: the alpha that the test compares with. */
static void SGL_GLIDE_CALL gr_alpha_test_reference_value(unsigned value) { ss.alpha_ref = (int)value; }

/* grFogMode: the fog table on or off. */
static void SGL_GLIDE_CALL gr_fog_mode(int mode) { ss.fog = mode != GR_FOG_DISABLE; }

/* grFogColorValue: the fog color (0xRRGGBB). */
static void SGL_GLIDE_CALL gr_fog_color_value(unsigned long color) { ss.fog_color = (uint32_t)color & 0xFFFFFFu; }

/* grFogTable: keeps the table, and the eye depth of each entry: entry
 * i is at w = 2^(3 + i / 4) / (8 - i % 4), as on a Voodoo. */
static void SGL_GLIDE_CALL gr_fog_table(const unsigned char *table) {
  int i;
  for (i = 0; i < GR_FOG_TABLE_SIZE; i++) {
    ss.fog_table[i] = table[i];
    ss.fog_w[i] = (float)(1ul << (3 + (i >> 2))) / (float)(8 - (i & 3));
  }
}

/* grCullMode: glide.c culls in software. */
static void SGL_GLIDE_CALL gr_cull_mode(int mode) { (void)mode; }

/* grDitherMode: the engine does not dither. */
static void SGL_GLIDE_CALL gr_dither_mode(int mode) { (void)mode; }

/* grTexMinAddress: the textures start after the Z buffer. */
static unsigned long SGL_GLIDE_CALL gr_tex_min_address(int tmu) {
  (void)tmu;
  return ss.tex_min;
}

/* grTexMaxAddress: the textures end at the end of video memory. */
static unsigned long SGL_GLIDE_CALL gr_tex_max_address(int tmu) {
  (void)tmu;
  return ss.memory;
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

/* Gives the log2 of the width (*w) and of the height (*h) of the level
 * of a texture whose long side is 2^n texels. wlog and hlog are the
 * sizes of the largest level, whose long side is 2^large texels. */
static void level_size(int n, int large, int wlog, int hlog, int *w, int *h) {
  *w = wlog - (large - n), *h = hlog - (large - n);
  if (*w < 0) *w = 0;
  if (*h < 0) *h = 0;
}

/* Gives the row pitch of a level in bytes: 2 bytes for each texel, at
 * least 4 bytes. */
static unsigned long level_pitch(int w) { return w ? 2ul << w : 4ul; }

/* Gives the bytes of the level of a texture whose long side is 2^n
 * texels, rounded up to 16 bytes. */
static unsigned long level_bytes(int n, int large, int wlog, int hlog) {
  int w, h;
  level_size(n, large, wlog, hlog, &w, &h);
  return ((level_pitch(w) << h) + 15ul) & ~15ul;
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
  if (!ss.open || start + gr_tex_texture_mem_required(0, info) > ss.memory) return;
  tex_size(info, &wlog, &hlog);
  idle();
  for (n = large; n >= small; n--) {
    int w, h, x, y;
    unsigned long pitch;
    level_size(n, large, wlog, hlog, &w, &h);
    pitch = level_pitch(w);
    for (y = 0; y < 1 << h; y++) {
      volatile uint16_t *dst = (volatile uint16_t *)(ss.lfb + off + (unsigned long)y * pitch);
      for (x = 0; x < 1 << w; x++) dst[x] = *src++;
    }
    off += level_bytes(n, large, wlog, hlog);
  }
}

/* grTexSource: the texture that the triangles use. Level 0 is the
 * largest level. The pitch of a level is log2 (pitch / 4) from bit 7. */
static void SGL_GLIDE_CALL gr_tex_source(int tmu, unsigned long start, unsigned long even_odd, sgl_gr_texinfo *info) {
  int n, level;
  uint32_t off = (uint32_t)start, pitch[10];
  (void)tmu, (void)even_odd;
  ss.textured = 1;
  ss.tex_large = 8 - info->large_lod;
  ss.tex_small = 8 - info->small_lod;
  tex_size(info, &ss.tex_wlog, &ss.tex_hlog);
  ss.tex_format = info->format == GR_TEXFMT_ARGB_4444 ? TEX_4444 : TEX_565;
  if (!ss.open) return;
  memset(pitch, 0, sizeof pitch);
  idle();
  for (n = ss.tex_large, level = 0; n >= ss.tex_small && level < 10; n--, level++) {
    int w, h;
    level_size(n, ss.tex_large, ss.tex_wlog, ss.tex_hlog, &w, &h);
    wr(R_TEX_BASE + (uint32_t)level * 4u, off);
    pitch[level] = (uint32_t)(w ? w - 1 : 0) << 7;
    off += (uint32_t)level_bytes(n, ss.tex_large, ss.tex_wlog, ss.tex_hlog);
  }
  for (level = 0; level < 10; level += 2) wr(R_TEX_PITCH + (uint32_t)level * 2u, (pitch[level] << 16) | pitch[level + 1]);
  wr(R_TEX_SIZE, ((uint32_t)ss.tex_wlog << 28) | ((uint32_t)ss.tex_hlog << 24));
}

/* grTexFilterMode: the filters for the minification and for the
 * magnification. */
static void SGL_GLIDE_CALL gr_tex_filter_mode(int tmu, int min, int mag) {
  (void)tmu;
  ss.tex_min_linear = min == GR_TEXTUREFILTER_BILINEAR;
  ss.tex_mag_linear = mag == GR_TEXTUREFILTER_BILINEAR;
}

/* grTexClampMode: wrap or clamp, for s and for t. */
static void SGL_GLIDE_CALL gr_tex_clamp_mode(int tmu, int s, int t) {
  (void)tmu;
  ss.tex_clamp = (s == GR_TEXTURECLAMP_CLAMP ? TEX_CLAMP_U : TEX_WRAP_U) |
                 (t == GR_TEXTURECLAMP_CLAMP ? TEX_CLAMP_V : TEX_WRAP_V);
}

/* grTexMipMapMode: the nearest mipmap, or the largest level only. */
static void SGL_GLIDE_CALL gr_tex_mip_map_mode(int tmu, int mode, int lod_blend) {
  (void)tmu, (void)lod_blend;
  ss.tex_mipmap = mode != GR_MIPMAP_DISABLE;
}

/* Gives the offset of the buffer of a lock: the front buffer, the back
 * buffer or the Z buffer. The rows have the same pitch. */
static uint32_t lock_buffer(int buffer) {
  if (buffer == GR_BUFFER_AUXBUFFER) return ss.zbuffer;
  return ss.buffer[buffer == GR_BUFFER_BACKBUFFER ? ss.back : ss.front];
}

/* grLfbLock: gives a copy of the buffer in system memory, in RGB 565
 * (or the Z values of 16 bits), rows in the order of origin. A write
 * lock copies the pixels to the card in grLfbUnlock. */
static int SGL_GLIDE_CALL gr_lfb_lock(int type, int buffer, int write_mode, int origin, int pixel_pipeline,
                                      sgl_gr_lfbinfo *info) {
  int x, y;
  uint32_t base;
  (void)pixel_pipeline;
  if (!ss.open) return 0;
  if (!ss.shadow) ss.shadow = (uint16_t *)malloc((size_t)ss.width * (size_t)ss.height * 2u);
  if (!ss.shadow) return 0;
  idle();
  ss.lock_origin = origin;
  base = lock_buffer(buffer);
  if (type == GR_LFB_READ_ONLY) {
    for (y = 0; y < ss.height; y++) {
      int row = origin == GR_ORIGIN_LOWER_LEFT ? ss.height - 1 - y : y;
      const volatile uint16_t *p = (const volatile uint16_t *)(ss.lfb + base + (unsigned long)row * ss.stride);
      uint16_t *q = ss.shadow + (long)y * ss.width;
      if (buffer == GR_BUFFER_AUXBUFFER) {
        /* 15 bits to 16 bits. */
        for (x = 0; x < ss.width; x++) q[x] = (uint16_t)((p[x] << 1) | ((p[x] >> 14) & 1u));
      } else {
        for (x = 0; x < ss.width; x++) q[x] = p[x];
      }
    }
  }
  info->ptr = ss.shadow;
  info->stride = (unsigned long)ss.width * 2ul;
  info->write_mode = write_mode;
  info->origin = origin;
  return 1;
}

/* grLfbUnlock: copies the pixels of a write lock to the card. */
static int SGL_GLIDE_CALL gr_lfb_unlock(int type, int buffer) {
  int x, y;
  uint32_t base;
  if (!ss.open || !ss.shadow || type != GR_LFB_WRITE_ONLY) return 1;
  base = lock_buffer(buffer);
  for (y = 0; y < ss.height; y++) {
    int row = ss.lock_origin == GR_ORIGIN_LOWER_LEFT ? ss.height - 1 - y : y;
    volatile uint16_t *p = (volatile uint16_t *)(ss.lfb + base + (unsigned long)row * ss.stride);
    const uint16_t *q = ss.shadow + (long)y * ss.width;
    if (buffer == GR_BUFFER_AUXBUFFER) {
      for (x = 0; x < ss.width; x++) p[x] = (uint16_t)(q[x] >> 1);
    } else {
      for (x = 0; x < ss.width; x++) p[x] = q[x];
    }
  }
  return 1;
}

/* Fills api with the SiS driver (see softgl_glide.h). */
int sgl_glide_sis(sgl_glide_api *api) {
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
  api->limits = SGL_GLIDE_LIMIT_COLOR_MASK | SGL_GLIDE_LIMIT_DECAL;
  return 1;
}

#endif
