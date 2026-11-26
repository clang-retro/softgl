# The Amiga chipset back end

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

## How to use it

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

## The pens

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

## The chunky-to-planar step

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

## The Blitter

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

## Limits

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
