# Back ends

A back end lets a driver use hardware for some of the work of a softgl
context. The interface is in `include/softgl.h`. The code is in
`src/backend.c`.

softgl always does this work itself:

- The GL state and the errors.
- The matrices, lighting, texture coordinate generation and the fog
  factor.
- Clipping, culling, selection and feedback.

A back end can do this work:

- Allocate the depth and stencil buffers (for example in memory that a
  blitter can get to).
- Clear the color, depth and stencil buffers.
- Fill the rows of flat triangles.
- Draw triangles, lines and points.
- Show the frame.

All the back ends thus give the same GL state. Each entry of
`sgl_backend` can be NULL. When an entry is NULL, softgl does that work
in software, as it does without a back end.

## Back ends in this repository

| Back end | Systems | Document |
| --- | --- | --- |
| Chipset: Blitter clears and flat fills, chunky-to-planar with Akiko or the CPU | Amiga (OCS, ECS, AGA) | [amiga.md](amiga.md) |
| Warp3D: triangles, lines and points on a 3D card | Amiga with a 3D graphics card | [amiga.md](amiga.md#the-warp3d-back-end) |
| Glide: triangles, lines and points on a 3dfx Voodoo card, an S3 ViRGE card, a Matrox card or an ATI Rage card | Windows 95 and 98; 32-bit DOS (built-in Voodoo, ViRGE, Matrox and ATI drivers, or the 3dfx Glide library) | [glide.md](glide.md) |
| Screen: bit planes or Falcon true color, Blitter clears | Atari ST, STE, TT, Falcon | [atari.md](atari.md) |

## How to use a back end

1. Make the context (`sgl_create_context`).
2. Fill an `sgl_backend` structure. Keep it in memory while the context
   uses it.
3. Call `sgl_set_backend(ctx, &backend, user)`. The value `user` goes
   to each entry.
4. Call `sgl_set_buffer` as usual.
5. After each frame, call `sgl_present(ctx)`. It waits until the work
   of `clear` and `fill_span` is done, and then calls the `present`
   entry. It does not call `software`: after hardware primitives, the
   `present` entry shows the frame of the hardware.

If you call `sgl_set_backend` after `sgl_set_buffer`, softgl makes the
depth and stencil buffers again through the new back end. Their
contents are then lost.

If the window layer copies the color buffer itself, it must call
`sgl_present` or `glFinish` first. Otherwise the work of `clear` and
`fill_span` can be incomplete.

## The entries

| Entry | When softgl calls it |
| --- | --- |
| `alloc`, `release` | To make and free the depth and stencil buffers. If `alloc` is set, `release` must also be set. |
| `clear` | At `glClear`, with the rectangle (the scissor box) and the values. softgl gives only the buffers that have no mask that keeps bits. The entry gives back the buffers that it cleared. softgl clears the other buffers. |
| `fill_span` | For each row of a flat triangle that has no per-pixel test or operation. |
| `sync` | Before softgl reads or writes the buffers, after `clear` or `fill_span`. |
| `triangle`, `line`, `point` | Before softgl rasterizes the primitive. The entry gives 1 when it drew the primitive, or 0 when softgl must draw it. |
| `software` | Before softgl draws in software, after the hardware drew a primitive. |
| `texture_deleted` | At `glDeleteTextures`, with the id of the texture. |
| `flush` | At `glFlush` (finish 0) and `glFinish` (finish 1). |
| `present` | At `sgl_present`. |
| `detach` | When the context is destroyed, or when it gets a different back end. |

### Clear

`sgl_clear_rect` gives the rectangle and a pointer to its first pixel in
each buffer. Row 0 is the top row. The pitch of each buffer is in
values, not in bytes, except for the stencil buffer (bytes).

- The color value is the 32-bit value of each pixel (`0xAARRGGBB`, or
  the index in color index mode).
- The depth value has `depth_bytes` bytes: 2 on the 68k and on 16-bit
  x86, else 4.
- softgl gives the color buffer only when the color mask (or the index
  mask) is all on. It gives the depth buffer only when the depth mask is
  on. It gives the stencil buffer only when the stencil write mask is
  0xFF.

The entry can start the clear and return before the clear is done.
softgl then calls `sync` before it uses the buffers.

### Flat spans

softgl uses the flat fill when all of these are true for a triangle:

- The triangle has one color (flat shading, or the same color at the
  three vertices).
- No texture, depth test, stencil test, alpha test, blending, fog,
  polygon stipple or logical operation is on.
- The color mask (or the index mask) is all on.

Without a back end, softgl then writes each row with one loop. With a
`fill_span` entry, softgl gives each row to the entry. The entry can
start a blitter and return. softgl calculates the next row while the
blitter operates. softgl calls `sync` before the next primitive that
is not a flat fill.

A short row costs more on a blitter than on the CPU. The entry can
fill a short row with the CPU itself.

### Primitives

`sgl_hw_vertex` gives a vertex in window coordinates:

- `x` and `y` are in pixels from the bottom-left corner.
- `z` is from 0 to 1. softgl adds the polygon offset to `z` of a
  triangle before it calls the entry.
- `w` is 1 / w of the clip coordinates, for perspective.
- `color` is the color of the correct side. With flat shading, it is
  the color of the provoking vertex at the three vertices.
- `spec` is the secondary color (0 when it is not used).
- `fog` is the fog factor: 1 is no fog.
- `tex` has s, t, r and q of the texture units 0 and 1, after the
  texture matrix. They are not divided by q.

The entry reads the GL state of the context to know if the hardware
can draw the primitive. A back end in this repository includes
`src/sgl.h` for this. If the hardware cannot do the state (for example
the logical operation, or color index mode), the entry gives 0.

softgl and the hardware use the same color buffer. Before softgl draws
in software after a hardware primitive, it calls `software`. The back
end then waits for the hardware and makes the buffers of the context
agree with the hardware.

### Textures

`sgl_texture_id(ctx, unit, &serial)` gives the id of the 2D texture of
a unit and its serial. The serial changes when the image or a parameter
of the texture changes. A back end keeps its copy of a texture by the
id. It loads the texture again when the serial changes. At
`glDeleteTextures`, softgl calls `texture_deleted` with the id.

The ids and the serials come from one counter that is not safe for
threads. If two threads change textures at the same time, a back end
can load a texture one time too many. The picture is correct.

## Tests

`tests/backend.c` has a back end that records each call. The test
makes sure that:

- The picture is the same with and without the back end.
- `clear` gets the correct rectangle and values, also in the scissor
  box. A color mask keeps the clear in softgl.
- The flat triangles go to `fill_span`, and the smooth and
  depth-tested triangles do not.
- softgl calls `sync` before `glReadPixels` and before it draws in
  software.
- `triangle` gets the correct window coordinates, color, 1 / w and
  polygon offset. When it gives 0, softgl draws the triangle.
- The texture serials change after `glTexSubImage2D` and
  `glTexParameter`, and `texture_deleted` gets the id.
- Color index mode fills with the index.
