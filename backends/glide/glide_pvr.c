/* glide_pvr.c - the Glide 2 functions of the Glide back end, on the
 * VideoLogic and NEC PowerVR PCX2, for 32-bit DOS. sgl_glide_pvr fills
 * an sgl_glide_api, and the Glide back end (glide.c) uses it as it
 * uses a Voodoo. The cards are the Matrox m3D, the VideoLogic
 * Apocalypse 3Dx and 5D, and the other PCX2 cards.
 *
 * The PCX2 is a 3D card without a frame buffer of its own. It draws a
 * full scene at a time, in tiles of 32 x 32 pixels, and writes the
 * pixels to the frame buffer of the VGA card through the PCI bus:
 *
 * - The driver keeps the triangles of the scene in system memory.
 *   Each triangle is one depth plane and three edge planes (the ISP
 *   parameters), and a shading block in texture memory (the TSP
 *   parameters).
 * - When the scene must be on the screen (grBufferSwap, grLfbLock,
 *   grSstIdle), the driver writes the planes and a list of the objects
 *   of each tile into DOS memory, and starts the card. The card reads
 *   the list through its page table (the TLB) by bus mastering.
 * - The card has no Z buffer in memory: it sorts the planes of each
 *   tile itself. A scene that is drawn in two parts loses the depth
 *   of the first part.
 *
 * The card shades a triangle with one color times an intensity. For a
 * triangle with three different colors and no texture, the driver adds
 * one or two translucent passes of the same plane. Each pass has one
 * vertex color, and an alpha ramp texture gives the part of that color.
 * Fog is a translucent pass of the fog color in the same way.
 *
 * The driver sets the limits of the table (SGL_GLIDE_LIMIT_), and
 * softgl then draws in software what the card cannot do: no alpha
 * test, only the blend (source alpha, one minus source alpha), no fog
 * with a blend, no color mask, no clamp, and with a texture or a blend
 * the vertex colors must be one color times an intensity.
 *
 * The screen is a VBE mode of 16-bit color (5:6:5) with the linear
 * frame buffer of the VGA card, and two color buffers.
 *
 * The register facts and the formats of the parameters come from the
 * PowerVR Series 1 driver source of Imagination Technologies (MIT
 * license) and from the PCX2 of the 86Box emulator, where this driver
 * is tested.
 *
 * Part of clang-retro softgl, a software implementation of OpenGL.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#if defined(__MSDOS__) && defined(__i386__)

#include <dos.h>
#include <dpmi.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "glide_api.h"
#include "softgl_dos.h"
#include "softgl_glide.h"

/* The registers of BAR 0 (byte offsets). */
enum {
  R_INTSTATUS = 0x00C,
  R_INTMASK = 0x010,
  R_STARTRENDER = 0x014,
  R_FOGAMOUNT = 0x018,
  R_OBJECT_OFFSET = 0x01C,
  R_PAGE_CTRL = 0x020,
  R_FOGCOL = 0x038,
  R_CAMERA = 0x03C,
  R_PACKMODE = 0x040,
  R_LSTRIDE = 0x048,
  R_SOFADDR = 0x04C,
  R_XCLIP = 0x050,
  R_IEEEFP = 0x060,
  R_BILINEAR = 0x064,
  R_FASTFOG = 0x074,
  R_TLB = 0x400
};

/* INTSTATUS bit 1: the end of the render. A read clears it. */
#define INT_END_OF_RENDER 0x2u

/* The instructions of an ISP plane (the low 4 bits of its fourth
 * word). */
#define ISP_EDGE 2u        /* forw_perp: an edge; outside it, the object is empty */
#define ISP_DEPTH 8u       /* forw_visib_fp: the depth plane that starts an object */
#define ISP_BEGIN_TRANS 15u /* the start of a translucent pass */

/* The words of the tile list. */
#define TILE_REGION 0x40000000u /* a tile: then its objects */
#define TILE_LAST 0x80000000u   /* the last object of the last tile */

/* The bits of TSP word 0. */
#define TSP_TEXTURE 0x80000000u
#define TSP_SMOOTH 0x40000000u
#define TSP_NO_FOG 0x20000000u
#define TSP_TRANS 0x00000400u
#define TSP_EXP_SHIFT 18
#define TSP_GTRANS_SHIFT 13

/* The bits of TSP word 6. */
#define TSP_16BIT 0x40000000u
#define TSP_SIZE_SHIFT 28 /* 0: 32 x 32, 1: 64, 2: 128, 3: 256 */
#define TSP_4444 0x08000000u

/* The value of CAMERA: the scale of the constant terms of the texture
 * planes. */
#define CAMERA_SCALE 1024

/* The tiles: 32 x 32 pixels. */
#define TILE 32
#define MAX_TILES (32 * 32)

/* The scene: at most MAX_REC triangles and passes. */
#define MAX_REC 8192

/* Texture memory (bytes of BAR 1): words 0 to 15 are not used (TSP
 * address 2 is the masking plane), the shading block of the clear
 * rectangle is at word 16, the blocks of the triangles from word 32,
 * then the alpha ramp, then the textures. */
#define TEX_SIZE 0x400000ul
#define TSP_CLEAR 16u
#define TSP_FIRST 32u
#define TSP_WORDS 16u
#define RAMP_ADDR (((TSP_FIRST + TSP_WORDS * MAX_REC) * 4u + 0xFFFu) & ~0xFFFu)
#define TEX_MIN (RAMP_ADDR + 0x1000u)

/* The alpha ramp: texel 4 + 16 a has the alpha a; the texels before
 * are 0 and the texels after are 1. */
#define RAMP_BASE 4.0f
#define RAMP_STEPS 16.0f

/* The ISP parameters (words): the masking plane, the plane of
 * begin_trans, the five planes of the clear rectangle, then the planes
 * of the triangles. */
#define ISP_MASK 0u
#define ISP_TRANS 4u
#define ISP_CLEAR 8u
#define ISP_FIRST 28u

/* The flags of a record. */
#define REC_TRANS 0x01u  /* a translucent pass */
#define REC_CLIP 0x02u   /* four more edges: the clip window */
#define REC_ZWRITE 0x04u /* writes the Glide depth (for a lock of the Z buffer) */
#define REC_BILINEAR 0x08u

/* The color combines of glide.c, as the driver knows them. */
enum { COMBINE_COLOR, COMBINE_REPLACE, COMBINE_MODULATE, COMBINE_DECAL };

/* The alpha combines of glide.c: the vertex alpha, the texture alpha,
 * or their product. */
enum { ALPHA_VERTEX, ALPHA_TEXTURE, ALPHA_PRODUCT };

/* A plane: a x + b y + c, with x and y the integer pixel position
 * (the value is that of the pixel center). */
typedef struct {
  float a, b, c;
} plane;

/* One object of the scene: a triangle or a translucent pass of a
 * triangle. */
typedef struct {
  plane edge[3];       /* the inside is 0 or more */
  plane depth;         /* the ISP depth: more is nearer */
  plane z;             /* the Glide depth (0 to 65535) */
  int16_t x0, y0, x1, y1; /* the tiles: the pixels of the bounding box (inclusive) */
  int16_t clip[4];     /* the clip window: x0, y0, x1, y1 (the maxima are not in it) */
  uint8_t flags, ntsp;
  uint32_t tsp[12];
} rec;

/* The state of the driver. */
static struct {
  sgl_pci_device pci;
  volatile uint8_t *regs; /* BAR 0 */
  volatile uint8_t *tex;  /* BAR 1: texture memory */
  volatile uint8_t *lfb;  /* the frame buffer of the VGA card */
  uint32_t lfb_phys, lfb_size;
  uint32_t *isp;          /* the ISP parameters, in DOS memory */
  uint32_t isp_phys, isp_words;
  int open, width, height, pitch, origin; /* pitch in bytes */
  int back, front;
  int busy;               /* the card draws */
  /* The scene. */
  rec *recs;
  int nrec, first, zfirst; /* records; the first not drawn; the first after the last Z clear */
  uint32_t isp_need;       /* the ISP words that the scene needs */
  int has_clear, bilinear;
  uint32_t clear_color;
  int clear_rect[4];
  float order;             /* the ISP depth of the next triangle without the depth test */
  uint16_t clear_z;
  uint16_t *cnt;           /* the objects of each tile */
  uint32_t *cursor;
  uint16_t *zbuf;          /* the Z buffer of a lock */
  uint16_t *shadow;        /* the copy of a locked color buffer */
  int lock_origin;
  /* The state that Glide sets. */
  int depth, depth_func, depth_mask, color_combine, alpha_combine, dither, fog;
  int blend_src, blend_dst;
  uint8_t fog_table[GR_FOG_TABLE_SIZE];
  float fog_w[GR_FOG_TABLE_SIZE];
  uint32_t fog_color;
  int clip[4]; /* minx, miny, maxx, maxy (Glide coordinates) */
  /* The texture that TexSource selected. */
  int textured, tex_n, tex_4444, tex_bilinear;
  uint32_t tex_addr;           /* in pixels (16 bits) */
  float tex_sscale, tex_tscale; /* Glide coordinates to texels */
} pv;

/* The PCI ID of the PCX2 (NEC). */
static const sgl_pci_id pvr_ids[] = {{0x1033, 0x0046}, {0, 0}};

/* Writes a register of BAR 0. */
static void wr(uint32_t reg, uint32_t value) { *(volatile uint32_t *)(pv.regs + reg) = value; }

/* Reads a register of BAR 0. */
static uint32_t rd(uint32_t reg) { return *(volatile uint32_t *)(pv.regs + reg); }

/* Writes word i (32 bits) of texture memory. */
static void tex_wr(uint32_t i, uint32_t value) { *(volatile uint32_t *)(pv.tex + i * 4u) = value; }

/* Gives the 32 bits of the IEEE single float f. */
static uint32_t float_bits(float f) {
  uint32_t u;
  memcpy(&u, &f, sizeof u);
  return u;
}

/* Gives v limited to lo to hi. */
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

/* Gives the y of a Glide vertex with the origin at the top. */
static float screen_y(float y) { return pv.origin == GR_ORIGIN_LOWER_LEFT ? (float)pv.height - y : y; }

/* Waits until the card has drawn the scene that it was given. */
static void wait_render(void) {
  uint32_t start;
  if (!pv.busy) return;
  start = sgl_dos_ms();
  while (!(rd(R_INTSTATUS) & INT_END_OF_RENDER) && sgl_dos_ms() - start < 2000u) {
  }
  pv.busy = 0;
}

/* grGlideInit: finds the card, maps its registers and its texture
 * memory, and turns on its bus mastering. */
static void SGL_GLIDE_CALL gr_glide_init(void) {
  uint32_t bar0, bar1;
  memset(&pv, 0, sizeof pv);
  if (!sgl_pci_find(pvr_ids, 0, &pv.pci)) return;
  bar0 = sgl_pci_bar(&pv.pci, 0);
  bar1 = sgl_pci_bar(&pv.pci, 1);
  if (!bar0 || !bar1) return;
  /* Memory space and bus mastering. */
  sgl_pci_write16(&pv.pci, 0x04, (uint16_t)(sgl_pci_read16(&pv.pci, 0x04) | 0x0006));
  if (!sgl_pci_read8(&pv.pci, 0x0D)) sgl_pci_write8(&pv.pci, 0x0D, 0x40);
  pv.regs = (volatile uint8_t *)sgl_map_physical(bar0, 0x1000ul);
  pv.tex = (volatile uint8_t *)sgl_map_physical(bar1, TEX_SIZE);
  if (!pv.regs || !pv.tex) {
    if (pv.regs) sgl_unmap_physical(pv.regs);
    if (pv.tex) sgl_unmap_physical(pv.tex);
    pv.regs = pv.tex = NULL;
  }
}

/* grGlideShutdown: closes the screen and frees the mappings. */
static void SGL_GLIDE_CALL gr_glide_shutdown(void);

/* grSstQueryHardware: one board with one TMU, as a Voodoo Graphics
 * (GrHwConfiguration as 32-bit words). */
static int SGL_GLIDE_CALL gr_sst_query_hardware(void *config) {
  int32_t *hw = (int32_t *)config;
  if (!pv.regs) return 0;
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

/* Gives DOS memory of up to want bytes for the ISP parameters, aligned
 * to 4 KB, with its physical address (in the flat model, the linear
 * address of DOS memory is its physical address). Gives the size in
 * bytes, or 0. */
static uint32_t isp_memory(uint32_t want) {
  int selector;
  for (; want >= 0x10000ul; want /= 2) {
    int seg = __dpmi_allocate_dos_memory((int)((want + 0x1000ul) >> 4), &selector);
    if (seg < 0) continue;
    pv.isp_phys = (((uint32_t)seg << 4) + 0xFFFu) & ~0xFFFu;
    pv.isp = (uint32_t *)(uintptr_t)pv.isp_phys;
    return want;
  }
  return 0;
}

/* Writes the alpha ramp: a 32 x 32 ARGB 4444 texture of white texels.
 * The alpha goes up with the column (see RAMP_BASE); the card keeps
 * the transparency, 15 minus the alpha. */
static void write_ramp(void) {
  uint32_t i;
  for (i = 0; i < 512; i++) {
    /* Word i has two texels with the same column: the column is the
     * odd bits of the pixel address 2 i. */
    uint32_t addr = 2 * i, u = 0, b;
    int j, t;
    for (b = 0; b < 5; b++) u |= ((addr >> (2 * b + 1)) & 1u) << b;
    j = (int)u - (int)RAMP_BASE;
    t = j <= 0 ? 15 : j >= 16 ? 0 : 16 - j;
    if (t > 15) t = 15;
    tex_wr(RAMP_ADDR / 4u + i, (((uint32_t)t << 12 | 0xFFFu) << 16) | ((uint32_t)t << 12 | 0xFFFu));
  }
}

/* grSstWinOpen: sets the VBE mode, maps the frame buffer of the VGA
 * card, gets the memory of the scene and sets the card. 16-bit color
 * only; the other arguments are not used. */
static int SGL_GLIDE_CALL gr_sst_win_open(unsigned long hwnd, int resolution, int refresh, int color_format,
                                          int origin, int color_buffers, int aux_buffers) {
  sgl_vbe_mode info;
  uint32_t frame, bytes, i;
  int mode, w, h;
  (void)hwnd, (void)refresh, (void)color_format, (void)color_buffers, (void)aux_buffers;
  if (!pv.regs || pv.open) return 0;
  mode = vbe_mode(resolution, &w, &h);
  if (mode < 0 || w > 1024 || h > 1024) return 0;
  if (!sgl_vbe_mode_info(mode, &info) || !(info.attributes & 1) || !(info.attributes & 0x80) || info.bits != 16 ||
      !info.lfb)
    return 0;
  frame = (uint32_t)info.stride * (uint32_t)h;
  if (info.stride > 0x1FFCu) return 0;
  pv.recs = (rec *)malloc(sizeof(rec) * MAX_REC);
  pv.cnt = (uint16_t *)malloc(sizeof(uint16_t) * MAX_TILES);
  pv.cursor = (uint32_t *)malloc(sizeof(uint32_t) * MAX_TILES);
  bytes = isp_memory(0x60000ul);
  if (!pv.recs || !pv.cnt || !pv.cursor || !bytes || !sgl_vbe_set_mode(mode, 1)) {
    free(pv.recs), free(pv.cnt), free(pv.cursor);
    pv.recs = NULL;
    return 0;
  }
  pv.lfb_phys = info.lfb;
  pv.lfb_size = 2 * frame;
  pv.lfb = (volatile uint8_t *)sgl_map_physical(pv.lfb_phys, pv.lfb_size);
  if (!pv.lfb) {
    sgl_vbe_text_mode();
    free(pv.recs), free(pv.cnt), free(pv.cursor);
    pv.recs = NULL;
    return 0;
  }
  pv.isp_words = bytes / 4u;
  pv.width = w, pv.height = h, pv.pitch = info.stride, pv.origin = origin;
  pv.front = 0, pv.back = 1;
  pv.clip[0] = 0, pv.clip[1] = 0, pv.clip[2] = w, pv.clip[3] = h;
  pv.depth_func = GR_CMP_LESS;
  pv.depth_mask = 1;
  pv.dither = 1;
  pv.blend_src = GR_BLEND_ONE, pv.blend_dst = GR_BLEND_ZERO;
  pv.clear_z = 0xFFFF;
  pv.order = 0.5f;
  pv.open = 1;
  /* The page table: the ISP parameters in pages of 4 KB. All the pages
   * are in one window of 16 MB (PAGE_CTRL has the top 8 bits). */
  wr(R_INTMASK, 0);
  wr(R_PAGE_CTRL, (pv.isp_phys >> 24) & 0xFFu);
  for (i = 0; i < bytes / 0x1000u && i < 128; i++) wr(R_TLB + i * 4u, ((pv.isp_phys >> 12) + i) & 0xFFFu);
  wr(R_IEEEFP, 7u); /* IEEE floats, scaled edges, pointer concatenation */
  wr(R_CAMERA, CAMERA_SCALE);
  wr(R_FOGCOL, 0);
  wr(R_FOGAMOUNT, 0);
  wr(R_FASTFOG, 0);
  wr(R_LSTRIDE, (uint32_t)pv.pitch);
  write_ramp();
  (void)rd(R_INTSTATUS);
  sgl_vbe_display_start(0, 0);
  return 1;
}

/* grSstWinClose: sets the text mode again and frees the memory. */
static void SGL_GLIDE_CALL gr_sst_win_close(void) {
  if (!pv.open) return;
  wait_render();
  sgl_vbe_text_mode();
  sgl_unmap_physical(pv.lfb);
  pv.lfb = NULL;
  free(pv.recs), free(pv.cnt), free(pv.cursor), free(pv.zbuf), free(pv.shadow);
  pv.recs = NULL, pv.cnt = NULL, pv.cursor = NULL, pv.zbuf = NULL, pv.shadow = NULL;
  pv.open = 0;
}

/* grGlideShutdown: closes the screen and frees the mappings. */
static void SGL_GLIDE_CALL gr_glide_shutdown(void) {
  gr_sst_win_close();
  if (pv.regs) sgl_unmap_physical(pv.regs);
  if (pv.tex) sgl_unmap_physical(pv.tex);
  memset(&pv, 0, sizeof pv);
}

/* Writes plane p as ISP plane words at w: a, b and c as floats, then
 * the TSP index and the instruction. */
static void isp_plane(uint32_t *w, const plane *p, uint32_t index, uint32_t instr) {
  w[0] = float_bits(p->a);
  w[1] = float_bits(p->b);
  w[2] = float_bits(p->c);
  w[3] = (index << 4) | instr;
}

/* Gives the edge plane of the pixel centers right of x (left 1) or
 * left of x (left 0), or below y or above y (vertical 1). */
static plane side(int v, int left, int vertical) {
  plane p;
  float s = left ? 1.0f : -1.0f;
  p.a = vertical ? 0.0f : s;
  p.b = vertical ? s : 0.0f;
  p.c = s * (0.5f - (float)v);
  return p;
}

/* Writes the four edges of a clip rectangle r (x0, y0, x1, y1) at w. */
static void isp_rect(uint32_t *w, const int16_t *r) {
  plane p;
  p = side(r[0], 1, 0), isp_plane(w, &p, 0, ISP_EDGE);
  p = side(r[2], 0, 0), isp_plane(w + 4, &p, 0, ISP_EDGE);
  p = side(r[1], 1, 1), isp_plane(w + 8, &p, 0, ISP_EDGE);
  p = side(r[3], 0, 1), isp_plane(w + 12, &p, 0, ISP_EDGE);
}

/* Gives the object word of count planes at the ISP word off. */
static uint32_t object(uint32_t off, uint32_t count) { return (count << 19) | off; }

/* Gives the number of tiles across and down. */
static int tiles_x(void) { return (pv.width + TILE - 1) / TILE; }
static int tiles_y(void) { return (pv.height + TILE - 1) / TILE; }

/* Draws the scene into the back buffer: writes the shading blocks to
 * texture memory, the planes and the tile list to the ISP memory, and
 * starts the card. The scene is then empty; the next scene shows the
 * pixels of the frame buffer where it draws nothing. */
static void render(void) {
  int n = pv.nrec - pv.first, nx = tiles_x(), ny = tiles_y(), i, tx, ty, last = -1;
  uint32_t *isp = pv.isp, w = ISP_FIRST, pos, list;
  int16_t crect[4];
  if (!pv.open || (n == 0 && !pv.has_clear)) return;
  wait_render();
  /* The masking plane (TSP address 2: the pixel is not written) and
   * the plane of begin_trans. */
  {
    plane z = {0.0f, 0.0f, 0.0f};
    isp_plane(isp + ISP_MASK, &z, 1, ISP_DEPTH);
    isp_plane(isp + ISP_TRANS, &z, 0, ISP_BEGIN_TRANS);
    if (pv.has_clear) {
      uint32_t c = pv.clear_color;
      tex_wr(TSP_CLEAR, TSP_NO_FOG | ((c >> 16) & 0xFFu));
      tex_wr(TSP_CLEAR + 1, ((c & 0xFF00u) << 16) | ((c & 0xFFu) << 16));
      isp_plane(isp + ISP_CLEAR, &z, TSP_CLEAR >> 1, ISP_DEPTH);
      for (i = 0; i < 4; i++) crect[i] = (int16_t)pv.clear_rect[i];
      isp_rect(isp + ISP_CLEAR + 4, crect);
    }
  }
  /* The shading blocks and the planes of the triangles. */
  for (i = 0; i < n; i++) {
    rec *r = &pv.recs[pv.first + i];
    uint32_t t = TSP_FIRST + TSP_WORDS * (uint32_t)i, k;
    for (k = 0; k < r->ntsp; k++) tex_wr(t + k, r->tsp[k]);
    isp_plane(isp + w, &r->depth, t >> 1, ISP_DEPTH);
    for (k = 0; k < 3; k++) isp_plane(isp + w + 4 + 4 * k, &r->edge[k], 0, ISP_EDGE);
    w += 16;
    if (r->flags & REC_CLIP) isp_rect(isp + w, r->clip), w += 16;
  }
  list = w;
  /* The number of objects of each tile. */
  memset(pv.cnt, 0, sizeof(uint16_t) * (size_t)(nx * ny));
  for (i = 0; i < n; i++) {
    const rec *r = &pv.recs[pv.first + i];
    int add = (r->flags & REC_TRANS) ? 2 : 1;
    for (ty = r->y0 / TILE; ty <= r->y1 / TILE; ty++)
      for (tx = r->x0 / TILE; tx <= r->x1 / TILE; tx++) pv.cnt[ty * nx + tx] += (uint16_t)add;
  }
  /* The tile list: the region word, the masking plane, the clear
   * rectangle, the opaque objects, then the translucent passes. */
  pos = list;
  for (ty = 0; ty < ny; ty++)
    for (tx = 0; tx < nx; tx++) {
      int t = ty * nx + tx, cl = 0, rows;
      if (pv.has_clear && tx * TILE < pv.clear_rect[2] && (tx + 1) * TILE > pv.clear_rect[0] &&
          ty * TILE < pv.clear_rect[3] && (ty + 1) * TILE > pv.clear_rect[1])
        cl = 1;
      if (!pv.cnt[t] && !cl) {
        pv.cursor[t] = 0;
        continue;
      }
      rows = pv.height - ty * TILE < TILE ? pv.height - ty * TILE : TILE;
      isp[pos] = TILE_REGION | ((uint32_t)(ty * TILE) << 20) | ((uint32_t)tx << 15) | ((uint32_t)(rows - 1) << 5);
      isp[pos + 1] = object(ISP_MASK, 1);
      if (cl) isp[pos + 2] = object(ISP_CLEAR, 5);
      pv.cursor[t] = pos + 2 + (uint32_t)cl;
      pos += 2 + (uint32_t)cl + pv.cnt[t];
      last = t;
    }
  if (last < 0) {
    pv.first = pv.nrec;
    pv.has_clear = 0, pv.isp_need = 0, pv.bilinear = 0;
    return;
  }
  for (w = ISP_FIRST, i = 0; i < n; i++) {
    const rec *r = &pv.recs[pv.first + i];
    uint32_t np = (r->flags & REC_CLIP) ? 8u : 4u;
    if (!(r->flags & REC_TRANS))
      for (ty = r->y0 / TILE; ty <= r->y1 / TILE; ty++)
        for (tx = r->x0 / TILE; tx <= r->x1 / TILE; tx++) isp[pv.cursor[ty * nx + tx]++] = object(w, np);
    w += 4 * np;
  }
  for (w = ISP_FIRST, i = 0; i < n; i++) {
    const rec *r = &pv.recs[pv.first + i];
    uint32_t np = (r->flags & REC_CLIP) ? 8u : 4u;
    if (r->flags & REC_TRANS)
      for (ty = r->y0 / TILE; ty <= r->y1 / TILE; ty++)
        for (tx = r->x0 / TILE; tx <= r->x1 / TILE; tx++) {
          uint32_t *c = &pv.cursor[ty * nx + tx];
          isp[(*c)++] = object(ISP_TRANS, 1);
          isp[(*c)++] = object(w, np);
        }
    w += 4 * np;
  }
  isp[pos - 1] |= TILE_LAST;
  /* The output: the back buffer, 5:6:5. The right clip when the last
   * column of tiles is not full. */
  wr(R_SOFADDR, pv.lfb_phys + (uint32_t)pv.back * (uint32_t)pv.pitch * (uint32_t)pv.height);
  wr(R_LSTRIDE, (uint32_t)pv.pitch);
  wr(R_PACKMODE, 2u | (pv.dither ? 0x10u : 0u));
  wr(R_XCLIP, pv.width % TILE ? (1u << 28) | ((uint32_t)pv.width << 16) : 0u);
  wr(R_BILINEAR, pv.bilinear ? 0u : 3u);
  wr(R_OBJECT_OFFSET, list * 4u);
  (void)rd(R_INTSTATUS);
  wr(R_STARTRENDER, 1);
  pv.busy = 1;
  pv.first = pv.nrec;
  pv.has_clear = 0, pv.isp_need = 0, pv.bilinear = 0;
}

/* grSstIdle: draws the scene and waits until the card has drawn it. */
static void SGL_GLIDE_CALL gr_sst_idle(void) {
  render();
  wait_render();
}

/* grClipWindow: the clip rectangle (the maxima are not in it). */
static void SGL_GLIDE_CALL gr_clip_window(unsigned long minx, unsigned long miny, unsigned long maxx,
                                          unsigned long maxy) {
  pv.clip[0] = (int)minx, pv.clip[1] = (int)miny, pv.clip[2] = (int)maxx, pv.clip[3] = (int)maxy;
}

/* Gives the clip window in screen rows (the origin at the top) in r. */
static void clip_rect(int *r) {
  int top = pv.origin == GR_ORIGIN_LOWER_LEFT ? pv.height - pv.clip[3] : pv.clip[1];
  r[0] = pv.clip[0] < 0 ? 0 : pv.clip[0];
  r[1] = top < 0 ? 0 : top;
  r[2] = pv.clip[2] > pv.width ? pv.width : pv.clip[2];
  r[3] = top + (pv.clip[3] - pv.clip[1]);
  if (r[3] > pv.height) r[3] = pv.height;
}

/* grBufferClear: the clear is a rectangle of the clear color behind
 * all the triangles. A clear of the color and the depth of all the
 * screen removes the scene; another clear draws the scene first. */
static void SGL_GLIDE_CALL gr_buffer_clear(unsigned long color, unsigned alpha, unsigned depth) {
  int r[4], full, zclear = pv.depth && pv.depth_mask;
  (void)alpha;
  if (!pv.open) return;
  clip_rect(r);
  if (r[2] <= r[0] || r[3] <= r[1]) return;
  full = r[0] == 0 && r[1] == 0 && r[2] == pv.width && r[3] == pv.height;
  if (full && zclear) {
    pv.nrec = pv.first;
    pv.isp_need = 0, pv.has_clear = 0, pv.bilinear = 0;
  } else if (pv.nrec > pv.first || pv.has_clear) {
    render();
  }
  if (zclear) {
    pv.zfirst = pv.nrec;
    pv.clear_z = (uint16_t)depth;
  }
  pv.has_clear = 1;
  pv.clear_color = (uint32_t)color & 0xFFFFFFu;
  memcpy(pv.clear_rect, r, sizeof r);
  pv.order = 0.5f;
}

/* Waits for the start of the next vertical retrace (bit 3 of port
 * 3DAh). If the retrace is active, it waits for its end first. */
static void next_frame(void) {
  uint32_t start = sgl_dos_ms();
  while ((inportb(0x3DA) & 8) && sgl_dos_ms() - start < 100) {
  }
  while (!(inportb(0x3DA) & 8) && sgl_dos_ms() - start < 100) {
  }
}

/* grBufferSwap: draws the scene and shows the back buffer. With
 * interval 1 or more, the change occurs in the vertical retrace. Some
 * VBE BIOSes do not wait for the retrace, thus the driver waits for it
 * before it sets the display start. The monitor shows the new buffer
 * from the next frame, thus the old front buffer is the next back
 * buffer. */
static void SGL_GLIDE_CALL gr_buffer_swap(int interval) {
  if (!pv.open) return;
  render();
  wait_render();
  if (interval > 0) next_frame();
  sgl_vbe_display_start(pv.back * pv.height, 0);
  pv.front = pv.back;
  pv.back = 1 - pv.back;
  pv.order = 0.5f;
}

/* Gives the fog (0 is no fog, 1 is all fog) of a vertex from the Glide
 * fog table, by the eye depth w = 1 / oow. */
static float fog_amount(float oow) {
  float w = oow > 0.0f ? 1.0f / oow : 1e30f, f;
  int i;
  if (w <= pv.fog_w[0]) return pv.fog_table[0] / 255.0f;
  for (i = 1; i < GR_FOG_TABLE_SIZE; i++)
    if (w < pv.fog_w[i]) break;
  if (i == GR_FOG_TABLE_SIZE) return pv.fog_table[GR_FOG_TABLE_SIZE - 1] / 255.0f;
  f = (w - pv.fog_w[i - 1]) / (pv.fog_w[i] - pv.fog_w[i - 1]);
  return ((float)pv.fog_table[i - 1] + f * ((float)pv.fog_table[i] - (float)pv.fog_table[i - 1])) / 255.0f;
}

/* A triangle on the screen: the positions (y from the top) and the
 * inverse of twice the signed area. */
typedef struct {
  double x[3], y[3], inv;
} tri;

/* Gives the plane of the values f at the vertices of t, for the
 * pixel centers. */
static plane fit(const tri *t, const double *f) {
  plane p;
  double a = ((f[1] - f[0]) * (t->y[2] - t->y[0]) - (f[2] - f[0]) * (t->y[1] - t->y[0])) * t->inv;
  double b = ((f[2] - f[0]) * (t->x[1] - t->x[0]) - (f[1] - f[0]) * (t->x[2] - t->x[0])) * t->inv;
  double c = f[0] - a * t->x[0] - b * t->y[0];
  p.a = (float)a, p.b = (float)b, p.c = (float)(c + 0.5 * (a + b));
  return p;
}

/* Gives the 16-bit integer of v, limited. */
static uint32_t i16(double v) {
  long n = lrint(v);
  if (n > 32767) n = 32767;
  if (n < -32768) n = -32768;
  return (uint32_t)n & 0xFFFFu;
}

/* Fills the TSP words 2 to 7 of a texture with the planes of s, t and
 * w (u = s / w and v = t / w in texels), and gives the exponent of
 * word 0. Each plane is three 16-bit integers; the constant term is
 * multiplied by CAMERA. */
static uint32_t texture_words(const plane *s, const plane *t, const plane *w, uint32_t *tsp) {
  double ms = 0, mw = 0, f1, f2;
  int e;
  const plane *p[2] = {s, t};
  int k;
  for (k = 0; k < 2; k++) {
    double m = fabs(p[k]->a) > fabs(p[k]->b) ? fabs(p[k]->a) : fabs(p[k]->b);
    double c = fabs(p[k]->c) / CAMERA_SCALE;
    if (m > ms) ms = m;
    if (c > ms) ms = c;
  }
  mw = fabs(w->a) > fabs(w->b) ? fabs(w->a) : fabs(w->b);
  if (fabs(w->c) / CAMERA_SCALE > mw) mw = fabs(w->c) / CAMERA_SCALE;
  if (mw <= 0) mw = 1e-30;
  if (ms <= 0) ms = 1e-30;
  f2 = 32000.0 / mw;
  /* u = 2^e (s f1) / (w f2): f1 = f2 2^-e, as large as allowed. */
  e = (int)ceil(log2(f2 * ms / 32000.0));
  if (e < 0) e = 0;
  if (e > 15) {
    e = 15;
    f2 = 32000.0 / ms * 32768.0;
  }
  f1 = f2 / (double)(1L << e);
  tsp[2] = i16(w->c * f2 / CAMERA_SCALE);
  tsp[3] = (i16(w->b * f2) << 16) | i16(w->a * f2);
  tsp[4] = ((pv.tex_addr & 0xFFFFu) << 16) | i16(s->c * f1 / CAMERA_SCALE);
  tsp[5] = (i16(s->b * f1) << 16) | i16(s->a * f1);
  tsp[6] = TSP_16BIT | ((uint32_t)(pv.tex_n - 5) << TSP_SIZE_SHIFT) | (pv.tex_4444 ? TSP_4444 : 0u) |
           (pv.tex_addr & 0xFF0000u) | i16(t->c * f1 / CAMERA_SCALE);
  tsp[7] = (i16(t->b * f1) << 16) | i16(t->a * f1);
  return (uint32_t)e << TSP_EXP_SHIFT;
}

/* Fills the two smooth shading words of the intensity plane k (0 to 1)
 * and the 5:5:5 color c, for word 1 at (xr, yr). */
static void smooth_words(const plane *k, uint32_t c555, int xr, int yr, uint32_t *s) {
  double base = (k->a * xr + k->b * yr + k->c) * 65536.0 / 4.0;
  s[0] = (c555 << 16) | i16(base);
  s[1] = (i16(k->b * 65536.0) << 16) | i16(k->a * 65536.0);
}

/* Gives the 5:5:5 color of 8-bit channels. */
static uint32_t c555(double r, double g, double b) {
  return ((uint32_t)lrint(clampf((float)r, 0, 31)) << 10) | ((uint32_t)lrint(clampf((float)g, 0, 31)) << 5) |
         (uint32_t)lrint(clampf((float)b, 0, 31));
}

/* Adds record r to the scene. When the scene is full, the driver draws
 * it first. */
static void add(const rec *r) {
  int nx = (r->x1 / TILE - r->x0 / TILE + 1) * (r->y1 / TILE - r->y0 / TILE + 1);
  uint32_t need = ((r->flags & REC_CLIP) ? 32u : 16u) + (uint32_t)nx * ((r->flags & REC_TRANS) ? 2u : 1u);
  uint32_t fixed = ISP_FIRST + 3u * (uint32_t)(tiles_x() * tiles_y()) + 16u;
  if (pv.nrec - pv.first >= MAX_REC || pv.isp_need + need + fixed > pv.isp_words) render();
  if (pv.nrec >= MAX_REC) {
    /* The records that are drawn are kept for a lock of the Z buffer
     * only: remove them. */
    wait_render();
    pv.nrec = pv.first = pv.zfirst = 0;
  }
  pv.recs[pv.nrec++] = *r;
  pv.isp_need += need;
  if (r->flags & REC_BILINEAR) pv.bilinear = 1;
}

/* Gives 1 when the colors c[3][3] (0 to 255) are one color times an
 * intensity, within a small tolerance. */
static int one_hue(const double c[3][3]) {
  double s[3] = {0, 0, 0}, st;
  int i, k;
  for (i = 0; i < 3; i++)
    for (k = 0; k < 3; k++) s[k] += c[i][k];
  st = s[0] + s[1] + s[2];
  if (st <= 0) return 1;
  for (i = 0; i < 3; i++) {
    double vi = c[i][0] + c[i][1] + c[i][2];
    for (k = 0; k < 3; k++)
      if (fabs(c[i][k] - s[k] * vi / st) > 6.0) return 0;
  }
  return 1;
}

/* Gives the ISP depth plane of the Glide depth plane z: 1 / 8 at depth
 * 0 to 0 at 65536, more is nearer. A steep plane whose value at the
 * pixel (0, 0) is out of range is made less steep. Without the depth
 * test, the depth is a constant above all others, and grows with each
 * triangle. */
static plane isp_depth(const plane *z, const tri *t) {
  plane d;
  double xc = (t->x[0] + t->x[1] + t->x[2]) / 3.0, yc = (t->y[0] + t->y[1] + t->y[2]) / 3.0, dc, c;
  if (!pv.depth || pv.depth_func == GR_CMP_ALWAYS) {
    d.a = d.b = 0.0f;
    d.c = pv.order;
    pv.order += 1.0f / (1 << 20);
    if (pv.order > 0.99f) pv.order = 0.99f;
    return d;
  }
  d.a = (float)(-z->a / (65536.0 * 8.0));
  d.b = (float)(-z->b / (65536.0 * 8.0));
  dc = 0.125 - (z->a * xc + z->b * yc + z->c) / (65536.0 * 8.0);
  c = dc - d.a * xc - d.b * yc;
  if (fabs(c) > 0.99) {
    double k = (dc - (c > 0 ? 0.99 : -0.99)) / (dc - c);
    d.a = (float)(d.a * k), d.b = (float)(d.b * k);
    c = dc - d.a * xc - d.b * yc;
  }
  d.c = (float)c;
  return d;
}

/* Sets the clip edges of r when the triangle crosses the clip window
 * cr. Gives 0 when the bounding box is outside the window. */
static int clip_box(rec *r, const int *cr) {
  if (r->x0 < cr[0] || r->y0 < cr[1] || r->x1 >= cr[2] || r->y1 >= cr[3]) {
    int k;
    r->flags |= REC_CLIP;
    for (k = 0; k < 4; k++) r->clip[k] = (int16_t)cr[k];
    if (r->x0 < cr[0]) r->x0 = (int16_t)cr[0];
    if (r->y0 < cr[1]) r->y0 = (int16_t)cr[1];
    if (r->x1 >= cr[2]) r->x1 = (int16_t)(cr[2] - 1);
    if (r->y1 >= cr[3]) r->y1 = (int16_t)(cr[3] - 1);
  }
  return r->x0 <= r->x1 && r->y0 <= r->y1;
}

/* Fills the TSP words of a translucent pass of the color c (8-bit
 * channels) whose part is the plane a of the texture u (s / w, in
 * the alpha ramp), and adds it. base has the planes of the triangle. */
static void add_pass(const rec *base, const double *c, const plane *s, const plane *w) {
  rec r = *base;
  plane t = {0.0f, 0.0f, 0.0f};
  int tex_n = pv.tex_n, tex_4444 = pv.tex_4444;
  uint32_t tex_addr = pv.tex_addr;
  /* The ramp texel is white (252 of 255): make the color brighter. */
  uint32_t cr = (uint32_t)lrint(clampf((float)(c[0] * 256.0 / 252.0), 0, 255));
  uint32_t cg = (uint32_t)lrint(clampf((float)(c[1] * 256.0 / 252.0), 0, 255));
  uint32_t cb = (uint32_t)lrint(clampf((float)(c[2] * 256.0 / 252.0), 0, 255));
  r.flags = (uint8_t)((base->flags & REC_CLIP) | REC_TRANS);
  pv.tex_n = 5, pv.tex_4444 = 1, pv.tex_addr = RAMP_ADDR / 2u;
  r.tsp[0] = TSP_TEXTURE | TSP_TRANS | TSP_NO_FOG | cr | texture_words(s, &t, w, r.tsp);
  r.tsp[1] = (cg << 24) | (cb << 16);
  r.ntsp = 8;
  pv.tex_n = tex_n, pv.tex_4444 = tex_4444, pv.tex_addr = tex_addr;
  add(&r);
}

/* Gives the plane of the alpha ramp coordinate for the part f (0 to 1)
 * at the vertices, with the denominator plane w: (RAMP_BASE + 16 f) w,
 * and half a texel more without the bilinear filter (the nearest
 * texel). */
static plane ramp_plane(const tri *t, const double *f, const double *wv, const plane *w) {
  double s[3];
  plane p;
  int i;
  for (i = 0; i < 3; i++) s[i] = (double)RAMP_STEPS * f[i] * wv[i];
  p = fit(t, s);
  {
    float add = RAMP_BASE + (pv.bilinear ? 0.0f : 0.5f);
    p.a += add * w->a, p.b += add * w->b, p.c += add * w->c;
  }
  return p;
}

/* grDrawTriangle: adds the triangle to the scene, with its passes. */
static void SGL_GLIDE_CALL gr_draw_triangle(const sgl_gr_vertex *a, const sgl_gr_vertex *b, const sgl_gr_vertex *c) {
  const sgl_gr_vertex *v[3];
  tri t;
  rec r;
  double det, col[3][3], z[3], minx, maxx, miny, maxy;
  int i, k, cr[4], textured, trans, flat, alpha_t = 0, ramped = 0;
  uint32_t w0;
  if (!pv.open) return;
  v[0] = a, v[1] = b, v[2] = c;
  for (i = 0; i < 3; i++) t.x[i] = v[i]->x, t.y[i] = screen_y(v[i]->y);
  det = (t.x[1] - t.x[0]) * (t.y[2] - t.y[0]) - (t.x[2] - t.x[0]) * (t.y[1] - t.y[0]);
  if (fabs(det) < 1e-6) return;
  t.inv = 1.0 / det;
  memset(&r, 0, sizeof r);
  /* The edges, with the inside 0 or more. */
  for (i = 0; i < 3; i++) {
    int j = (i + 1) % 3;
    double ea = -(t.y[j] - t.y[i]), eb = t.x[j] - t.x[i], s = det > 0 ? 1.0 : -1.0;
    double ec = -(ea * t.x[i] + eb * t.y[i]);
    r.edge[i].a = (float)(s * ea), r.edge[i].b = (float)(s * eb);
    r.edge[i].c = (float)(s * (ec + 0.5 * (ea + eb)));
  }
  minx = maxx = t.x[0], miny = maxy = t.y[0];
  for (i = 1; i < 3; i++) {
    if (t.x[i] < minx) minx = t.x[i];
    if (t.x[i] > maxx) maxx = t.x[i];
    if (t.y[i] < miny) miny = t.y[i];
    if (t.y[i] > maxy) maxy = t.y[i];
  }
  clip_rect(cr);
  r.x0 = (int16_t)clampf((float)floor(minx), 0, (float)(pv.width - 1));
  r.x1 = (int16_t)clampf((float)ceil(maxx), 0, (float)(pv.width - 1));
  r.y0 = (int16_t)clampf((float)floor(miny), 0, (float)(pv.height - 1));
  r.y1 = (int16_t)clampf((float)ceil(maxy), 0, (float)(pv.height - 1));
  if (!clip_box(&r, cr)) return;
  for (i = 0; i < 3; i++) z[i] = clampf(v[i]->ooz, 0.0f, 65535.0f);
  r.z = fit(&t, z);
  r.depth = isp_depth(&r.z, &t);
  if (pv.depth && pv.depth_mask) r.flags |= REC_ZWRITE;
  textured = pv.textured && pv.color_combine != COMBINE_COLOR;
  trans = pv.blend_src == GR_BLEND_SRC_ALPHA && pv.blend_dst == GR_BLEND_ONE_MINUS_SRC_ALPHA;
  for (i = 0; i < 3; i++) col[i][0] = v[i]->r, col[i][1] = v[i]->g, col[i][2] = v[i]->b;
  if (textured && pv.color_combine != COMBINE_MODULATE)
    for (i = 0; i < 3; i++) col[i][0] = col[i][1] = col[i][2] = 255.0;
  flat = 1;
  for (i = 1; i < 3; i++)
    for (k = 0; k < 3; k++)
      if (fabs(col[i][k] - col[0][k]) > 0.5) flat = 0;
  w0 = TSP_NO_FOG;
  if (trans) {
    double al = (v[0]->a + v[1]->a + v[2]->a) / 3.0;
    if (pv.alpha_combine == ALPHA_TEXTURE && textured && pv.tex_4444) al = 255.0;
    alpha_t = (int)lrint(15.0 - clampf((float)al, 0, 255) * 15.0 / 255.0);
    w0 |= TSP_TRANS | ((uint32_t)alpha_t << TSP_GTRANS_SHIFT);
    r.flags |= REC_TRANS;
  }
  if (textured) {
    double sv[3], tv[3], wv[3];
    plane s, tp, w;
    for (i = 0; i < 3; i++) {
      wv[i] = v[i]->tmu[0].oow;
      sv[i] = v[i]->tmu[0].sow * pv.tex_sscale;
      tv[i] = v[i]->tmu[0].tow * pv.tex_tscale;
      if (pv.tex_bilinear) sv[i] -= 0.5 * wv[i], tv[i] -= 0.5 * wv[i];
    }
    s = fit(&t, sv), tp = fit(&t, tv), w = fit(&t, wv);
    w0 |= TSP_TEXTURE | texture_words(&s, &tp, &w, r.tsp);
    if (pv.tex_bilinear) r.flags |= REC_BILINEAR;
  } else if (trans) {
    /* The card adds the global transparency to the texture alpha
     * only: a translucent color takes a texel of the alpha ramp with
     * the alpha 1. The ramp texel is white (252 of 255): the color is
     * made brighter. */
    plane s = {0.0f, 0.0f, RAMP_BASE + RAMP_STEPS + 2.5f}, tp = {0.0f, 0.0f, 0.0f}, w = {0.0f, 0.0f, 1.0f};
    int tex_n = pv.tex_n, tex_4444 = pv.tex_4444;
    uint32_t tex_addr = pv.tex_addr;
    pv.tex_n = 5, pv.tex_4444 = 1, pv.tex_addr = RAMP_ADDR / 2u;
    w0 |= TSP_TEXTURE | texture_words(&s, &tp, &w, r.tsp);
    pv.tex_n = tex_n, pv.tex_4444 = tex_4444, pv.tex_addr = tex_addr;
    ramped = 1;
    for (i = 0; i < 3; i++)
      for (k = 0; k < 3; k++) col[i][k] = clampf((float)(col[i][k] * 256.0 / 252.0), 0, 255);
  }
  {
    int base = textured || ramped ? 8 : 2;
    double sum[3] = {0, 0, 0}, m;
    if (flat || (!one_hue(col) && !textured && !trans)) {
      /* A flat color: the color of vertex 0. */
      uint32_t cr8 = (uint32_t)lrint(clampf((float)col[0][0], 0, 255));
      uint32_t cg8 = (uint32_t)lrint(clampf((float)col[0][1], 0, 255));
      uint32_t cb8 = (uint32_t)lrint(clampf((float)col[0][2], 0, 255));
      r.tsp[0] = w0 | cr8;
      r.tsp[1] = (cg8 << 24) | (cb8 << 16);
      r.ntsp = (uint8_t)base;
    } else {
      /* One color times an intensity plane, from the reference pixel
       * (xr, yr). */
      double kv[3];
      plane kp;
      int xr = (int)clampf((float)((t.x[0] + t.x[1] + t.x[2]) / 3.0), 0, 1023);
      int yr = (int)clampf((float)((t.y[0] + t.y[1] + t.y[2]) / 3.0), 0, 1023);
      for (i = 0; i < 3; i++)
        for (k = 0; k < 3; k++) sum[k] += col[i][k];
      m = sum[0] > sum[1] ? sum[0] : sum[1];
      if (sum[2] > m) m = sum[2];
      if (m <= 0) m = 1;
      for (i = 0; i < 3; i++) kv[i] = (col[i][0] + col[i][1] + col[i][2]) / (248.0 * (sum[0] + sum[1] + sum[2]) / m);
      kp = fit(&t, kv);
      r.tsp[0] = w0 | TSP_SMOOTH;
      r.tsp[1] = ((uint32_t)xr << 16) | (uint32_t)yr;
      smooth_words(&kp, c555(31.0 * sum[0] / m, 31.0 * sum[1] / m, 31.0 * sum[2] / m), xr, yr, r.tsp + base);
      r.ntsp = (uint8_t)(base + 2);
    }
  }
  add(&r);
  /* Three colors without a texture: one or two passes of the other
   * vertex colors, each with its part of the color. The base is the
   * color of vertex 0. */
  if (!flat && !textured && !trans && !one_hue(col)) {
    double one[3] = {1, 1, 1}, f[3];
    plane w1 = fit(&t, one), s;
    int same01 = 1, same02 = 1, same12 = 1;
    for (k = 0; k < 3; k++) {
      if (fabs(col[1][k] - col[0][k]) > 0.5) same01 = 0;
      if (fabs(col[2][k] - col[0][k]) > 0.5) same02 = 0;
      if (fabs(col[2][k] - col[1][k]) > 0.5) same12 = 0;
    }
    if (same01) {
      /* Vertex 2 with the part b2. */
      f[0] = 0, f[1] = 0, f[2] = 1;
      s = ramp_plane(&t, f, one, &w1);
      add_pass(&r, col[2], &s, &w1);
    } else if (same02 || same12) {
      /* Vertex 1 with the part b1, or b1 + b2. */
      f[0] = 0, f[1] = 1, f[2] = same12 ? 1 : 0;
      s = ramp_plane(&t, f, one, &w1);
      add_pass(&r, col[1], &s, &w1);
    } else {
      /* Vertex 1 with the part b1 / (b0 + b1), then vertex 2 with the
       * part b2. */
      double wv[3] = {1, 1, 0};
      plane w01 = fit(&t, wv);
      f[0] = 0, f[1] = 1, f[2] = 0;
      s = ramp_plane(&t, f, one, &w01);
      add_pass(&r, col[1], &s, &w01);
      f[0] = 0, f[1] = 0, f[2] = 1;
      s = ramp_plane(&t, f, one, &w1);
      add_pass(&r, col[2], &s, &w1);
    }
  }
  /* The fog: a pass of the fog color with the part of the fog at each
   * vertex, interpolated with the perspective. */
  if (pv.fog && !trans) {
    double f[3], wv[3];
    int any = 0;
    plane w, s;
    for (i = 0; i < 3; i++) {
      f[i] = fog_amount(v[i]->oow);
      wv[i] = v[i]->oow > 0 ? v[i]->oow : 1e-6;
      if (f[i] > 1.0 / 32.0) any = 1;
    }
    if (any) {
      double fc[3];
      fc[0] = (pv.fog_color >> 16) & 0xFF, fc[1] = (pv.fog_color >> 8) & 0xFF, fc[2] = pv.fog_color & 0xFF;
      w = fit(&t, wv);
      s = ramp_plane(&t, f, wv, &w);
      add_pass(&r, fc, &s, &w);
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

/* grDepthBufferMode: the depth test on or off. */
static void SGL_GLIDE_CALL gr_depth_buffer_mode(int mode) { pv.depth = mode != GR_DEPTHBUFFER_DISABLE; }

/* grDepthBufferFunction: the depth compare. The card keeps the nearest
 * surface: only GR_CMP_ALWAYS changes the order. */
static void SGL_GLIDE_CALL gr_depth_buffer_function(int func) { pv.depth_func = func; }

/* grDepthMask: the depth writes. */
static void SGL_GLIDE_CALL gr_depth_mask(int on) { pv.depth_mask = on != 0; }

/* grColorMask: the card always writes the color (see the limits). */
static void SGL_GLIDE_CALL gr_color_mask(int rgb, int alpha) { (void)rgb, (void)alpha; }

/* grColorCombine: the combines of glide.c: the vertex color, the
 * texture (GL_REPLACE), the texture times the vertex color
 * (GL_MODULATE), or the vertex color and the texture mixed by the
 * texture alpha (GL_DECAL, only with an RGB texture: the texture). */
static void SGL_GLIDE_CALL gr_color_combine(int func, int factor, int local, int other, int invert) {
  (void)local, (void)invert;
  if (other != GR_COMBINE_OTHER_TEXTURE || func == GR_COMBINE_FUNCTION_LOCAL)
    pv.color_combine = COMBINE_COLOR;
  else if (func == GR_COMBINE_FUNCTION_BLEND)
    pv.color_combine = COMBINE_DECAL;
  else if (func == GR_COMBINE_FUNCTION_SCALE_OTHER && factor == GR_COMBINE_FACTOR_LOCAL)
    pv.color_combine = COMBINE_MODULATE;
  else
    pv.color_combine = COMBINE_REPLACE;
}

/* grAlphaCombine: the vertex alpha (LOCAL), the texture alpha
 * (SCALE_OTHER with ONE) or the product (SCALE_OTHER with LOCAL). */
static void SGL_GLIDE_CALL gr_alpha_combine(int func, int factor, int local, int other, int invert) {
  (void)local, (void)other, (void)invert;
  if (func != GR_COMBINE_FUNCTION_SCALE_OTHER)
    pv.alpha_combine = ALPHA_VERTEX;
  else
    pv.alpha_combine = factor == GR_COMBINE_FACTOR_ONE ? ALPHA_TEXTURE : ALPHA_PRODUCT;
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
  pv.blend_src = rgb_src, pv.blend_dst = rgb_dst;
}

/* grAlphaTestFunction: the card has no alpha test (see the limits). */
static void SGL_GLIDE_CALL gr_alpha_test_function(int func) { (void)func; }

/* grAlphaTestReferenceValue: the card has no alpha test. */
static void SGL_GLIDE_CALL gr_alpha_test_reference_value(unsigned value) { (void)value; }

/* grFogMode: the fog table on or off. */
static void SGL_GLIDE_CALL gr_fog_mode(int mode) { pv.fog = mode != GR_FOG_DISABLE; }

/* grFogColorValue: the fog color (0xRRGGBB). */
static void SGL_GLIDE_CALL gr_fog_color_value(unsigned long color) { pv.fog_color = (uint32_t)color & 0xFFFFFFu; }

/* grFogTable: keeps the table, and the eye depth of each entry: entry
 * i is at w = 2^(3 + i / 4) / (8 - i % 4), as on a Voodoo. */
static void SGL_GLIDE_CALL gr_fog_table(const unsigned char *table) {
  int i;
  for (i = 0; i < GR_FOG_TABLE_SIZE; i++) {
    pv.fog_table[i] = table[i];
    pv.fog_w[i] = (float)(1ul << (3 + (i >> 2))) / (float)(8 - (i & 3));
  }
}

/* grCullMode: glide.c culls in software. */
static void SGL_GLIDE_CALL gr_cull_mode(int mode) { (void)mode; }

/* grDitherMode: dither the 16-bit pixels, or not. */
static void SGL_GLIDE_CALL gr_dither_mode(int mode) { pv.dither = mode != GR_DITHER_DISABLE; }

/* grTexMinAddress: the textures start after the shading blocks and the
 * alpha ramp. */
static unsigned long SGL_GLIDE_CALL gr_tex_min_address(int tmu) {
  (void)tmu;
  return TEX_MIN;
}

/* grTexMaxAddress: the end of texture memory. */
static unsigned long SGL_GLIDE_CALL gr_tex_max_address(int tmu) {
  (void)tmu;
  return TEX_SIZE;
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

/* Gives the log2 of the side of the square texture of the card for a
 * Glide texture: the long side, at least 32 texels. */
static int square_log(const sgl_gr_texinfo *info) {
  int wlog, hlog, n;
  tex_size(info, &wlog, &hlog);
  n = wlog > hlog ? wlog : hlog;
  return n < 5 ? 5 : n;
}

/* grTexTextureMemRequired: the card uses the largest level only, as a
 * square texture. */
static unsigned long SGL_GLIDE_CALL gr_tex_texture_mem_required(unsigned long even_odd, sgl_gr_texinfo *info) {
  int n = square_log(info);
  (void)even_odd;
  return (2ul << (2 * n));
}

/* Gives the twiddled address of texel (u, v): the bits of u at the odd
 * bits, the bits of v at the even bits. */
static uint32_t twiddle(uint32_t u, uint32_t v) {
  uint32_t r = 0;
  int i;
  for (i = 0; i < 8; i++) r |= ((u >> i) & 1u) << (2 * i + 1) | ((v >> i) & 1u) << (2 * i);
  return r;
}

/* Gives a texel of the card from a Glide texel: RGB 565 to RGB 555,
 * and in ARGB 4444 the alpha to the transparency. */
static uint32_t card_texel(uint16_t t, int is4444) {
  if (is4444) return (uint32_t)(t ^ 0xF000u);
  return (uint32_t)(((t >> 1) & 0x7FE0u) | (t & 0x1Fu));
}

/* grTexDownloadMipMap: writes the largest level at start as a square
 * texture in twiddled order. A smaller side is stretched. */
static void SGL_GLIDE_CALL gr_tex_download_mip_map(int tmu, unsigned long start, unsigned long even_odd,
                                                   sgl_gr_texinfo *info) {
  const uint16_t *src = (const uint16_t *)info->data;
  int wlog, hlog, n = square_log(info), is4444 = info->format == GR_TEXFMT_ARGB_4444;
  uint32_t size = 1u << n, base = (uint32_t)start / 4u, u, v;
  (void)tmu, (void)even_odd;
  if (!pv.open || start < TEX_MIN || start + gr_tex_texture_mem_required(0, info) > TEX_SIZE) return;
  tex_size(info, &wlog, &hlog);
  wait_render();
  for (u = 0; u < size; u++)
    for (v = 0; v < size; v += 2) {
      uint32_t su = (u << wlog) >> n, sv0 = (v << hlog) >> n, sv1 = ((v + 1) << hlog) >> n;
      uint32_t t0 = card_texel(src[(sv0 << wlog) + su], is4444), t1 = card_texel(src[(sv1 << wlog) + su], is4444);
      tex_wr(base + (twiddle(u, v) >> 1), (t0 << 16) | t1);
    }
}

/* grTexSource: the texture that the triangles use. The texture
 * coordinates of Glide go from 0 to 256 on the long side. */
static void SGL_GLIDE_CALL gr_tex_source(int tmu, unsigned long start, unsigned long even_odd, sgl_gr_texinfo *info) {
  int wlog, hlog, l;
  (void)tmu, (void)even_odd;
  tex_size(info, &wlog, &hlog);
  l = wlog > hlog ? wlog : hlog;
  pv.textured = 1;
  pv.tex_n = square_log(info);
  pv.tex_4444 = info->format == GR_TEXFMT_ARGB_4444;
  pv.tex_addr = (uint32_t)start / 2u;
  pv.tex_sscale = (float)(1 << pv.tex_n) / (256.0f * (float)(1 << wlog) / (float)(1 << l));
  pv.tex_tscale = (float)(1 << pv.tex_n) / (256.0f * (float)(1 << hlog) / (float)(1 << l));
}

/* grTexFilterMode: the card filters all the scene in one way: the
 * bilinear filter when a triangle of the scene asks for it. */
static void SGL_GLIDE_CALL gr_tex_filter_mode(int tmu, int min, int mag) {
  (void)tmu;
  pv.tex_bilinear = min == GR_TEXTUREFILTER_BILINEAR || mag == GR_TEXTUREFILTER_BILINEAR;
}

/* grTexClampMode: the card wraps the textures (see the limits). */
static void SGL_GLIDE_CALL gr_tex_clamp_mode(int tmu, int s, int t) { (void)tmu, (void)s, (void)t; }

/* grTexMipMapMode: the card uses the largest level. */
static void SGL_GLIDE_CALL gr_tex_mip_map_mode(int tmu, int mode, int lod_blend) {
  (void)tmu, (void)mode, (void)lod_blend;
}

/* Gives 1 when pixel (x, y) is in record r. */
static int inside(const rec *r, int x, int y) {
  int k;
  for (k = 0; k < 3; k++)
    if (r->edge[k].a * x + r->edge[k].b * y + r->edge[k].c < 0.0f) return 0;
  return 1;
}

/* Fills pv.zbuf (rows from the top) with the Glide depth of the
 * triangles after the last clear of the depth: the card keeps no Z
 * buffer, so the driver calculates it for a lock of the Z buffer. */
static void make_zbuf(void) {
  int i, x, y;
  for (i = 0; i < pv.width * pv.height; i++) pv.zbuf[i] = pv.clear_z;
  for (i = pv.zfirst; i < pv.nrec; i++) {
    const rec *r = &pv.recs[i];
    if (!(r->flags & REC_ZWRITE)) continue;
    for (y = r->y0; y <= r->y1; y++)
      for (x = r->x0; x <= r->x1; x++) {
        float zf;
        uint16_t z, *p;
        if (!inside(r, x, y)) continue;
        if ((r->flags & REC_CLIP) && (x < r->clip[0] || x >= r->clip[2] || y < r->clip[1] || y >= r->clip[3]))
          continue;
        zf = clampf(r->z.a * x + r->z.b * y + r->z.c, 0.0f, 65535.0f);
        z = (uint16_t)zf;
        p = &pv.zbuf[y * pv.width + x];
        if (z < *p) *p = z;
      }
  }
}

/* grLfbLock: draws the scene and gives a copy of the buffer in system
 * memory, in RGB 565 (or the Z values), rows in the order of origin. A
 * write lock of a color buffer copies the pixels to the frame buffer
 * in grLfbUnlock; the card cannot use a Z buffer that is written. */
static int SGL_GLIDE_CALL gr_lfb_lock(int type, int buffer, int write_mode, int origin, int pixel_pipeline,
                                      sgl_gr_lfbinfo *info) {
  int x, y;
  uint16_t *out;
  (void)pixel_pipeline;
  if (!pv.open) return 0;
  render();
  wait_render();
  pv.lock_origin = origin;
  if (buffer == GR_BUFFER_AUXBUFFER) {
    if (!pv.zbuf) pv.zbuf = (uint16_t *)malloc((size_t)pv.width * (size_t)pv.height * 2u);
    if (!pv.shadow) pv.shadow = (uint16_t *)malloc((size_t)pv.width * (size_t)pv.height * 2u);
    if (!pv.zbuf || !pv.shadow) return 0;
    if (type == GR_LFB_READ_ONLY) {
      make_zbuf();
      for (y = 0; y < pv.height; y++) {
        int row = origin == GR_ORIGIN_LOWER_LEFT ? pv.height - 1 - y : y;
        memcpy(pv.shadow + (long)y * pv.width, pv.zbuf + (long)row * pv.width, (size_t)pv.width * 2u);
      }
    }
    out = pv.shadow;
  } else {
    const volatile uint8_t *base;
    if (!pv.shadow) pv.shadow = (uint16_t *)malloc((size_t)pv.width * (size_t)pv.height * 2u);
    if (!pv.shadow) return 0;
    base = pv.lfb + (uint32_t)(buffer == GR_BUFFER_BACKBUFFER ? pv.back : pv.front) * (uint32_t)pv.pitch *
                        (uint32_t)pv.height;
    if (type == GR_LFB_READ_ONLY) {
      for (y = 0; y < pv.height; y++) {
        int row = origin == GR_ORIGIN_LOWER_LEFT ? pv.height - 1 - y : y;
        const volatile uint16_t *p = (const volatile uint16_t *)(base + (long)row * pv.pitch);
        uint16_t *q = pv.shadow + (long)y * pv.width;
        for (x = 0; x < pv.width; x++) q[x] = p[x];
      }
    }
    out = pv.shadow;
  }
  info->ptr = out;
  info->stride = (unsigned long)pv.width * 2ul;
  info->write_mode = write_mode;
  info->origin = origin;
  return 1;
}

/* grLfbUnlock: copies the pixels of a write lock of a color buffer to
 * the frame buffer. */
static int SGL_GLIDE_CALL gr_lfb_unlock(int type, int buffer) {
  int x, y;
  volatile uint8_t *base;
  if (!pv.open || !pv.shadow || type != GR_LFB_WRITE_ONLY || buffer == GR_BUFFER_AUXBUFFER) return 1;
  base = pv.lfb + (uint32_t)(buffer == GR_BUFFER_BACKBUFFER ? pv.back : pv.front) * (uint32_t)pv.pitch *
                      (uint32_t)pv.height;
  for (y = 0; y < pv.height; y++) {
    int row = pv.lock_origin == GR_ORIGIN_LOWER_LEFT ? pv.height - 1 - y : y;
    volatile uint16_t *p = (volatile uint16_t *)(base + (long)row * pv.pitch);
    const uint16_t *q = pv.shadow + (long)y * pv.width;
    for (x = 0; x < pv.width; x++) p[x] = q[x];
  }
  return 1;
}

/* Fills api with the PowerVR driver (see softgl_glide.h). */
int sgl_glide_pvr(sgl_glide_api *api) {
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
                SGL_GLIDE_LIMIT_FOG_ALPHA | SGL_GLIDE_LIMIT_ONE_ALPHA | SGL_GLIDE_LIMIT_NO_CLAMP |
                SGL_GLIDE_LIMIT_ONE_HUE;
  return 1;
}

#endif
