#!/bin/sh
# gen-warp3d-stubs.sh AMIGA_SDK - writes backends/amiga/warp3d_stubs.s
# from backends/amiga/warp3d.fd with tools/fd2stub.py of clang-retro
# amiga-sdk (the path of its checkout is the argument).
#
# Part of clang-retro softgl, a software implementation of OpenGL.
# SPDX-License-Identifier: AGPL-3.0-only
set -e
SDK=${1:?usage: gen-warp3d-stubs.sh AMIGA_SDK}
TMP=$(mktemp -d)
python3 "$SDK/tools/fd2stub.py" "$TMP" backends/amiga/warp3d.fd
OUT=backends/amiga/warp3d_stubs.s
{
  echo "# warp3d_stubs.s - the calls to warp3d.library: each stub moves the C"
  echo "# arguments to the registers of backends/amiga/warp3d.fd and calls the"
  echo "# library vector. Made by tools/gen-warp3d-stubs.sh; do not edit."
  echo "#"
  echo "# Part of clang-retro softgl, a software implementation of OpenGL."
  echo "# SPDX-License-Identifier: AGPL-3.0-only"
  for f in $(grep -o '^sgl_W3D_[A-Za-z]*' backends/amiga/warp3d.fd); do cat "$TMP/$f.s"; done
} > "$OUT"
rm -rf "$TMP"
