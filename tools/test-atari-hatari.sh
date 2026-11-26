#!/bin/sh
# test-atari-hatari.sh OUT - runs tests/atari/present.c in the Hatari
# emulator with EmuTOS, on an ST (no Blitter, 68000), an STE (Blitter,
# 68000) and a Falcon (Blitter, 68030, true color). OUT is the build
# directory of "make all atari" with the Atari TOS compiler; CC is that
# compiler; CFLAGS (default -O2) builds the test. The script downloads
# EmuTOS 1.3 to OUT once and checks its SHA-256.
#
# Part of clang-retro softgl, a software implementation of OpenGL.
# SPDX-License-Identifier: AGPL-3.0-only
set -e
OUT=${1:?usage: test-atari-hatari.sh OUT}
CFLAGS=${CFLAGS:--O2}
ZIP=$OUT/emutos-1024k-1.3.zip
SUM=076d451f15ddf7b64530c14431142b026569b1e5d6becc1af37aa008db81333f
if [ ! -f "$ZIP" ]; then
  curl -sSfL -o "$ZIP" https://downloads.sourceforge.net/project/emutos/emutos/1.3/emutos-1024k-1.3.zip
fi
echo "$SUM  $ZIP" | sha256sum -c -
rm -rf "$OUT/emutos" "$OUT/hd"
mkdir -p "$OUT/emutos" "$OUT/hd"
unzip -q -o "$ZIP" -d "$OUT/emutos"
TOS=$(find "$OUT/emutos" -name etos1024k.img | head -n 1)
$CC $CFLAGS -std=gnu99 -Wall -Iinclude tests/atari/present.c "$OUT/libsoftgl-atari.a" "$OUT/libsoftgl.a" -lm \
  -o "$OUT/hd/PRESENT.TOS"
fail=0
for machine in st ste falcon; do
  rm -f "$OUT/hd/RESULT.TXT"
  SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy hatari --machine $machine --memsize 4 --tos "$TOS" \
    -d "$OUT/hd" --auto 'C:\PRESENT.TOS' --fast-forward on --sound off --confirm-quit off \
    --run-vbls 20000 > "$OUT/hatari-$machine.log" 2>&1 || true
  result=$(tr -d '\r' < "$OUT/hd/RESULT.TXT" 2>/dev/null || echo "no result")
  echo "$machine: $result" | tr '\n' ' '
  echo
  echo "$result" | grep -q "^PASS softgl atari$" || fail=1
done
exit $fail
