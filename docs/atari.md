# The Atari back end

The Atari back end shows the frame of a softgl context on the screen of
an Atari ST, STE, Mega STE, TT or Falcon. On the STE, the Mega STE and
the Falcon, the Blitter can also clear the buffers. The code is in
`backends/atari/atari.c` and `backends/common/c2p.c`.

## The screen formats

| `depth` | Format | Machines |
| --- | --- | --- |
| 1, 2, 4 | Interleaved bit planes | ST, STE, TT, Falcon |
| 8 | Interleaved bit planes | TT, Falcon |
| 16 | True color, RGB 565 (Videl) | Falcon |

With bit planes, the back end changes each pixel to the nearest pen of
its fixed palette, and then writes the planes as interleaved 16-bit words.
The width must be a multiple of 16. The conversion is the same code as
the chunky-to-planar conversion of the Amiga back end
(`include/softgl_c2p.h`).

## How to use it

1. Get the screen memory. The screen must be in ST RAM and on a
   boundary of 256 bytes. For two screens, get two blocks.
2. Set the screen mode (for example with `VsetMode` on the Falcon).
   With bit planes, the back end uses a fixed palette:
   `sgl_c2p_palette(depth, rgb)` gives its colors. Load them into the
   hardware with `Setcolor` (ST and STE) or `VsetRGB` (Falcon).
3. Make the context and give it a color buffer (`sgl_set_buffer`).
4. Fill an `sgl_atari`: `screens` (`screens[1]` NULL for one screen),
   `depth`, `width`, `height`, `pitch` (0 for `width * depth / 8`) and
   `flags`. Call `sgl_atari_init(&a, &be)`, then
   `sgl_set_backend(ctx, &be, &a)`.
5. Draw, and call `sgl_present(ctx)` after each frame. It writes the
   frame to the back screen and shows it with `Setscreen`. Then it
   waits for the vertical blank (`Vsync`), unless `SGL_ATARI_NO_VSYNC`
   is set.
6. After the context no longer uses the back end, call
   `sgl_atari_free(&a)`.

`examples/atari/cube.c` shows all the steps. On a Falcon it uses
320 x 240 on VGA or 320 x 200 on a TV or RGB monitor, in true color.
On an ST or STE it uses the low resolution (320 x 200, 4 planes). Run
it as `cube.tos [frames]`.

The C library takes the free memory for `malloc`. Thus `Mxalloc` can
fail. The example tries `Mxalloc` first, and then `malloc`.

## Clears with the Blitter

Set `SGL_ATARI_BLITTER_CLEAR` to clear the color, depth and stencil
buffers with the Blitter. The back end uses the Blitter only when all
of these are true:

- `Blitmode(-1)` tells that the Blitter is there.
- The CPU is a 68000, 68020 or 68030 (the `_CPU` cookie). A 68040 or
  68060 can keep changed lines in its copyback cache over the buffers.
  There, the CPU clears.
- The buffer is in ST RAM, below `phystop`. The Blitter cannot get to
  TT RAM or Fast RAM.

The Blitter runs in hog mode. The CPU waits until the blit is done.
After a blit on a 68020 or 68030, the back end clears the data cache.

- A 32-bit color with two equal halves is one blit of words.
- A 32-bit color with two different halves is two blits. Each blit
  writes each second word.
- A 16-bit depth buffer is one blit. A 32-bit depth buffer stays in
  softgl.
- The stencil buffer has bytes. The end masks keep the bytes outside
  the rectangle.

## Tests

- `tests/c2p.c` runs in `make check`. It tests the pens, the bit
  planes, the interleaved words and the 565 conversion on the build
  machine. The output is `PASS softgl c2p`.
- `tools/test-atari-hatari.sh OUT` builds `tests/atari/present.c` and
  runs it in the Hatari emulator with EmuTOS on an ST, an STE and a
  Falcon. The test examines the clear, a clear of the scissor box, a
  triangle and the screen memory. It also examines that the back end
  uses the Blitter only when the machine has it. On each machine the
  output must be `PASS softgl atari`. CI runs it.

The Hatari job of CI compiles with `-O0`. The clang-retro nightly of
2026-11-25 and earlier compiles some stores of narrow values
incorrectly at `-O1` and above. On a 68000 this can cause an address
error. The fix is in llvm (commit 8e80744). When a nightly has the fix,
the job can use `-O2` again.

## Limits

- **No Falcon DSP.** The DSP 56001 cannot get to the main memory. Each
  vertex and each pixel must go through the host port, and the
  back-end interface gives no work that is faster on the DSP than on
  the CPU.
- **No hardware tests.** Hatari is the only test. A test on a real
  machine is necessary.
- **No dithering.** With bit planes, each pixel goes to the nearest pen.
- **The screen** must be in ST RAM, because the video hardware reads
  only ST RAM.
- **One context at a time.** TOS has no thread-local storage.
