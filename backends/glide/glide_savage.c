/* glide_savage.c - the Glide 2 functions of the Glide back end, on the
 * 3D engine of an S3 Savage4, ProSavage, Twister or ProSavage DDR card,
 * for 32-bit DOS. sgl_glide_savage fills an sgl_glide_api, and the
 * Glide back end (glide.c) uses it as it uses a Voodoo.
 *
 * The driver writes commands to the Burst Command Interface (BCI), a
 * FIFO at the offset 0x10000 of the registers. A DrawPrimitive command
 * sends the three vertices of a triangle as IEEE floats (x, y, z, 1/w,
 * the colors, s and t), and the card does the set-up.
 *
 * The 3D engine draws only into tiled buffers: tiles of 2 KB, 64 x 16
 * pixels of 16 bits. The screen is a VBE mode of 16-bit color (5:6:5)
 * with its linear frame buffer. The driver draws into a tiled back
 * buffer and a tiled 16-bit Z buffer after the screen. grBufferSwap
 * copies the back buffer to the screen with a 2D blit: the 2D engine
 * reads the tiled buffer and writes the linear buffer. A lock of the
 * back buffer or of the Z buffer reads and writes the tiled buffer
 * through the tiled aperture of the card, which shows each tiled
 * surface as a linear buffer.
 *
 * The textures are also tiled. grTexDownloadMipMap writes the texels
 * in the tile order of the card.
 *
 * The driver sets no limits (SGL_GLIDE_LIMIT_), but it uses only the
 * largest level of a texture (no mipmaps) and one texture unit.
 *
 * The register facts come from the open source Savage drivers: the
 * Mesa savage DRI driver (savage_3d_reg.h, savagestate.c, savagetex.c,
 * savagetris.c), the Linux savage DRM driver (savage_drv.h,
 * savage_state.c) and the X.Org savage driver (savage_accel.c,
 * savage_exa.c, savage_driver.c). 86Box does not emulate the cards,
 * and the driver is not tested.
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

/* The memory-mapped registers (byte offsets in PCI region 0). */
enum {
  R_PLANE_WRITE_MASK = 0x8128,
  R_PLANE_READ_MASK = 0x812C,
  R_PLANE_8134 = 0x8134,
  R_PLANE_8136 = 0x8136,
  R_GBD_LOW = 0x8168,  /* the global bitmap descriptor */
  R_GBD_HIGH = 0x816C,
  R_ADVANCED_FUNCTION = 0x850C,
  R_BCI = 0x10000,     /* the BCI FIFO */
  R_SHADOW_STATUS = 0x48C0C,
  R_BCI_CONTROL = 0x48C18,
  R_TILED_SURFACE = 0x48C40, /* surface n at R_TILED_SURFACE + 4 n */
  R_STATUS = 0x48C60
};

/* The registers that a SetRegister command writes (indices). */
enum {
  G_DRAW_LOCAL_CTRL = 0x1E,
  G_TEX_PALETTE = 0x1F,
  G_TEX_CTRL0 = 0x20,
  G_TEX_CTRL1 = 0x21,
  G_TEX_ADDR0 = 0x22,
  G_TEX_ADDR1 = 0x23,
  G_TEX_BLEND0 = 0x24,
  G_TEX_BLEND1 = 0x25,
  G_TEX_KEY = 0x26,
  G_TEX_DESCR = 0x27,
  G_FOG_CTRL = 0x30,
  G_STENCIL_CTRL = 0x31,
  G_ZBUF_CTRL = 0x32,
  G_ZBUF_OFFSET = 0x33,
  G_DEST_CTRL = 0x34,
  G_DRAW_CTRL0 = 0x35,
  G_DRAW_CTRL1 = 0x36,
  G_Z_WATERMARKS = 0x37,
  G_DEST_TEX_WATERMARKS = 0x38,
  G_TEX_BLEND_COLOR = 0x39,
  G_FIRST = G_DRAW_LOCAL_CTRL,
  G_LAST = G_TEX_BLEND_COLOR,
  G_BITPLANE_WRITE_MASK = 0xD7,
  G_PBD_LOW = 0xE2, /* the primary bitmap descriptor: the 2D destination */
  G_PBD_HIGH = 0xE3,
  G_SBD_LOW = 0xE4, /* the secondary bitmap descriptor: the 2D source */
  G_SBD_HIGH = 0xE5
};

/* The BCI commands. */
#define CMD_SET_REGISTER 0x96000000u /* | count << 16 | first index */
#define CMD_WAIT 0xC0000000u
#define CMD_WAIT_3D 0x00010000u
#define CMD_WAIT_2D 0x00020000u
#define CMD_DRAW_PRIM 0x80000000u /* | type | skip | count << 16 */
#define CMD_RECT 0x48000000u      /* | rop << 16 */
#define CMD_RECT_XP 0x01000000u
#define CMD_RECT_YP 0x02000000u
#define CMD_SEND_COLOR 0x00008000u
#define CMD_DEST_PBD 0x00000800u
#define CMD_DEST_PBD_NEW 0x00000C00u
#define CMD_SRC_SBD_COLOR 0x00000100u
#define ROP_COPY 0xCCu

/* The skip flags of DrawPrimitive: the parts of a vertex that are not
 * sent. The driver always skips the second texture (u1, v1). */
#define SKIP_ST0 0x30u
#define SKIP_ST1 0xC0u

/* The bitmap descriptor: the stride in pixels, the bits per pixel, the
 * tile format and no block write. */
#define BD_BPP16 (16u << 16)
#define BD_BLOCK_WRITE_OFF (1u << 28)

/* The texture blends of TexBlendCtrl (from the Mesa driver). The low 16
 * bits select the color, the high 16 bits select the alpha. */
#define TBC_COLOR_MODULATE 0x0011u
#define TBC_COLOR_TEXTURE 0x0410u
#define TBC_COLOR_DECAL 0x2A04u
#define TBC_COLOR_VERTEX 0x0405u
#define TBC_ALPHA_VERTEX 0x00850000u
#define TBC_ALPHA_TEXTURE 0x00840000u
#define TBC_ALPHA_MODULATE 0x00110000u
#define TBC_NO_TEXTURE1 0x00870407u

/* The texture formats of TexDescr. */
#define TFT_ARGB4444 5u
#define TFT_RGB565 6u

/* A TexCtrl without a texture (from the Mesa driver). */
#define TEX_CTRL_OFF 0x0020F040u

/* The color combines of glide.c, as the driver knows them. */
enum { COMBINE_COLOR, COMBINE_REPLACE, COMBINE_MODULATE, COMBINE_DECAL };

/* The alpha of a textured triangle. */
enum { ALPHA_VERTEX, ALPHA_TEXTURE, ALPHA_PRODUCT };

/* The tiles of the 3D engine: 64 x 16 pixels of 16 bits. */
#define TILE_W 64
#define TILE_H 16
#define TILE_BYTES 2048u

/* The pitch of a 16-bit surface in the tiled aperture, in bytes. */
#define APERTURE_PITCH 0x1000u

/* The tiled surfaces of the aperture: one for the back buffer and one
 * for the Z buffer. */
enum { SURFACE_BACK = 1, SURFACE_DEPTH = 2 };

/* The state of the driver. */
static struct {
  sgl_pci_device pci;
  volatile uint8_t *lfb;  /* video memory (region 1) */
  volatile uint8_t *regs; /* the registers (region 0, 512 KB) */
  volatile uint8_t *aperture[3]; /* the tiled surfaces (index by SURFACE_) */
  volatile uint32_t *bci; /* the next write to the BCI FIFO */
  uint32_t lfb_phys, memory;
  int pro;         /* 1 on a ProSavage, a Twister or a ProSavage DDR */
  int open, width, height, stride, origin;
  int tiles_w, tiles_h; /* the size of a tiled buffer in tiles */
  uint32_t back, zbuffer, tex_min, tex_max; /* byte offsets in video memory */
  uint32_t front_bd, back_bd;               /* bitmap descriptors */
  int engine_2d;        /* 1 when the last command was a 2D command */
  /* The state that Glide sets. */
  int depth, depth_func, depth_mask, color_mask, fog, dither, color_combine, alpha_mode;
  int blend_src, blend_dst, alpha_func, alpha_ref;
  uint8_t fog_table[GR_FOG_TABLE_SIZE];
  float fog_w[GR_FOG_TABLE_SIZE];
  uint32_t fog_color; /* 0xRRGGBB */
  int clip[4];        /* minx, miny, maxx, maxy (Glide coordinates) */
  /* The texture that TexSource selected. */
  int textured, tex_wlog, tex_hlog, tex_alpha, tex_linear; /* the logs of the card (see card_log) */
  float tex_ss, tex_ts;                                       /* the scales from S and T to 0 to 1 */
  uint32_t tex_addr, tex_wrap;
  /* The values that the driver wrote last to the 3D registers, and 1
   * when the value is known. */
  uint32_t last[G_LAST + 1];
  uint8_t known[G_LAST + 1];
  uint16_t *shadow; /* the copy of a locked buffer */
  int lock_origin;
} sv;

/* The PCI IDs of the Savage4 family. */
static const sgl_pci_id savage_ids[] = {{0x5333, 0x8A22}, {0x5333, 0x8A25}, {0x5333, 0x8A26}, {0x5333, 0x8D01},
                                        {0x5333, 0x8D02}, {0x5333, 0x8D03}, {0x5333, 0x8D04}, {0, 0}};

/* Reads a 32-bit register. */
static uint32_t rd(uint32_t reg) { return *(volatile uint32_t *)(sv.regs + reg); }

/* Writes a 32-bit register. */
static void wr(uint32_t reg, uint32_t value) { *(volatile uint32_t *)(sv.regs + reg) = value; }

/* Writes a 16-bit register. */
static void wr16(uint32_t reg, uint16_t value) { *(volatile uint16_t *)(sv.regs + reg) = value; }

/* Reads the CRT controller register index. */
static uint8_t crtc_rd(int index) {
  outportb(0x3D4, (unsigned char)index);
  return (uint8_t)inportb(0x3D5);
}

/* Writes the CRT controller register index. */
static void crtc_wr(int index, uint8_t value) {
  outportb(0x3D4, (unsigned char)index);
  outportb(0x3D5, value);
}

/* Waits until the BCI FIFO has n free entries: the low 21 bits of the
 * status are the used entries of 0x7F00. Then the next write goes to
 * the start of the FIFO window. Stops after about one second. */
static void begin(unsigned n) {
  uint32_t start = sgl_dos_ms();
  while ((rd(R_STATUS) & 0x1FFFFFu) > 0x7F00u - n && sgl_dos_ms() - start < 1000) {
  }
  sv.bci = (volatile uint32_t *)(sv.regs + R_BCI);
}

/* Writes one word to the BCI FIFO. */
static void put(uint32_t value) { *sv.bci++ = value; }

/* Waits until the card has done all the commands: the FIFO is empty and
 * the engines are idle. Stops after about one second. */
static void idle(void) {
  uint32_t start = sgl_dos_ms();
  if (!sv.regs) return;
  while ((rd(R_STATUS) & 0x00E1FFFFu) != 0x00E00000u && sgl_dos_ms() - start < 1000) {
  }
}

/* Gives the screen row of a Glide y. */
static float screen_y(float y) { return sv.origin == GR_ORIGIN_LOWER_LEFT ? (float)sv.height - y : y; }

/* grGlideInit: finds the card, enables its memory space and maps the
 * registers (region 0, 512 KB). */
static void SGL_GLIDE_CALL gr_glide_init(void) {
  uint32_t regs_phys;
  memset(&sv, 0, sizeof sv);
  if (!sgl_pci_find(savage_ids, 0, &sv.pci)) return;
  regs_phys = sgl_pci_bar(&sv.pci, 0);
  sv.lfb_phys = sgl_pci_bar(&sv.pci, 1);
  if (!regs_phys || !sv.lfb_phys) return;
  sv.pro = sv.pci.device != 0x8A22;
  sgl_pci_write16(&sv.pci, 0x04, (uint16_t)(sgl_pci_read16(&sv.pci, 0x04) | 0x0002));
  sv.regs = (volatile uint8_t *)sgl_map_physical(regs_phys, 0x80000ul);
}

/* grGlideShutdown: closes the screen and frees the mappings. */
static void SGL_GLIDE_CALL gr_glide_shutdown(void);

/* grSstQueryHardware: one board with one TMU, as a Voodoo Graphics
 * (GrHwConfiguration as 32-bit words). */
static int SGL_GLIDE_CALL gr_sst_query_hardware(void *config) {
  int32_t *hw = (int32_t *)config;
  if (!sv.regs) return 0;
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

/* grClipWindow: the clip rectangle (the maxima are not in it). */
static void SGL_GLIDE_CALL gr_clip_window(unsigned long minx, unsigned long miny, unsigned long maxx,
                                          unsigned long maxy) {
  sv.clip[0] = (int)minx, sv.clip[1] = (int)miny, sv.clip[2] = (int)maxx, sv.clip[3] = (int)maxy;
}

/* Makes sure that the 3D engine can take commands: after a 2D command,
 * the BCI waits until the 2D engine is idle. Writes 0 or 1 word; the
 * caller reserved the FIFO space. */
static void to_3d(void) {
  if (!sv.engine_2d) return;
  put(CMD_WAIT | CMD_WAIT_2D);
  sv.engine_2d = 0;
}

/* Writes the 3D register index when its value changes. Before the first
 * change of a group, *waited is 0: the function then waits until the 3D
 * engine is idle, as the Linux driver does before a change of the
 * global state. Each call needs 3 words of FIFO space. */
static void reg3d(int index, uint32_t value, int *waited) {
  if (sv.known[index] && sv.last[index] == value) return;
  if (!*waited) {
    put(CMD_WAIT | CMD_WAIT_3D);
    *waited = 1;
  }
  put(CMD_SET_REGISTER | (1u << 16) | (uint32_t)index);
  put(value);
  sv.last[index] = value;
  sv.known[index] = 1;
}

/* Writes a register for the 2D engine with a SetRegister command (2
 * words of FIFO space). */
static void reg2d(int index, uint32_t value) {
  put(CMD_SET_REGISTER | (1u << 16) | (uint32_t)index);
  put(value);
}

/* Sets the 2D engine and the BCI, as the X.Org driver does for the
 * Savage4 family: the MMIO of the enhanced registers, the plane masks,
 * the BCI without the command overflow buffer and without the shadow
 * status, the global bitmap descriptor (the screen) and no block write.
 * Then the tiled surfaces of the aperture. */
static void setup_engine(void) {
  uint32_t enable = sv.pro ? 0u : 8u; /* the BCI enable bit of the GBD */
  uint32_t tsr = ((uint32_t)sv.tiles_w << 20) | 0x80000000u; /* the width in tiles, 16 bits */
  crtc_wr(0x38, 0x48); /* unlock the S3 registers */
  crtc_wr(0x39, 0xA5);
  crtc_wr(0x40, (uint8_t)(crtc_rd(0x40) | 0x01));
  crtc_wr(0x31, (uint8_t)((crtc_rd(0x31) | 0x0C) & ~0x01));
  wr(R_PLANE_WRITE_MASK, 0xFFFFFFFFu);
  wr(R_PLANE_READ_MASK, 0xFFFFFFFFu);
  wr16(R_PLANE_8134, 0x27);
  wr16(R_PLANE_8136, 0x07);
  wr(R_BCI_CONTROL, rd(R_BCI_CONTROL) & 0x3FF0u);
  wr(R_SHADOW_STATUS, 0);
  wr(R_BCI_CONTROL, rd(R_BCI_CONTROL) | 0x08u);
  wr(R_GBD_HIGH, enable | 1u);
  crtc_wr(0x50, (uint8_t)(crtc_rd(0x50) | 0xC1)); /* use the GBD */
  wr(R_ADVANCED_FUNCTION, rd(R_ADVANCED_FUNCTION) | 0x8000u);
  wr(R_GBD_LOW, 0);
  wr(R_GBD_HIGH, sv.front_bd | enable | 1u);
  crtc_wr(0x88, (uint8_t)(crtc_rd(0x88) | 0x10)); /* no 2D block write */
  wr(R_TILED_SURFACE + 4u * SURFACE_BACK, tsr | (sv.back >> 5));
  wr(R_TILED_SURFACE + 4u * SURFACE_DEPTH, tsr | (sv.zbuffer >> 5));
}

/* Sets the 3D registers that do not change while the screen is open,
 * with the values of the Mesa driver: the targets, the watermarks, the
 * stencil (off), the second texture (off) and the write mask. */
static void setup_3d(void) {
  int waited = 0;
  memset(sv.known, 0, sizeof sv.known);
  begin(64);
  reg3d(G_DEST_CTRL, (uint32_t)sv.tiles_w | (1u << 7) | ((sv.back >> 11) << 8), &waited);
  reg3d(G_ZBUF_OFFSET, (sv.zbuffer >> 11) | ((uint32_t)sv.tiles_w << 25), &waited);
  reg3d(G_Z_WATERMARKS, 24u | (24u << 8), &waited);
  reg3d(G_DEST_TEX_WATERMARKS, (15u << 24) | (1u << 30), &waited);
  reg3d(G_STENCIL_CTRL, 7u | (0xFFu << 4) | (0xFFu << 12), &waited);
  reg3d(G_TEX_PALETTE, 0, &waited);
  reg3d(G_TEX_CTRL1, TEX_CTRL_OFF, &waited);
  reg3d(G_TEX_ADDR1, sv.tex_min | 2u, &waited);
  reg3d(G_TEX_BLEND1, TBC_NO_TEXTURE1, &waited);
  reg3d(G_TEX_KEY, 0, &waited);
  reg3d(G_TEX_BLEND_COLOR, 0, &waited);
  reg2d(G_BITPLANE_WRITE_MASK, 0xFFFFFFFFu);
}

/* Maps the tiled surface n of the aperture: 16 MB for each surface after
 * the first 32 MB of region 1. */
static volatile uint8_t *map_surface(int n) {
  uint32_t rows = (uint32_t)sv.tiles_h * TILE_H;
  return (volatile uint8_t *)sgl_map_physical(sgl_pci_bar(&sv.pci, 1) + 0x02000000ul + 0x01000000ul * (uint32_t)n,
                                              APERTURE_PITCH * rows);
}

/* grSstWinOpen: sets the VBE mode, maps the frame buffer, divides
 * video memory and sets the engines. 16-bit color only; the other
 * arguments are not used. */
static int SGL_GLIDE_CALL gr_sst_win_open(unsigned long hwnd, int resolution, int refresh, int color_format,
                                          int origin, int color_buffers, int aux_buffers) {
  sgl_vbe_mode info;
  uint32_t screen, tiled, need;
  int mode, w, h;
  (void)hwnd, (void)refresh, (void)color_format, (void)color_buffers, (void)aux_buffers;
  if (!sv.regs || sv.open) return 0;
  mode = vbe_mode(resolution, &w, &h);
  if (mode < 0 || !sgl_vbe_mode_info(mode, &info) || !(info.attributes & 1) || info.bits != 16) return 0;
  sv.tiles_w = (w + TILE_W - 1) / TILE_W;
  sv.tiles_h = (h + TILE_H - 1) / TILE_H;
  screen = ((uint32_t)info.stride * (uint32_t)h + 0xFFFu) & ~0xFFFu;
  tiled = (uint32_t)sv.tiles_w * (uint32_t)sv.tiles_h * TILE_BYTES;
  sv.memory = sgl_vbe_memory();
  if (!sv.memory) sv.memory = 0x800000ul;
  need = screen + 2 * tiled + 0x100000ul;
  if (need > sv.memory || info.stride & 31) return 0;
  if (!(info.attributes & 0x80) || !sgl_vbe_set_mode(mode, 1)) return 0;
  if (info.lfb) sv.lfb_phys = info.lfb;
  sv.lfb = (volatile uint8_t *)sgl_map_physical(sv.lfb_phys, sv.memory);
  if (!sv.lfb) {
    sgl_vbe_text_mode();
    return 0;
  }
  sv.width = w, sv.height = h, sv.stride = info.stride, sv.origin = origin;
  sv.back = screen;
  sv.zbuffer = sv.back + tiled;
  sv.tex_min = sv.zbuffer + tiled;
  sv.tex_max = sv.memory - 0x1000ul; /* the hardware cursor */
  sv.front_bd = (uint32_t)(info.stride / 2) | BD_BPP16 | BD_BLOCK_WRITE_OFF;
  sv.back_bd = (uint32_t)(sv.tiles_w * TILE_W) | BD_BPP16 | BD_BLOCK_WRITE_OFF | ((sv.pro ? 1u : 2u) << 24);
  sv.aperture[SURFACE_BACK] = map_surface(SURFACE_BACK);
  sv.aperture[SURFACE_DEPTH] = map_surface(SURFACE_DEPTH);
  sv.clip[0] = 0, sv.clip[1] = 0, sv.clip[2] = w, sv.clip[3] = h;
  sv.depth_func = GR_CMP_LESS;
  sv.depth_mask = 1;
  sv.color_mask = 1;
  sv.dither = 1;
  sv.blend_src = GR_BLEND_ONE, sv.blend_dst = GR_BLEND_ZERO;
  sv.alpha_func = GR_CMP_ALWAYS;
  sv.engine_2d = 0;
  sv.open = 1;
  setup_engine();
  setup_3d();
  idle();
  sgl_vbe_display_start(0, 0);
  return 1;
}

/* grSstWinClose: stops the BCI, sets the text mode again and frees the
 * mappings of the frame buffer. */
static void SGL_GLIDE_CALL gr_sst_win_close(void) {
  int i;
  if (!sv.open) return;
  idle();
  wr(R_BCI_CONTROL, rd(R_BCI_CONTROL) & ~0x08u);
  sgl_vbe_text_mode();
  for (i = 0; i < 3; i++) {
    if (sv.aperture[i]) sgl_unmap_physical(sv.aperture[i]);
    sv.aperture[i] = NULL;
  }
  sgl_unmap_physical(sv.lfb);
  sv.lfb = NULL;
  free(sv.shadow);
  sv.shadow = NULL;
  sv.open = 0;
}

/* grGlideShutdown: closes the screen and frees the mappings. */
static void SGL_GLIDE_CALL gr_glide_shutdown(void) {
  gr_sst_win_close();
  if (sv.regs) sgl_unmap_physical(sv.regs);
  memset(&sv, 0, sizeof sv);
}

/* grSstIdle: waits until the card has drawn all. */
static void SGL_GLIDE_CALL gr_sst_idle(void) { idle(); }

/* Gives the top screen row of the clip window. */
static int clip_top(void) { return sv.origin == GR_ORIGIN_LOWER_LEFT ? sv.height - sv.clip[3] : sv.clip[1]; }

/* Fills the clip window of the tiled buffer at the byte offset off with
 * the 16-bit value value: a 2D rectangle with the color in the command,
 * as the Linux driver clears its buffers. The back buffer and the Z
 * buffer have the same bitmap descriptor. */
static void fill(uint32_t off, uint16_t value) {
  int w = sv.clip[2] - sv.clip[0], h = sv.clip[3] - sv.clip[1];
  if (w <= 0 || h <= 0) return;
  begin(8);
  if (!sv.engine_2d) put(CMD_WAIT | CMD_WAIT_3D);
  sv.engine_2d = 1;
  put(CMD_RECT | (ROP_COPY << 16) | CMD_RECT_XP | CMD_RECT_YP | CMD_SEND_COLOR | CMD_DEST_PBD_NEW);
  put(off);
  put(sv.back_bd);
  put((uint32_t)value | ((uint32_t)value << 16));
  put(((uint32_t)clip_top() << 16) | (uint32_t)sv.clip[0]);
  put(((uint32_t)h << 16) | (uint32_t)w);
}

/* Converts a Glide color (0xAARRGGBB) to a 5:6:5 pixel. */
static uint16_t pixel565(unsigned long c) {
  return (uint16_t)(((c >> 8) & 0xF800u) | ((c >> 5) & 0x07E0u) | ((c >> 3) & 0x001Fu));
}

/* grBufferClear: fills the clip window of the back buffer with color,
 * and of the Z buffer with depth when the depth writes are on. The
 * card keeps 1 - z in the Z buffer (see gr_draw_triangle). */
static void SGL_GLIDE_CALL gr_buffer_clear(unsigned long color, unsigned alpha, unsigned depth) {
  (void)alpha;
  if (!sv.open) return;
  if (sv.color_mask) fill(sv.back, pixel565(color));
  if (sv.depth && sv.depth_mask) fill(sv.zbuffer, (uint16_t)(0xFFFFu - (depth & 0xFFFFu)));
}

/* Waits for the start of the vertical retrace (bit 3 of port 3DAh).
 * Stops after 100 ms. */
static void retrace(void) {
  uint32_t start = sgl_dos_ms();
  while ((inportb(0x3DA) & 8) && sgl_dos_ms() - start < 100) {
  }
  while (!(inportb(0x3DA) & 8) && sgl_dos_ms() - start < 100) {
  }
}

/* grBufferSwap: copies the tiled back buffer to the screen with a 2D
 * blit (the destination is the screen, the source is the back buffer).
 * With interval 1 or more, the blit starts at the vertical retrace. */
static void SGL_GLIDE_CALL gr_buffer_swap(int interval) {
  if (!sv.open) return;
  idle();
  if (interval > 0) retrace();
  begin(12);
  put(CMD_WAIT | CMD_WAIT_3D);
  sv.engine_2d = 1;
  reg2d(G_PBD_LOW, 0);
  reg2d(G_PBD_HIGH, sv.front_bd);
  reg2d(G_SBD_LOW, sv.back);
  reg2d(G_SBD_HIGH, sv.back_bd);
  put(CMD_RECT | (ROP_COPY << 16) | CMD_RECT_XP | CMD_RECT_YP | CMD_DEST_PBD | CMD_SRC_SBD_COLOR);
  put(0);                                                    /* the source x and y */
  put(0);                                                    /* the destination x and y */
  put(((uint32_t)sv.height << 16) | (uint32_t)sv.width); /* the width and height */
}

/* Gives the fog visibility of a vertex (255 is no fog) from the Glide
 * fog table, by the eye depth w = 1 / oow. */
static float fog_visibility(float oow) {
  float w = oow > 0.0f ? 1.0f / oow : 1e30f, f;
  int i;
  if (w <= sv.fog_w[0]) return 255.0f - sv.fog_table[0];
  for (i = 1; i < GR_FOG_TABLE_SIZE; i++)
    if (w < sv.fog_w[i]) break;
  if (i == GR_FOG_TABLE_SIZE) return 255.0f - sv.fog_table[GR_FOG_TABLE_SIZE - 1];
  f = (w - sv.fog_w[i - 1]) / (sv.fog_w[i] - sv.fog_w[i - 1]);
  return 255.0f - ((float)sv.fog_table[i - 1] + f * ((float)sv.fog_table[i] - (float)sv.fog_table[i - 1]));
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

/* The Glide compare functions for the Z buffer. The card keeps 1 - z,
 * thus each compare is the reverse compare (as in the Mesa driver).
 * The compare codes of the card are the Glide codes: 0 never, 1 less,
 * 2 equal, 3 less or equal, 4 greater, 5 not equal, 6 greater or
 * equal, 7 always. */
static const uint8_t reverse_cmp[8] = {0, 4, 2, 6, 1, 5, 3, 7};

/* The Glide blend factors as the blend codes of the card, for the source
 * and for the destination. The source codes are 0 zero, 1 one, 2
 * destination color, 3 one minus destination color, 4 source alpha, 5
 * one minus source alpha; the destination codes are the same, with the
 * source color for 2 and 3. The screen has no alpha: the destination
 * alpha is 1. Glide factor 2 is the destination color for the source,
 * and the source color for the destination. */
static const uint8_t blend_code[8] = {0, 4, 2, 1, 1, 5, 3, 0};

/* Gives the texture blend of TexBlendCtrl for the color combine and the
 * alpha of glide.c. */
static uint32_t texture_blend(void) {
  uint32_t color, alpha;
  int mode = sv.alpha_mode;
  switch (sv.color_combine) {
    case COMBINE_MODULATE: color = TBC_COLOR_MODULATE; break;
    case COMBINE_DECAL:
      color = sv.tex_alpha ? TBC_COLOR_DECAL : TBC_COLOR_TEXTURE;
      mode = ALPHA_VERTEX;
      break;
    default: color = TBC_COLOR_TEXTURE; break;
  }
  alpha = mode == ALPHA_TEXTURE ? TBC_ALPHA_TEXTURE : mode == ALPHA_PRODUCT ? TBC_ALPHA_MODULATE : TBC_ALPHA_VERTEX;
  return color | alpha;
}

/* Writes the state of a triangle: the clip window, the Z buffer, the
 * blend, the alpha test, the fog and the texture. Needs at most 40
 * words of FIFO space. */
static void triangle_state(int textured) {
  int top = clip_top(), bottom = top + (sv.clip[3] - sv.clip[1]) - 1, waited = 0;
  int blend = !(sv.blend_src == GR_BLEND_ONE && sv.blend_dst == GR_BLEND_ZERO);
  uint32_t src = blend ? blend_code[sv.blend_src & 7] : 1u;
  uint32_t dst = blend ? blend_code[sv.blend_dst & 7] : 0u;
  uint32_t local = dst | (src << 4) | (1u << 31); /* the flush of the Z writes */
  uint32_t zctrl = (1u << 31); /* W to Z, as in the Mesa driver */
  uint32_t ctrl0 = (uint32_t)(sv.clip[0] & 0x7FF) | (1u << 11) | ((uint32_t)(top & 0xFFF) << 12) |
                   ((uint32_t)(sv.alpha_ref & 0xFF) << 24);
  uint32_t ctrl1 = (uint32_t)((sv.clip[2] - 1) & 0x7FF) | ((uint32_t)(bottom & 0xFFF) << 12) | (1u << 26);
  if (sv.color_mask) local |= 1u << 26;
  if (sv.depth && sv.depth_mask) local |= 1u << 27;
  if (dst != 0 || src == 2 || src == 3) local |= 1u << 30; /* the blend reads the destination */
  if (sv.depth) zctrl |= reverse_cmp[sv.depth_func & 7] | (1u << 5);
  else zctrl |= 7u;
  if (sv.dither) ctrl1 |= 1u << 24;
  if (sv.alpha_func != GR_CMP_ALWAYS) ctrl1 |= ((uint32_t)(sv.alpha_func & 7) << 28) | (1u << 31);
  to_3d();
  reg3d(G_DRAW_LOCAL_CTRL, local, &waited);
  reg3d(G_ZBUF_CTRL, zctrl, &waited);
  reg3d(G_DRAW_CTRL0, ctrl0, &waited);
  reg3d(G_DRAW_CTRL1, ctrl1, &waited);
  reg3d(G_FOG_CTRL, sv.fog ? sv.fog_color | (1u << 28) | (1u << 29) : 0u, &waited);
  if (textured) {
    uint32_t ctrl = (sv.tex_linear ? 1u : 0u) | (10u << 3) | sv.tex_wrap | (1u << 21);
    uint32_t descr = (uint32_t)sv.tex_wlog | ((uint32_t)sv.tex_hlog << 4) |
                     ((sv.tex_alpha ? TFT_ARGB4444 : TFT_RGB565) << 8) | (1u << 25) | (3u << 29);
    reg3d(G_TEX_CTRL0, ctrl, &waited);
    reg3d(G_TEX_ADDR0, sv.tex_addr | 2u, &waited);
    reg3d(G_TEX_BLEND0, texture_blend(), &waited);
    reg3d(G_TEX_DESCR, descr, &waited);
  } else {
    reg3d(G_TEX_CTRL0, TEX_CTRL_OFF, &waited);
    reg3d(G_TEX_BLEND0, TBC_ALPHA_VERTEX | TBC_COLOR_VERTEX, &waited);
    reg3d(G_TEX_DESCR, 3u << 29, &waited);
  }
}

/* grDrawTriangle: writes the state, then a DrawPrimitive command with
 * the three vertices: x, y, z, 1/w, the color (A R G B), the specular
 * color with the fog visibility in its alpha, and s and t. The card
 * keeps 1 - z in the Z buffer, as the Mesa driver does. S and T go from
 * 0 to 1 across the texture; the card divides by 1/w itself. The x and
 * y offsets are the offsets of the Mesa driver. */
static void SGL_GLIDE_CALL gr_draw_triangle(const sgl_gr_vertex *a, const sgl_gr_vertex *b,
                                            const sgl_gr_vertex *c) {
  const sgl_gr_vertex *g[3];
  int i, textured;
  if (!sv.open || (sv.depth && sv.depth_func == GR_CMP_NEVER) || sv.alpha_func == GR_CMP_NEVER) return;
  g[0] = a, g[1] = b, g[2] = c;
  for (i = 0; i < 3; i++) {
    float x = g[i]->x, y = g[i]->y;
    if (!(x > -2048.0f && x < 2048.0f && y > -2048.0f && y < 2048.0f)) return;
  }
  textured = sv.textured && sv.color_combine != COMBINE_COLOR;
  begin(48 + 3 * 8);
  triangle_state(textured);
  put(CMD_DRAW_PRIM | (3u << 16) | SKIP_ST1 | (textured ? 0u : SKIP_ST0));
  for (i = 0; i < 3; i++) {
    const sgl_gr_vertex *v = g[i];
    uint32_t vis = sv.fog ? byte(fog_visibility(v->oow)) : 255u;
    float w = v->oow > 0.0f ? v->oow : 1e-6f;
    put(float_bits(v->x - 0.5f));
    put(float_bits(screen_y(v->y) - 0.375f));
    put(float_bits(1.0f - clampf(v->ooz, 0.0f, 65535.0f) / 65535.0f));
    put(float_bits(w));
    put((byte(v->a) << 24) | (byte(v->r) << 16) | (byte(v->g) << 8) | byte(v->b));
    put(vis << 24);
    if (textured) {
      put(float_bits(v->tmu[0].sow / w * sv.tex_ss));
      put(float_bits(v->tmu[0].tow / w * sv.tex_ts));
    }
  }
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
static void SGL_GLIDE_CALL gr_depth_buffer_mode(int mode) { sv.depth = mode != GR_DEPTHBUFFER_DISABLE; }

/* grDepthBufferFunction: the Z compare. */
static void SGL_GLIDE_CALL gr_depth_buffer_function(int func) { sv.depth_func = func; }

/* grDepthMask: the Z writes. */
static void SGL_GLIDE_CALL gr_depth_mask(int on) { sv.depth_mask = on != 0; }

/* grColorMask: the color writes on or off (the screen has no alpha). */
static void SGL_GLIDE_CALL gr_color_mask(int rgb, int alpha) {
  (void)alpha;
  sv.color_mask = rgb != 0;
}

/* grColorCombine: the combines of glide.c: the vertex color, the
 * texture (GL_REPLACE), the texture times the vertex color
 * (GL_MODULATE), or GL_DECAL. */
static void SGL_GLIDE_CALL gr_color_combine(int func, int factor, int local, int other, int invert) {
  (void)local, (void)invert;
  if (other != GR_COMBINE_OTHER_TEXTURE || func == GR_COMBINE_FUNCTION_LOCAL)
    sv.color_combine = COMBINE_COLOR;
  else if (func == GR_COMBINE_FUNCTION_BLEND)
    sv.color_combine = COMBINE_DECAL;
  else if (func == GR_COMBINE_FUNCTION_SCALE_OTHER && factor == GR_COMBINE_FACTOR_LOCAL)
    sv.color_combine = COMBINE_MODULATE;
  else
    sv.color_combine = COMBINE_REPLACE;
}

/* grAlphaCombine: the alpha is the vertex alpha (LOCAL), the texture
 * alpha (SCALE_OTHER by ONE), or the texture alpha times the vertex
 * alpha (SCALE_OTHER by LOCAL). */
static void SGL_GLIDE_CALL gr_alpha_combine(int func, int factor, int local, int other, int invert) {
  (void)local, (void)other, (void)invert;
  if (func != GR_COMBINE_FUNCTION_SCALE_OTHER)
    sv.alpha_mode = ALPHA_VERTEX;
  else if (factor == GR_COMBINE_FACTOR_LOCAL)
    sv.alpha_mode = ALPHA_PRODUCT;
  else
    sv.alpha_mode = ALPHA_TEXTURE;
}

/* grTexCombine: the driver uses one texture unit. */
static void SGL_GLIDE_CALL gr_tex_combine(int tmu, int rgb_func, int rgb_factor, int alpha_func, int alpha_factor,
                                          int rgb_invert, int alpha_invert) {
  (void)tmu, (void)rgb_func, (void)rgb_factor, (void)alpha_func, (void)alpha_factor, (void)rgb_invert,
      (void)alpha_invert;
}

/* grAlphaBlendFunction: the blend factors of the color. */
static void SGL_GLIDE_CALL gr_alpha_blend_function(int rgb_src, int rgb_dst, int alpha_src, int alpha_dst) {
  (void)alpha_src, (void)alpha_dst;
  sv.blend_src = rgb_src, sv.blend_dst = rgb_dst;
}

/* grAlphaTestFunction: the alpha test compare. */
static void SGL_GLIDE_CALL gr_alpha_test_function(int func) { sv.alpha_func = func; }

/* grAlphaTestReferenceValue: the alpha test reference (0 to 255). */
static void SGL_GLIDE_CALL gr_alpha_test_reference_value(unsigned value) { sv.alpha_ref = (int)value; }

/* grFogMode: the fog table on or off. */
static void SGL_GLIDE_CALL gr_fog_mode(int mode) { sv.fog = mode != GR_FOG_DISABLE; }

/* grFogColorValue: the fog color (0xRRGGBB), as FogCtrl keeps it. */
static void SGL_GLIDE_CALL gr_fog_color_value(unsigned long color) { sv.fog_color = (uint32_t)color & 0xFFFFFFu; }

/* grFogTable: keeps the table, and the eye depth of each entry: entry
 * i is at w = 2^(3 + i / 4) / (8 - i % 4), as on a Voodoo. */
static void SGL_GLIDE_CALL gr_fog_table(const unsigned char *table) {
  int i;
  for (i = 0; i < GR_FOG_TABLE_SIZE; i++) {
    sv.fog_table[i] = table[i];
    sv.fog_w[i] = (float)(1ul << (3 + (i >> 2))) / (float)(8 - (i & 3));
  }
}

/* grCullMode: glide.c culls in software. */
static void SGL_GLIDE_CALL gr_cull_mode(int mode) { (void)mode; }

/* grDitherMode: the dither of the 16-bit color on or off. */
static void SGL_GLIDE_CALL gr_dither_mode(int mode) { sv.dither = mode != GR_DITHER_DISABLE; }

/* grTexMinAddress: the textures start after the Z buffer. */
static unsigned long SGL_GLIDE_CALL gr_tex_min_address(int tmu) {
  (void)tmu;
  return sv.tex_min;
}

/* grTexMaxAddress: the textures end before the hardware cursor. */
static unsigned long SGL_GLIDE_CALL gr_tex_max_address(int tmu) {
  (void)tmu;
  return sv.tex_max;
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

/* Gives the log2 of a side of the texture in the card: at least 3 (8
 * texels). The driver repeats each texel of a smaller side; S and T
 * go from 0 to 1, thus the result is the same. */
static int card_log(int log) { return log < 3 ? 3 : log; }

/* grTexTextureMemRequired: the bytes of the largest level, the only
 * level that the driver uses, in whole tiles of 2 KB. */
static unsigned long SGL_GLIDE_CALL gr_tex_texture_mem_required(unsigned long even_odd, sgl_gr_texinfo *info) {
  int wlog, hlog;
  (void)even_odd;
  tex_size(info, &wlog, &hlog);
  return ((2ul << (card_log(wlog) + card_log(hlog))) + TILE_BYTES - 1ul) & ~(unsigned long)(TILE_BYTES - 1u);
}

/* Gives the byte offset of the texel (x, y) in a tiled texture of
 * 2^wlog x 2^hlog texels of 16 bits (wlog and hlog 3 or more), as the
 * Mesa driver writes a texture. A tile is 64 x 16 texels, or the width
 * and the height of the texture when they are smaller; the tiles are in
 * rows. A tile is made of subtiles of 4 x 8 texels on a Savage4 and of
 * 8 x 8 texels on the other cards, in rows; the texels of a subtile
 * are in rows. */
static unsigned long texel_offset(int x, int y, int wlog, int hlog) {
  int tw = wlog < 6 ? 1 << wlog : TILE_W, th = hlog < 4 ? 1 << hlog : TILE_H;
  int sw = sv.pro ? 8 : 4;
  unsigned long tile = (unsigned long)(y / th) * (unsigned long)((1 << wlog) / tw) + (unsigned long)(x / tw);
  int tx = x % tw, ty = y % th;
  unsigned long sub = (unsigned long)(ty / 8) * (unsigned long)(tw / sw) + (unsigned long)(tx / sw);
  return (tile * (unsigned long)(tw * th) + sub * (unsigned long)(sw * 8) + (unsigned long)((ty % 8) * sw + tx % sw)) *
         2ul;
}

/* grTexDownloadMipMap: writes the largest level at start in the tile
 * order of the card (16 bits for each texel). RGB 565 and ARGB 4444
 * are formats of the card. */
static void SGL_GLIDE_CALL gr_tex_download_mip_map(int tmu, unsigned long start, unsigned long even_odd,
                                                   sgl_gr_texinfo *info) {
  const uint16_t *src = (const uint16_t *)info->data;
  int wlog, hlog, cw, ch, x, y;
  (void)tmu, (void)even_odd;
  if (!sv.open || start < sv.tex_min || start + gr_tex_texture_mem_required(0, info) > sv.tex_max) return;
  tex_size(info, &wlog, &hlog);
  cw = card_log(wlog), ch = card_log(hlog);
  idle();
  for (y = 0; y < 1 << ch; y++) {
    const uint16_t *row = src + ((long)(y >> (ch - hlog)) << wlog);
    for (x = 0; x < 1 << cw; x++)
      *(volatile uint16_t *)(sv.lfb + start + texel_offset(x, y, cw, ch)) = row[x >> (cw - wlog)];
  }
}

/* grTexSource: the texture that the triangles use. */
static void SGL_GLIDE_CALL gr_tex_source(int tmu, unsigned long start, unsigned long even_odd, sgl_gr_texinfo *info) {
  int wlog, hlog;
  (void)tmu, (void)even_odd;
  tex_size(info, &wlog, &hlog);
  sv.textured = 1;
  sv.tex_wlog = card_log(wlog), sv.tex_hlog = card_log(hlog);
  /* In Glide, 256 is the long side of the texture (2^(8 - large_lod)
   * texels). S and T go from 0 to 1 across the texture as Glide gives
   * it, before the driver repeats the texels of a small side. */
  sv.tex_ss = (float)(1 << (8 - info->large_lod)) / ((float)(1 << wlog) * 256.0f);
  sv.tex_ts = (float)(1 << (8 - info->large_lod)) / ((float)(1 << hlog) * 256.0f);
  sv.tex_alpha = info->format == GR_TEXFMT_ARGB_4444;
  sv.tex_addr = (uint32_t)start;
}

/* grTexFilterMode: the bilinear filter when the magnification or the
 * minification asks for it. */
static void SGL_GLIDE_CALL gr_tex_filter_mode(int tmu, int min, int mag) {
  (void)tmu;
  sv.tex_linear = min == GR_TEXTUREFILTER_BILINEAR || mag == GR_TEXTUREFILTER_BILINEAR;
}

/* grTexClampMode: repeat (0) or clamp (1), for s (bits 16 and 17 of
 * TexCtrl) and for t (bits 18 and 19). */
static void SGL_GLIDE_CALL gr_tex_clamp_mode(int tmu, int s, int t) {
  (void)tmu;
  sv.tex_wrap = (s == GR_TEXTURECLAMP_CLAMP ? 1u << 16 : 0u) | (t == GR_TEXTURECLAMP_CLAMP ? 1u << 18 : 0u);
}

/* grTexMipMapMode: the driver uses the largest level only. */
static void SGL_GLIDE_CALL gr_tex_mip_map_mode(int tmu, int mode, int lod_blend) {
  (void)tmu, (void)mode, (void)lod_blend;
}

/* Gives the address of the row row of the buffer of a lock: the screen
 * (linear), or the back buffer or the Z buffer through the tiled
 * aperture. Gives NULL when the aperture is not mapped. */
static volatile uint16_t *lock_row(int buffer, int row) {
  volatile uint8_t *base;
  if (buffer != GR_BUFFER_BACKBUFFER && buffer != GR_BUFFER_AUXBUFFER) return (volatile uint16_t *)(sv.lfb + (unsigned long)row * sv.stride);
  base = sv.aperture[buffer == GR_BUFFER_AUXBUFFER ? SURFACE_DEPTH : SURFACE_BACK];
  return base ? (volatile uint16_t *)(base + (unsigned long)row * APERTURE_PITCH) : NULL;
}

/* Gives the value of a pixel of the buffer as Glide sees it: the Z
 * buffer keeps 0xFFFF - z. The same function converts back. */
static uint16_t lock_value(int buffer, uint16_t v) { return buffer == GR_BUFFER_AUXBUFFER ? (uint16_t)(0xFFFFu - v) : v; }

/* grLfbLock: gives a copy of the buffer in system memory, in RGB 565
 * (or the Z values of 16 bits), rows in the order of origin. A write
 * lock copies the pixels to the card in grLfbUnlock. */
static int SGL_GLIDE_CALL gr_lfb_lock(int type, int buffer, int write_mode, int origin, int pixel_pipeline,
                                      sgl_gr_lfbinfo *info) {
  int x, y;
  (void)pixel_pipeline;
  if (!sv.open || !lock_row(buffer, 0)) return 0;
  if (!sv.shadow) sv.shadow = (uint16_t *)malloc((size_t)sv.width * (size_t)sv.height * 2u);
  if (!sv.shadow) return 0;
  idle();
  sv.lock_origin = origin;
  if (type == GR_LFB_READ_ONLY) {
    for (y = 0; y < sv.height; y++) {
      const volatile uint16_t *p = lock_row(buffer, origin == GR_ORIGIN_LOWER_LEFT ? sv.height - 1 - y : y);
      uint16_t *q = sv.shadow + (long)y * sv.width;
      for (x = 0; x < sv.width; x++) q[x] = lock_value(buffer, p[x]);
    }
  }
  info->ptr = sv.shadow;
  info->stride = (unsigned long)sv.width * 2ul;
  info->write_mode = write_mode;
  info->origin = origin;
  return 1;
}

/* grLfbUnlock: copies the pixels of a write lock to the card. */
static int SGL_GLIDE_CALL gr_lfb_unlock(int type, int buffer) {
  int x, y;
  if (!sv.open || !sv.shadow || type != GR_LFB_WRITE_ONLY || !lock_row(buffer, 0)) return 1;
  for (y = 0; y < sv.height; y++) {
    volatile uint16_t *p = lock_row(buffer, sv.lock_origin == GR_ORIGIN_LOWER_LEFT ? sv.height - 1 - y : y);
    const uint16_t *q = sv.shadow + (long)y * sv.width;
    for (x = 0; x < sv.width; x++) p[x] = lock_value(buffer, q[x]);
  }
  return 1;
}

/* Fills api with the Savage driver (see softgl_glide.h). */
int sgl_glide_savage(sgl_glide_api *api) {
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
  api->limits = 0;
  return 1;
}

#endif
