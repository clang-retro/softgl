# The Amiga back ends

softgl has two back ends for the Amiga:

- The [chipset back end](#the-chipset-back-end) uses the Blitter and
  Akiko of every Amiga (OCS, ECS and AGA).
- The [Warp3D back end](#the-warp3d-back-end) draws with a 3D card
  through warp3d.library.

## The chipset back end

The chipset back end uses the hardware of every Amiga (OCS, ECS and
AGA) for softgl. It is a back end of the interface in
[backends.md](backends.md). The code is in `backends/amiga/`. The
interface is in `include/softgl_amiga.h`.

The back end does this work:

- **Clear.** The Blitter clears the color, depth and stencil buffers
  when they are in Chip RAM. The CPU can calculate the next primitive
  while the Blitter operates.
- **Flat fill (optional).** The Blitter fills the long rows of flat
  triangles. Rows of fewer than 16 pixels stay on the CPU, because a
  short blit costs more to set up than it saves.
- **Present.** `sgl_present` changes the 32-bit frame to pens, and the
  pens to bit planes (chunky-to-planar). Akiko does the bit planes on
  the CD32. The CPU does them on other machines. With two screen
  buffers, the back end then flips the screen.

The rasterizer, the depth test and all of GL stay in softgl. The
Blitter has no depth buffer, shading or textures, so it does not draw
triangles. See [Limits](#limits).

### How to use it

1. Open `graphics.library` (into `GfxBase`) and, for double buffering,
   `intuition.library` (into `IntuitionBase`), version 39 or later.
2. Open a screen. Load the palette of the back end into it:
   `sgl_amiga_palette(depth, rgb)` gives `1 << depth` colors.
3. Fill an `sgl_amiga_chipset`:
   - `bitmap`: the bitmap to write, or
   - `screen` and `buffers[0]`, `buffers[1]`: two buffers from
     `AllocScreenBuffer`, for double buffering.
   - `depth`: the number of planes to write, 1 to 8.
   - `flags`: `SGL_AMIGA_BLITTER_CLEAR`, `SGL_AMIGA_BLITTER_FILL`,
     `SGL_AMIGA_NO_AKIKO`.
4. Call `sgl_amiga_chipset_init(&chip, &backend)`, then
   `sgl_set_backend(ctx, &backend, &chip)`.
5. For Blitter clears and fills, put the color buffer in Chip RAM:
   `sgl_amiga_alloc_color(width, height)`. With
   `SGL_AMIGA_BLITTER_CLEAR`, the back end puts the depth and stencil
   buffers in Chip RAM when there is sufficient Chip RAM.
6. Call `sgl_set_buffer`, draw, and call `sgl_present(ctx)` after each
   frame.

`examples/amiga/cube.c` shows all the steps: a flat-shaded cube that
turns on a screen of 3 to 8 planes. `make amiga` with the AmigaOS
compiler builds it.

### The pens

A pen has `depth` bits. The back end divides them between red, green
and blue:

| Planes | Red, green, blue bits | Colors |
| --- | --- | --- |
| 8 | 3, 3, 2 | 256 |
| 7 | 2, 3, 2 | 128 |
| 6 | 2, 2, 2 | 64 |
| 5 | 2, 2, 1 | 32 |
| 4 | 1, 2, 1 | 16 |
| 3 | 1, 1, 1 | 8 |
| 1 or 2 | gray levels | 2 or 4 |

Three tables of 256 values change a pixel to a pen with three reads and
two additions. In color index mode, the pen is the index, with the bits
above `depth` removed.

### The chunky-to-planar step

- **CPU.** Each group of 8 pens is an 8 x 8 matrix of bits. A transpose
  with 32-bit operations gives one byte of each plane (Hacker's Delight,
  section 7-3). It needs no multiply and no table, so it suits the
  68000.
- **Akiko.** On the CD32, `GfxBase->ChunkyToPlanarPtr` (graphics.library
  V40) gives the Akiko register. The back end writes 8 longwords of pens
  (32 pixels) and reads 8 longwords of planes. Set `SGL_AMIGA_NO_AKIKO`
  to use the CPU.

The width must be a multiple of 8 pixels, or the back end writes up to
7 more pixels of the bitmap row. Akiko does each group of 32 pixels;
the CPU does the remainder.

### The Blitter

The back end owns the Blitter (`OwnBlitter`) from its first blit until
softgl calls `sync`, and then gives it back (`DisownBlitter`). softgl
calls `sync` before it uses the buffers itself, and `sgl_present` calls
it before the chunky-to-planar step.

- A 32-bit value with two equal halves is one blit of words.
- A 32-bit value with two different halves is two blits: a column of
  1 word with a modulo of 2 bytes writes each second word.
- A 16-bit depth buffer is one blit of the rectangle.
- The stencil buffer has bytes. The first-word and last-word masks keep
  the bytes outside the rectangle (`D = A B + (not A) C`, with A the
  mask and B the value).
- On OCS, a blit is at most 64 words wide and 1024 rows high. On ECS
  and AGA (`GFXF_BIG_BLITS`), it is at most 2048 words wide and 32768
  rows high. The back end cuts larger blits.

The Blitter gets to Chip RAM only. The back end examines the address
against the top of Chip RAM (`SysBase->MaxLocMem`). For a buffer in
other memory, the back end gives the work back to the CPU.

### Limits

- **No hardware tests in CI.** CI builds the back end and the example
  with `-Werror`, and `tests/c2p.c` tests the pens and the bit planes on
  the build machine. The Blitter, Akiko and the screen flip need a test
  on a real Amiga or in an emulator (WinUAE or FS-UAE).
- **Chip RAM.** A color buffer of 320 x 200 pixels is 256000 bytes.
  The 16-bit depth buffer is 128000 bytes more. Thus Blitter clears of
  a full screen need 1 MB of Chip RAM or more. On a machine with less
  Chip RAM, put the color buffer in other memory and leave out the
  Blitter flags: the CPU then does the work.
- **Speed of the flat fill.** The Blitter and the CPU share the Chip
  RAM bus. The flat fill gives the largest gain when the program and
  its data are in Fast RAM. For this reason, the flat fill is optional.
- **No dithering.** Each pixel goes to the nearest lower level of each
  channel.
- **The screen flip** does not wait for the display. To prevent
  tearing, call `WaitTOF` after `sgl_present`.
- **One context at a time.** AmigaOS has no thread-local storage, so one
  context is current for the whole program.

## The Warp3D back end

The Warp3D back end gives the triangles, lines and points to a 3D card
through warp3d.library (version 2 or later): for example a CyberVision
64/3D (ViRGE), a CyberVision PPC or BlizzardVision PPC (Permedia 2), or
a Voodoo card. The code is in `backends/amiga/warp3d.c`.

softgl still does the GL state, the transform, lighting, clipping and
culling. The card does the rasterization: Gouraud shading, the texture
of unit 0, the Z buffer, the alpha test, blending, the secondary color
and the scissor box.

### How to use it

1. Open a screen on the graphics card (15, 16 or 32 bits), for example
   with `SA_LikeWorkbench`. For double buffering, get two buffers with
   `AllocScreenBuffer`.
2. Make the context and give it a color buffer of the size of the
   screen (`sgl_set_buffer`). This buffer is for the primitives that
   softgl draws itself.
3. Fill an `sgl_amiga_warp3d` (`bitmap`, or `screen` and `buffers`) and
   call `sgl_amiga_warp3d_attach(&w3d, ctx)`. When it gives 0, the
   context stays in software.
4. Draw, and call `sgl_present(ctx)` after each frame. It finishes the
   frame of the card and flips the buffers.

`examples/amiga/w3dcube.c` shows all the steps: a lit cube with the
depth test.

### When softgl draws

The card draws a primitive only when it can do the GL state. softgl
draws the primitive in software when one of these is on:

- Color index mode, the logical operation, the stencil test, fog, a
  color mask, or `glDrawBuffer(GL_NONE)`.
- The polygon or line stipple, smooth points, lines or polygons, or
  point size attenuation.
- An occlusion query.
- A blend equation other than `GL_FUNC_ADD`, separate blend factors for
  alpha, or the constant blend color.
- A texture on unit 1, a texture that is not 2D, a paletted texture, a
  depth texture, a texture that is not RGB or RGBA, or a texture
  environment other than `GL_REPLACE`, `GL_MODULATE`, `GL_DECAL` and
  `GL_BLEND`.
- A feature that the driver does not have (`W3D_Query`), for example
  the Z buffer.

Then the back end waits for the card and copies the frame and the Z
buffer to the buffers of softgl. Before the next primitive on the card,
it copies the changed buffers back. These copies are slow, so a program
is fast only when most of its primitives stay on the card. `glClear` of
the whole frame goes to the card; a clear of the scissor box stays in
softgl.

### The pixel formats

The back end copies the frame in these formats of the drawing region:
`W3D_FMT_A8R8G8B8`, `W3D_FMT_B8G8R8A8`, `W3D_FMT_R5G6B5`,
`W3D_FMT_R5G6B5PC`, `W3D_FMT_R5G5B5` and `W3D_FMT_R5G5B5PC`. With
other formats, `sgl_amiga_warp3d_attach` gives 0.

### The driver

`W3D_CheckDriver` tells if the driver uses 3D hardware. The CPU driver
of Warp3D is slower than softgl on a 68k, so the back end does not use
it. Set `SGL_AMIGA_W3D_ALLOW_CPU` to use it (for example on a PowerPC
card).

### The interface to warp3d.library

`backends/amiga/warp3d_api.h` gives only the types, values and calls
that the back end uses. It is not the Warp3D SDK. The calls go through
`backends/amiga/warp3d_stubs.s`, which `tools/gen-warp3d-stubs.sh` makes
from `backends/amiga/warp3d.fd` with `tools/fd2stub.py` of amiga-sdk.
The stubs have the prefix `sgl_`, so they do not collide with the
Warp3D stubs of a program.

### Limits of the Warp3D back end

- **No hardware tests.** CI builds the back end and links
  `w3dcube` with `-Werror`. A test on a real card (or in WinUAE with a
  Warp3D wrapper) is necessary.
- **No mipmaps on the card.** A mipmap filter uses the filter of the
  base level.
- **`GL_CLAMP_TO_EDGE` and `GL_MIRRORED_REPEAT`** are `W3D_CLAMP` on the
  card.
- **One context for each back end.** The size of the color buffer must
  be the size of the drawing region.
