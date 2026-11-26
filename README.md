# softgl

softgl is a software renderer for OpenGL. It copies the design of the
"generic implementation" of `opengl32.dll` on Windows NT. It gives
OpenGL 1.2 with the Windows extensions and multitexture, and GLU 1.3
from the SGI reference code.
It operates on a 68000 without a floating-point unit. clang-retro MFC
uses it under the `wgl` functions on the systems that have no OpenGL
of their own.

To build and test: `make check`. For the design, the build, the tests
and the limits, see [docs/software-opengl.md](docs/software-opengl.md).

The renderer is under `AGPL-3.0-only` (see [LICENSE](LICENSE)). GLU
(`glu/`) is under the SGI Free Software License B 2.0; see
[glu/README.md](glu/README.md).
