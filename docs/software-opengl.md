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

The renderer gives the functions of OpenGL 1.1, as `opengl32.dll` of
Windows NT 4.0 gives them:

- `glGetString(GL_VERSION)` gives `1.1.0`.
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
- `wglGetProcAddress` also gives the functions of these extensions by
  their OpenGL 1.3 names, without the `ARB` suffix (for example
  `glActiveTexture`). `GL_VERSION` stays `1.1.0` until all of
  OpenGL 1.2 and 1.3 is available.
- Vertices: immediate mode, vertex arrays, display lists, evaluators
  (`glMap1`, `glMap2`, `glEvalMesh`) and rectangles.
- Transformation: the modelview and projection matrix stacks, one
  texture matrix stack for each texture unit, the viewport, the depth
  range and six user clip planes.
- Lighting: eight lights, two-sided lighting, local viewer, spot
  lights, attenuation and `glColorMaterial`.
- Texture coordinate generation: object linear, eye linear and sphere
  map, for each texture unit.
- Rasterization: points, lines (width and stipple), polygons (fill,
  line and point modes, polygon stipple, culling, polygon offset), flat
  and smooth shading.
- Textures: 1D and 2D, mipmaps, nearest and linear filters, repeat,
  clamp and clamp to edge, texture objects, the texture environments
  (`GL_MODULATE`, `GL_DECAL`, `GL_BLEND`, `GL_REPLACE` and `GL_ADD`)
  and perspective correction.
- Multitexture: 2 texture units (see below). Each unit has its own
  enables, binding, environment, coordinate generation, texture
  matrix, current coordinates and coordinate array.
- Per-fragment operations: fog, scissor, alpha test, stencil, depth
  test, blending, the logical operation (`glLogicOp`) and color mask.
- Pixels: `glDrawPixels`, `glReadPixels`, `glCopyPixels`, `glBitmap`,
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

`GL_CLAMP` operates as `GL_CLAMP_TO_EDGE`: the renderer does not keep
the border texels, and the linear filter does not use the border
color.

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
- `tests/gl13.c`: two texture units (immediate mode, vertex arrays and
  a display list), a texture matrix for one unit, `glPushAttrib` of
  the units, `GL_ADD`, `GL_CLAMP_TO_EDGE`, the transpose matrices and
  the names that `wglGetProcAddress` gives. The output is
  `PASS softgl gl13`.

CI also compiles the renderer, GLU and the NURBS for AmigaOS and for
Atari TOS (68000) with the clang-retro toolchain.

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
- Of OpenGL 1.2 and 1.3, only multitexture, the transpose matrices,
  `GL_ADD` and `GL_CLAMP_TO_EDGE` are available. `glTexImage3D` is a
  stub for GLU only.
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
