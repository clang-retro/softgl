#!/bin/sh
# setup-86box.sh DIR - gets what tools/test-glide-86box.py needs, in DIR:
#
#   DIR/86Box/build/src/86Box   the 86Box emulator, built from source
#                               without the Qt interface and with the
#                               automation socket
#   DIR/roms                    the ROM files of 86Box
#   DIR/FD13LITE.img            the FreeDOS 1.3 disk image
#
# The commits and the image are fixed, so that each run uses the same
# emulator. On Ubuntu, the script first installs the packages for the
# build (with sudo when it is not root). It needs git, cmake, ninja,
# curl and unzip.
#
# Part of clang-retro softgl, a software implementation of OpenGL.
# SPDX-License-Identifier: AGPL-3.0-only
set -eu
DIR=${1:?usage: setup-86box.sh DIR}
BOX_COMMIT=a04521c1f81925af19818f7acae71cc9b872d7c8
ROMS_COMMIT=de5a7d21982622def2c51a8e04119eea4119fc31
FREEDOS_URL=https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/distributions/1.3/official/FD13-LiteUSB.zip
FREEDOS_SHA256=64a934585087ccd91a18c55e20ee01f5f6762be712eeaa5f456be543778f9f7e
mkdir -p "$DIR"

# fetch NAME URL COMMIT: gets one commit of a repository into DIR/NAME.
fetch() {
  if [ ! -d "$DIR/$1/.git" ]; then
    git init -q "$DIR/$1"
    git -C "$DIR/$1" remote add origin "$2"
  fi
  if [ "$(git -C "$DIR/$1" rev-parse HEAD 2>/dev/null)" != "$3" ]; then
    git -C "$DIR/$1" fetch -q --depth 1 origin "$3"
    git -C "$DIR/$1" checkout -q FETCH_HEAD
  fi
}

if command -v apt-get >/dev/null 2>&1; then
  SUDO=; [ "$(id -u)" = 0 ] || SUDO=sudo
  $SUDO apt-get install -y -q --no-install-recommends cmake ninja-build pkg-config libsdl2-dev libslirp-dev \
    libsndfile1-dev libevdev-dev libxkbcommon-x11-dev libopenal-dev libzstd-dev libarchive-dev \
    libserialport-dev libpng-dev libfreetype-dev mtools xvfb >/dev/null
fi

fetch 86Box https://github.com/86Box/86Box "$BOX_COMMIT"
fetch roms https://github.com/86Box/roms "$ROMS_COMMIT"
if [ ! -x "$DIR/86Box/build/src/86Box" ]; then
  cmake -S "$DIR/86Box" -B "$DIR/86Box/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DQT=OFF -DSDL2=ON \
    -DRTMIDI=OFF -DFLUIDSYNTH=OFF -DMUNT=OFF -DSOUNDCANVAS=OFF -DDISCORD=OFF -DDISASM=OFF -DAUTOMATION=ON >/dev/null
  ninja -C "$DIR/86Box/build" >/dev/null
fi

if [ ! -f "$DIR/FD13LITE.img" ]; then
  curl -sSfL -o "$DIR/freedos.zip" "$FREEDOS_URL"
  echo "$FREEDOS_SHA256  $DIR/freedos.zip" | sha256sum -c - >/dev/null
  unzip -o -q "$DIR/freedos.zip" FD13LITE.img -d "$DIR"
  rm -f "$DIR/freedos.zip"
fi
echo "86Box, its ROMs and FreeDOS are in $DIR"
