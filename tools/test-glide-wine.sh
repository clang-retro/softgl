#!/bin/sh
# test-glide-wine.sh OUT - runs the Glide example under Wine with the
# fake glide2x.dll of tests/wine. OUT is the build directory of
# "make glide" with a Windows 95 compiler; CC is that compiler. The test
# passes when the example draws on the "card" for each frame, swaps 30
# times and gives no bad vertex.
#
# Part of clang-retro softgl, a software implementation of OpenGL.
# SPDX-License-Identifier: AGPL-3.0-only
set -e
OUT=${1:?usage: test-glide-wine.sh OUT}
DIR=$OUT/wine-glide
mkdir -p "$DIR"
# Export each function with the name of the 3dfx DLL: _grX@N.
$CC -O2 -c tests/wine/fake_glide2x.c -o "$DIR/fake.o"
{
  echo "LIBRARY glide2x.dll"
  echo "EXPORTS"
  ${NM:-llvm-nm} "$DIR/fake.o" | awk '/ T _gr/ { s = $3; sub(/^_/, "", s); print "  _" s " = " s }'
} > "$DIR/glide2x.def"
$CC -O2 -shared tests/wine/fake_glide2x.c "$DIR/glide2x.def" -o "$DIR/glide2x.dll"
cp "$OUT/examples/glide/glidecube.exe" "$DIR/"
LOG=$(cd "$DIR" && WINEDEBUG=-all ${WINE:-wine} glidecube.exe 30 2>&1 | tr -d '\r')
echo "$LOG"
echo "$LOG" | grep -q "^fake glide: opens 1 triangles [1-9][0-9]* .*swaps 30 .*bad 0$"
echo "PASS softgl glide under Wine"
