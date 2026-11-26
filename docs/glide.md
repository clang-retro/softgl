# The Glide back end

The Glide back end gives the triangles, lines and points of a softgl
context to a 3dfx Voodoo card through Glide 2. The cards are the
Voodoo Graphics, the Voodoo Rush and the Voodoo2. The code is in
`backends/glide/`.

softgl still does the GL state, the transform, lighting, clipping and
culling. The card does the rasterization: Gouraud shading, the
textures of units 0 and 1, mipmaps, the Z buffer, the alpha test,
blending, fog, the color mask and the scissor box.

## How to get the Glide functions

The back end calls Glide through a table of functions,
`sgl_glide_api` in `include/softgl_glide.h`. Fill the table in one of
these two ways:

- **Windows 95 and 98.** `sgl_glide_load(&api)` loads `glide2x.dll`
  and gets each function. It gives 0 when the DLL is not there or when
  a function is missing. `sgl_glide_unload(&api)` frees the DLL. Link
  `libsoftgl-glide.a` (from `make glide`).
- **A 32-bit DOS extender.** Link the Glide 2 library of the 3dfx SDK,
  include its `glide.h`, and fill the table with `SGL_GLIDE_FILL(&api)`.
  Compile `backends/glide/glide.c` with the program. The clang-retro
  DOS target is 16-bit, and Glide is 32-bit. Thus this repository does
  not build or test Glide on DOS.

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

## Tests

- `tests/glide.c` runs in `make check`. It gives the back end a fake
  Glide table and examines the calls: the vertices, the copies of the
  buffers, the textures, the two units, the fog table and the other
  state. The output is `PASS softgl glide`.
- `tools/test-glide-wine.sh OUT` builds `glidecube.exe` and a fake
  `glide2x.dll` (`tests/wine/fake_glide2x.c`) for Windows 95, and runs
  the example in Wine for 30 frames. It examines the number of opened
  screens, the swaps and the bad vertices. CI runs it.

## Limits

- **No hardware tests.** The tests use a fake Glide. A test on a real
  Voodoo card is necessary.
- **One context for each back end.** The size of the color buffer must
  be the size of the screen.
- **No alpha in the frame.** The Voodoo frame buffer has no alpha
  channel. Blend factors with the destination alpha go to softgl.
- **Glide 2 only.** Glide 3 has different calls.
