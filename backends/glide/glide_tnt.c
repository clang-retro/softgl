/* glide_tnt.c - the Glide 2 functions of the Glide back end, on the 3D
 * engine of an NVIDIA RIVA TNT or TNT2 card (NV4 and NV5), for 32-bit
 * DOS. sgl_glide_tnt fills an sgl_glide_api, and the Glide back end
 * (glide.c) uses it as it uses a Voodoo.
 *
 * The driver writes the methods of the graphics objects directly to
 * the FIFO of channel 0 (the PIO mode of the XFree86 riva driver). It
 * loads the fixed state of the XFree86 riva driver: the PFIFO, the
 * PGRAPH, and the objects and the hash table in the instance memory.
 * That state has a DX5 textured triangle object (class 0x54) on
 * subchannel 7. The driver adds a 3D surface object (class 0x53) and a
 * DMA object of video memory, and binds them to the triangle object.
 *
 * For each triangle, the driver writes the three vertices (TLVERTEX:
 * x, y, z, 1/w, the color, the specular color with the fog factor in
 * its alpha, u and v) and a DrawPrimitive method. The color buffers and
 * the 16-bit Z buffer are linear (pitch) surfaces. The textures are
 * swizzled.
 *
 * The driver sets the limits of the table (SGL_GLIDE_LIMIT_), and
 * softgl then draws in software what the card cannot do:
 *
 * - The color writes cannot be stopped.
 * - No mipmaps: the card uses the largest level of a texture.
 * - One texture unit.
 *
 * The register facts come from open source drivers: the XFree86 4.3
 * riva driver (riva_hw.c, riva_tbl.h), the Mesa nouveau classic driver
 * (nv04_3d.xml.h, nv04_state_*.c, nv04_surface.c) and the Linux nouveau
 * driver (nvkm/engine/gr/nv04.c). The RIVA 128 (NV3) has another 3D
 * engine, and the driver does not find it. 86Box does not emulate the
 * cards, and the driver is not tested.
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

/* The units of the registers (byte offsets in PCI region 0). */
enum {
  U_PMC = 0x000000,
  U_PFIFO = 0x002000,
  U_PTIMER = 0x009000,
  U_PFB = 0x100000,
  U_PGRAPH = 0x400000,
  U_PRAMIN = 0x710000, /* the objects (64 KB after the instance base 0x700000) */
  U_FIFO = 0x800000    /* the FIFO of channel 0: subchannel n at + 0x2000 n */
};

/* A table of fixed state: the index of a 32-bit register in its unit,
 * and the value. */
typedef struct {
  uint16_t index;
  uint32_t value;
} fixed;

/* The fixed state of the XFree86 riva driver (riva_tbl.h) for the NV4
 * architecture and 16-bit color. */
static const fixed table_pmc[] = {{0x0050, 0x00000000}, {0x0080, 0xFFFF00FF}, {0x0080, 0xFFFFFFFF}};
static const fixed table_ptimer[] = {
    {0x0080, 0x00000008}, {0x0084, 0x00000003}, {0x0050, 0x00000000}, {0x0040, 0xFFFFFFFF}};
static const fixed table_pfifo[] = {
    {0x0140, 0x00000000}, {0x0480, 0x00000000}, {0x0494, 0x00000000}, {0x0481, 0x00000000}, {0x048B, 0x00000000},
    {0x0400, 0x00000000}, {0x0414, 0x00000000}, {0x0084, 0x03000100}, {0x0085, 0x00000110}, {0x0086, 0x00000112},
    {0x0143, 0x0000FFFF}, {0x0496, 0x0000FFFF}, {0x0050, 0x00000000}, {0x0040, 0xFFFFFFFF}, {0x0415, 0x00000001},
    {0x0480, 0x00000001}, {0x0494, 0x00000001}, {0x0495, 0x00000001}, {0x0140, 0x00000001}};
static const fixed table_pgraph[] = {
    {0x0020, 0x1231C001}, {0x0021, 0x72111101}, {0x0022, 0x11D5F071}, {0x0023, 0x10D4FF31}, {0x0060, 0x00000000},
    {0x0068, 0x00000000}, {0x0070, 0x00000000}, {0x0078, 0x00000000}, {0x0061, 0x00000000}, {0x0069, 0x00000000},
    {0x0071, 0x00000000}, {0x0079, 0x00000000}, {0x0062, 0x00000000}, {0x006A, 0x00000000}, {0x0072, 0x00000000},
    {0x007A, 0x00000000}, {0x0063, 0x00000000}, {0x006B, 0x00000000}, {0x0073, 0x00000000}, {0x007B, 0x00000000},
    {0x0064, 0x00000000}, {0x006C, 0x00000000}, {0x0074, 0x00000000}, {0x007C, 0x00000000}, {0x0065, 0x00000000},
    {0x006D, 0x00000000}, {0x0075, 0x00000000}, {0x007D, 0x00000000}, {0x0066, 0x00000000}, {0x006E, 0x00000000},
    {0x0076, 0x00000000}, {0x007E, 0x00000000}, {0x0067, 0x00000000}, {0x006F, 0x00000000}, {0x0077, 0x00000000},
    {0x007F, 0x00000000}, {0x0058, 0x00000000}, {0x0059, 0x00000000}, {0x005A, 0x00000000}, {0x005B, 0x00000000},
    {0x0196, 0x00000000}, {0x01A1, 0x01FFFFFF}, {0x0197, 0x00000000}, {0x01A2, 0x01FFFFFF}, {0x0198, 0x00000000},
    {0x01A3, 0x01FFFFFF}, {0x0199, 0x00000000}, {0x01A4, 0x01FFFFFF}, {0x0050, 0x00000000}, {0x0040, 0xFFFFFFFF},
    {0x005C, 0x10010100}, {0x01C4, 0xFFFFFFFF}, {0x01C8, 0x00000001}, {0x0204, 0x00000000}, {0x01C3, 0x00000001},
    /* 16-bit color */
    {0x01C9, 0x00556555}, {0x0186, 0x000050C2}, {0x020C, 0x0C0B0B0B}};
static const fixed table_pramin[] = {
    /* The hash table: handle, then context (valid, graphics, instance). */
    {0x0000, 0x80000010}, {0x0001, 0x80011145}, {0x0002, 0x80000011}, {0x0003, 0x80011146}, {0x0004, 0x80000012},
    {0x0005, 0x80011147}, {0x0006, 0x80000013}, {0x0007, 0x80011148}, {0x0008, 0x80000014}, {0x0009, 0x80011149},
    {0x000A, 0x80000015}, {0x000B, 0x8001114A}, {0x000C, 0x80000016}, {0x000D, 0x8001114F}, {0x0020, 0x80000000},
    {0x0021, 0x80011142}, {0x0022, 0x80000001}, {0x0023, 0x80011143}, {0x0024, 0x80000002}, {0x0025, 0x80011144},
    {0x0026, 0x80000003}, {0x0027, 0x8001114B}, {0x0028, 0x80000004}, {0x0029, 0x8001114C}, {0x002A, 0x80000005},
    {0x002B, 0x8001114D}, {0x002C, 0x80000006}, {0x002D, 0x8001114E},
    /* The objects. */
    {0x0500, 0x00003000}, {0x0501, 0x01FFFFFF}, {0x0502, 0x00000002}, {0x0503, 0x00000002}, {0x0508, 0x01008043},
    {0x050A, 0x00000000}, {0x050B, 0x00000000}, {0x050C, 0x01008019}, {0x050E, 0x00000000}, {0x050F, 0x00000000},
    {0x0510, 0x01008018}, {0x0512, 0x00000000}, {0x0513, 0x00000000}, {0x0514, 0x01008021}, {0x0516, 0x00000000},
    {0x0517, 0x00000000}, {0x0518, 0x0100805F}, {0x051A, 0x00000000}, {0x051B, 0x00000000}, {0x051C, 0x0100804B},
    {0x051E, 0x00000000}, {0x051F, 0x00000000}, {0x0520, 0x0100A048}, {0x0521, 0x00000D01}, {0x0522, 0x11401140},
    {0x0523, 0x00000000}, {0x0524, 0x0300A054}, {0x0525, 0x00000D01}, {0x0526, 0x11401140}, {0x0527, 0x00000000},
    {0x0528, 0x0300A055}, {0x0529, 0x00000D01}, {0x052A, 0x11401140}, {0x052B, 0x00000000}, {0x052C, 0x00000058},
    {0x052E, 0x11401140}, {0x052F, 0x00000000}, {0x0530, 0x00000059}, {0x0532, 0x11401140}, {0x0533, 0x00000000},
    {0x0534, 0x0000005A}, {0x0536, 0x11401140}, {0x0537, 0x00000000}, {0x0538, 0x0000005B}, {0x053A, 0x11401140},
    {0x053B, 0x00000000}, {0x053C, 0x0300A01C}, {0x053E, 0x11401140}, {0x053F, 0x00000000},
    /* 16-bit color */
    {0x0509, 0x00000C02}, {0x050D, 0x00000C02}, {0x0511, 0x00000B02}, {0x0515, 0x00000C02}, {0x0519, 0x00000C02},
    {0x051D, 0x00000C02}, {0x052D, 0x00000C02}, {0x052E, 0x00000C02}, {0x0535, 0x00000702}, {0x0539, 0x00000702},
    {0x053D, 0x00000C02},
    /* Added by this driver: a 3D surface object (class 0x53, instance
     * 0x1150), a DMA object of video memory (class 0x3D, instance
     * 0x1151; the limit is set at open), and their hash entries. The
     * hash of handle h is the XOR of its 9-bit pieces, times 8 bytes. */
    {0x0540, 0x00000053}, {0x0541, 0x00000000}, {0x0542, 0x00000000}, {0x0543, 0x00000000},
    {0x0544, 0x0000303D}, {0x0546, 0x00000002}, {0x0547, 0x00000002},
    {0x002E, 0x80000007}, {0x002F, 0x80011150}, {0x0030, 0x80000008}, {0x0031, 0x80001151}};

/* The handles of the objects that the driver uses. */
#define H_TRIANGLE 0x80000014u /* class 0x54, bound to subchannel 7 */
#define H_SURFACE_3D 0x80000007u
#define H_DMA_VRAM 0x80000008u

/* The subchannels. */
enum { SUB_SURFACE_3D = 6, SUB_TRIANGLE = 7 };

/* The methods of the 3D surface object (class 0x53). */
enum {
  M_SF_DMA_COLOR = 0x184,
  M_SF_DMA_ZETA = 0x188,
  M_SF_FORMAT = 0x300,
  M_SF_CLIP_SIZE = 0x304,
  M_SF_PITCH = 0x308,
  M_SF_OFFSET_COLOR = 0x30C,
  M_SF_OFFSET_ZETA = 0x310
};

/* The methods of the textured triangle object (class 0x54). */
enum {
  M_TT_DMA_A = 0x184,
  M_TT_DMA_B = 0x188,
  M_TT_SURFACES = 0x18C,
  M_TT_COLORKEY = 0x300, /* then OFFSET, FORMAT, FILTER, BLEND, CONTROL, FOGCOLOR */
  M_TT_TLVERTEX = 0x400, /* vertex n at + 0x20 n */
  M_TT_DRAW = 0x600
};

/* The values of FORMAT (the texture). */
#define FMT_DMA_A 0x1u
#define FMT_ORIGIN_CORNER 0xA0u /* the texel origins of the Mesa driver */
#define FMT_R5G6B5 0x500u
#define FMT_A4R4G4B4 0x400u
#define FMT_ONE_LEVEL (1u << 12)
#define FMT_REPEAT 1u
#define FMT_CLAMP 3u /* clamp to edge */

/* The values of BLEND. */
#define BL_DECAL 1u      /* GL_REPLACE in the Mesa driver */
#define BL_DECALALPHA 3u /* GL_DECAL */
#define BL_MODULATEALPHA 4u
#define BL_MASK_MSB 0x20u
#define BL_GOURAUD 0x80u
#define BL_PERSPECTIVE 0x100u
#define BL_FOG 0x10000u
#define BL_BLEND 0x100000u

/* The values of CONTROL. */
#define CT_ALPHA_ENABLE 0x1000u
#define CT_ORIGIN_CORNER 0x2000u
#define CT_Z_ENABLE 0x4000u
#define CT_CULL_NONE 0x100000u
#define CT_DITHER 0x400000u
#define CT_Z_WRITE 0x1000000u
#define CT_Z_FIXED 0x40000000u

/* The color combines of glide.c, as the driver knows them. */
enum { COMBINE_COLOR, COMBINE_REPLACE, COMBINE_MODULATE, COMBINE_DECAL };

/* The state methods of the triangle, in the order of the methods. */
enum { S_COLORKEY, S_OFFSET, S_FORMAT, S_FILTER, S_BLEND, S_CONTROL, S_FOGCOLOR, S_COUNT };

/* The state of the driver. */
static struct {
  sgl_pci_device pci;
  volatile uint8_t *lfb;  /* video memory (region 1) */
  volatile uint8_t *regs; /* the registers (region 0, 16 MB) */
  uint32_t lfb_phys, memory;
  unsigned fifo_free;  /* the free FIFO entries that the driver knows of */
  unsigned fifo_empty; /* the free FIFO entries of an empty FIFO */
  int open, width, height, stride, origin;
  uint32_t buffer[2]; /* the byte offsets of the color buffers */
  int back;           /* the color buffer that is drawn */
  uint32_t zbuffer, white, tex_min, tex_max;
  /* The state that Glide sets. */
  int depth, depth_func, depth_mask, color_mask, fog, dither, color_combine;
  int blend_src, blend_dst, alpha_func, alpha_ref;
  uint8_t fog_table[GR_FOG_TABLE_SIZE];
  float fog_w[GR_FOG_TABLE_SIZE];
  uint32_t fog_color; /* 0xRRGGBB */
  int clip[4];        /* minx, miny, maxx, maxy (Glide coordinates) */
  int clip_dirty;
  /* The texture that TexSource selected. */
  int textured, tex_wlog, tex_hlog, tex_alpha, tex_linear;
  float tex_ss, tex_ts; /* the scales from S and T to 0 to 1 */
  uint32_t tex_addr, tex_wrap;
  /* The values that the driver wrote last to the state methods. */
  uint32_t last[S_COUNT];
  int known;
  uint16_t *shadow; /* the copy of a locked buffer */
  int lock_origin;
} tn;

/* The PCI IDs of the TNT, the TNT2 (Pro, Ultra, M64), the Vanta and the
 * Aladdin TNT2. */
static const sgl_pci_id tnt_ids[] = {{0x10DE, 0x0020}, {0x10DE, 0x0028}, {0x10DE, 0x0029}, {0x10DE, 0x002A},
                                     {0x10DE, 0x002B}, {0x10DE, 0x002C}, {0x10DE, 0x002D}, {0x10DE, 0x002E},
                                     {0x10DE, 0x002F}, {0x10DE, 0x00A0}, {0, 0}};

/* Reads a 32-bit register. */
static uint32_t rd(uint32_t reg) { return *(volatile uint32_t *)(tn.regs + reg); }

/* Writes a 32-bit register. */
static void wr(uint32_t reg, uint32_t value) { *(volatile uint32_t *)(tn.regs + reg) = value; }

/* Writes the table of fixed state t of n entries to the unit at unit. */
static void load(uint32_t unit, const fixed *t, size_t n) {
  size_t i;
  for (i = 0; i < n; i++) wr(unit + 4u * t[i].index, t[i].value);
}

/* Gives the free entries of the FIFO: the 16-bit FifoFree of
 * subchannel 0 counts bytes. */
static unsigned fifo_free(void) { return (unsigned)(*(volatile uint16_t *)(tn.regs + U_FIFO + 0x10) >> 2); }

/* Waits until the FIFO has n free entries. Stops after about one
 * second. */
static void fifo(unsigned n) {
  uint32_t start;
  if (tn.fifo_free >= n) {
    tn.fifo_free -= n;
    return;
  }
  start = sgl_dos_ms();
  do
    tn.fifo_free = fifo_free();
  while (tn.fifo_free < n && sgl_dos_ms() - start < 1000);
  tn.fifo_free = tn.fifo_free >= n ? tn.fifo_free - n : 0;
}

/* Writes the method mthd of the object on subchannel sub. The caller
 * reserved the FIFO entry. */
static void method(int sub, uint32_t mthd, uint32_t value) {
  *(volatile uint32_t *)(tn.regs + U_FIFO + 0x2000u * (uint32_t)sub + mthd) = value;
}

/* Waits until the card has done all the commands: the FIFO is empty and
 * the PGRAPH is not busy. Stops after about one second. */
static void idle(void) {
  uint32_t start = sgl_dos_ms();
  if (!tn.regs) return;
  while ((fifo_free() < tn.fifo_empty || (rd(U_PGRAPH + 0x700) & 1)) && sgl_dos_ms() - start < 1000) {
  }
  tn.fifo_free = 0;
}

/* Gives the screen row of a Glide y. */
static float screen_y(float y) { return tn.origin == GR_ORIGIN_LOWER_LEFT ? (float)tn.height - y : y; }

/* grGlideInit: finds the card, enables its memory space and maps the
 * registers (region 0, 16 MB). */
static void SGL_GLIDE_CALL gr_glide_init(void) {
  uint32_t regs_phys;
  memset(&tn, 0, sizeof tn);
  if (!sgl_pci_find(tnt_ids, 0, &tn.pci)) return;
  regs_phys = sgl_pci_bar(&tn.pci, 0);
  tn.lfb_phys = sgl_pci_bar(&tn.pci, 1);
  if (!regs_phys || !tn.lfb_phys) return;
  sgl_pci_write16(&tn.pci, 0x04, (uint16_t)(sgl_pci_read16(&tn.pci, 0x04) | 0x0002));
  tn.regs = (volatile uint8_t *)sgl_map_physical(regs_phys, 0x1000000ul);
}

/* grGlideShutdown: closes the screen and frees the mappings. */
static void SGL_GLIDE_CALL gr_glide_shutdown(void);

/* grSstQueryHardware: one board with one TMU, as a Voodoo Graphics
 * (GrHwConfiguration as 32-bit words). */
static int SGL_GLIDE_CALL gr_sst_query_hardware(void *config) {
  int32_t *hw = (int32_t *)config;
  if (!tn.regs) return 0;
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
  tn.clip[0] = (int)minx, tn.clip[1] = (int)miny, tn.clip[2] = (int)maxx, tn.clip[3] = (int)maxy;
  tn.clip_dirty = 1;
}

/* Gives the top screen row of the clip window. */
static int clip_top(void) { return tn.origin == GR_ORIGIN_LOWER_LEFT ? tn.height - tn.clip[3] : tn.clip[1]; }

/* Writes the clip window to the PGRAPH. On the NV4, the clip methods of
 * the 3D surface object are software methods, and the Linux driver
 * writes these registers for them. The card must be idle. */
static void write_clip(void) {
  uint32_t top = (uint32_t)clip_top();
  wr(U_PGRAPH + 0x53C, (uint32_t)tn.clip[0]);
  wr(U_PGRAPH + 0x544, (uint32_t)tn.clip[2]);
  wr(U_PGRAPH + 0x540, top);
  wr(U_PGRAPH + 0x548, top + (uint32_t)(tn.clip[3] - tn.clip[1]));
  tn.clip_dirty = 0;
}

/* Points the 3D surface object to the back buffer: the color offset,
 * and the Z buffer, which has the pitch of the color buffers. */
static void target(void) {
  fifo(1);
  method(SUB_SURFACE_3D, M_SF_OFFSET_COLOR, tn.buffer[tn.back]);
}

/* Loads the fixed state (PMC, PTIMER, PFB, PFIFO, the objects, PGRAPH)
 * as the XFree86 riva driver does for 16-bit color, binds the objects to
 * the subchannels, and sets the 3D surface and the triangle objects. */
static void setup(void) {
  load(U_PMC, table_pmc, sizeof table_pmc / sizeof table_pmc[0]);
  load(U_PTIMER, table_ptimer, sizeof table_ptimer / sizeof table_ptimer[0]);
  wr(U_PFB + 0x200, 0x00001114);
  load(U_PFIFO, table_pfifo, sizeof table_pfifo / sizeof table_pfifo[0]);
  load(U_PRAMIN, table_pramin, sizeof table_pramin / sizeof table_pramin[0]);
  wr(U_PRAMIN + 4u * 0x0545, tn.memory - 1u); /* the limit of the DMA object */
  load(U_PGRAPH, table_pgraph, sizeof table_pgraph / sizeof table_pgraph[0]);
  wr(U_PGRAPH + 0x640, 0), wr(U_PGRAPH + 0x644, 0), wr(U_PGRAPH + 0x648, 0), wr(U_PGRAPH + 0x64C, 0);
  wr(U_PGRAPH + 0x670, (uint32_t)tn.stride), wr(U_PGRAPH + 0x674, (uint32_t)tn.stride);
  wr(U_PGRAPH + 0x678, (uint32_t)tn.stride), wr(U_PGRAPH + 0x67C, (uint32_t)tn.stride);
  tn.fifo_empty = fifo_free();
  tn.fifo_free = 0;
  fifo(14);
  method(SUB_SURFACE_3D, 0, H_SURFACE_3D);
  method(SUB_TRIANGLE, 0, H_TRIANGLE);
  method(SUB_SURFACE_3D, M_SF_DMA_COLOR, H_DMA_VRAM);
  method(SUB_SURFACE_3D, M_SF_DMA_ZETA, H_DMA_VRAM);
  method(SUB_SURFACE_3D, M_SF_FORMAT, 0x103u); /* pitch, R5G6B5 */
  method(SUB_SURFACE_3D, M_SF_CLIP_SIZE, ((uint32_t)tn.height << 16) | (uint32_t)tn.width);
  method(SUB_SURFACE_3D, M_SF_PITCH, ((uint32_t)tn.stride << 16) | (uint32_t)tn.stride);
  method(SUB_SURFACE_3D, M_SF_OFFSET_COLOR, tn.buffer[tn.back]);
  method(SUB_SURFACE_3D, M_SF_OFFSET_ZETA, tn.zbuffer);
  method(SUB_TRIANGLE, M_TT_DMA_A, H_DMA_VRAM);
  method(SUB_TRIANGLE, M_TT_DMA_B, H_DMA_VRAM);
  method(SUB_TRIANGLE, M_TT_SURFACES, H_SURFACE_3D);
  idle();
  write_clip();
  tn.known = 0;
}

/* grSstWinOpen: sets the VBE mode, maps the frame buffer, divides
 * video memory and sets the engine. 16-bit color only; the other
 * arguments are not used. */
static int SGL_GLIDE_CALL gr_sst_win_open(unsigned long hwnd, int resolution, int refresh, int color_format,
                                          int origin, int color_buffers, int aux_buffers) {
  sgl_vbe_mode info;
  uint32_t frame;
  int mode, w, h;
  (void)hwnd, (void)refresh, (void)color_format, (void)color_buffers, (void)aux_buffers;
  if (!tn.regs || tn.open) return 0;
  mode = vbe_mode(resolution, &w, &h);
  if (mode < 0 || !sgl_vbe_mode_info(mode, &info) || !(info.attributes & 1) || info.bits != 16) return 0;
  frame = ((uint32_t)info.stride * (uint32_t)h + 0xFFFu) & ~0xFFFu;
  tn.memory = sgl_vbe_memory();
  if (!tn.memory) tn.memory = 0x800000ul;
  /* The top 128 KB of video memory hold the instance memory. */
  if (3 * frame + 0x20000ul + 0x80000ul > tn.memory || info.stride & 63) return 0;
  if (!(info.attributes & 0x80) || !sgl_vbe_set_mode(mode, 1)) return 0;
  if (info.lfb) tn.lfb_phys = info.lfb;
  tn.lfb = (volatile uint8_t *)sgl_map_physical(tn.lfb_phys, tn.memory);
  if (!tn.lfb) {
    sgl_vbe_text_mode();
    return 0;
  }
  tn.width = w, tn.height = h, tn.stride = info.stride, tn.origin = origin;
  tn.buffer[0] = 0;
  tn.buffer[1] = frame;
  tn.zbuffer = 2 * frame;
  tn.white = 3 * frame; /* a texture of one white texel */
  tn.tex_min = tn.white + 0x100u;
  tn.tex_max = tn.memory - 0x20000ul;
  *(volatile uint16_t *)(tn.lfb + tn.white) = 0xFFFFu;
  tn.back = 1;
  tn.clip[0] = 0, tn.clip[1] = 0, tn.clip[2] = w, tn.clip[3] = h;
  tn.depth_func = GR_CMP_LESS;
  tn.depth_mask = 1;
  tn.color_mask = 1;
  tn.dither = 1;
  tn.blend_src = GR_BLEND_ONE, tn.blend_dst = GR_BLEND_ZERO;
  tn.alpha_func = GR_CMP_ALWAYS;
  tn.open = 1;
  setup();
  sgl_vbe_display_start(0, 0);
  return 1;
}

/* grSstWinClose: sets the text mode again and frees the frame buffer. */
static void SGL_GLIDE_CALL gr_sst_win_close(void) {
  if (!tn.open) return;
  idle();
  sgl_vbe_text_mode();
  sgl_unmap_physical(tn.lfb);
  tn.lfb = NULL;
  free(tn.shadow);
  tn.shadow = NULL;
  tn.open = 0;
}

/* grGlideShutdown: closes the screen and frees the mappings. */
static void SGL_GLIDE_CALL gr_glide_shutdown(void) {
  gr_sst_win_close();
  if (tn.regs) sgl_unmap_physical(tn.regs);
  memset(&tn, 0, sizeof tn);
}

/* grSstIdle: waits until the card has drawn all. */
static void SGL_GLIDE_CALL gr_sst_idle(void) { idle(); }

/* Gives the fog visibility of a vertex (255 is no fog) from the Glide
 * fog table, by the eye depth w = 1 / oow. */
static float fog_visibility(float oow) {
  float w = oow > 0.0f ? 1.0f / oow : 1e30f, f;
  int i;
  if (w <= tn.fog_w[0]) return 255.0f - tn.fog_table[0];
  for (i = 1; i < GR_FOG_TABLE_SIZE; i++)
    if (w < tn.fog_w[i]) break;
  if (i == GR_FOG_TABLE_SIZE) return 255.0f - tn.fog_table[GR_FOG_TABLE_SIZE - 1];
  f = (w - tn.fog_w[i - 1]) / (tn.fog_w[i] - tn.fog_w[i - 1]);
  return 255.0f - ((float)tn.fog_table[i - 1] + f * ((float)tn.fog_table[i] - (float)tn.fog_table[i - 1]));
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

/* The Glide blend factors as the blend codes of the card (the codes of
 * the Mesa driver): 1 zero, 2 one, 3 source color, 4 one minus source
 * color, 5 source alpha, 6 one minus source alpha, 7 destination
 * alpha, 8 one minus destination alpha, 9 destination color, 10 one
 * minus destination color, 11 the saturated source alpha. Glide factor
 * 2 is the destination color for the source, and the source color for
 * the destination. The screen has no alpha: the destination alpha is
 * 1. */
static const uint8_t blend_src_code[8] = {1, 5, 9, 2, 2, 6, 10, 1};
static const uint8_t blend_dst_code[8] = {1, 5, 3, 2, 2, 6, 4, 1};

/* Gives the values of the state methods (COLORKEY to FOGCOLOR) for a
 * triangle in s. The compare codes of the card are the Glide codes
 * plus 1. */
static void triangle_state(int textured, uint32_t *s) {
  int blend = !(tn.blend_src == GR_BLEND_ONE && tn.blend_dst == GR_BLEND_ZERO);
  uint32_t src = tn.blend_src == 0xF ? 11u : blend_src_code[tn.blend_src & 7];
  uint32_t mode;
  s[S_COLORKEY] = 0;
  s[S_FOGCOLOR] = 0xFF000000u | tn.fog_color;
  s[S_CONTROL] = CT_Z_FIXED | CT_ORIGIN_CORNER | CT_CULL_NONE | ((uint32_t)((tn.depth_func & 7) + 1) << 16) |
                 ((uint32_t)((tn.alpha_func & 7) + 1) << 8) | (uint32_t)(tn.alpha_ref & 0xFF);
  if (tn.dither) s[S_CONTROL] |= CT_DITHER;
  if (tn.depth) s[S_CONTROL] |= CT_Z_ENABLE | (tn.depth_mask ? CT_Z_WRITE : 0u);
  if (tn.alpha_func != GR_CMP_ALWAYS) s[S_CONTROL] |= CT_ALPHA_ENABLE;
  if (textured) {
    switch (tn.color_combine) {
      case COMBINE_MODULATE: mode = BL_MODULATEALPHA; break;
      case COMBINE_DECAL: mode = BL_DECALALPHA; break;
      default: mode = BL_DECAL; break;
    }
    s[S_OFFSET] = tn.tex_addr;
    s[S_FORMAT] = FMT_DMA_A | FMT_ORIGIN_CORNER | (tn.tex_alpha ? FMT_A4R4G4B4 : FMT_R5G6B5) | FMT_ONE_LEVEL |
                  ((uint32_t)tn.tex_wlog << 16) | ((uint32_t)tn.tex_hlog << 20) | tn.tex_wrap;
    s[S_FILTER] = 0x1010u | (tn.tex_linear ? 0x22000000u : 0x11000000u);
  } else {
    /* The white texel, modulated by the vertex color. */
    mode = BL_MODULATEALPHA;
    s[S_OFFSET] = tn.white;
    s[S_FORMAT] = FMT_DMA_A | FMT_ORIGIN_CORNER | FMT_R5G6B5 | FMT_ONE_LEVEL | (FMT_REPEAT << 24) |
                  (FMT_REPEAT << 28);
    s[S_FILTER] = 0x11001010u;
  }
  s[S_BLEND] = mode | BL_MASK_MSB | BL_GOURAUD | BL_PERSPECTIVE | (src << 24) |
               ((uint32_t)blend_dst_code[tn.blend_dst & 7] << 28) | (blend ? BL_BLEND : 0u) |
               (tn.fog ? BL_FOG : 0u);
}

/* Writes the state methods of the triangle when one of them changes. A
 * change of the clip window waits until the card is idle. */
static void write_state(int textured) {
  uint32_t s[S_COUNT];
  int i;
  if (tn.clip_dirty) {
    idle();
    write_clip();
  }
  triangle_state(textured, s);
  if (tn.known && !memcmp(s, tn.last, sizeof s)) return;
  fifo(S_COUNT);
  for (i = 0; i < S_COUNT; i++) method(SUB_TRIANGLE, M_TT_COLORKEY + 4u * (uint32_t)i, s[i]);
  memcpy(tn.last, s, sizeof s);
  tn.known = 1;
}

/* Writes the vertex v to the vertex slot slot of the triangle object:
 * x, y, z (0 to 1), 1/w, the color (A R G B), the specular color with
 * the fog factor in its alpha, and u and v (0 to 1 across the texture).
 * The card divides u and v by 1/w itself. */
static void vertex(int slot, const sgl_gr_vertex *v, int textured) {
  uint32_t m = M_TT_TLVERTEX + 0x20u * (uint32_t)slot;
  uint32_t vis = tn.fog ? byte(fog_visibility(v->oow)) : 255u;
  float w = v->oow > 0.0f ? v->oow : 1e-6f;
  method(SUB_TRIANGLE, m + 0x00, float_bits(v->x));
  method(SUB_TRIANGLE, m + 0x04, float_bits(screen_y(v->y)));
  method(SUB_TRIANGLE, m + 0x08, float_bits(clampf(v->ooz, 0.0f, 65535.0f) / 65535.0f));
  method(SUB_TRIANGLE, m + 0x0C, float_bits(w));
  method(SUB_TRIANGLE, m + 0x10, (byte(v->a) << 24) | (byte(v->r) << 16) | (byte(v->g) << 8) | byte(v->b));
  method(SUB_TRIANGLE, m + 0x14, vis << 24);
  method(SUB_TRIANGLE, m + 0x18, float_bits(textured ? v->tmu[0].sow / w * tn.tex_ss : 0.0f));
  method(SUB_TRIANGLE, m + 0x1C, float_bits(textured ? v->tmu[0].tow / w * tn.tex_ts : 0.0f));
}

/* grDrawTriangle: writes the state, the three vertices to the slots 0,
 * 1 and 2, and a DrawPrimitive method with the slots. */
static void SGL_GLIDE_CALL gr_draw_triangle(const sgl_gr_vertex *a, const sgl_gr_vertex *b,
                                            const sgl_gr_vertex *c) {
  const sgl_gr_vertex *g[3];
  int i, textured;
  if (!tn.open || (tn.depth && tn.depth_func == GR_CMP_NEVER) || tn.alpha_func == GR_CMP_NEVER) return;
  g[0] = a, g[1] = b, g[2] = c;
  for (i = 0; i < 3; i++) {
    float x = g[i]->x, y = g[i]->y;
    if (!(x > -2048.0f && x < 2048.0f && y > -2048.0f && y < 2048.0f)) return;
  }
  textured = tn.textured && tn.color_combine != COMBINE_COLOR;
  write_state(textured);
  fifo(3 * 8 + 1);
  for (i = 0; i < 3; i++) vertex(i, g[i], textured);
  method(SUB_TRIANGLE, M_TT_DRAW, 0x210u);
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

/* Fills the clip window of the Z buffer with the 16-bit value value,
 * with the CPU. */
static void fill_depth(uint16_t value) {
  int x, y, top = clip_top();
  idle();
  for (y = 0; y < tn.clip[3] - tn.clip[1]; y++) {
    volatile uint16_t *p = (volatile uint16_t *)(tn.lfb + tn.zbuffer + (unsigned long)(top + y) * tn.stride);
    for (x = tn.clip[0]; x < tn.clip[2]; x++) p[x] = value;
  }
}

/* grBufferClear: fills the clip window of the back buffer with color,
 * and of the Z buffer with depth when the depth writes are on. The
 * card draws a quad of the color over the clip window, with the Z
 * compare ALWAYS and the Z writes of depth. Without the color writes,
 * the CPU fills the Z buffer. */
static void SGL_GLIDE_CALL gr_buffer_clear(unsigned long color, unsigned alpha, unsigned depth) {
  sgl_gr_vertex q[4];
  int save_depth = tn.depth, save_func = tn.depth_func, save_mask = tn.depth_mask, save_textured = tn.textured;
  int save_src = tn.blend_src, save_dst = tn.blend_dst, save_alpha = tn.alpha_func, save_fog = tn.fog, i;
  float top;
  if (!tn.open) return;
  if (!tn.color_mask) {
    if (tn.depth && tn.depth_mask) fill_depth((uint16_t)(depth & 0xFFFFu));
    return;
  }
  memset(q, 0, sizeof q);
  top = tn.origin == GR_ORIGIN_LOWER_LEFT ? (float)tn.clip[1] : (float)tn.clip[1];
  q[0].x = q[3].x = (float)tn.clip[0];
  q[1].x = q[2].x = (float)tn.clip[2];
  q[0].y = q[1].y = top;
  q[2].y = q[3].y = (float)tn.clip[3];
  for (i = 0; i < 4; i++) {
    q[i].r = (float)((color >> 16) & 0xFF), q[i].g = (float)((color >> 8) & 0xFF), q[i].b = (float)(color & 0xFF);
    q[i].a = (float)(alpha & 0xFF);
    q[i].ooz = (float)(depth & 0xFFFFu);
    q[i].oow = 1.0f;
  }
  tn.depth = tn.depth && tn.depth_mask;
  tn.depth_func = GR_CMP_ALWAYS;
  tn.textured = 0;
  tn.blend_src = GR_BLEND_ONE, tn.blend_dst = GR_BLEND_ZERO;
  tn.alpha_func = GR_CMP_ALWAYS;
  tn.fog = 0;
  quad(q);
  tn.depth = save_depth, tn.depth_func = save_func, tn.depth_mask = save_mask, tn.textured = save_textured;
  tn.blend_src = save_src, tn.blend_dst = save_dst, tn.alpha_func = save_alpha, tn.fog = save_fog;
}

/* grBufferSwap: shows the back buffer, then draws into the old front
 * buffer. With interval 1 or more, the change waits for the vertical
 * retrace, and the driver waits until the monitor shows the new
 * buffer. */
static void SGL_GLIDE_CALL gr_buffer_swap(int interval) {
  uint32_t start;
  if (!tn.open) return;
  idle();
  sgl_vbe_display_start((int)(tn.buffer[tn.back] / (uint32_t)tn.stride), interval > 0);
  if (interval > 0) {
    start = sgl_dos_ms();
    while ((inportb(0x3DA) & 8) && sgl_dos_ms() - start < 100) {
    }
    while (!(inportb(0x3DA) & 8) && sgl_dos_ms() - start < 100) {
    }
  }
  tn.back ^= 1;
  target();
}

/* grDepthBufferMode: the Z buffer on or off. */
static void SGL_GLIDE_CALL gr_depth_buffer_mode(int mode) { tn.depth = mode != GR_DEPTHBUFFER_DISABLE; }

/* grDepthBufferFunction: the Z compare. */
static void SGL_GLIDE_CALL gr_depth_buffer_function(int func) { tn.depth_func = func; }

/* grDepthMask: the Z writes. */
static void SGL_GLIDE_CALL gr_depth_mask(int on) { tn.depth_mask = on != 0; }

/* grColorMask: the color writes cannot be stopped (see the limits); the
 * driver keeps the value for grBufferClear. */
static void SGL_GLIDE_CALL gr_color_mask(int rgb, int alpha) {
  (void)alpha;
  tn.color_mask = rgb != 0;
}

/* grColorCombine: the combines of glide.c: the vertex color, the
 * texture (GL_REPLACE), the texture times the vertex color
 * (GL_MODULATE), or GL_DECAL. */
static void SGL_GLIDE_CALL gr_color_combine(int func, int factor, int local, int other, int invert) {
  (void)local, (void)invert;
  if (other != GR_COMBINE_OTHER_TEXTURE || func == GR_COMBINE_FUNCTION_LOCAL)
    tn.color_combine = COMBINE_COLOR;
  else if (func == GR_COMBINE_FUNCTION_BLEND)
    tn.color_combine = COMBINE_DECAL;
  else if (func == GR_COMBINE_FUNCTION_SCALE_OTHER && factor == GR_COMBINE_FACTOR_LOCAL)
    tn.color_combine = COMBINE_MODULATE;
  else
    tn.color_combine = COMBINE_REPLACE;
}

/* grAlphaCombine: the texture blend of the card sets the alpha with the
 * color (see triangle_state). */
static void SGL_GLIDE_CALL gr_alpha_combine(int func, int factor, int local, int other, int invert) {
  (void)func, (void)factor, (void)local, (void)other, (void)invert;
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
  tn.blend_src = rgb_src, tn.blend_dst = rgb_dst;
}

/* grAlphaTestFunction: the alpha test compare. */
static void SGL_GLIDE_CALL gr_alpha_test_function(int func) { tn.alpha_func = func; }

/* grAlphaTestReferenceValue: the alpha test reference (0 to 255). */
static void SGL_GLIDE_CALL gr_alpha_test_reference_value(unsigned value) { tn.alpha_ref = (int)value; }

/* grFogMode: the fog table on or off. */
static void SGL_GLIDE_CALL gr_fog_mode(int mode) { tn.fog = mode != GR_FOG_DISABLE; }

/* grFogColorValue: the fog color (0xRRGGBB), as FOGCOLOR keeps it. */
static void SGL_GLIDE_CALL gr_fog_color_value(unsigned long color) { tn.fog_color = (uint32_t)color & 0xFFFFFFu; }

/* grFogTable: keeps the table, and the eye depth of each entry: entry
 * i is at w = 2^(3 + i / 4) / (8 - i % 4), as on a Voodoo. */
static void SGL_GLIDE_CALL gr_fog_table(const unsigned char *table) {
  int i;
  for (i = 0; i < GR_FOG_TABLE_SIZE; i++) {
    tn.fog_table[i] = table[i];
    tn.fog_w[i] = (float)(1ul << (3 + (i >> 2))) / (float)(8 - (i & 3));
  }
}

/* grCullMode: glide.c culls in software. */
static void SGL_GLIDE_CALL gr_cull_mode(int mode) { (void)mode; }

/* grDitherMode: the dither of the 16-bit color on or off. */
static void SGL_GLIDE_CALL gr_dither_mode(int mode) { tn.dither = mode != GR_DITHER_DISABLE; }

/* grTexMinAddress: the textures start after the Z buffer and the white
 * texel. */
static unsigned long SGL_GLIDE_CALL gr_tex_min_address(int tmu) {
  (void)tmu;
  return tn.tex_min;
}

/* grTexMaxAddress: the textures end before the instance memory. */
static unsigned long SGL_GLIDE_CALL gr_tex_max_address(int tmu) {
  (void)tmu;
  return tn.tex_max;
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

/* grTexTextureMemRequired: the bytes of the largest level, the only
 * level that the driver uses, in units of 256 bytes. */
static unsigned long SGL_GLIDE_CALL gr_tex_texture_mem_required(unsigned long even_odd, sgl_gr_texinfo *info) {
  int wlog, hlog;
  (void)even_odd;
  tex_size(info, &wlog, &hlog);
  return ((2ul << (wlog + hlog)) + 255ul) & ~255ul;
}

/* Gives the texel index of (x, y) in a swizzled texture of 2^wlog x
 * 2^hlog texels, as the Mesa driver writes a texture: the bits of x and
 * y alternate (x in the even bits) for the square part of 2^k x 2^k
 * texels (k the smaller log); the rest of the larger coordinate is
 * above them. */
static unsigned long swizzle(unsigned x, unsigned y, int wlog, int hlog) {
  int k = wlog < hlog ? wlog : hlog, i;
  unsigned long index = 0;
  for (i = 0; i < k; i++) {
    index |= (unsigned long)((x >> i) & 1u) << (2 * i);
    index |= (unsigned long)((y >> i) & 1u) << (2 * i + 1);
  }
  return index | ((unsigned long)(x >> k) << (2 * k)) | ((unsigned long)(y >> k) << (2 * k));
}

/* grTexDownloadMipMap: writes the largest level at start in the
 * swizzled order (16 bits for each texel). RGB 565 and ARGB 4444 are
 * formats of the card. */
static void SGL_GLIDE_CALL gr_tex_download_mip_map(int tmu, unsigned long start, unsigned long even_odd,
                                                   sgl_gr_texinfo *info) {
  const uint16_t *src = (const uint16_t *)info->data;
  int wlog, hlog;
  unsigned x, y;
  (void)tmu, (void)even_odd;
  if (!tn.open || start < tn.tex_min || start + gr_tex_texture_mem_required(0, info) > tn.tex_max) return;
  tex_size(info, &wlog, &hlog);
  idle();
  for (y = 0; y < 1u << hlog; y++)
    for (x = 0; x < 1u << wlog; x++)
      *(volatile uint16_t *)(tn.lfb + start + 2ul * swizzle(x, y, wlog, hlog)) = *src++;
}

/* grTexSource: the texture that the triangles use. In Glide, 256 is the
 * long side of the texture (2^(8 - large_lod) texels). */
static void SGL_GLIDE_CALL gr_tex_source(int tmu, unsigned long start, unsigned long even_odd, sgl_gr_texinfo *info) {
  (void)tmu, (void)even_odd;
  tex_size(info, &tn.tex_wlog, &tn.tex_hlog);
  tn.textured = 1;
  tn.tex_ss = (float)(1 << (8 - info->large_lod)) / ((float)(1 << tn.tex_wlog) * 256.0f);
  tn.tex_ts = (float)(1 << (8 - info->large_lod)) / ((float)(1 << tn.tex_hlog) * 256.0f);
  tn.tex_alpha = info->format == GR_TEXFMT_ARGB_4444;
  tn.tex_addr = (uint32_t)start;
}

/* grTexFilterMode: the bilinear filter when the magnification or the
 * minification asks for it. */
static void SGL_GLIDE_CALL gr_tex_filter_mode(int tmu, int min, int mag) {
  (void)tmu;
  tn.tex_linear = min == GR_TEXTUREFILTER_BILINEAR || mag == GR_TEXTUREFILTER_BILINEAR;
}

/* grTexClampMode: repeat or clamp to the edge, for u (bits 24 to 26 of
 * FORMAT) and for v (bits 28 to 30). */
static void SGL_GLIDE_CALL gr_tex_clamp_mode(int tmu, int s, int t) {
  (void)tmu;
  tn.tex_wrap = ((s == GR_TEXTURECLAMP_CLAMP ? FMT_CLAMP : FMT_REPEAT) << 24) |
                ((t == GR_TEXTURECLAMP_CLAMP ? FMT_CLAMP : FMT_REPEAT) << 28);
}

/* grTexMipMapMode: the driver uses the largest level only. */
static void SGL_GLIDE_CALL gr_tex_mip_map_mode(int tmu, int mode, int lod_blend) {
  (void)tmu, (void)mode, (void)lod_blend;
}

/* Gives the offset of the buffer of a lock: the front buffer, the back
 * buffer or the Z buffer. The rows have the same pitch. */
static uint32_t lock_buffer(int buffer) {
  if (buffer == GR_BUFFER_AUXBUFFER) return tn.zbuffer;
  return tn.buffer[buffer == GR_BUFFER_BACKBUFFER ? tn.back : tn.back ^ 1];
}

/* grLfbLock: gives a copy of the buffer in system memory, in RGB 565
 * (or the Z values of 16 bits), rows in the order of origin. A write
 * lock copies the pixels to the card in grLfbUnlock. */
static int SGL_GLIDE_CALL gr_lfb_lock(int type, int buffer, int write_mode, int origin, int pixel_pipeline,
                                      sgl_gr_lfbinfo *info) {
  int x, y;
  uint32_t base;
  (void)pixel_pipeline;
  if (!tn.open) return 0;
  if (!tn.shadow) tn.shadow = (uint16_t *)malloc((size_t)tn.width * (size_t)tn.height * 2u);
  if (!tn.shadow) return 0;
  idle();
  tn.lock_origin = origin;
  base = lock_buffer(buffer);
  if (type == GR_LFB_READ_ONLY) {
    for (y = 0; y < tn.height; y++) {
      int row = origin == GR_ORIGIN_LOWER_LEFT ? tn.height - 1 - y : y;
      const volatile uint16_t *p = (const volatile uint16_t *)(tn.lfb + base + (unsigned long)row * tn.stride);
      uint16_t *q = tn.shadow + (long)y * tn.width;
      for (x = 0; x < tn.width; x++) q[x] = p[x];
    }
  }
  info->ptr = tn.shadow;
  info->stride = (unsigned long)tn.width * 2ul;
  info->write_mode = write_mode;
  info->origin = origin;
  return 1;
}

/* grLfbUnlock: copies the pixels of a write lock to the card. */
static int SGL_GLIDE_CALL gr_lfb_unlock(int type, int buffer) {
  int x, y;
  uint32_t base;
  if (!tn.open || !tn.shadow || type != GR_LFB_WRITE_ONLY) return 1;
  base = lock_buffer(buffer);
  for (y = 0; y < tn.height; y++) {
    int row = tn.lock_origin == GR_ORIGIN_LOWER_LEFT ? tn.height - 1 - y : y;
    volatile uint16_t *p = (volatile uint16_t *)(tn.lfb + base + (unsigned long)row * tn.stride);
    const uint16_t *q = tn.shadow + (long)y * tn.width;
    for (x = 0; x < tn.width; x++) p[x] = q[x];
  }
  return 1;
}

/* Fills api with the TNT driver (see softgl_glide.h). */
int sgl_glide_tnt(sgl_glide_api *api) {
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
  api->limits = SGL_GLIDE_LIMIT_COLOR_MASK | SGL_GLIDE_LIMIT_ONE_ALPHA;
  return 1;
}

#endif
