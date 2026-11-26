/* glide_r128.c - the Glide 2 functions of the Glide back end, on the 3D
 * engine of an ATI Rage 128 card, for 32-bit DOS. sgl_glide_r128 fills
 * an sgl_glide_api, and the Glide back end (glide.c) uses it as it uses
 * a Voodoo. The cards are the Rage 128 GL and VR, the Rage 128 Pro, the
 * Rage 128 Ultra, the Rage Fury, the Xpert 128, and the Rage Mobility
 * M3 and M4.
 *
 * The Rage 128 draws only from command packets of its command engine
 * (the CCE). The driver writes the packets to the packet FIFO of the
 * card with the processor (the PIO mode): the dwords go in pairs to
 * PM4_FIFO_DATA_EVEN and PM4_FIFO_DATA_ODD. Type-0 packets write the
 * registers, and a 3D_RNDR_GEN_PRIM packet draws a triangle from its
 * three vertices. The engine calculates the edges, the gradients and
 * the perspective division.
 *
 * The CCE runs microcode. The BIOS does not load it. On a real card,
 * the driver loads it from the file R128CCE.BIN, or from the file that
 * the environment variable SGL_R128_CCE names. The file is the
 * r128_cce.bin file of linux-firmware: 256 entries of two big-endian
 * 32-bit words. Without the file, the driver starts the CCE without
 * new microcode (the 86Box emulator does not need it).
 *
 * The engine does all of Glide that glide.c uses, with one texture
 * unit: the driver sets no limits (SGL_GLIDE_LIMIT_).
 *
 * The screen is a VBE mode of 16-bit color (5:6:5) with its linear
 * frame buffer. Video memory holds three color buffers (two when the
 * memory is small), the 16-bit Z buffer, and then the textures with
 * their mipmaps.
 *
 * The register facts come from the Rage 128 register guide and the
 * Rage 128 software development kit of ATI, from the open Rage 128
 * drivers (the r128 driver of Mesa and the r128 DRM driver of Linux),
 * and from the Rage 128 cards of the 86Box emulator, where this driver
 * is tested.
 *
 * Part of clang-retro softgl, a software implementation of OpenGL.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#if defined(__MSDOS__) && defined(__i386__)

#include <dos.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "glide_api.h"
#include "softgl_dos.h"
#include "softgl_glide.h"

/* The registers (byte offsets in the register window). */
enum {
  R_PC_NGUI_CTLSTAT = 0x0184,
  R_PM4_BUFFER_CNTL = 0x0704,
  R_PM4_VC_FPU_SETUP = 0x071C,
  R_PM4_STAT = 0x07B8,
  R_PM4_MICROCODE_ADDR = 0x07D4,
  R_PM4_MICROCODE_DATAH = 0x07DC,
  R_PM4_MICROCODE_DATAL = 0x07E0,
  R_PM4_BUFFER_ADDR = 0x07F0,
  R_PM4_MICRO_CNTL = 0x07FC,
  R_PM4_FIFO_DATA_EVEN = 0x1000,
  R_PM4_FIFO_DATA_ODD = 0x1004,
  R_DP_WRITE_MASK = 0x16CC,
  R_GUI_STAT = 0x1740,
  R_SCALE_3D_CNTL = 0x1A00,
  R_SETUP_CNTL = 0x1BC4,
  R_WINDOW_XY_OFFSET = 0x1BCC,
  R_DST_PITCH_OFFSET_C = 0x1C80, /* then DP_GUI_MASTER_CNTL_C, SC_TOP_LEFT_C, SC_BOTTOM_RIGHT_C */
  R_Z_OFFSET_C = 0x1C90,         /* then Z_PITCH_C */
  R_Z_STEN_CNTL_C = 0x1C98,      /* then TEX_CNTL_C, MISC_3D_STATE_CNTL */
  R_TEX_CLR_CMP_CLR_C = 0x1CA4,  /* then TEX_CLR_CMP_MSK_C, FOG_COLOR_C */
  R_FOG_COLOR_C = 0x1CAC,
  R_PRIM_TEX_CNTL_C = 0x1CB0,    /* then PRIM_TEX_COMBINE_CNTL_C */
  R_TEX_SIZE_PITCH_C = 0x1CB8,   /* then PRIM_TEX_0_OFFSET_C to PRIM_TEX_10_OFFSET_C */
  R_CONSTANT_COLOR_C = 0x1D34,
  R_STEN_REF_MASK_C = 0x1D40,
  R_PLANE_3D_MASK_C = 0x1D44
};

/* The bits of the CCE registers. */
#define PM4_192PIO (1u << 28)     /* PM4_BUFFER_CNTL: packets by PIO, 192 FIFO entries */
#define PM4_NOUPDATE (1u << 27)   /* PM4_BUFFER_CNTL: no read pointer in system memory */
#define PM4_FIFO_SIZE 192u
#define PM4_FREERUN (1u << 30)    /* PM4_MICRO_CNTL: the microcode runs */
#define PM4_FIFOCNT 0xFFFu        /* PM4_STAT: the free FIFO entries */
#define PM4_BUSY (1u << 16)       /* PM4_STAT */
#define GUI_ACTIVE (1u << 31)     /* PM4_STAT and GUI_STAT */
#define PC_FLUSH_ALL 0xFFu        /* PC_NGUI_CTLSTAT: write the pixel cache to memory */
#define PC_BUSY (1u << 31)

/* The packet headers. */
#define PACKET0(reg, n) ((((uint32_t)(n) - 1u) << 16) | ((uint32_t)(reg) >> 2)) /* n registers from reg */
#define PACKET2 0x80000000u                                                   /* no operation */
#define PACKET3(op, n) (0xC0000000u | (((uint32_t)(n) - 1u) << 16) | ((uint32_t)(op) << 8)) /* n dwords */
#define OP_GEN_PRIM 0x25u   /* 3D_RNDR_GEN_PRIM */
#define OP_PAINT_MULTI 0x9Au

/* The vertex of the driver (VC_FORMAT): x, y, z, rhw, the color
 * (ARGB), the specular color with the fog factor in the high byte, and
 * s and t. 8 dwords. */
#define VC_FORMAT 0xC9u
#define VC_DWORDS 8
/* VC_CNTL: a list of triangles, the vertices in the packet. */
#define VC_TRIANGLES (4u | (3u << 4))

/* The bits of DP_GUI_MASTER_CNTL. */
#define GMC_DST_PITCH_OFFSET (1u << 1)
#define GMC_DST_CLIPPING (1u << 3)
#define GMC_BRUSH_SOLID (13u << 4)
#define GMC_DST_565 (4u << 8)
#define GMC_SRC_COLOR (3u << 12)
#define GMC_ROP3_S (0xCCu << 16)
#define GMC_ROP3_P (0xF0u << 16)
#define GMC_SRC_MEMORY (2u << 24)
#define GMC_3D_FCN_EN (1u << 27)
#define GMC_CLR_CMP_DIS (1u << 28)
#define GMC_AUX_CLIP_DIS (1u << 29)
#define GMC_WR_MSK_DIS (1u << 30)
#define GMC_3D (GMC_DST_PITCH_OFFSET | GMC_DST_CLIPPING | GMC_BRUSH_SOLID | GMC_DST_565 | GMC_SRC_COLOR | GMC_ROP3_S | \
                GMC_SRC_MEMORY | GMC_3D_FCN_EN | GMC_CLR_CMP_DIS | GMC_AUX_CLIP_DIS | GMC_WR_MSK_DIS)
#define GMC_FILL (GMC_DST_PITCH_OFFSET | GMC_BRUSH_SOLID | GMC_DST_565 | GMC_SRC_COLOR | GMC_ROP3_P | GMC_CLR_CMP_DIS | \
                  GMC_AUX_CLIP_DIS)

/* The bits of TEX_CNTL_C. */
#define TC_Z_EN (1u << 0)
#define TC_Z_WRITE (1u << 1)
#define TC_TEXMAP (1u << 4)
#define TC_FOG (1u << 7)
#define TC_DITHER (1u << 8)
#define TC_BLEND (1u << 9)
#define TC_ALPHA_TEST (1u << 10)
#define TC_TEX_CACHE_FLUSH (1u << 23)
#define TC_LOD_BIAS (0x3Fu << 24)

/* The bits of SCALE_3D_CNTL and MISC_3D_STATE_CNTL: the blend factors
 * and the alpha test are at the same bits in the two registers. */
#define S3D_DITHER_TABLE (1u << 1)
#define S3D_DITHER_INIT_RESET (1u << 3)
#define S3D_TEXMAP_SHADE (2u << 6)
#define S3D_PIX_REPLICATE (1u << 8)
#define S3D_TEX_ALPHA (1u << 30)
#define S3D_CACHE_LINE_4QW (1u << 31)
#define MISC_TEXMAP_SHADE (2u << 8)
#define MISC_PIX_REPLICATE (1u << 10)
#define BLEND_SRC_SHIFT 16
#define BLEND_DST_SHIFT 20
#define ALPHA_TEST_SHIFT 24

/* PM4_VC_FPU_SETUP: draw the front and the back faces, Gouraud shading,
 * 4 bits of subpixel, the 3D mode. */
#define FPU_SETUP ((1u << 0) | (3u << 1) | (3u << 3) | (2u << 5) | (1u << 7) | (1u << 8) | (1u << 9))

/* SETUP_CNTL: Gouraud shading, S and T come multiplied by W (rhw),
 * vertices 1 to 3, 4 bits of subpixel. */
#define SETUP ((4u << 3) | (1u << 9) | (1u << 14) | (3u << 16) | (1u << 18) | (1u << 19))

/* The bits of PRIM_TEX_CNTL_C. */
#define PT_MIN_LINEAR (1u << 1)
#define PT_MIN_MIP_NEAREST (2u << 1)
#define PT_MIN_LINEAR_MIP_NEAREST (4u << 1)
#define PT_MAG_LINEAR (1u << 4)
#define PT_MIP_MAP_DISABLE (1u << 7)
#define PT_CLAMP_S (2u << 8)
#define PT_CLAMP_T (2u << 11)
#define PT_DATATYPE_SHIFT 16

/* The pixel types of the textures. */
#define TEX_565 4u
#define TEX_4444 15u

/* The bits of PRIM_TEX_COMBINE_CNTL_C. */
#define CB_COLOR_REPLACE 0u
#define CB_COLOR_MODULATE 3u
#define CB_COLOR_DECAL 9u /* the texture and the vertex color, mixed by the texture alpha */
#define CB_COLOR_TEX (4u << 4)
#define CB_INPUT_COLOR (4u << 10)
#define CB_ALPHA_TEXTURE 0u
#define CB_ALPHA_VERTEX (2u << 14)
#define CB_ALPHA_MODULATE (3u << 14)
#define CB_ALPHA_TEX (6u << 18)
#define CB_INPUT_ALPHA (2u << 25)

/* The color combines of glide.c, as the driver knows them. */
enum { COMBINE_COLOR, COMBINE_REPLACE, COMBINE_MODULATE, COMBINE_DECAL };

/* The alpha of a textured triangle: the vertex alpha, the texture
 * alpha, or the product. */
enum { ALPHA_VERTEX, ALPHA_TEXTURE, ALPHA_PRODUCT };

/* The size of the packet buffer in dwords. */
#define PACKET_BUFFER 256

/* The state of the driver. */
static struct {
  sgl_pci_device pci;
  volatile uint8_t *lfb;  /* video memory */
  volatile uint8_t *regs; /* the register window */
  uint32_t lfb_phys, memory;
  int open, width, height, pitch, origin;
  uint32_t buffer[3]; /* the offsets of the color buffers */
  int buffers;        /* 3, or 2 when video memory is small */
  int back;           /* the color buffer that is drawn */
  int front;          /* the color buffer that is shown */
  uint32_t zbuffer, tex_min;
  /* The packets that are not yet in the FIFO, and the free FIFO
   * entries that the driver knows of. */
  uint32_t packet[PACKET_BUFFER];
  int packets;
  uint32_t fifo_free;
  /* The state that Glide sets. */
  int depth, depth_func, depth_mask, color_mask, fog, dither, color_combine, alpha_mode;
  int blend_src, blend_dst, alpha_func, alpha_ref;
  uint8_t fog_table[GR_FOG_TABLE_SIZE];
  float fog_w[GR_FOG_TABLE_SIZE];
  uint32_t fog_color;
  int clip[4]; /* minx, miny, maxx, maxy (Glide coordinates) */
  /* The texture that TexSource selected. */
  int textured, tex_wlog, tex_hlog, tex_large, tex_small, tex_mipmap, tex_min_linear, tex_mag_linear;
  uint32_t tex_format, tex_clamp, tex_flush;
  /* The registers that the driver wrote last (0xFFFFFFFF: unknown). */
  uint32_t last_zsten, last_tex, last_misc, last_prim, last_combine, last_plane;
  int target_dirty; /* the target registers must be written again */
  uint16_t *shadow; /* the copy of a locked buffer */
  int lock_origin;
} rr;

/* The PCI IDs of the Rage 128 chips (vendor 1002h): the Rage 128 GL
 * and VR (RE, RF, RG, RK, RL), the Rage 128 Pro (PA to PX), the Rage
 * 128 Pro Ultra (TF, TL, TR to TU), the Rage 128 4X (SE to SH, SK to
 * SN), and the Rage Mobility M3 and M4 (LE, LF, MF, ML). */
static const sgl_pci_id r128_ids[] = {
    {0x1002, 0x5245}, {0x1002, 0x5246}, {0x1002, 0x5247}, {0x1002, 0x524B}, {0x1002, 0x524C}, {0x1002, 0x5041},
    {0x1002, 0x5042}, {0x1002, 0x5043}, {0x1002, 0x5044}, {0x1002, 0x5045}, {0x1002, 0x5046}, {0x1002, 0x5047},
    {0x1002, 0x5048}, {0x1002, 0x5049}, {0x1002, 0x504A}, {0x1002, 0x504B}, {0x1002, 0x504C}, {0x1002, 0x504D},
    {0x1002, 0x504E}, {0x1002, 0x504F}, {0x1002, 0x5050}, {0x1002, 0x5051}, {0x1002, 0x5052}, {0x1002, 0x5053},
    {0x1002, 0x5054}, {0x1002, 0x5055}, {0x1002, 0x5056}, {0x1002, 0x5057}, {0x1002, 0x5058}, {0x1002, 0x5446},
    {0x1002, 0x544C}, {0x1002, 0x5452}, {0x1002, 0x5453}, {0x1002, 0x5454}, {0x1002, 0x5455}, {0x1002, 0x5345},
    {0x1002, 0x5346}, {0x1002, 0x5347}, {0x1002, 0x5348}, {0x1002, 0x534B}, {0x1002, 0x534C}, {0x1002, 0x534D},
    {0x1002, 0x534E}, {0x1002, 0x4C45}, {0x1002, 0x4C46}, {0x1002, 0x4D46}, {0x1002, 0x4D4C}, {0, 0}};

/* Writes a 32-bit register. */
static void wr(uint32_t reg, uint32_t value) { *(volatile uint32_t *)(rr.regs + reg) = value; }

/* Reads a 32-bit register. */
static uint32_t rd(uint32_t reg) { return *(volatile uint32_t *)(rr.regs + reg); }

/* Writes the packet buffer to the FIFO, two dwords at a time. A type-2
 * packet fills an odd count. Waits for free FIFO entries, and stops
 * after about one second. */
static void flush(void) {
  int i;
  uint32_t start = sgl_dos_ms();
  if (!rr.packets) return;
  if (rr.packets & 1) rr.packet[rr.packets++] = PACKET2;
  for (i = 0; i < rr.packets; i += 2) {
    while (rr.fifo_free < 2) {
      rr.fifo_free = rd(R_PM4_STAT) & PM4_FIFOCNT;
      if (rr.fifo_free < 2 && sgl_dos_ms() - start > 1000) {
        rr.packets = 0;
        return;
      }
    }
    wr(R_PM4_FIFO_DATA_EVEN, rr.packet[i]);
    wr(R_PM4_FIFO_DATA_ODD, rr.packet[i + 1]);
    rr.fifo_free -= 2;
  }
  rr.packets = 0;
}

/* Makes space for a packet of n dwords in the packet buffer, and gives
 * the place of its first dword. */
static uint32_t *begin(int n) {
  uint32_t *p;
  if (rr.packets + n > PACKET_BUFFER - 1) flush();
  p = rr.packet + rr.packets;
  rr.packets += n;
  return p;
}

/* Adds a type-0 packet that writes value to the register reg. */
static void reg1(uint32_t reg, uint32_t value) {
  uint32_t *p = begin(2);
  p[0] = PACKET0(reg, 1);
  p[1] = value;
}

static void forget(void);

/* Waits until the CCE and the engine are idle, and the pixel cache is
 * in memory. Stops after about one second. Then writes DP_WRITE_MASK
 * (all planes) with the processor: in the 86Box emulator, a write of a
 * register of the engine by the processor draws the triangles that wait
 * in the emulator. The 3D state is written again after it. */
static void idle(void) {
  uint32_t start = sgl_dos_ms();
  if (!rr.regs) return;
  flush();
  for (;;) {
    uint32_t s = rd(R_PM4_STAT);
    if ((s & PM4_FIFOCNT) >= PM4_FIFO_SIZE && !(s & (PM4_BUSY | GUI_ACTIVE))) break;
    if (sgl_dos_ms() - start > 1000) break;
  }
  rr.fifo_free = PM4_FIFO_SIZE;
  wr(R_PC_NGUI_CTLSTAT, rd(R_PC_NGUI_CTLSTAT) | PC_FLUSH_ALL);
  while ((rd(R_PC_NGUI_CTLSTAT) & PC_BUSY) && sgl_dos_ms() - start < 1000) {
  }
  wr(R_DP_WRITE_MASK, 0xFFFFFFFFu);
  forget();
}

/* Waits until the engine is idle when the CCE does not run (GUI_STAT
 * bit 31, GUI_ACTIVE). Stops after about one second. */
static void gui_idle(void) {
  uint32_t start = sgl_dos_ms();
  while ((rd(R_GUI_STAT) & GUI_ACTIVE) && sgl_dos_ms() - start < 1000) {
  }
}

/* Stops the CCE: the registers take the writes of the processor again. */
static void cce_stop(void) {
  wr(R_PM4_MICRO_CNTL, 0);
  wr(R_PM4_BUFFER_CNTL, PM4_NOUPDATE);
}

/* Loads the microcode of the CCE from the file that SGL_R128_CCE names,
 * else from R128CCE.BIN. Does nothing when there is no file of the
 * correct size (2048 bytes). */
static void load_microcode(void) {
  const char *name = getenv("SGL_R128_CCE");
  unsigned char data[2048];
  FILE *f = fopen(name && *name ? name : "R128CCE.BIN", "rb");
  size_t n;
  int i;
  if (!f) return;
  n = fread(data, 1, sizeof data, f);
  fclose(f);
  if (n != sizeof data) return;
  gui_idle();
  wr(R_PM4_MICROCODE_ADDR, 0);
  for (i = 0; i < 256; i++) {
    const unsigned char *e = data + i * 8;
    wr(R_PM4_MICROCODE_DATAH, ((uint32_t)e[0] << 24) | ((uint32_t)e[1] << 16) | ((uint32_t)e[2] << 8) | e[3]);
    wr(R_PM4_MICROCODE_DATAL, ((uint32_t)e[4] << 24) | ((uint32_t)e[5] << 16) | ((uint32_t)e[6] << 8) | e[7]);
  }
}

/* Starts the CCE in the PIO mode. */
static void cce_start(void) {
  gui_idle();
  wr(R_PM4_BUFFER_CNTL, PM4_192PIO | PM4_NOUPDATE);
  (void)rd(R_PM4_BUFFER_ADDR); /* as the sample code of ATI does */
  wr(R_PM4_MICRO_CNTL, PM4_FREERUN);
  rr.packets = 0;
  rr.fifo_free = 0;
}

/* Gives the screen row of a Glide y. */
static float screen_y(float y) { return rr.origin == GR_ORIGIN_LOWER_LEFT ? (float)rr.height - y : y; }

/* grGlideInit: finds the card and maps its registers (BAR 2, 16 KB). */
static void SGL_GLIDE_CALL gr_glide_init(void) {
  uint32_t regs_phys;
  memset(&rr, 0, sizeof rr);
  if (!sgl_pci_find(r128_ids, 0, &rr.pci)) return;
  rr.lfb_phys = sgl_pci_bar(&rr.pci, 0);
  regs_phys = sgl_pci_bar(&rr.pci, 2);
  if (!rr.lfb_phys || !regs_phys) return;
  sgl_pci_write16(&rr.pci, 0x04, (uint16_t)(sgl_pci_read16(&rr.pci, 0x04) | 0x0006));
  rr.regs = (volatile uint8_t *)sgl_map_physical(regs_phys, 0x4000ul);
}

/* grGlideShutdown: closes the screen and frees the mappings. */
static void SGL_GLIDE_CALL gr_glide_shutdown(void);

/* grSstQueryHardware: one board with one TMU, as a Voodoo Graphics
 * (GrHwConfiguration as 32-bit words). */
static int SGL_GLIDE_CALL gr_sst_query_hardware(void *config) {
  int32_t *hw = (int32_t *)config;
  if (!rr.regs) return 0;
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

/* Gives the value of DST_PITCH_OFFSET for a buffer at the byte offset
 * off: the offset in units of 32 bytes, and the pitch in units of 8
 * pixels from bit 21. */
static uint32_t pitch_offset(uint32_t off) { return (off >> 5) | ((uint32_t)(rr.pitch / 8) << 21); }

/* Adds the packet that sets the drawing target (the back buffer), the
 * 3D mode of the 2D engine, and the scissor (the clip window; the last
 * pixel is in the scissor). */
static void target(void) {
  int top = rr.origin == GR_ORIGIN_LOWER_LEFT ? rr.height - rr.clip[3] : rr.clip[1];
  int bottom = top + (rr.clip[3] - rr.clip[1]) - 1;
  int left = rr.clip[0], right = rr.clip[2] - 1;
  uint32_t *p = begin(5);
  if (right < left || bottom < top) left = 1, right = 0, top = 1, bottom = 0;
  p[0] = PACKET0(R_DST_PITCH_OFFSET_C, 4);
  p[1] = pitch_offset(rr.buffer[rr.back]);
  p[2] = GMC_3D;
  p[3] = ((uint32_t)top << 16) | (uint32_t)left;
  p[4] = ((uint32_t)bottom << 16) | (uint32_t)right;
  rr.target_dirty = 0;
}

/* Forgets the registers that the driver wrote last. */
static void forget(void) {
  rr.last_zsten = rr.last_tex = rr.last_misc = rr.last_prim = rr.last_combine = rr.last_plane = 0xFFFFFFFFu;
  rr.target_dirty = 1;
}

/* grClipWindow: the clip rectangle (the maxima are not in it). */
static void SGL_GLIDE_CALL gr_clip_window(unsigned long minx, unsigned long miny, unsigned long maxx,
                                          unsigned long maxy) {
  rr.clip[0] = (int)minx, rr.clip[1] = (int)miny, rr.clip[2] = (int)maxx, rr.clip[3] = (int)maxy;
  rr.target_dirty = 1;
}

/* Adds the packets that set the 3D state that does not change: the
 * setup engine, the Z buffer, the masks and the texture cache. */
static void context(void) {
  uint32_t *p;
  reg1(R_SCALE_3D_CNTL, S3D_DITHER_TABLE | S3D_DITHER_INIT_RESET | S3D_TEXMAP_SHADE | S3D_PIX_REPLICATE |
                            S3D_TEX_ALPHA | S3D_CACHE_LINE_4QW | (1u << BLEND_SRC_SHIFT) |
                            (7u << ALPHA_TEST_SHIFT));
  reg1(R_PM4_VC_FPU_SETUP, FPU_SETUP);
  reg1(R_SETUP_CNTL, SETUP);
  reg1(R_WINDOW_XY_OFFSET, 0);
  p = begin(3);
  p[0] = PACKET0(R_Z_OFFSET_C, 2);
  p[1] = rr.zbuffer;
  p[2] = (uint32_t)rr.pitch / 8u;
  p = begin(4);
  p[0] = PACKET0(R_TEX_CLR_CMP_CLR_C, 3);
  p[1] = 0;
  p[2] = 0;
  p[3] = rr.fog_color;
  reg1(R_CONSTANT_COLOR_C, 0xFFFFFFFFu);
  reg1(R_DP_WRITE_MASK, 0xFFFFFFFFu); /* the fills write all planes */
  reg1(R_STEN_REF_MASK_C, 0xFFFF0000u);
  target();
}

/* grSstWinOpen: sets the VBE mode, maps the frame buffer, divides
 * video memory, starts the CCE and sets the engine. 16-bit color only;
 * the other arguments are not used. */
static int SGL_GLIDE_CALL gr_sst_win_open(unsigned long hwnd, int resolution, int refresh, int color_format,
                                          int origin, int color_buffers, int aux_buffers) {
  sgl_vbe_mode info;
  uint32_t frame, need;
  int mode, w, h, i;
  (void)hwnd, (void)refresh, (void)color_format, (void)color_buffers, (void)aux_buffers;
  if (!rr.regs || rr.open) return 0;
  mode = vbe_mode(resolution, &w, &h);
  if (mode < 0 || !sgl_vbe_mode_info(mode, &info) || !(info.attributes & 1) || info.bits != 16) return 0;
  /* The engine needs a pitch of a multiple of 8 pixels, and buffers at
   * multiples of 32 bytes. */
  frame = (uint32_t)info.stride * (uint32_t)h;
  if (info.stride % 16u || frame % 32u) return 0;
  rr.memory = sgl_vbe_memory();
  if (!rr.memory) rr.memory = 0x800000ul;
  need = 3 * frame + 0x20000ul;
  if (need > rr.memory) return 0;
  if (!(info.attributes & 0x80) || !sgl_vbe_set_mode(mode, 1)) return 0;
  if (info.lfb) rr.lfb_phys = info.lfb;
  rr.lfb = (volatile uint8_t *)sgl_map_physical(rr.lfb_phys, rr.memory);
  if (!rr.lfb) {
    sgl_vbe_text_mode();
    return 0;
  }
  rr.width = w, rr.height = h, rr.pitch = info.stride / 2, rr.origin = origin;
  /* Three color buffers when the textures keep 1 MB: the card draws
   * into the third while the monitor can still show the old front. */
  rr.buffers = need + frame + 0xC0000ul <= rr.memory ? 3 : 2;
  for (i = 0; i < rr.buffers; i++) rr.buffer[i] = (uint32_t)i * frame;
  rr.zbuffer = rr.buffer[rr.buffers - 1] + frame;
  rr.tex_min = (rr.zbuffer + frame + 63u) & ~63u;
  rr.front = 0;
  rr.back = 1;
  rr.clip[0] = 0, rr.clip[1] = 0, rr.clip[2] = w, rr.clip[3] = h;
  rr.depth_func = GR_CMP_LESS;
  rr.depth_mask = 1;
  rr.color_mask = 1;
  rr.dither = 1;
  rr.blend_src = GR_BLEND_ONE, rr.blend_dst = GR_BLEND_ZERO;
  rr.alpha_func = GR_CMP_ALWAYS;
  rr.open = 1;
  cce_stop();
  load_microcode();
  cce_start();
  forget();
  context();
  flush();
  sgl_vbe_display_start(0, 0);
  return 1;
}

/* grSstWinClose: stops the CCE, sets the text mode again and frees the
 * frame buffer. */
static void SGL_GLIDE_CALL gr_sst_win_close(void) {
  if (!rr.open) return;
  idle();
  cce_stop();
  sgl_vbe_text_mode();
  sgl_unmap_physical(rr.lfb);
  rr.lfb = NULL;
  free(rr.shadow);
  rr.shadow = NULL;
  rr.open = 0;
}

/* grGlideShutdown: closes the screen and frees the mappings. */
static void SGL_GLIDE_CALL gr_glide_shutdown(void) {
  gr_sst_win_close();
  if (rr.regs) sgl_unmap_physical(rr.regs);
  memset(&rr, 0, sizeof rr);
}

/* grSstIdle: waits until the card has drawn all. */
static void SGL_GLIDE_CALL gr_sst_idle(void) { idle(); }

/* Adds the packet that fills the clip window of the buffer at the byte
 * offset off with the 16-bit value value (PAINT_MULTI). */
static void fill(uint32_t off, uint16_t value) {
  int top = rr.origin == GR_ORIGIN_LOWER_LEFT ? rr.height - rr.clip[3] : rr.clip[1];
  int w = rr.clip[2] - rr.clip[0], h = rr.clip[3] - rr.clip[1];
  uint32_t *p;
  if (w <= 0 || h <= 0) return;
  p = begin(6);
  p[0] = PACKET3(OP_PAINT_MULTI, 5);
  p[1] = GMC_FILL;
  p[2] = pitch_offset(off);
  p[3] = value;
  p[4] = ((uint32_t)rr.clip[0] << 16) | (uint32_t)top;
  p[5] = ((uint32_t)w << 16) | (uint32_t)h;
  /* The paint changes the target registers of the 2D engine, and the
   * 3D state must be written again after it, as the r128 DRM driver
   * does after a clear. */
  forget();
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
  if (!rr.open) return;
  if (rr.color_mask) fill(rr.buffer[rr.back], pixel565(color));
  if (rr.depth && rr.depth_mask) fill(rr.zbuffer, (uint16_t)depth);
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
  int old = rr.front;
  if (!rr.open) return;
  idle();
  sgl_vbe_display_start((int)(rr.buffer[rr.back] / ((uint32_t)rr.pitch * 2u)), interval > 0);
  rr.front = rr.back;
  if (rr.buffers == 3) {
    rr.back = 3 - rr.front - old;
  } else {
    if (interval > 0) next_frame();
    rr.back = old;
  }
  rr.target_dirty = 1;
}

/* Gives the fog visibility of a vertex (255 is no fog) from the Glide
 * fog table, by the eye depth w = 1 / oow. */
static float fog_visibility(float oow) {
  float w = oow > 0.0f ? 1.0f / oow : 1e30f, f;
  int i;
  if (w <= rr.fog_w[0]) return 255.0f - rr.fog_table[0];
  for (i = 1; i < GR_FOG_TABLE_SIZE; i++)
    if (w < rr.fog_w[i]) break;
  if (i == GR_FOG_TABLE_SIZE) return 255.0f - rr.fog_table[GR_FOG_TABLE_SIZE - 1];
  f = (w - rr.fog_w[i - 1]) / (rr.fog_w[i] - rr.fog_w[i - 1]);
  return 255.0f - ((float)rr.fog_table[i - 1] + f * ((float)rr.fog_table[i] - (float)rr.fog_table[i - 1]));
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

/* The Glide blend factors as the blend codes of the engine, for the
 * source and for the destination. The codes are 0 zero, 1 one, 2
 * source color, 3 one minus source color, 4 source alpha, 5 one minus
 * source alpha, 6 destination alpha, 7 one minus destination alpha, 8
 * destination color, 9 one minus destination color and 10 the
 * saturated source alpha. Glide factor 2 is the destination color for
 * the source, and the source color for the destination. */
static const uint8_t blend_src_code[8] = {0, 4, 8, 6, 1, 5, 9, 7};
static const uint8_t blend_dst_code[8] = {0, 4, 2, 6, 1, 5, 3, 7};

/* Writes the value of a register to the 3D engine when it changes.
 * last is the value that the driver wrote last. */
static void reg_changed(uint32_t reg, uint32_t value, uint32_t *last) {
  if (*last == value) return;
  *last = value;
  reg1(reg, value);
}

/* Adds the packets of the state of a triangle: the Z buffer, the
 * texture, the fog, the blend and the alpha test. */
static void triangle_state(int textured) {
  int blend = !(rr.blend_src == GR_BLEND_ONE && rr.blend_dst == GR_BLEND_ZERO);
  uint32_t tex = TC_LOD_BIAS;
  uint32_t zsten = (uint32_t)cmp_code[rr.depth_func & 7] << 4;
  uint32_t src = rr.blend_src == 0xF ? 10u : blend_src_code[rr.blend_src & 7]; /* 0xF: alpha saturate */
  uint32_t misc = MISC_TEXMAP_SHADE | MISC_PIX_REPLICATE | (uint32_t)(rr.alpha_ref & 0xFF) |
                  (src << BLEND_SRC_SHIFT) | ((uint32_t)blend_dst_code[rr.blend_dst & 7] << BLEND_DST_SHIFT) |
                  ((uint32_t)cmp_code[rr.alpha_func & 7] << ALPHA_TEST_SHIFT);
  if (rr.depth) tex |= TC_Z_EN | (rr.depth_mask ? TC_Z_WRITE : 0u);
  if (rr.dither) tex |= TC_DITHER;
  if (blend) tex |= TC_BLEND;
  if (rr.fog) tex |= TC_FOG;
  if (rr.alpha_func != GR_CMP_ALWAYS) tex |= TC_ALPHA_TEST;
  if (textured) {
    uint32_t prim = (rr.tex_format << PT_DATATYPE_SHIFT) | rr.tex_clamp;
    uint32_t combine = CB_COLOR_TEX | CB_INPUT_COLOR | CB_INPUT_ALPHA;
    int alpha = rr.alpha_mode;
    tex |= TC_TEXMAP;
    if (rr.tex_flush) {
      tex |= TC_TEX_CACHE_FLUSH;
      rr.tex_flush = 0;
    }
    if (rr.tex_mag_linear) prim |= PT_MAG_LINEAR;
    if (rr.tex_mipmap && rr.tex_small < rr.tex_large)
      prim |= rr.tex_min_linear ? PT_MIN_LINEAR_MIP_NEAREST : PT_MIN_MIP_NEAREST;
    else
      prim |= PT_MIP_MAP_DISABLE | (rr.tex_min_linear ? PT_MIN_LINEAR : 0u);
    switch (rr.color_combine) {
      case COMBINE_MODULATE: combine |= CB_COLOR_MODULATE; break;
      case COMBINE_DECAL:
        combine |= CB_COLOR_DECAL;
        alpha = ALPHA_VERTEX;
        break;
      default: combine |= CB_COLOR_REPLACE; break;
    }
    combine |= alpha == ALPHA_TEXTURE   ? CB_ALPHA_TEXTURE | CB_ALPHA_TEX
               : alpha == ALPHA_PRODUCT ? CB_ALPHA_MODULATE | CB_ALPHA_TEX
                                        : CB_ALPHA_VERTEX;
    if (prim != rr.last_prim || combine != rr.last_combine) {
      uint32_t *p = begin(3);
      p[0] = PACKET0(R_PRIM_TEX_CNTL_C, 2);
      p[1] = prim;
      p[2] = combine;
      rr.last_prim = prim, rr.last_combine = combine;
    }
  }
  if (zsten != rr.last_zsten || tex != rr.last_tex || misc != rr.last_misc) {
    uint32_t *p = begin(4);
    p[0] = PACKET0(R_Z_STEN_CNTL_C, 3);
    p[1] = zsten;
    p[2] = tex;
    p[3] = misc;
    /* The blend and the alpha test are also in SCALE_3D_CNTL. */
    if (misc != rr.last_misc)
      reg1(R_SCALE_3D_CNTL, S3D_DITHER_TABLE | S3D_DITHER_INIT_RESET | S3D_TEXMAP_SHADE | S3D_PIX_REPLICATE |
                                S3D_TEX_ALPHA | S3D_CACHE_LINE_4QW | (misc & 0x07FF0000u));
    rr.last_zsten = zsten, rr.last_misc = misc;
    /* A flush is done once: the next value is different. */
    rr.last_tex = tex;
  }
  reg_changed(R_PLANE_3D_MASK_C, rr.color_mask ? 0xFFFFu : 0u, &rr.last_plane);
}

/* grDrawTriangle: adds the state of the triangle and a 3D_RNDR_GEN_PRIM
 * packet with the three vertices. */
static void SGL_GLIDE_CALL gr_draw_triangle(const sgl_gr_vertex *a, const sgl_gr_vertex *b,
                                            const sgl_gr_vertex *c) {
  const sgl_gr_vertex *g[3];
  uint32_t *p;
  int i, textured;
  float ss = 0.0f, ts = 0.0f;
  if (!rr.open || (rr.depth && rr.depth_func == GR_CMP_NEVER) || rr.alpha_func == GR_CMP_NEVER) return;
  g[0] = a, g[1] = b, g[2] = c;
  for (i = 0; i < 3; i++) {
    float x = g[i]->x, y = g[i]->y;
    if (!(x > -4096.0f && x < 4096.0f && y > -4096.0f && y < 4096.0f)) return;
  }
  if (rr.target_dirty) target();
  textured = rr.textured && rr.color_combine != COMBINE_COLOR;
  triangle_state(textured);
  if (textured) {
    /* S and T go from 0 to 1 across the texture. In Glide, 256 is the
     * long side. */
    ss = (float)(1 << rr.tex_large) / ((float)(1 << rr.tex_wlog) * 256.0f);
    ts = (float)(1 << rr.tex_large) / ((float)(1 << rr.tex_hlog) * 256.0f);
  }
  p = begin(3 + 3 * VC_DWORDS);
  p[0] = PACKET3(OP_GEN_PRIM, 2 + 3 * VC_DWORDS);
  p[1] = VC_FORMAT;
  p[2] = VC_TRIANGLES | (3u << 16);
  p += 3;
  for (i = 0; i < 3; i++, p += VC_DWORDS) {
    const sgl_gr_vertex *v = g[i];
    uint32_t vis = rr.fog ? byte(fog_visibility(v->oow)) : 255u;
    p[0] = float_bits(v->x);
    p[1] = float_bits(screen_y(v->y));
    p[2] = float_bits(clampf(v->ooz, 0.0f, 65535.0f) / 65535.0f);
    p[3] = float_bits(textured ? v->tmu[0].oow : 1.0f);
    p[4] = (byte(v->a) << 24) | (byte(v->r) << 16) | (byte(v->g) << 8) | byte(v->b);
    p[5] = vis << 24;
    p[6] = float_bits(textured ? v->tmu[0].sow * ss : 0.0f);
    p[7] = float_bits(textured ? v->tmu[0].tow * ts : 0.0f);
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
static void SGL_GLIDE_CALL gr_depth_buffer_mode(int mode) { rr.depth = mode != GR_DEPTHBUFFER_DISABLE; }

/* grDepthBufferFunction: the Z compare. */
static void SGL_GLIDE_CALL gr_depth_buffer_function(int func) { rr.depth_func = func; }

/* grDepthMask: the Z writes. */
static void SGL_GLIDE_CALL gr_depth_mask(int on) { rr.depth_mask = on != 0; }

/* grColorMask: the color writes on or off (PLANE_3D_MASK_C). */
static void SGL_GLIDE_CALL gr_color_mask(int rgb, int alpha) {
  (void)alpha;
  rr.color_mask = rgb != 0;
}

/* grColorCombine: the combines of glide.c: the vertex color, the
 * texture (GL_REPLACE), the texture times the vertex color
 * (GL_MODULATE), or the vertex color and the texture mixed by the
 * texture alpha (GL_DECAL). */
static void SGL_GLIDE_CALL gr_color_combine(int func, int factor, int local, int other, int invert) {
  (void)local, (void)invert;
  if (other != GR_COMBINE_OTHER_TEXTURE || func == GR_COMBINE_FUNCTION_LOCAL)
    rr.color_combine = COMBINE_COLOR;
  else if (func == GR_COMBINE_FUNCTION_BLEND)
    rr.color_combine = COMBINE_DECAL;
  else if (func == GR_COMBINE_FUNCTION_SCALE_OTHER && factor == GR_COMBINE_FACTOR_LOCAL)
    rr.color_combine = COMBINE_MODULATE;
  else
    rr.color_combine = COMBINE_REPLACE;
}

/* grAlphaCombine: the alpha is the vertex alpha (LOCAL), the texture
 * alpha (SCALE_OTHER by ONE), or the texture alpha times the vertex
 * alpha (SCALE_OTHER by LOCAL). */
static void SGL_GLIDE_CALL gr_alpha_combine(int func, int factor, int local, int other, int invert) {
  (void)local, (void)other, (void)invert;
  if (func != GR_COMBINE_FUNCTION_SCALE_OTHER)
    rr.alpha_mode = ALPHA_VERTEX;
  else if (factor == GR_COMBINE_FACTOR_LOCAL)
    rr.alpha_mode = ALPHA_PRODUCT;
  else
    rr.alpha_mode = ALPHA_TEXTURE;
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
  rr.blend_src = rgb_src, rr.blend_dst = rgb_dst;
}

/* grAlphaTestFunction: the alpha test. */
static void SGL_GLIDE_CALL gr_alpha_test_function(int func) { rr.alpha_func = func; }

/* grAlphaTestReferenceValue: the alpha that the test compares with. */
static void SGL_GLIDE_CALL gr_alpha_test_reference_value(unsigned value) { rr.alpha_ref = (int)value; }

/* grFogMode: the fog table on or off. */
static void SGL_GLIDE_CALL gr_fog_mode(int mode) { rr.fog = mode != GR_FOG_DISABLE; }

/* grFogColorValue: the fog color (0xRRGGBB), in FOG_COLOR_C. */
static void SGL_GLIDE_CALL gr_fog_color_value(unsigned long color) {
  rr.fog_color = (uint32_t)color & 0xFFFFFFu;
  if (rr.open) reg1(R_FOG_COLOR_C, rr.fog_color);
}

/* grFogTable: keeps the table, and the eye depth of each entry: entry
 * i is at w = 2^(3 + i / 4) / (8 - i % 4), as on a Voodoo. */
static void SGL_GLIDE_CALL gr_fog_table(const unsigned char *table) {
  int i;
  for (i = 0; i < GR_FOG_TABLE_SIZE; i++) {
    rr.fog_table[i] = table[i];
    rr.fog_w[i] = (float)(1ul << (3 + (i >> 2))) / (float)(8 - (i & 3));
  }
}

/* grCullMode: glide.c culls in software. */
static void SGL_GLIDE_CALL gr_cull_mode(int mode) { (void)mode; }

/* grDitherMode: dither the 16-bit pixels, or not. */
static void SGL_GLIDE_CALL gr_dither_mode(int mode) { rr.dither = mode != GR_DITHER_DISABLE; }

/* grTexMinAddress: the textures start after the Z buffer. */
static unsigned long SGL_GLIDE_CALL gr_tex_min_address(int tmu) {
  (void)tmu;
  return rr.tex_min;
}

/* grTexMaxAddress: the textures end at the end of video memory. */
static unsigned long SGL_GLIDE_CALL gr_tex_max_address(int tmu) {
  (void)tmu;
  return rr.memory;
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
 * texels (16 bits for each texel), rounded up to 32 bytes. wlog and
 * hlog are the sizes of the largest level, whose long side is 2^large
 * texels. */
static unsigned long level_bytes(int n, int large, int wlog, int hlog) {
  int w = wlog - (large - n), h = hlog - (large - n);
  if (w < 0) w = 0;
  if (h < 0) h = 0;
  return ((1ul << (w + h)) * 2ul + 31ul) & ~31ul;
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
 * first. Each row of a level has the width of the level. RGB 565 and
 * ARGB 4444 are the formats of the card. */
static void SGL_GLIDE_CALL gr_tex_download_mip_map(int tmu, unsigned long start, unsigned long even_odd,
                                                   sgl_gr_texinfo *info) {
  const uint16_t *src = (const uint16_t *)info->data;
  int wlog, hlog, large = 8 - info->large_lod, small = 8 - info->small_lod, n;
  unsigned long off = start;
  (void)tmu, (void)even_odd;
  if (!rr.open || start + gr_tex_texture_mem_required(0, info) > rr.memory) return;
  tex_size(info, &wlog, &hlog);
  idle();
  for (n = large; n >= small; n--) {
    long i;
    int w = wlog - (large - n), h = hlog - (large - n);
    long texels = (1l << ((w < 0 ? 0 : w) + (h < 0 ? 0 : h)));
    volatile uint16_t *dst = (volatile uint16_t *)(rr.lfb + off);
    for (i = 0; i < texels; i++) dst[i] = src[i];
    src += texels;
    off += level_bytes(n, large, wlog, hlog);
  }
  rr.tex_flush = 1;
}

/* grTexSource: the texture that the triangles use. Offset slot k is
 * the level whose long side is 2^(small + k) texels: slot 0 is the
 * smallest level. */
static void SGL_GLIDE_CALL gr_tex_source(int tmu, unsigned long start, unsigned long even_odd, sgl_gr_texinfo *info) {
  int n;
  uint32_t off = (uint32_t)start, slot[11], *p;
  (void)tmu, (void)even_odd;
  rr.textured = 1;
  rr.tex_large = 8 - info->large_lod;
  rr.tex_small = 8 - info->small_lod;
  tex_size(info, &rr.tex_wlog, &rr.tex_hlog);
  rr.tex_format = info->format == GR_TEXFMT_ARGB_4444 ? TEX_4444 : TEX_565;
  if (!rr.open) return;
  for (n = rr.tex_large; n >= rr.tex_small; n--) {
    slot[n - rr.tex_small] = off;
    off += (uint32_t)level_bytes(n, rr.tex_large, rr.tex_wlog, rr.tex_hlog);
  }
  /* The slots above the largest level are the largest level again. */
  for (n = rr.tex_large - rr.tex_small + 1; n < 11; n++) slot[n] = (uint32_t)start;
  p = begin(13);
  p[0] = PACKET0(R_TEX_SIZE_PITCH_C, 12);
  p[1] = (uint32_t)rr.tex_wlog | ((uint32_t)rr.tex_large << 4) | ((uint32_t)rr.tex_hlog << 8) |
         ((uint32_t)rr.tex_small << 12);
  for (n = 0; n < 11; n++) p[2 + n] = slot[n];
  rr.tex_flush = 1;
}

/* grTexFilterMode: the filters for the minification and for the
 * magnification. */
static void SGL_GLIDE_CALL gr_tex_filter_mode(int tmu, int min, int mag) {
  (void)tmu;
  rr.tex_min_linear = min == GR_TEXTUREFILTER_BILINEAR;
  rr.tex_mag_linear = mag == GR_TEXTUREFILTER_BILINEAR;
}

/* grTexClampMode: wrap or clamp, for s and for t. */
static void SGL_GLIDE_CALL gr_tex_clamp_mode(int tmu, int s, int t) {
  (void)tmu;
  rr.tex_clamp = (s == GR_TEXTURECLAMP_CLAMP ? PT_CLAMP_S : 0u) | (t == GR_TEXTURECLAMP_CLAMP ? PT_CLAMP_T : 0u);
}

/* grTexMipMapMode: the nearest mipmap, or the largest level only. */
static void SGL_GLIDE_CALL gr_tex_mip_map_mode(int tmu, int mode, int lod_blend) {
  (void)tmu, (void)lod_blend;
  rr.tex_mipmap = mode != GR_MIPMAP_DISABLE;
}

/* Gives the offset of the buffer of a lock: the front buffer, the back
 * buffer or the Z buffer. The rows have the same pitch. */
static uint32_t lock_buffer(int buffer) {
  if (buffer == GR_BUFFER_AUXBUFFER) return rr.zbuffer;
  return rr.buffer[buffer == GR_BUFFER_BACKBUFFER ? rr.back : rr.front];
}

/* grLfbLock: gives a copy of the buffer in system memory, in RGB 565
 * (or the Z values), rows in the order of origin. A write lock copies
 * the pixels to the card in grLfbUnlock. */
static int SGL_GLIDE_CALL gr_lfb_lock(int type, int buffer, int write_mode, int origin, int pixel_pipeline,
                                      sgl_gr_lfbinfo *info) {
  int x, y;
  uint32_t base;
  (void)pixel_pipeline;
  if (!rr.open) return 0;
  if (!rr.shadow) rr.shadow = (uint16_t *)malloc((size_t)rr.width * (size_t)rr.height * 2u);
  if (!rr.shadow) return 0;
  idle();
  rr.lock_origin = origin;
  base = lock_buffer(buffer);
  if (type == GR_LFB_READ_ONLY) {
    for (y = 0; y < rr.height; y++) {
      int row = origin == GR_ORIGIN_LOWER_LEFT ? rr.height - 1 - y : y;
      const volatile uint16_t *p = (const volatile uint16_t *)(rr.lfb + base) + (long)row * rr.pitch;
      uint16_t *q = rr.shadow + (long)y * rr.width;
      for (x = 0; x < rr.width; x++) q[x] = p[x];
    }
  }
  info->ptr = rr.shadow;
  info->stride = (unsigned long)rr.width * 2ul;
  info->write_mode = write_mode;
  info->origin = origin;
  return 1;
}

/* grLfbUnlock: copies the pixels of a write lock to the card. */
static int SGL_GLIDE_CALL gr_lfb_unlock(int type, int buffer) {
  int x, y;
  uint32_t base;
  if (!rr.open || !rr.shadow || type != GR_LFB_WRITE_ONLY) return 1;
  base = lock_buffer(buffer);
  for (y = 0; y < rr.height; y++) {
    int row = rr.lock_origin == GR_ORIGIN_LOWER_LEFT ? rr.height - 1 - y : y;
    volatile uint16_t *p = (volatile uint16_t *)(rr.lfb + base) + (long)row * rr.pitch;
    const uint16_t *q = rr.shadow + (long)y * rr.width;
    for (x = 0; x < rr.width; x++) p[x] = q[x];
  }
  return 1;
}

/* Fills api with the Rage 128 driver (see softgl_glide.h). The card
 * does all of the Glide state that glide.c uses: no limits. */
int sgl_glide_r128(sgl_glide_api *api) {
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
