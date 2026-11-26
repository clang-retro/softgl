/* glide_mga.c - the Glide 2 functions of the Glide back end, on the
 * drawing engine of a Matrox card, for 32-bit DOS. sgl_glide_mga fills
 * an sgl_glide_api, and the Glide back end (glide.c) uses it as it uses
 * a Voodoo. The cards are the Mystique (MGA-1064SG), Mystique 220
 * (MGA-1164SG), Millennium II (MGA-2164W) and Productiva G100. The
 * driver also accepts the G200, G400, G450 and G550, whose drawing
 * engine has the registers of the G100. It is not tested on these cards.
 *
 * The engine draws trapezoids: a span of pixels on each row, between a
 * left edge and a right edge that move with a Bresenham error term. The
 * driver divides each triangle into two trapezoids. The engine
 * interpolates Z, the color and the texture coordinates (S, T and Q)
 * along x and along y.
 *
 * The Matrox engine draws less than a Voodoo. The driver sets the
 * limits of the table (SGL_GLIDE_LIMIT_), and softgl then draws in
 * software what the card cannot do:
 *
 * - No blend and no alpha test.
 * - No fog on the Mystique and the Millennium II. On the G100, the fog
 *   of a textured triangle is per pixel; the fog of a triangle without
 *   a texture is calculated at the vertices.
 * - The alpha is the texture alpha or the vertex alpha, not a product.
 * - No mipmaps: the card uses the largest level of a texture.
 * - The Mystique and the Millennium II sample the nearest texel; the
 *   G100 also has the bilinear filter.
 *
 * The screen is a VBE mode of 16-bit color (5:6:5) with its linear
 * frame buffer. Video memory holds three color buffers (two when the
 * memory is small), the 16-bit Z buffer, and then the textures.
 *
 * The register facts come from the Matrox documentation and from the
 * Matrox cards of the 86Box emulator, where this driver is tested.
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

/* The registers (byte offsets in the control aperture). A write to a
 * drawing register at offset + 0x100 also starts the drawing. */
enum {
  R_DWGCTL = 0x1C00,
  R_MACCESS = 0x1C04,
  R_ZORG = 0x1C0C,
  R_PLNWT = 0x1C1C,
  R_FCOL = 0x1C24,
  R_SGN = 0x1C58,
  R_AR0 = 0x1C60,
  R_AR1 = 0x1C64,
  R_AR2 = 0x1C68,
  R_AR4 = 0x1C70,
  R_AR5 = 0x1C74,
  R_AR6 = 0x1C78,
  R_CXBNDRY = 0x1C80,
  R_FXBNDRY = 0x1C84,
  R_YDSTLEN = 0x1C88,
  R_PITCH = 0x1C8C,
  R_YDSTORG = 0x1C94,
  R_YTOP = 0x1C98,
  R_YBOT = 0x1C9C,
  R_DR0 = 0x1CC0,
  R_FOGSTART = 0x1CC4,
  R_DR2 = 0x1CC8,
  R_DR3 = 0x1CCC,
  R_DR4 = 0x1CD0,
  R_FOGXINC = 0x1CD4,
  R_DR6 = 0x1CD8,
  R_DR7 = 0x1CDC,
  R_DR8 = 0x1CE0,
  R_FOGYINC = 0x1CE4,
  R_DR10 = 0x1CE8,
  R_DR11 = 0x1CEC,
  R_DR12 = 0x1CF0,
  R_FOGCOL = 0x1CF4,
  R_DR14 = 0x1CF8,
  R_DR15 = 0x1CFC,
  R_FIFOSTATUS = 0x1E10,
  R_STATUS = 0x1E14,
  R_TMR0 = 0x2C00,
  R_TEXORG = 0x2C24,
  R_TEXWIDTH = 0x2C28,
  R_TEXHEIGHT = 0x2C2C,
  R_TEXCTL = 0x2C30,
  R_TEXTRANS = 0x2C34,
  R_TEXFILTER = 0x2C58,
  R_ALPHACTRL = 0x2C7C,
  R_GO = 0x100
};

/* The bits of DWGCTL. */
#define DWG_TRAP 0x4u
#define DWG_TEXTURE_TRAP 0x6u
#define DWG_RPL (0u << 4)    /* write the color */
#define DWG_ZI (3u << 4)     /* Gouraud shading, Z compare and Z write */
#define DWG_I (7u << 4)      /* Gouraud shading, Z compare, no Z write */
#define DWG_SOLID (1u << 11)
#define DWG_ARZERO (1u << 12)
#define DWG_SGNZERO (1u << 13)
#define DWG_SHFTZERO (1u << 14)
#define DWG_BOP_COPY (0xCu << 16)

/* The bits of MACCESS. */
#define MACCESS_16BPP 1u
#define MACCESS_FOGEN (1u << 26)
#define MACCESS_NODITHER (1u << 30)

/* The bits of TEXCTL. */
#define TEX_TW16 3u          /* RGB 5:6:5 */
#define TEX_TW12 4u          /* ARGB 4:4:4:4 */
#define TEX_TAKEY (1u << 25) /* with TAMASK 0: no texel is transparent */
#define TEX_CLAMPV (1u << 27)
#define TEX_CLAMPU (1u << 28)
#define TEX_MODULATE (1u << 29)

/* The bits of SGN: the left and the right edge go to smaller x. */
#define SGN_SDXL (1u << 1)
#define SGN_SDXR (1u << 5)

/* The color combines of glide.c, as the driver knows them. */
enum { COMBINE_COLOR, COMBINE_REPLACE, COMBINE_MODULATE };

/* The state of the driver. */
static struct {
  sgl_pci_device pci;
  volatile uint8_t *lfb;   /* video memory */
  volatile uint8_t *ctrl;  /* the registers */
  uint32_t lfb_phys, memory;
  int g100;                /* 1 on a G100: fog */
  int open, width, height, pitch, origin;
  uint32_t buffer[3];      /* the offsets of the color buffers */
  int buffers;             /* 3, or 2 when video memory is small */
  int back;                /* the color buffer that is drawn */
  int front;               /* the color buffer that is shown */
  uint32_t zbuffer, tex_min;
  uint32_t org;            /* YDSTORG now (pixels) */
  uint32_t maccess;        /* MACCESS now */
  /* The state that Glide sets. */
  int depth, depth_func, depth_mask, fog, dither, color_combine;
  uint32_t plnwt;
  uint8_t fog_table[GR_FOG_TABLE_SIZE];
  float fog_w[GR_FOG_TABLE_SIZE];
  float fog_color[3];
  int clip[4];             /* minx, miny, maxx, maxy (Glide coordinates) */
  /* The texture that TexSource selected. */
  int textured, tex_wlog, tex_hlog, tex_large, tex_linear;
  uint32_t tex_base, tex_format, tex_clamp;
  uint16_t *shadow;        /* the copy of a locked buffer */
  int lock_origin;
} mg;

/* The PCI IDs of the Matrox cards. */
static const sgl_pci_id mga_ids[] = {
    {0x102B, 0x051A}, {0x102B, 0x051B}, {0x102B, 0x051F}, {0x102B, 0x1000}, {0x102B, 0x1001},
    {0x102B, 0x0520}, {0x102B, 0x0521}, {0x102B, 0x0525}, {0x102B, 0x2527}, {0, 0}};

/* The PCI IDs of the cards with the G100 engine: the G100, G200 (PCI
 * and AGP), G400 and G450 (0525h) and G550. These cards have fog and
 * the bilinear filter. */
static const sgl_pci_id g100_ids[] = {{0x102B, 0x1000}, {0x102B, 0x1001}, {0x102B, 0x0520},
                                      {0x102B, 0x0521}, {0x102B, 0x0525}, {0x102B, 0x2527},
                                      {0, 0}};

/* Gives 1 when the PCI device ID is in g100_ids. */
static int g100_class(uint16_t device) {
  const sgl_pci_id *id;
  for (id = g100_ids; id->vendor; id++)
    if (id->device == device) return 1;
  return 0;
}

/* Writes a 32-bit register. */
static void wr(uint32_t reg, uint32_t value) { *(volatile uint32_t *)(mg.ctrl + reg) = value; }

/* Writes a 32-bit register with a signed value. */
static void wri(uint32_t reg, int32_t value) { wr(reg, (uint32_t)value); }

/* Reads a 32-bit register. */
static uint32_t rd(uint32_t reg) { return *(volatile uint32_t *)(mg.ctrl + reg); }

/* Waits until the FIFO of the engine has n free entries (the low byte
 * of FIFOSTATUS). Stops after about one second. */
static void slots(unsigned n) {
  uint32_t start = sgl_dos_ms();
  while ((rd(R_FIFOSTATUS) & 0xFFu) < n)
    if (sgl_dos_ms() - start > 1000) break;
}

/* Waits until the engine is idle (STATUS bit 16, DWGENGSTS). */
static void idle(void) {
  uint32_t start = sgl_dos_ms();
  if (!mg.ctrl) return;
  while (rd(R_STATUS) & 0x10000u)
    if (sgl_dos_ms() - start > 1000) break;
}

/* Gives the screen row of a Glide y. */
static float screen_y(float y) { return mg.origin == GR_ORIGIN_LOWER_LEFT ? (float)mg.height - y : y; }

/* grGlideInit: finds the card and maps its registers. The Mystique
 * (revision 0 to 2 of 051Ah) has the registers at BAR 0 and video
 * memory at BAR 1; the cards after it have the two the other way. */
static void SGL_GLIDE_CALL gr_glide_init(void) {
  int ctrl_bar;
  memset(&mg, 0, sizeof mg);
  if (!sgl_pci_find(mga_ids, 0, &mg.pci)) return;
  mg.g100 = g100_class(mg.pci.device);
  ctrl_bar = (mg.pci.device == 0x051A && mg.pci.revision < 3) ? 0 : 1;
  mg.lfb_phys = sgl_pci_bar(&mg.pci, 1 - ctrl_bar);
  if (!mg.lfb_phys || !sgl_pci_bar(&mg.pci, ctrl_bar)) return;
  sgl_pci_write16(&mg.pci, 0x04, (uint16_t)(sgl_pci_read16(&mg.pci, 0x04) | 0x0002));
  mg.ctrl = (volatile uint8_t *)sgl_map_physical(sgl_pci_bar(&mg.pci, ctrl_bar), 0x4000ul);
}

/* grGlideShutdown: closes the screen and frees the mappings. */
static void SGL_GLIDE_CALL gr_glide_shutdown(void);

/* grSstQueryHardware: one board with one TMU, as a Voodoo Graphics
 * (GrHwConfiguration as 32-bit words). */
static int SGL_GLIDE_CALL gr_sst_query_hardware(void *config) {
  int32_t *hw = (int32_t *)config;
  if (!mg.ctrl) return 0;
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

/* Sets the drawing target: the buffer that starts at pixel org (the
 * Z buffer is at the same place in its own memory), and the clip
 * window. */
static void target(uint32_t org) {
  int top = mg.origin == GR_ORIGIN_LOWER_LEFT ? mg.height - mg.clip[3] : mg.clip[1];
  int bottom = top + (mg.clip[3] - mg.clip[1]) - 1;
  int left = mg.clip[0], right = mg.clip[2] - 1;
  if (right < left || bottom < top) left = 1, right = 0, top = 1, bottom = 0;
  mg.org = org;
  slots(5);
  wr(R_YDSTORG, org);
  wr(R_ZORG, mg.zbuffer - org * 2u);
  wr(R_CXBNDRY, ((uint32_t)right << 16) | (uint32_t)left);
  wr(R_YTOP, org + (uint32_t)top * (uint32_t)mg.pitch);
  wr(R_YBOT, org + (uint32_t)bottom * (uint32_t)mg.pitch);
}

/* Writes MACCESS when it changes. */
static void set_maccess(uint32_t value) {
  if (value == mg.maccess) return;
  mg.maccess = value;
  slots(1);
  wr(R_MACCESS, value);
}

/* grClipWindow: the clip rectangle (the maxima are not in it). */
static void SGL_GLIDE_CALL gr_clip_window(unsigned long minx, unsigned long miny, unsigned long maxx,
                                          unsigned long maxy) {
  mg.clip[0] = (int)minx, mg.clip[1] = (int)miny, mg.clip[2] = (int)maxx, mg.clip[3] = (int)maxy;
  if (mg.open) target(mg.buffer[mg.back] / 2u);
}

/* grSstWinOpen: sets the VBE mode, maps the frame buffer and divides
 * video memory. 16-bit color only; the other arguments are not used. */
static int SGL_GLIDE_CALL gr_sst_win_open(unsigned long hwnd, int resolution, int refresh, int color_format,
                                          int origin, int color_buffers, int aux_buffers) {
  sgl_vbe_mode info;
  uint32_t frame, need;
  int mode, w, h, i;
  (void)hwnd, (void)refresh, (void)color_format, (void)color_buffers, (void)aux_buffers;
  if (!mg.ctrl || mg.open) return 0;
  mode = vbe_mode(resolution, &w, &h);
  if (mode < 0 || !sgl_vbe_mode_info(mode, &info) || !(info.attributes & 1) || info.bits != 16) return 0;
  /* The engine needs a pitch of a multiple of 32 pixels. */
  if (info.stride % 64u) return 0;
  mg.memory = sgl_vbe_memory();
  if (!mg.memory) mg.memory = 0x200000ul;
  frame = (uint32_t)info.stride * (uint32_t)h;
  need = 3 * frame + 0x20000ul;
  if (need > mg.memory) return 0;
  if (!(info.attributes & 0x80) || !sgl_vbe_set_mode(mode, 1)) return 0;
  if (info.lfb) mg.lfb_phys = info.lfb;
  mg.lfb = (volatile uint8_t *)sgl_map_physical(mg.lfb_phys, mg.memory);
  if (!mg.lfb) {
    sgl_vbe_text_mode();
    return 0;
  }
  mg.width = w, mg.height = h, mg.pitch = info.stride / 2, mg.origin = origin;
  /* Three color buffers when the textures keep 1 MB: the card draws
   * into the third while the monitor can still show the old front. */
  mg.buffers = need + frame + 0xC0000ul <= mg.memory ? 3 : 2;
  for (i = 0; i < mg.buffers; i++) mg.buffer[i] = (uint32_t)i * frame;
  mg.zbuffer = mg.buffer[mg.buffers - 1] + frame;
  mg.tex_min = (mg.zbuffer + frame + 63u) & ~63u;
  mg.front = 0;
  mg.back = 1;
  mg.clip[0] = 0, mg.clip[1] = 0, mg.clip[2] = w, mg.clip[3] = h;
  mg.depth_func = GR_CMP_LESS;
  mg.depth_mask = 1;
  mg.dither = 1;
  mg.plnwt = 0xFFFFFFFFu;
  mg.open = 1;
  mg.maccess = 0xFFFFFFFFu;
  set_maccess(MACCESS_16BPP);
  slots(4);
  wr(R_PITCH, (uint32_t)mg.pitch);
  wr(R_PLNWT, mg.plnwt);
  /* G100: the alpha of a pixel is the vertex alpha, the blend is (1, 0). */
  if (mg.g100) wr(R_ALPHACTRL, 0x01000001u);
  target(mg.buffer[mg.back] / 2u);
  sgl_vbe_display_start(0, 0);
  return 1;
}

/* grSstWinClose: sets the text mode again and frees the frame buffer. */
static void SGL_GLIDE_CALL gr_sst_win_close(void) {
  if (!mg.open) return;
  idle();
  sgl_vbe_text_mode();
  sgl_unmap_physical(mg.lfb);
  mg.lfb = NULL;
  free(mg.shadow);
  mg.shadow = NULL;
  mg.open = 0;
}

/* grGlideShutdown: closes the screen and frees the mappings. */
static void SGL_GLIDE_CALL gr_glide_shutdown(void) {
  gr_sst_win_close();
  if (mg.ctrl) sgl_unmap_physical(mg.ctrl);
  memset(&mg, 0, sizeof mg);
}

/* grSstIdle: waits until the card has drawn all. */
static void SGL_GLIDE_CALL gr_sst_idle(void) { idle(); }

/* Fills the clip window of the buffer at pixel org with the 16-bit
 * value value: a trapezoid with vertical edges and one color. */
static void fill(uint32_t org, uint16_t value) {
  int top = mg.origin == GR_ORIGIN_LOWER_LEFT ? mg.height - mg.clip[3] : mg.clip[1];
  int w = mg.clip[2] - mg.clip[0], h = mg.clip[3] - mg.clip[1];
  if (w <= 0 || h <= 0) return;
  if (org != mg.org) target(org);
  slots(4);
  wr(R_DWGCTL, DWG_TRAP | DWG_RPL | DWG_SOLID | DWG_ARZERO | DWG_SGNZERO | DWG_SHFTZERO | DWG_BOP_COPY);
  wr(R_FCOL, (uint32_t)value | ((uint32_t)value << 16));
  wr(R_FXBNDRY, ((uint32_t)mg.clip[2] << 16) | (uint32_t)mg.clip[0]);
  wr(R_YDSTLEN + R_GO, ((uint32_t)top << 16) | (uint32_t)h);
}

/* Converts a Glide color (0xAARRGGBB) to a 5:6:5 pixel. */
static uint16_t pixel565(unsigned long c) {
  return (uint16_t)(((c >> 8) & 0xF800u) | ((c >> 5) & 0x07E0u) | ((c >> 3) & 0x001Fu));
}

/* grBufferClear: fills the clip window of the back buffer with color,
 * and of the Z buffer with depth when the depth writes are on. */
static void SGL_GLIDE_CALL gr_buffer_clear(unsigned long color, unsigned alpha, unsigned depth) {
  (void)alpha;
  if (!mg.open) return;
  set_maccess(MACCESS_16BPP | MACCESS_NODITHER);
  fill(mg.buffer[mg.back] / 2u, pixel565(color));
  if (mg.depth && mg.depth_mask) {
    slots(1);
    wr(R_PLNWT, 0xFFFFFFFFu);
    fill(mg.zbuffer / 2u, (uint16_t)depth);
    slots(1);
    wr(R_PLNWT, mg.plnwt);
  }
  target(mg.buffer[mg.back] / 2u);
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
  int old = mg.front;
  if (!mg.open) return;
  idle();
  sgl_vbe_display_start((int)(mg.buffer[mg.back] / ((uint32_t)mg.pitch * 2u)), interval > 0);
  mg.front = mg.back;
  if (mg.buffers == 3) {
    mg.back = 3 - mg.front - old;
  } else {
    if (interval > 0) next_frame();
    mg.back = old;
  }
  target(mg.buffer[mg.back] / 2u);
}

/* Gives the fog visibility of a vertex (255 is no fog) from the Glide
 * fog table, by the eye depth w = 1 / oow. */
static float fog_visibility(float oow) {
  float w = oow > 0.0f ? 1.0f / oow : 1e30f, f;
  int i;
  if (w <= mg.fog_w[0]) return 255.0f - mg.fog_table[0];
  for (i = 1; i < GR_FOG_TABLE_SIZE; i++)
    if (w < mg.fog_w[i]) break;
  if (i == GR_FOG_TABLE_SIZE) return 255.0f - mg.fog_table[GR_FOG_TABLE_SIZE - 1];
  f = (w - mg.fog_w[i - 1]) / (mg.fog_w[i] - mg.fog_w[i - 1]);
  return 255.0f - ((float)mg.fog_table[i - 1] + f * ((float)mg.fog_table[i] - (float)mg.fog_table[i - 1]));
}

/* The values that the engine interpolates. */
enum { A_R, A_G, A_B, A_Z, A_S, A_T, A_Q, A_FOG, A_COUNT };

/* A vertex on the screen: x, y and the values. */
typedef struct {
  float x, y, v[A_COUNT];
} sv;

/* Gives v limited to lo to hi. */
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

/* Gives v as an int32_t, limited to the range of int32_t. */
static int32_t fix(float v) {
  if (v >= 2147483520.0f) return 2147483647;
  if (v <= -2147483520.0f) return -2147483647;
  return (int32_t)lrintf(v);
}

/* How a triangle is drawn. */
typedef struct {
  int textured;   /* 1: TEXTURE_TRAP; 0: TRAP with Gouraud shading */
  int modulate;   /* the texture times the color */
  int pixel_fog;  /* the engine does the fog (G100, textured) */
  int vertex_fog; /* the fog is in the vertex colors */
  int persp;      /* the texture has perspective (the W are not equal) */
  float qmax;     /* the largest oow of the triangle */
} how;

/* Converts a Glide vertex to a screen vertex. The colors are 0 to 255,
 * Z is 0 to 65535. S and T are the texture coordinates in units of the
 * texture (2^20 is one width or height), times Q when the texture has
 * perspective. Q is oow / qmax times 2^22. */
static void to_screen(const sgl_gr_vertex *g, const how *hw, sv *s) {
  float c[3], vis = mg.fog ? fog_visibility(g->oow) : 255.0f;
  float ss = (float)(1 << mg.tex_large) / (float)(1 << mg.tex_wlog) * (1048576.0f / 256.0f);
  float ts = (float)(1 << mg.tex_large) / (float)(1 << mg.tex_hlog) * (1048576.0f / 256.0f);
  int i;
  c[0] = g->r, c[1] = g->g, c[2] = g->b;
  for (i = 0; i < 3; i++) {
    c[i] = clampf(c[i], 0.0f, 255.0f);
    if (hw->vertex_fog) c[i] = (c[i] * vis + mg.fog_color[i] * (255.0f - vis)) / 255.0f;
  }
  s->x = g->x;
  s->y = screen_y(g->y);
  s->v[A_R] = c[0];
  s->v[A_G] = c[1];
  s->v[A_B] = c[2];
  s->v[A_Z] = clampf(g->ooz, 0.0f, 65535.0f);
  s->v[A_FOG] = vis;
  if (hw->persp) {
    float q = g->tmu[0].oow / hw->qmax;
    s->v[A_S] = g->tmu[0].sow / hw->qmax * ss * 64.0f;
    s->v[A_T] = g->tmu[0].tow / hw->qmax * ts * 64.0f;
    s->v[A_Q] = q * 4194304.0f;
  } else {
    float w = g->tmu[0].oow != 0.0f ? 1.0f / g->tmu[0].oow : 1.0f;
    s->v[A_S] = g->tmu[0].sow * w * ss;
    s->v[A_T] = g->tmu[0].tow * w * ts;
    s->v[A_Q] = 0.0f;
  }
}

/* An edge of a trapezoid for the Bresenham registers of the engine. */
typedef struct {
  int x;          /* the first pixel (left edge) or the pixel after the last (right edge) on the first row */
  int32_t err;    /* AR1 or AR4 */
  int32_t major;  /* AR0 or AR6: added to the error at each step of x */
  int32_t minor;  /* AR2 or AR5: added to the error on each row */
  int neg;        /* x goes to smaller values */
} edge;

/* Calculates the edge from (xa, ya) to (xb, yb), with ya < yb, in units
 * of 1 / 2^b pixel, for the rows from row. On each row the edge is at
 * X, the x of the edge at the row center, and the pixel x is
 * ceil(X - 0.5). */
static void edge_setup(int32_t xa, int32_t ya, int32_t xb, int32_t yb, int b, int row, edge *e) {
  int64_t d = yb - ya, k = xb - xa, one = (int64_t)1 << b, half = one >> 1;
  /* n = (X - 0.5) * 2^b * d on the first row; it changes by k * 2^b on
   * each row. */
  int64_t n = (int64_t)xa * d + ((int64_t)row * one + half - ya) * k - half * d;
  int64_t step = one * d, x;
  x = (int64_t)ceil((double)n / (double)step);
  e->x = (int)x;
  e->major = (int32_t)step;
  if (k >= 0) {
    e->neg = 0;
    e->minor = (int32_t)(-k * one);
    e->err = (int32_t)(x * step - n - k * one);
  } else {
    e->neg = 1;
    e->minor = (int32_t)(k * one);
    e->err = (int32_t)(n - (x - 1) * step - 1 + k * one);
  }
}

/* The Glide depth functions as the Z modes of DWGCTL (bits 8 to 10).
 * GR_CMP_NEVER (0) is not drawn. */
static const uint8_t zmode[8] = {0, 4, 2, 5, 6, 3, 7, 0};

/* Writes the start value, the x gradient and the y gradient of one
 * value in the 8.15 format, at the three registers. */
static void wr3(uint32_t r0, uint32_t rx, uint32_t ry, float base, float gx, float gy) {
  wri(r0, fix(base * 32768.0f));
  wri(rx, fix(gx * 32768.0f));
  wri(ry, fix(gy * 32768.0f));
}

/* Draws the rows r0 to r1 - 1 of a triangle between the edges l and r
 * (in units of 1 / 2^b pixel). gx and gy are the gradients of the
 * values, p0 a vertex. */
static void trapezoid(uint32_t dwgctl, const how *hw, const int32_t *l, const int32_t *r, int b, int r0, int r1,
                      const sv *p0, const float *gx, const float *gy) {
  edge el, er;
  float base[A_COUNT], xc, yc;
  int i;
  if (r1 <= r0) return;
  edge_setup(l[0], l[1], l[2], l[3], b, r0, &el);
  edge_setup(r[0], r[1], r[2], r[3], b, r0, &er);
  /* The values at the center of the first pixel of the first row. */
  xc = (float)el.x + 0.5f;
  yc = (float)r0 + 0.5f;
  for (i = 0; i < A_COUNT; i++) base[i] = p0->v[i] + gx[i] * (xc - p0->x) + gy[i] * (yc - p0->y);
  for (i = A_R; i <= A_B; i++) base[i] = clampf(base[i], 0.0f, 255.0f);
  base[A_FOG] = clampf(base[A_FOG], 0.0f, 255.0f);
  if (base[A_Z] < 0.0f) base[A_Z] = 0.0f;
  slots(13);
  wr(R_DWGCTL, dwgctl);
  wr3(R_DR0, R_DR2, R_DR3, base[A_Z], gx[A_Z], gy[A_Z]);
  wr3(R_DR4, R_DR6, R_DR7, base[A_R], gx[A_R], gy[A_R]);
  wr3(R_DR8, R_DR10, R_DR11, base[A_G], gx[A_G], gy[A_G]);
  wr3(R_DR12, R_DR14, R_DR15, base[A_B], gx[A_B], gy[A_B]);
  if (hw->textured) {
    slots(12);
    wri(R_TMR0 + 0, fix(gx[A_S]));
    wri(R_TMR0 + 4, fix(gy[A_S]));
    wri(R_TMR0 + 8, fix(gx[A_T]));
    wri(R_TMR0 + 12, fix(gy[A_T]));
    wri(R_TMR0 + 16, fix(gx[A_Q]));
    wri(R_TMR0 + 20, fix(gy[A_Q]));
    wri(R_TMR0 + 24, fix(base[A_S]));
    wri(R_TMR0 + 28, fix(base[A_T]));
    wri(R_TMR0 + 32, fix(base[A_Q]));
    if (hw->pixel_fog) {
      wr(R_FOGSTART, (uint32_t)fix(base[A_FOG] * 32768.0f) & 0xFFFFFFu);
      wr(R_FOGXINC, (uint32_t)fix(gx[A_FOG] * 32768.0f) & 0xFFFFFFu);
      wr(R_FOGYINC, (uint32_t)fix(gy[A_FOG] * 32768.0f) & 0xFFFFFFu);
    }
  }
  slots(9);
  wri(R_AR0, el.major);
  wri(R_AR1, el.err);
  wri(R_AR2, el.minor);
  wri(R_AR4, er.err);
  wri(R_AR5, er.minor);
  wri(R_AR6, er.major);
  wr(R_SGN, (el.neg ? SGN_SDXL : 0u) | (er.neg ? SGN_SDXR : 0u));
  wr(R_FXBNDRY, ((uint32_t)(er.x & 0xFFFF) << 16) | (uint32_t)(el.x & 0xFFFF));
  wr(R_YDSTLEN + R_GO, ((uint32_t)(r0 & 0xFFFF) << 16) | (uint32_t)(r1 - r0));
}

/* Writes the texture registers for a triangle. */
static void set_texture(const how *hw) {
  int tpitch = mg.tex_wlog < 3 ? 0 : mg.tex_wlog - 3;
  uint32_t ctl = mg.tex_format | ((uint32_t)tpitch << 16) | TEX_TAKEY | mg.tex_clamp;
  if (hw->modulate) ctl |= TEX_MODULATE;
  if (!hw->persp) ctl |= 1u << 21; /* NPCEN: no perspective */
  slots(6);
  wr(R_TEXORG, mg.tex_base);
  wr(R_TEXWIDTH, (uint32_t)mg.tex_wlog | ((uint32_t)((1 << mg.tex_wlog) - 1) << 18));
  wr(R_TEXHEIGHT, (uint32_t)mg.tex_hlog | ((uint32_t)((1 << mg.tex_hlog) - 1) << 18));
  wr(R_TEXCTL, ctl);
  wr(R_TEXTRANS, 1u); /* the color key 1 with the mask 0: no texel is transparent */
  /* The bilinear filter of the G100 (the Mystique samples the nearest texel). */
  if (mg.g100) wr(R_TEXFILTER, mg.tex_linear ? 0x22u : 0x00u);
}

/* grDrawTriangle: sorts the vertices by y, calculates the gradients of
 * the values, and gives the triangle to the engine as two trapezoids:
 * the rows above the middle vertex and the rows from it. A row is drawn
 * when its center is in the triangle; a pixel when its center is right
 * of the left edge, and not right of the right edge. */
static void SGL_GLIDE_CALL gr_draw_triangle(const sgl_gr_vertex *a, const sgl_gr_vertex *b,
                                            const sgl_gr_vertex *c) {
  const sgl_gr_vertex *g[3];
  how hw;
  sv p[3], tmp;
  float area, gx[A_COUNT], gy[A_COUNT], ext, xmin, xmax, x02;
  int32_t q[3][2], longe[4], e01[4], e12[4];
  int rows[3], sub, left, i;
  uint32_t dwgctl;
  if (!mg.open || (mg.depth && mg.depth_func == GR_CMP_NEVER)) return;
  g[0] = a, g[1] = b, g[2] = c;
  memset(&hw, 0, sizeof hw);
  hw.textured = mg.textured && mg.color_combine != COMBINE_COLOR;
  hw.modulate = hw.textured && mg.color_combine == COMBINE_MODULATE;
  if (hw.modulate) {
    /* The texture times white is the texture. */
    int white = 1;
    for (i = 0; i < 3; i++)
      if (g[i]->r < 254.5f || g[i]->g < 254.5f || g[i]->b < 254.5f) white = 0;
    if (white) hw.modulate = 0;
  }
  hw.pixel_fog = mg.fog && hw.textured;
  hw.vertex_fog = mg.fog && !hw.textured;
  if (hw.textured) {
    hw.qmax = a->tmu[0].oow;
    for (i = 1; i < 3; i++)
      if (g[i]->tmu[0].oow > hw.qmax) hw.qmax = g[i]->tmu[0].oow;
    hw.persp = fabsf(a->tmu[0].oow - b->tmu[0].oow) > 1e-6f * hw.qmax ||
               fabsf(a->tmu[0].oow - c->tmu[0].oow) > 1e-6f * hw.qmax;
    if (hw.qmax <= 0.0f) return;
  }
  for (i = 0; i < 3; i++) to_screen(g[i], &hw, &p[i]);
  /* Sort by y, the smallest (the top of the screen) first. */
  if (p[0].y > p[1].y) tmp = p[0], p[0] = p[1], p[1] = tmp;
  if (p[1].y > p[2].y) tmp = p[1], p[1] = p[2], p[2] = tmp;
  if (p[0].y > p[1].y) tmp = p[0], p[0] = p[1], p[1] = tmp;
  area = (p[1].x - p[0].x) * (p[2].y - p[0].y) - (p[2].x - p[0].x) * (p[1].y - p[0].y);
  if (area == 0.0f) return;
  for (i = 0; i < A_COUNT; i++) {
    float d1 = p[1].v[i] - p[0].v[i], d2 = p[2].v[i] - p[0].v[i];
    gx[i] = (d1 * (p[2].y - p[0].y) - d2 * (p[1].y - p[0].y)) / area;
    gy[i] = (d2 * (p[1].x - p[0].x) - d1 * (p[2].x - p[0].x)) / area;
  }
  /* The vertices on a grid of 1 / 2^sub pixel. The Bresenham terms are
   * up to 2^(2 sub) times the size; they must stay in 18 bits. */
  xmin = xmax = p[0].x;
  for (i = 1; i < 3; i++) {
    if (p[i].x < xmin) xmin = p[i].x;
    if (p[i].x > xmax) xmax = p[i].x;
  }
  ext = (xmax - xmin > p[2].y - p[0].y ? xmax - xmin : p[2].y - p[0].y) + 1.0f;
  if (ext > 4096.0f) return;
  for (sub = 4; sub > 1 && ext * (float)(1 << (2 * sub)) >= 65536.0f; sub--) {
  }
  for (i = 0; i < 3; i++) {
    q[i][0] = (int32_t)lrintf(p[i].x * (float)(1 << sub));
    q[i][1] = (int32_t)lrintf(p[i].y * (float)(1 << sub));
    /* The first row whose center is at or below the vertex. */
    rows[i] = (int)ceilf((float)q[i][1] / (float)(1 << sub) - 0.5f);
  }
  if (rows[0] >= rows[2] || q[0][1] == q[2][1]) return;
  longe[0] = q[0][0], longe[1] = q[0][1], longe[2] = q[2][0], longe[3] = q[2][1];
  e01[0] = q[0][0], e01[1] = q[0][1], e01[2] = q[1][0], e01[3] = q[1][1];
  e12[0] = q[1][0], e12[1] = q[1][1], e12[2] = q[2][0], e12[3] = q[2][1];
  /* The long edge 0-2 is on the left when it is left of vertex 1. */
  x02 = (float)q[0][0] + (float)(q[2][0] - q[0][0]) * (float)(q[1][1] - q[0][1]) / (float)(q[2][1] - q[0][1]);
  if (hw.textured) {
    dwgctl = DWG_TEXTURE_TRAP;
    set_texture(&hw);
  } else {
    dwgctl = DWG_TRAP;
  }
  dwgctl |= DWG_SOLID | DWG_BOP_COPY;
  if (mg.depth) {
    dwgctl |= (uint32_t)zmode[mg.depth_func & 7] << 8;
    dwgctl |= mg.depth_mask ? DWG_ZI : DWG_I;
  } else {
    dwgctl |= DWG_I;
  }
  set_maccess(MACCESS_16BPP | (mg.dither ? 0u : MACCESS_NODITHER) | (hw.pixel_fog ? MACCESS_FOGEN : 0u));
  /* The upper part, then the lower part with the new short edge. Each
   * part has all its registers: the Matrox engine of 86Box does not
   * always continue a trapezoid correctly. */
  left = x02 >= (float)q[1][0];
  if (rows[1] > rows[0] && q[1][1] != q[0][1])
    trapezoid(dwgctl, &hw, left ? e01 : longe, left ? longe : e01, sub, rows[0], rows[1], &p[0], gx, gy);
  if (rows[2] > rows[1] && q[2][1] != q[1][1])
    trapezoid(dwgctl, &hw, left ? e12 : longe, left ? longe : e12, sub, rows[1], rows[2], &p[0], gx, gy);
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
static void SGL_GLIDE_CALL gr_depth_buffer_mode(int mode) { mg.depth = mode != GR_DEPTHBUFFER_DISABLE; }

/* grDepthBufferFunction: the Z compare. */
static void SGL_GLIDE_CALL gr_depth_buffer_function(int func) { mg.depth_func = func; }

/* grDepthMask: the Z writes. */
static void SGL_GLIDE_CALL gr_depth_mask(int on) { mg.depth_mask = on != 0; }

/* grColorMask: the color writes on or off (the plane write mask). */
static void SGL_GLIDE_CALL gr_color_mask(int rgb, int alpha) {
  (void)alpha;
  mg.plnwt = rgb ? 0xFFFFFFFFu : 0u;
  if (!mg.open) return;
  slots(1);
  wr(R_PLNWT, mg.plnwt);
}

/* grColorCombine: the combines of glide.c: the vertex color, the
 * texture (GL_REPLACE and GL_DECAL), or the texture times the vertex
 * color (GL_MODULATE). */
static void SGL_GLIDE_CALL gr_color_combine(int func, int factor, int local, int other, int invert) {
  (void)local, (void)invert;
  if (other != GR_COMBINE_OTHER_TEXTURE || func == GR_COMBINE_FUNCTION_LOCAL)
    mg.color_combine = COMBINE_COLOR;
  else if (func == GR_COMBINE_FUNCTION_SCALE_OTHER && factor == GR_COMBINE_FACTOR_LOCAL)
    mg.color_combine = COMBINE_MODULATE;
  else
    mg.color_combine = COMBINE_REPLACE;
}

/* grAlphaCombine: the alpha is not used (no blend, no alpha test). */
static void SGL_GLIDE_CALL gr_alpha_combine(int func, int factor, int local, int other, int invert) {
  (void)func, (void)factor, (void)local, (void)other, (void)invert;
}

/* grTexCombine: the card has one texture unit. */
static void SGL_GLIDE_CALL gr_tex_combine(int tmu, int rgb_func, int rgb_factor, int alpha_func, int alpha_factor,
                                          int rgb_invert, int alpha_invert) {
  (void)tmu, (void)rgb_func, (void)rgb_factor, (void)alpha_func, (void)alpha_factor, (void)rgb_invert,
      (void)alpha_invert;
}

/* grAlphaBlendFunction: the card does not blend (see the limits). */
static void SGL_GLIDE_CALL gr_alpha_blend_function(int rgb_src, int rgb_dst, int alpha_src, int alpha_dst) {
  (void)rgb_src, (void)rgb_dst, (void)alpha_src, (void)alpha_dst;
}

/* grAlphaTestFunction: the card has no alpha test (see the limits). */
static void SGL_GLIDE_CALL gr_alpha_test_function(int func) { (void)func; }

/* grAlphaTestReferenceValue: the card has no alpha test. */
static void SGL_GLIDE_CALL gr_alpha_test_reference_value(unsigned value) { (void)value; }

/* grFogMode: the fog table on or off. */
static void SGL_GLIDE_CALL gr_fog_mode(int mode) { mg.fog = mode != GR_FOG_DISABLE; }

/* grFogColorValue: the fog color (0xRRGGBB). */
static void SGL_GLIDE_CALL gr_fog_color_value(unsigned long color) {
  mg.fog_color[0] = (float)((color >> 16) & 0xFF);
  mg.fog_color[1] = (float)((color >> 8) & 0xFF);
  mg.fog_color[2] = (float)(color & 0xFF);
  if (!mg.open || !mg.g100) return;
  slots(1);
  wr(R_FOGCOL, (uint32_t)color & 0xFFFFFFu);
}

/* grFogTable: keeps the table, and the eye depth of each entry: entry
 * i is at w = 2^(3 + i / 4) / (8 - i % 4), as on a Voodoo. */
static void SGL_GLIDE_CALL gr_fog_table(const unsigned char *table) {
  int i;
  for (i = 0; i < GR_FOG_TABLE_SIZE; i++) {
    mg.fog_table[i] = table[i];
    mg.fog_w[i] = (float)(1ul << (3 + (i >> 2))) / (float)(8 - (i & 3));
  }
}

/* grCullMode: glide.c culls in software. */
static void SGL_GLIDE_CALL gr_cull_mode(int mode) { (void)mode; }

/* grDitherMode: dither the 16-bit pixels, or not. */
static void SGL_GLIDE_CALL gr_dither_mode(int mode) { mg.dither = mode != GR_DITHER_DISABLE; }

/* grTexMinAddress: the textures start after the Z buffer. */
static unsigned long SGL_GLIDE_CALL gr_tex_min_address(int tmu) {
  (void)tmu;
  return mg.tex_min;
}

/* grTexMaxAddress: the textures end at the end of video memory. */
static unsigned long SGL_GLIDE_CALL gr_tex_max_address(int tmu) {
  (void)tmu;
  return mg.memory;
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

/* Gives the texels in a row of a texture in video memory: the width,
 * at least 8. */
static int tex_pitch(int wlog) { return wlog < 3 ? 8 : 1 << wlog; }

/* grTexTextureMemRequired: the bytes of the largest level, 16 bits for
 * each texel (the card does not use the other levels). */
static unsigned long SGL_GLIDE_CALL gr_tex_texture_mem_required(unsigned long even_odd, sgl_gr_texinfo *info) {
  int wlog, hlog;
  (void)even_odd;
  tex_size(info, &wlog, &hlog);
  return ((unsigned long)tex_pitch(wlog) * (1ul << hlog) * 2ul + 63ul) & ~63ul;
}

/* grTexDownloadMipMap: writes the largest level at start. RGB 565 and
 * ARGB 4444 are the formats of the card. */
static void SGL_GLIDE_CALL gr_tex_download_mip_map(int tmu, unsigned long start, unsigned long even_odd,
                                                   sgl_gr_texinfo *info) {
  const uint16_t *src = (const uint16_t *)info->data;
  int wlog, hlog, x, y, w, pitch;
  (void)tmu, (void)even_odd;
  if (!mg.open || start + gr_tex_texture_mem_required(0, info) > mg.memory) return;
  tex_size(info, &wlog, &hlog);
  w = 1 << wlog;
  pitch = tex_pitch(wlog);
  idle();
  for (y = 0; y < (1 << hlog); y++) {
    volatile uint16_t *dst = (volatile uint16_t *)(mg.lfb + start) + (long)y * pitch;
    for (x = 0; x < w; x++) dst[x] = src[x];
    src += w;
  }
}

/* grTexSource: the texture that the triangles use. */
static void SGL_GLIDE_CALL gr_tex_source(int tmu, unsigned long start, unsigned long even_odd, sgl_gr_texinfo *info) {
  (void)tmu, (void)even_odd;
  mg.textured = 1;
  mg.tex_base = (uint32_t)start;
  mg.tex_large = 8 - info->large_lod;
  tex_size(info, &mg.tex_wlog, &mg.tex_hlog);
  mg.tex_format = info->format == GR_TEXFMT_ARGB_4444 ? TEX_TW12 : TEX_TW16;
}

/* grTexFilterMode: one filter for both: bilinear when one of the two
 * is bilinear. */
static void SGL_GLIDE_CALL gr_tex_filter_mode(int tmu, int min, int mag) {
  (void)tmu;
  mg.tex_linear = min == GR_TEXTUREFILTER_BILINEAR || mag == GR_TEXTUREFILTER_BILINEAR;
}

/* grTexClampMode: wrap or clamp, for s and for t. */
static void SGL_GLIDE_CALL gr_tex_clamp_mode(int tmu, int s, int t) {
  (void)tmu;
  mg.tex_clamp = (s == GR_TEXTURECLAMP_CLAMP ? TEX_CLAMPU : 0u) | (t == GR_TEXTURECLAMP_CLAMP ? TEX_CLAMPV : 0u);
}

/* grTexMipMapMode: the card uses the largest level (see the limits). */
static void SGL_GLIDE_CALL gr_tex_mip_map_mode(int tmu, int mode, int lod_blend) {
  (void)tmu, (void)mode, (void)lod_blend;
}

/* Gives the offset of the buffer of a lock: the front buffer, the back
 * buffer or the Z buffer. The rows have the same pitch. */
static uint32_t lock_buffer(int buffer) {
  if (buffer == GR_BUFFER_AUXBUFFER) return mg.zbuffer;
  return mg.buffer[buffer == GR_BUFFER_BACKBUFFER ? mg.back : mg.front];
}

/* grLfbLock: gives a copy of the buffer in system memory, in RGB 565
 * (or the Z values), rows in the order of origin. A write lock copies
 * the pixels to the card in grLfbUnlock. */
static int SGL_GLIDE_CALL gr_lfb_lock(int type, int buffer, int write_mode, int origin, int pixel_pipeline,
                                      sgl_gr_lfbinfo *info) {
  int x, y;
  uint32_t base;
  (void)pixel_pipeline;
  if (!mg.open) return 0;
  if (!mg.shadow) mg.shadow = (uint16_t *)malloc((size_t)mg.width * (size_t)mg.height * 2u);
  if (!mg.shadow) return 0;
  idle();
  mg.lock_origin = origin;
  base = lock_buffer(buffer);
  if (type == GR_LFB_READ_ONLY) {
    for (y = 0; y < mg.height; y++) {
      int row = origin == GR_ORIGIN_LOWER_LEFT ? mg.height - 1 - y : y;
      const volatile uint16_t *p = (const volatile uint16_t *)(mg.lfb + base) + (long)row * mg.pitch;
      uint16_t *q = mg.shadow + (long)y * mg.width;
      for (x = 0; x < mg.width; x++) q[x] = p[x];
    }
  }
  info->ptr = mg.shadow;
  info->stride = (unsigned long)mg.width * 2ul;
  info->write_mode = write_mode;
  info->origin = origin;
  return 1;
}

/* grLfbUnlock: copies the pixels of a write lock to the card. */
static int SGL_GLIDE_CALL gr_lfb_unlock(int type, int buffer) {
  int x, y;
  uint32_t base;
  if (!mg.open || !mg.shadow || type != GR_LFB_WRITE_ONLY) return 1;
  base = lock_buffer(buffer);
  for (y = 0; y < mg.height; y++) {
    int row = mg.lock_origin == GR_ORIGIN_LOWER_LEFT ? mg.height - 1 - y : y;
    volatile uint16_t *p = (volatile uint16_t *)(mg.lfb + base) + (long)row * mg.pitch;
    const uint16_t *q = mg.shadow + (long)y * mg.width;
    for (x = 0; x < mg.width; x++) p[x] = q[x];
  }
  return 1;
}

/* Fills api with the Matrox driver (see softgl_glide.h). Only the G100
 * and the later cards have fog. */
int sgl_glide_mga(sgl_glide_api *api) {
  sgl_pci_device d;
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
  api->limits = SGL_GLIDE_LIMIT_ALPHA_TEST | SGL_GLIDE_LIMIT_NO_BLEND | SGL_GLIDE_LIMIT_ONE_ALPHA;
  if (!sgl_pci_find(g100_ids, 0, &d)) api->limits |= SGL_GLIDE_LIMIT_NO_FOG;
  return 1;
}

#endif
