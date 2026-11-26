# The Glide back end

The Glide back end gives the triangles, lines and points of a softgl
context to a 3dfx Voodoo card through Glide 2. The cards are the
Voodoo Graphics, the Voodoo Rush and the Voodoo2. On 32-bit DOS,
built-in drivers also give the Glide functions on an S3 ViRGE card and
on a Matrox card. The code is in `backends/glide/`.

softgl still does the GL state, the transform, lighting, clipping and
culling. The card does the rasterization: Gouraud shading, the
textures of units 0 and 1, mipmaps, the Z buffer, the alpha test,
blending, fog, the color mask and the scissor box.

## How to get the Glide functions

The back end calls Glide through a table of functions,
`sgl_glide_api` in `include/softgl_glide.h`. Fill the table in one of
these three ways:

- **Windows 95 and 98.** `sgl_glide_load(&api)` loads `glide2x.dll`
  and gets each function. It gives 0 when the DLL is not there or when
  a function is missing. `sgl_glide_unload(&api)` frees the DLL. Link
  `libsoftgl-glide.a` (from `make glide`).
- **32-bit DOS, with the driver of softgl.** `sgl_glide_sst1(&api)`
  fills the table with a driver that writes the registers of a Voodoo
  Graphics card directly. A Glide library is not necessary. Use the
  clang-retro `i386-pc-msdos` target (`--config=dos32.cfg`) and link
  `libsoftgl-glide-dos.a` (from `make glide-dos`). Refer to
  [The Voodoo driver for DOS](#the-voodoo-driver-for-dos) and
  [How to select a driver for DOS](#how-to-select-a-driver-for-dos).
- **A 32-bit DOS extender, with the Glide library of 3dfx.** Link the
  Glide 2 library of the 3dfx SDK, include its `glide.h`, and fill the
  table with `SGL_GLIDE_FILL(&api)`. Compile `backends/glide/glide.c`
  with the program.

## How to use it

1. Make the context and give it a color buffer of the size of the
   screen (`sgl_set_buffer`). This buffer is for the primitives that
   softgl draws itself.
2. Fill an `sgl_glide`: `api`, `width`, `height`, `refresh` (0 for
   60 Hz), `hwnd` (the window on Windows, or 0) and `flags`.
3. Call `sgl_glide_attach(&g, ctx)`. It opens the screen of the card.
   When it gives 0, the context stays in software.
4. Draw, and call `sgl_present(ctx)` after each frame. It swaps the
   buffers of the card. `SGL_GLIDE_NO_VSYNC` stops the wait for the
   vertical retrace.
5. `sgl_destroy_context` (or `sgl_set_backend(ctx, NULL, NULL)`) closes
   the screen and stops Glide.

The size must be a Glide resolution, for example 640 x 480 or
800 x 600.

`examples/glide/glidecube.c` shows all the steps: a cube with a
mipmapped texture and fog. Run it as `glidecube [frames]`. It stops at
Esc, after 30 seconds, or after the given number of frames.

## When softgl draws

The card draws a primitive only when it can do the GL state. softgl
draws the primitive in software when one of these is true:

- Color index mode, the logical operation, the stencil test, or
  `glDrawBuffer(GL_NONE)`.
- A color mask that is not the same for red, green and blue.
- The polygon or line stipple, smooth points, lines or polygons, or
  point size attenuation.
- An occlusion query.
- Fog with `GL_FOG_COORDINATE`, or fog without a perspective
  projection. The fog table of Glide uses the eye depth, which only a
  perspective projection gives in w.
- A blend equation other than `GL_FUNC_ADD`, separate blend factors for
  alpha, or a blend factor that Glide does not have (for example the
  constant color).
- A texture that is not 2D, a paletted texture, a depth texture, a
  texture that is not RGB or RGBA, or a texture environment other than
  `GL_MODULATE`, `GL_REPLACE` and `GL_DECAL`.
- A texture larger than 256 texels on a side, or with a ratio of width
  to height larger than 8.
- A texture on unit 1 with a card that has one TMU. With two TMUs,
  unit 1 must use `GL_MODULATE`, and unit 0 `GL_MODULATE` or
  `GL_REPLACE`.
- The secondary color together with a texture. Glide has one iterated
  color.

Then the back end waits for the card and copies the frame and the Z
buffer to the buffers of softgl through `grLfbLock`. Before the next
primitive on the card, it copies the changed buffers back. These copies
are slow, so a program is fast only when most of its primitives stay
on the card.

`glClear` goes to the card for the color and depth buffers, also for a
scissor box.

## How the back end uses Glide

- The screen origin is the lower left. The locks of the frame use the
  upper left, because row 0 of softgl is the top row.
- The depth is in the Z buffer, as `z * 65535` in `ooz`.
- The back end snaps x and y to 1/16 pixel, as Glide requires.
- Textures go to the card as RGB 565 or ARGB 4444, with all the mipmaps
  to 1 x 1 when the filter is a mipmap filter. A texture does not cross
  a 2 MB boundary of texture memory. When the texture memory is full,
  the back end frees all the textures on the card and loads them again
  when they are necessary.
- Wide lines and large points are two triangles.
- The fog of GL (linear, exp and exp2) goes to the fog table of Glide.

## The Voodoo driver for DOS

`backends/glide/glide_sst1.c` has the Glide 2 functions that the back
end uses. These functions write the registers of the card. They do not
use a Glide library. `backends/dos/dos_hw.c` has the functions for the
hardware (`include/softgl_dos.h`):

- PCI configuration mechanism 1 (ports 0CF8h and 0CFCh), to find the
  card and to read its base address.
- DPMI functions 0800h and 0801h, to map the 16 MB of the card into the
  address space of the program.
- A clock in milliseconds.

The driver does these steps:

1. `grGlideInit` finds the first PCI function with the IDs 121Ah:0001h
   (Voodoo Graphics) or 121Ah:0002h (Voodoo2), sets the memory space
   bit of its PCI command register, and maps BAR 0.
2. `grSstWinOpen` resets the chips, sets the frame buffer and the video
   timing, and gives the monitor to the Voodoo. The resolutions are
   512 x 384, 640 x 400, 640 x 480 and 800 x 600.
3. The draw functions calculate the start values and the gradients of
   each triangle, and write them to the floating point registers.
4. `grSstWinClose` gives the monitor back to the VGA card.

The driver has these limits:

- One board, one TMU, 16-bit color, a 16-bit Z buffer and two color
  buffers.
- The texture formats RGB 565 and ARGB 4444.
- **The video clock is not set.** A real Voodoo Graphics card needs
  the PLL of its DAC set for each resolution. The driver does not do
  this yet. DOSBox-X does not need it. Test the driver on a real card
  before you use it.
- The Voodoo2 uses the same registers for most functions, but the
  driver is not tested on a Voodoo2.

To build the library, the example and the test:

```sh
make OUT=build-dos32 CC="clang --config=dos32.cfg" \
  CXX="clang++ --config=dos32.cfg" AR=llvm-ar all glide-dos
```

![glidecube on the Voodoo card of DOSBox-X](images/glidecube-voodoo-dos.png)

## The ViRGE driver for DOS

`backends/glide/glide_virge.c` has the same Glide 2 functions for the
3D engine of an S3 ViRGE card. The back end uses it as it uses a
Voodoo. The cards are the ViRGE, ViRGE/VX, ViRGE/DX, ViRGE/GX,
ViRGE/GX2 and Trio3D/2X.

The driver does these steps:

1. `grGlideInit` finds the first PCI function with the vendor ID 5333h
   and the device ID of a ViRGE, and reads BAR 0.
2. `grSstWinOpen` sets a VBE mode of 15-bit color (5:5:5) at the
   resolution, and maps the linear frame buffer and the register
   window ("new MMIO", 16 MB after the frame buffer). When the VBE BIOS
   has no linear frame buffer (VBE 1.2), the driver sets the address
   of the frame buffer in the registers CR59 and CR5A of the card.
3. Video memory holds three color buffers (two when the memory is
   small), the 16-bit Z buffer, and then the textures.
4. The draw functions calculate the start values and the gradients of
   each triangle, and write them to the 3D registers. Lines and points
   are two triangles.
5. `grBufferClear` uses the rectangle fill of the 2D engine.
6. `grBufferSwap` shows the new buffer through VBE function 4F07h.
   With two buffers, the driver first waits for the vertical retrace.
7. `grSstWinClose` sets the text mode again.

The ViRGE draws less than a Voodoo. The driver sets the `limits` of
its table (the `SGL_GLIDE_LIMIT_` flags in `include/softgl_glide.h`).
When a draw needs a function that the card does not have, the back end
gives the draw to softgl, and softgl draws it in software:

- The only blend is (source alpha, 1 - source alpha).
- No alpha test and no color mask.
- The fog uses the vertex alpha. Thus, no fog with a blend.
- The alpha is the texture alpha or the vertex alpha. It is not the
  product of the two.
- Only square textures. The formats are RGB 565 (stored as ARGB 1555)
  and ARGB 4444.
- **A clamped texture wraps.** The card has no clamp mode.
- The driver is tested in 86Box only. Test it on a real card before you
  use it.

![glidecube on the ViRGE/DX card of 86Box](images/glidecube-virge-dos.png)

## The Matrox driver for DOS

`backends/glide/glide_mga.c` has the same Glide 2 functions for the
drawing engine of a Matrox card. The cards are the Mystique, the
Mystique 220, the Millennium II and the Productiva G100. The driver
does not support the Millennium (it has no texture engine) and the
G200 and later cards (86Box does not emulate them).

The driver does these steps:

1. `grGlideInit` finds the first PCI function with the vendor ID 102Bh
   and the device ID of a supported card. It maps the registers (the
   control aperture). On the Mystique, the registers are at BAR 0; on
   the other cards, they are at BAR 1.
2. `grSstWinOpen` sets a VBE mode of 16-bit color (5:6:5) with the
   linear frame buffer. Video memory holds three color buffers (two
   when the memory is small), the 16-bit Z buffer, and then the
   textures.
3. The engine draws trapezoids. The driver divides each triangle into
   two trapezoids (the rows above the middle vertex, and the rows from
   it). It calculates the Bresenham terms of the edges, and the start
   values and the gradients of Z, the color and the texture
   coordinates (S, T and Q). Lines and points are two triangles.
4. `grBufferClear` fills the clip window with a trapezoid of one color.
5. `grBufferSwap` shows the new buffer through VBE function 4F07h, as
   the ViRGE driver does.

The driver has these limits. The back end gives a draw that needs one
of them to softgl:

- No blend and no alpha test.
- No fog on the Mystique and the Millennium II. On the G100, the fog of
  a textured triangle is calculated for each pixel. The fog of a
  triangle without a texture is calculated at the vertices.
- The alpha is the texture alpha or the vertex alpha. It is not the
  product of the two.
- **No mipmaps.** The card uses the largest level of a texture.
- The Mystique and the Millennium II use the nearest texel. The G100
  also has the bilinear filter (86Box shows small errors with it).
- The texture formats are RGB 565 and ARGB 4444.
- The driver is tested in 86Box only. Test it on a real card before you
  use it.

The Matrox engine of 86Box does not always continue a trapezoid from
the registers of the trapezoid before it. Thus, the driver writes all
the registers for each trapezoid.

![glidecube on the Mystique card of 86Box, without fog](images/glidecube-matrox-dos.png)

## How to select a driver for DOS

A DOS program calls `sgl_glide_dos(&api, name)`. The name is
`"voodoo"`, `"virge"`, `"matrox"` or `NULL`. With `NULL`, the function
selects the first card that it finds. `glidecube.exe` and
`glidetest.exe` take the name as an argument:

```sh
glidecube 0 matrox
```

## Tests

- `tests/glide.c` runs in `make check`. It gives the back end a fake
  Glide table and examines the calls: the vertices, the copies of the
  buffers, the textures, the two units, the fog table and the other
  state. The output is `PASS softgl glide`.
- `tools/test-glide-wine.sh OUT` builds `glidecube.exe` and a fake
  `glide2x.dll` (`tests/wine/fake_glide2x.c`) for Windows 95, and runs
  the example in Wine for 30 frames. It examines the number of opened
  screens, the swaps and the bad vertices. CI runs it.
- `tests/dos/glidetest.c` is a test of the DOS drivers. It
  draws a clear, a flat quad, a smooth triangle, the Z buffer, a
  texture and a blend through the back end, and reads the pixels of
  the card back through the linear frame buffer. The output is
  `PASS softgl glide on the voodoo card` (or `virge`, or `matrox`).
  `tools/test-glide-dosbox.sh build-dos32/tests/dos/glidetest.exe` runs
  it in DOSBox-X, which emulates a Voodoo Graphics card
  (`[voodoo] voodoo_card=software`). CI runs it.
- `tools/setup-86box.sh DIR` builds the 86Box emulator from a fixed
  commit, and gets its ROM files and a FreeDOS disk image.
  `tools/test-glide-86box.py` runs a DOS program in 86Box on a Pentium
  computer with FreeDOS, and examines the output. CI runs `glidetest`
  on the ViRGE/DX, the ViRGE, the Mystique, the Millennium II, the
  G100 and the Voodoo Graphics. The G100 is an AGP card: use the ASUS
  P5A board (`--machine p5a`):

  ```sh
  sh tools/setup-86box.sh ~/86box
  python3 tools/test-glide-86box.py --card virge375_pci ~/86box \
    build-dos32/tests/dos/glidetest.exe virge
  python3 tools/test-glide-86box.py --machine p5a --card productiva_g100 \
    ~/86box build-dos32/tests/dos/glidetest.exe matrox
  python3 tools/test-glide-86box.py --voodoo ~/86box \
    build-dos32/tests/dos/glidetest.exe voodoo
  ```

## Limits

- **No hardware tests.** The tests use a fake Glide or the emulated
  cards of DOSBox-X and 86Box. A test on a real Voodoo card is necessary.
- **One context for each back end.** The size of the color buffer must
  be the size of the screen.
- **No alpha in the frame.** The Voodoo frame buffer has no alpha
  channel. Blend factors with the destination alpha go to softgl.
- **Glide 2 only.** Glide 3 has different calls.
