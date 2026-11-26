# Software OpenGL (the generic implementation)

Some systems have no OpenGL of their own. On these systems, the `wgl`
functions of clang-retro MFC use softgl, a software renderer. MFC has
softgl as a submodule in `port/softgl`. Windows has a
renderer of the same type: the "generic implementation" of
`opengl32.dll`, which GDI uses when there is no graphics driver for
OpenGL. softgl copies the design of that
implementation, so that OpenGL programs for Windows NT and Windows 95
operate without changes.

## Where the renderer is used

| System | OpenGL under the `wgl` functions |
| --- | --- |
| Linux | The generic implementation (`port/softgl`). |
| Classic Mac OS, 68k | The generic implementation (`port/softgl`). |
| Classic Mac OS, PowerPC | Apple OpenGL through AGL (`port/src/opengl_agl.cpp`). |
| Mac OS X and macOS | The OpenGL framework through CGL (`port/src/opengl.cpp`). |

The Apple renderers have the function of the installable client driver
(ICD) of Windows. The generic implementation is used on all other
portable targets.

To remove the generic implementation, compile with
`-DWINPORT_NO_SOFTGL`. Then `ChoosePixelFormat` fails with
`ERROR_NOT_SUPPORTED`, as before.

## What the renderer does

The renderer gives all of OpenGL 1.5 (without the optional imaging
subset of OpenGL 1.2). It stops at OpenGL 1.5: there is no GLSL. It exports the OpenGL 1.1 functions,
as `opengl32.dll` of Windows NT 4.0 exports them:

- `glGetString(GL_VERSION)` gives `1.5.0`.
  `glGetString(GL_RENDERER)` gives `clang-retro Generic`.
- These extensions are available. `wglGetProcAddress` gives their
  functions:
  - `GL_WIN_swap_hint` (`glAddSwapHintRectWIN`)
  - `GL_EXT_bgra`
  - `GL_EXT_paletted_texture` (`glColorTableEXT` and the related
    functions)
  - `GL_EXT_vertex_array` (`glVertexPointerEXT` and the related
    functions)
  - `GL_ARB_multitexture` (`glActiveTextureARB`,
    `glClientActiveTextureARB` and `glMultiTexCoord*ARB`)
  - `GL_ARB_transpose_matrix` (`glLoadTransposeMatrix*ARB` and
    `glMultTransposeMatrix*ARB`)
  - `GL_ARB_texture_env_add` and `GL_EXT_texture_env_add`
  - `GL_SGIS_texture_edge_clamp` and `GL_EXT_texture_edge_clamp`
  - `GL_ARB_texture_border_clamp` and `GL_ARB_texture_mirrored_repeat`
  - `GL_SGIS_texture_lod` (the LOD limits and the base and maximum
    levels)
  - `GL_EXT_packed_pixels` (the packed types such as
    `GL_UNSIGNED_SHORT_5_6_5` and `GL_UNSIGNED_INT_8_8_8_8_REV`)
  - `GL_EXT_separate_specular_color`
  - `GL_EXT_rescale_normal`
  - `GL_EXT_draw_range_elements` (`glDrawRangeElementsEXT`)
  - `GL_EXT_texture3D` (`glTexImage3DEXT` and `glTexSubImage3DEXT`)
  - `GL_ARB_texture_cube_map` and `GL_EXT_texture_cube_map`
  - `GL_ARB_texture_env_combine`, `GL_ARB_texture_env_dot3` and
    `GL_ARB_texture_env_crossbar` (also the `EXT` names of the first
    two)
  - `GL_ARB_texture_compression` and `GL_EXT_texture_compression_s3tc`
    (`glCompressedTexImage2DARB` and the related functions)
  - `GL_ARB_multisample` (`glSampleCoverageARB`)
  - `GL_EXT_blend_color`, `GL_EXT_blend_minmax`,
    `GL_EXT_blend_subtract`, `GL_EXT_blend_func_separate` and
    `GL_NV_blend_square` (`glBlendColorEXT`, `glBlendEquationEXT` and
    `glBlendFuncSeparateEXT`)
  - `GL_EXT_stencil_wrap` and `GL_EXT_texture_lod_bias`
  - `GL_SGIS_generate_mipmap`
  - `GL_ARB_depth_texture` and `GL_ARB_shadow`
  - `GL_ARB_window_pos` (`glWindowPos*ARB`)
  - `GL_EXT_multi_draw_arrays` (`glMultiDrawArraysEXT` and
    `glMultiDrawElementsEXT`)
  - `GL_ARB_point_parameters` and `GL_EXT_point_parameters`
    (`glPointParameterfARB` and the related functions)
  - `GL_EXT_secondary_color` (`glSecondaryColor3*EXT` and
    `glSecondaryColorPointerEXT`)
  - `GL_EXT_fog_coord` (`glFogCoord*EXT` and `glFogCoordPointerEXT`)
  - `GL_ARB_vertex_buffer_object` (`glGenBuffersARB` and the related
    functions)
  - `GL_ARB_occlusion_query` (`glGenQueriesARB` and the related
    functions)
  - `GL_EXT_shadow_funcs`
- `wglGetProcAddress` also gives the functions of these extensions by
  their OpenGL 1.2 to 1.5 names, without the `ARB` or `EXT`
  suffix (for example `glActiveTexture`, `glDrawRangeElements`,
  `glTexImage3D`, `glCompressedTexImage2D`, `glBlendFuncSeparate` and
  `glWindowPos2i`, `glBindBuffer` and `glBeginQuery`).
- Vertices: immediate mode, vertex arrays, buffer objects (OpenGL
  1.5), display lists, evaluators (`glMap1`, `glMap2`, `glEvalMesh`)
  and rectangles.
- Transformation: the modelview and projection matrix stacks, one
  texture matrix stack for each texture unit, the viewport, the depth
  range and six user clip planes.
- Lighting: eight lights, two-sided lighting, local viewer, spot
  lights, attenuation and `glColorMaterial`.
- Texture coordinate generation: object linear, eye linear, sphere
  map, normal map and reflection map, for each texture unit.
- Rasterization: points, lines (width and stipple), polygons (fill,
  line and point modes, polygon stipple, culling, polygon offset), flat
  and smooth shading.
- Textures: 1D, 2D, 3D and cube maps, depth textures, mipmaps (also
  generated: `GL_GENERATE_MIPMAP`), nearest and linear filters, the
  wrap modes (repeat, clamp, clamp to edge, clamp to border and
  mirrored repeat), the LOD limits and the LOD bias (of the texture
  and of the unit), the base and maximum levels, texture
  objects, the texture environments (`GL_MODULATE`, `GL_DECAL`,
  `GL_BLEND`, `GL_REPLACE`, `GL_ADD` and `GL_COMBINE`) and perspective
  correction.
- `GL_COMBINE`: the functions `GL_REPLACE`, `GL_MODULATE`, `GL_ADD`,
  `GL_ADD_SIGNED`, `GL_INTERPOLATE`, `GL_SUBTRACT`, `GL_DOT3_RGB` and
  `GL_DOT3_RGBA`, the scales, and the sources of OpenGL 1.4
  (`GL_TEXTUREn` gives the texel of an other unit).
- Compressed textures: the four S3TC formats (DXT1, DXT3 and DXT5).
  The renderer decodes the blocks when it gets the image. It keeps the
  blocks for `glGetCompressedTexImage`. The generic formats (for
  example `GL_COMPRESSED_RGB`) are kept without compression.
- Multisample: the state of OpenGL 1.3 (`GL_MULTISAMPLE`,
  `glSampleCoverage`). There is no multisample buffer:
  `GL_SAMPLE_BUFFERS` is 0, and the state has no effect.
- Separate specular color (`GL_LIGHT_MODEL_COLOR_CONTROL`): the
  renderer adds the specular color after the texture.
- Secondary color (OpenGL 1.4): without lighting, `GL_COLOR_SUM` adds
  the current secondary color (`glSecondaryColor3f` or the secondary
  color array) after the texture.
- Fog coordinate (OpenGL 1.4): with `GL_FOG_COORDINATE_SOURCE` set to
  `GL_FOG_COORDINATE`, the fog uses the current fog coordinate
  (`glFogCoordf` or the fog coordinate array) in place of the eye
  distance.
- Points: the size, and the point parameters of OpenGL 1.4 (the
  distance attenuation and the minimum and maximum size). The fade
  threshold applies to multisample rasterization only, thus it has no
  effect.
- `glWindowPos` (OpenGL 1.4) sets the raster position in window
  coordinates. `glMultiDrawArrays` and `glMultiDrawElements` draw
  some ranges of the arrays in one call.
- Normals: `GL_NORMALIZE` and `GL_RESCALE_NORMAL`.
- Multitexture: 2 texture units (see below). Each unit has its own
  enables, binding, environment, coordinate generation, texture
  matrix, current coordinates and coordinate array.
- Per-fragment operations: fog, scissor, alpha test, stencil (with
  `GL_INCR_WRAP` and `GL_DECR_WRAP`), depth test, blending, the
  logical operation (`glLogicOp`) and color mask.
- Blending of OpenGL 1.4: separate factors for RGB and for alpha
  (`glBlendFuncSeparate`), the constant color (`glBlendColor`), the
  equations `GL_FUNC_ADD`, `GL_FUNC_SUBTRACT`,
  `GL_FUNC_REVERSE_SUBTRACT`, `GL_MIN` and `GL_MAX`
  (`glBlendEquation`), `GL_SRC_COLOR` as a source factor and
  `GL_DST_COLOR` as a destination factor.
- Pixels: the packed pixel types of OpenGL 1.2, with `GL_BGR` and
  `GL_BGRA`. `glDrawPixels`, `glReadPixels`, `glCopyPixels`, `glBitmap`,
  the pixel store and zoom. Color, color index, stencil and depth
  images. The pixel transfer: scale and bias, index shift and offset,
  and the pixel maps (`GL_MAP_COLOR`, `GL_MAP_STENCIL`, and the
  `I_TO_R`, `I_TO_G`, `I_TO_B` and `I_TO_A` maps for color index
  images in RGBA mode).
- The accumulation buffer, `glPushAttrib` and `glPushClientAttrib`.
- Selection (`GL_SELECT`): the name stack and the hit records.
- Feedback (`GL_FEEDBACK`): the five vertex types, `glPassThrough`,
  and the tokens of points, lines, polygons, bitmaps and pixel
  rectangles.
- Color index mode (see below).

### Texture units

The number of texture units is `SGL_MAX_TEXTURE_UNITS` (default 2,
maximum 32). Each unit adds 16 bytes to each vertex and some time to
each textured pixel. A build for a small machine can set 1:

```sh
make CFLAGS="-O2 -DSGL_MAX_TEXTURE_UNITS=1"
```

The units apply in order: unit 0 uses the primary color, and each
next unit uses the result of the unit before it. The rasterizer does
the work only for the units that have a texture on. Thus a program
that uses one unit is not slower with 2 units in the build.

### 3D textures

A 3D texture is a stack of 2D images. The size of each side must be a
power of two, and not more than `SGL_MAX_3D_TEXTURE_SIZE` (default 64).
A 64 x 64 x 64 texture uses 1 MB. Set a smaller value for a small
machine:

```sh
make CFLAGS="-O2 -DSGL_MAX_3D_TEXTURE_SIZE=16"
```

The nearest filter reads one image. The linear filter reads the two
nearest images and mixes them. Thus a 3D texture with the linear
filter takes approximately two times the time of a 2D texture. The
renderer does the work of the r coordinate only for the primitives
that have a 3D texture on.

### Cube maps

A face of a cube map is square, and not larger than
`SGL_MAX_CUBE_MAP_SIZE` (default 256). For each pixel, the renderer
finds the face from the largest component of (s, t, r) and divides the
other two by it. This is one integer division for each pixel: on the
68000, a cube map is slower than a 2D texture.

### GL_COMBINE

The renderer reads the texels of all the units before it applies the
environments, so that a unit can use the texel of an other unit. This
occurs only when a unit uses `GL_COMBINE`; the other environments keep
their fast path.

`GL_CLAMP` operates as `GL_CLAMP_TO_EDGE`: the renderer does not keep
the border texels. Use `GL_CLAMP_TO_BORDER` to get the border color.

### Depth textures and shadows

A texture with a depth format (`GL_DEPTH_COMPONENT`,
`GL_DEPTH_COMPONENT16`, `GL_DEPTH_COMPONENT24` or
`GL_DEPTH_COMPONENT32`) is 1D or 2D. Each texel keeps the depth in 30
bits. `glCopyTexImage2D` and `glCopyTexSubImage2D` copy the depth
buffer into a depth texture. `GL_TEXTURE_DEPTH_SIZE` gives 24.

With `GL_TEXTURE_COMPARE_MODE` set to `GL_COMPARE_R_TO_TEXTURE`, the
renderer compares the r coordinate with the depth of the texel by
`GL_TEXTURE_COMPARE_FUNC`. The result is 0 or 1. `GL_DEPTH_TEXTURE_MODE`
makes the result (or the depth, without the comparison) a luminance,
an intensity or an alpha. The linear filter mixes the results of the
four nearest texels: the edge of a shadow is soft.

The comparison needs the r coordinate for each pixel. Thus a shadow
texture takes approximately the time of a 3D texture with the nearest
filter.

### Buffer objects

A buffer object (OpenGL 1.5) is a block of memory from `malloc`. While
a buffer is bound to `GL_ARRAY_BUFFER`, the pointer of `glVertexPointer`
and the other array functions is an offset in that buffer. While a
buffer is bound to `GL_ELEMENT_ARRAY_BUFFER`, the indices of
`glDrawElements` and `glDrawRangeElements` are an offset in it.
`glMapBuffer` gives the address of the memory: there is no copy. The
renderer reads the buffer when it draws, thus a buffer is not faster
than an array in the memory of the program.

The buffers are shared with the display lists and the textures
(`wglShareLists`). The name table and each binding hold a reference to
a buffer. `glDeleteBuffers` removes the name and the bindings of the
current context; a binding that remains (in an other context, or in
the state that `glPushClientAttrib` saved) keeps the buffer until it
is changed.

A display list keeps the vertices that an array command gives, not
the buffer.

### Occlusion queries

An occlusion query (OpenGL 1.5) counts the samples that pass the
depth test between `glBeginQuery` and `glEndQuery`. Each pixel is one
sample. The renderer draws before each call returns, thus the result
is available at once. The query objects are not shared between
contexts. The count costs one test for each pixel that passes.

The stencil and depth tests come after the alpha test, as the
specification tells. Without the alpha test, they come before the
texture: a pixel that fails them gets no texture, which is faster.

### Generated mipmaps

With `GL_GENERATE_MIPMAP` on, a change of the base level (by
`glTexImage`, `glTexSubImage` or `glCopyTexImage`) makes all the
levels after it again. Each texel is the mean of the 2, 4 or 8 texels
of the level before it. This occurs when the image changes, not when
the renderer draws.

### Color index mode

`sgl_create_index_context` makes a context in color index mode, for a
pixel format of type `PFD_TYPE_COLORINDEX`. Each pixel of the color
buffer holds an index in its low bits. The window layer changes the
indices to colors through its palette when it copies the buffer.

In color index mode, these operations use indices: clear
(`glClearIndex`), flat and smooth shading, lighting with the color
indices of the material (`GL_COLOR_INDEXES`), fog (`GL_FOG_INDEX`),
the logical operation (`GL_INDEX_LOGIC_OP`), the index mask
(`glIndexMask`), feedback, and `glDrawPixels`, `glReadPixels` and
`glCopyPixels` with indices. Blending, the alpha test and textures are
not used in color index mode, as the specification tells.

GLU 1.3 is also available: the SGI reference GLU (matrices, mipmaps,
image scale, quadrics, tessellation and NURBS). Its license is the SGI Free
Software License B 2.0. See [glu/README.md](../glu/README.md).

## How the renderer is built

The renderer is in C (C99). The folders are:

- `src`: the renderer.
- `glu`: GLU (`libtess` and `libutil`).
- `include/GL`: `gl.h`, `glext.h` and `glu.h`.
- `include/softgl.h`: the interface to the window layer.

The NURBS of GLU (`glu/libnurbs`) are in C++. The makefile compiles
them with `-fno-exceptions -fno-rtti`, so that they need only
`operator new` and `operator delete` from the C++ runtime. To build
without a C++ compiler, set `NURBS=0`. Then the `gluNurbs` functions
are not available.

To build `build/libsoftgl.a` (the renderer and GLU) and run the tests:

```sh
make check
```

For a cross compiler, set `CC`, `CXX`, `CFLAGS` and `AR`. For
example, with the clang-retro toolchain for AmigaOS:

```sh
make CC="clang --config=amiga.cfg" CXX="clang++ --config=amiga.cfg" AR=llvm-ar
```

In clang-retro MFC, the MFC makefile compiles the same files from
`port/softgl` and puts the objects in `libwinport.a`. A program links
only with `libwinport.a`. It does not need other libraries for OpenGL.
Add `-Iport/softgl/include` after `-Iport/include`, so that
`<GL/gl.h>` finds the headers.

`libglaux.a` gives the `aux` functions of the Windows SDK
(`GL/glaux.h`) on these systems too.

### The dispatch table

Each `gl` function calls the entry of the current dispatch table, as
`opengl32.dll` does. There are three tables:

- The table of no context. Its functions do nothing.
- The table that executes.
- The table that compiles a display list.

`glNewList` and `glEndList` change the table.
`tools/gen-dispatch.py` makes `src/dispatch.c` and
`src/dispatch.h` from `include/GL/gl.h`. Do not edit the generated
files. Change the script and run it again:

```sh
make dispatch
```

The portable layer of MFC defines `glFlush` and `glFinish` itself: for
a single-buffered pixel format, they copy the picture to the window.
Thus the MFC makefile compiles the renderer with `-DSGL_LAYER_FLUSH`, which
removes these two functions from `dispatch.c`.

### The interface to the window layer

`include/softgl.h` gives the functions that
`port/src/opengl_soft.cpp` of MFC uses:

| Function | Purpose |
| --- | --- |
| `sgl_create_context` | Makes a rendering context. It can share display lists and textures with another context. |
| `sgl_create_index_context` | Makes a rendering context in color index mode, with indices of 1 to 16 bits. |
| `sgl_destroy_context` | Destroys a context. |
| `sgl_make_current` | Makes a context the current context of the thread. |
| `sgl_set_buffer` | Gives the color buffer to the context. The context makes its depth, stencil and accumulation buffers in the same size. |
| `sgl_copy_context` | Copies attribute groups (`wglCopyContext`). |
| `sgl_share_lists` | Shares display lists (`wglShareLists`). |
| `sgl_get_proc` | Gives an extension function by its name (`wglGetProcAddress`). |

The color buffer has one 32-bit value for each pixel, `0xAARRGGBB` in
the byte order of the machine. The top row is first. The width and the
height must not be more than 32767 pixels.

To use the renderer on a different system, write a file that has the
same function as `opengl_soft.cpp`: give a buffer to the context, and
copy the buffer to the screen after `SwapBuffers` or `glFlush`.

A driver can also give a context a back end (`sgl_set_backend`). A
back end uses hardware for some of the work: clears, flat spans,
primitives and the display of the frame. See
[backends.md](backends.md).

## Speed on the 68000

The 68000 has no floating-point unit and no 32-bit multiply
instruction. The renderer is designed for these limits:

- Geometry (transformation, lighting and clipping) is in `float`. This
  work is done once for each vertex.
- The rasterizer converts each attribute plane of a triangle to fixed
  point. Each span starts and steps with integer operations.
- Color, blend and texture operations multiply 16-bit values only
  (`SGL_MUL16`). The compiler makes one `muls` or `mulu` instruction
  for each multiply.
- The depth buffer has 16 bits for each pixel on the 68k and on 16-bit
  x86. It has 32 bits on other systems. Set `SGL_DEPTH_BITS` to change
  it.
- Perspective correction divides once for each 16 pixels.
  `glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST)` removes the
  division.
- A triangle of one color with no per-pixel test or operation (no
  depth test, blending, texture, fog, stencil, stipple, color mask or
  logical operation) is a fill of each row with one 32-bit value. A
  back end can give these rows to a blitter (`fill_span`).
- The table of the specular power keeps the four last shininess
  values. A program that changes between some materials does not
  calculate the table again.

Most of the remaining time is in the floating-point functions of the
runtime library (`__mulsf3`, `__addsf3`). A faster runtime library
makes the renderer faster without changes to the renderer.

## Tests

`make check` builds and runs each test in `tests/`. Each test draws
scenes into a 64 x 64 buffer in memory and examines the pixels or the
values that OpenGL gives back:

- `tests/render.c`: flat and smooth shading, depth, a lit sphere in a
  display list, a texture with perspective, vertex arrays, an
  evaluator mesh, blending and the scissor. The output is
  `PASS softgl render`.
- `tests/gl11.c`: points, selection, feedback, the logical operation,
  the polygon stipple, color index images through the pixel maps,
  stencil and depth images, GLU NURBS (drawn and tessellated), and
  color index mode. The output is `PASS softgl gl11`.
- `tests/gl12.c`: the packed pixel types (draw and read), separate
  specular color, the wrap modes, the LOD limits and the base level,
  `glDrawRangeElements`, `GL_RESCALE_NORMAL` and the 3D textures. The
  output is `PASS softgl gl12`.
- `tests/gl13.c`: two texture units (immediate mode, vertex arrays and
  a display list), a texture matrix for one unit, `glPushAttrib` of
  the units, `GL_ADD`, `GL_CLAMP_TO_EDGE`, the transpose matrices, the
  names that `wglGetProcAddress` gives, cube maps (the faces, a texel
  in a face, `GL_NORMAL_MAP` and `GL_REFLECTION_MAP`), `GL_COMBINE`
  (the functions, the scale, `GL_DOT3_RGB` and a source of an other
  unit), the S3TC formats and the multisample state. The output is
  `PASS softgl gl13`.
- `tests/gl14.c`: the blend factors, the constant color and the
  equations, the stencil wrap, the LOD bias, the generated mipmaps,
  `glWindowPos`, `glMultiDrawArrays`, the point parameters, the
  secondary color (immediate mode, array and display list), the fog
  coordinate, the depth textures (from pixels and from the depth
  buffer) and the shadow comparison. The output is
  `PASS softgl gl14`.
- `tests/gl15.c`: the buffer objects (arrays and elements, the map,
  the sub data, the deletion, display lists, `glPushClientAttrib` and
  a shared context), the occlusion queries (also with the depth test,
  the alpha test and in a display list) and the eight shadow
  functions. The output is `PASS softgl gl15`.
- `tests/backend.c`: the back-end interface. A back end in the test
  records each call. The test makes sure that the picture is the same
  with and without the back end, that the entries get the correct
  values, that softgl waits for the work of the back end before it
  uses the buffers, and that the texture serials change. The output is
  `PASS softgl backend`.
- `tests/c2p.c`: the chunky-to-planar code of the Amiga and Atari back
  ends: the pens, the palette, the bit planes (compared with a
  conversion of one bit at a time), the interleaved planes and RGB 565.
  The output is `PASS softgl c2p`.
- `tests/glide.c`: the Glide back end with a fake Glide table. The
  output is `PASS softgl glide`.

CI also compiles the renderer, GLU and the NURBS for AmigaOS and for
Atari TOS (68000) with the clang-retro toolchain. For AmigaOS, it also
builds the Amiga back end and links `examples/amiga/cube.c`. For Atari
TOS, it builds the Atari back end and its example. For Windows 95, it
builds the Glide back end. Two more CI jobs run programs in emulators:

- `tools/test-glide-wine.sh` runs `glidecube.exe` with a fake
  `glide2x.dll` in Wine (see [glide.md](glide.md#tests)).
- `tools/test-atari-hatari.sh` runs `tests/atari/present.c` in Hatari
  on an ST, an STE and a Falcon (see [atari.md](atari.md#tests)).

In clang-retro MFC:

- MFC's `tests/gltest.cpp` uses the `wgl` functions. With the generic
  implementation, it also draws a triangle and examines its pixels.
  `make check` runs it on Linux. `make check TARGET=m68k-apple-macos`
  runs it in the 68000 and 68020 simulators of mac-sdk.
- MFC's `tools/build-sdk-samples.py` builds the OpenGL screen savers of the
  Win32 SDK for Linux and for the 68k Mac. See
  `docs/sdk-samples.md` in MFC.

## Limits

- Dithering has no effect. The buffer has 8 bits for each component.
- Smooth points, lines and polygons are drawn without antialiasing.
- The width and the height of a texture must be powers of two.
- There is no multisample buffer. Thus multisample antialiasing has
  no effect.
- There is no compressed format for 1D and 3D textures.
- A depth texture is 1D or 2D: a 3D texture or a cube map with a depth
  format is not permitted, as in OpenGL 1.4.
- Pixel rectangles (`glDrawPixels`, `glBitmap`) are not textured.
- The pixel format tells 32 depth bits. On the 68k, the depth buffer
  has 16 bits (`glGetIntegerv(GL_DEPTH_BITS)` gives 16).

## Other systems

The renderer has no dependency on the window layer. It compiles for
the systems that have 32-bit pointers, such as AmigaOS, Atari TOS,
32-bit DOS and OS/2. It does not compile for 16-bit x86 targets
(16-bit DOS and 16-bit Windows): a context is more than 64 KB. A front
end for each system must copy the color buffer to the screen, as
`opengl_soft.cpp` does through GDI.
