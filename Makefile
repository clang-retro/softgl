# Makefile - builds softgl and GLU as static libraries and runs the tests.
#
#   make            builds build/libsoftgl.a (the renderer and GLU)
#   make check      builds and runs the tests in tests/
#   make dispatch   writes src/dispatch.c and src/dispatch.h again
#   make amiga      builds build/libsoftgl-amiga.a (the Amiga back ends)
#                   and the Amiga examples, with the AmigaOS compiler
#   make atari      builds build/libsoftgl-atari.a (the Atari back end)
#                   and its example, with the Atari TOS compiler
#   make glide      builds build/libsoftgl-glide.a (the 3dfx Glide back
#                   end) and its example, with a Windows 9x compiler
#
# Set CC, CFLAGS and AR for a cross compiler, for example:
#   make CC="clang --target=m68k-apple-macos" AR=llvm-ar
#
# Part of clang-retro softgl, a software implementation of OpenGL.
# SPDX-License-Identifier: AGPL-3.0-only

CC ?= cc
CXX ?= c++
AR ?= ar
CFLAGS ?= -O2
OUT ?= build
PYTHON ?= python3

SRCS := $(wildcard src/*.c)
GLU_SRCS := $(filter-out %/priorityq-heap.c,$(wildcard glu/libtess/*.c glu/libutil/*.c))
# The NURBS of GLU are in C++. NURBS=0 leaves them out, for a system
# without a C++ compiler.
NURBS ?= 1
NURBS_SRCS := $(if $(filter 1,$(NURBS)),$(wildcard glu/libnurbs/*/*.cc))
OBJS := $(patsubst %.c,$(OUT)/%.o,$(SRCS) $(GLU_SRCS)) $(patsubst %.cc,$(OUT)/%.o,$(NURBS_SRCS))

SGL_CFLAGS := -std=c99 -Wall -Iinclude -Isrc
GLU_CFLAGS := -std=gnu99 -w -Iinclude -Iglu/include
CXXFLAGS ?= $(CFLAGS)
NURBS_CXXFLAGS := -w -fno-exceptions -fno-rtti -DNDEBUG -DLIBRARYBUILD -Iinclude -Iglu/include -Iglu/libnurbs/internals \
  -Iglu/libnurbs/interface -Iglu/libnurbs/nurbtess

all: $(OUT)/libsoftgl.a

# The back ends of the Amiga. Build them with the AmigaOS compiler.
AMIGA_SRCS := $(wildcard backends/amiga/*.c) backends/common/c2p.c backends/amiga/warp3d_stubs.s
AMIGA_OBJS := $(patsubst %.s,$(OUT)/%.o,$(patsubst %.c,$(OUT)/%.o,$(AMIGA_SRCS)))

amiga: $(OUT)/libsoftgl-amiga.a $(OUT)/examples/amiga/cube $(OUT)/examples/amiga/w3dcube

$(OUT)/examples/amiga/%: examples/amiga/%.c $(OUT)/libsoftgl-amiga.a $(OUT)/libsoftgl.a
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -std=gnu99 -Wall -Iinclude $< $(OUT)/libsoftgl-amiga.a $(OUT)/libsoftgl.a -lm -o $@

# The Atari back end. Build it with the Atari TOS compiler.
ATARI_SRCS := backends/atari/atari.c backends/common/c2p.c
ATARI_OBJS := $(patsubst %.c,$(OUT)/%.o,$(ATARI_SRCS))

atari: $(OUT)/libsoftgl-atari.a $(OUT)/examples/atari/cube.tos

$(OUT)/examples/atari/%.tos: examples/atari/%.c $(OUT)/libsoftgl-atari.a $(OUT)/libsoftgl.a
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -std=gnu99 -Wall -Iinclude $< $(OUT)/libsoftgl-atari.a $(OUT)/libsoftgl.a -lm -o $@

$(OUT)/libsoftgl-atari.a: $(ATARI_OBJS)
	rm -f $@
	$(AR) rcs $@ $^

# The 3dfx Glide back end. Build it with a Windows 95 or 98 compiler.
GLIDE_SRCS := backends/glide/glide.c backends/glide/glide_win32.c
GLIDE_OBJS := $(patsubst %.c,$(OUT)/%.o,$(GLIDE_SRCS))

glide: $(OUT)/libsoftgl-glide.a $(OUT)/examples/glide/glidecube.exe

$(OUT)/examples/glide/%.exe: examples/glide/%.c $(OUT)/libsoftgl-glide.a $(OUT)/libsoftgl.a
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -std=gnu99 -Wall -Iinclude $< $(OUT)/libsoftgl-glide.a $(OUT)/libsoftgl.a -lm -o $@

$(OUT)/libsoftgl-glide.a: $(GLIDE_OBJS)
	rm -f $@
	$(AR) rcs $@ $^

$(OUT)/libsoftgl-amiga.a: $(AMIGA_OBJS)
	rm -f $@
	$(AR) rcs $@ $^

$(OUT)/backends/%.o: backends/%.c src/sgl.h include/softgl.h include/softgl_amiga.h include/softgl_glide.h \
  include/softgl_atari.h include/softgl_c2p.h \
  backends/amiga/warp3d_api.h backends/glide/glide_api.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(SGL_CFLAGS) -c $< -o $@

$(OUT)/backends/%.o: backends/%.s
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@

$(OUT)/libsoftgl.a: $(OBJS)
	rm -f $@
	$(AR) rcs $@ $^

$(OUT)/src/%.o: src/%.c src/sgl.h src/dispatch.h include/softgl.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(SGL_CFLAGS) -c $< -o $@

$(OUT)/glu/%.o: glu/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(GLU_CFLAGS) -c $< -o $@

$(OUT)/glu/%.o: glu/%.cc
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(NURBS_CXXFLAGS) -c $< -o $@

TESTS := $(patsubst tests/%.c,$(OUT)/tests/%,$(wildcard tests/*.c))

$(OUT)/tests/%: tests/%.c tests/harness.h $(wildcard backends/*/*.c backends/*/*.h) $(OUT)/libsoftgl.a
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(SGL_CFLAGS) -c $< -o $@.o
	$(if $(NURBS_SRCS),$(CXX),$(CC)) $(CFLAGS) $@.o $(OUT)/libsoftgl.a -lm -o $@

check: $(TESTS)
	@set -e; for t in $(TESTS); do $$t; done

dispatch:
	$(PYTHON) tools/gen-dispatch.py

clean:
	rm -rf $(OUT)

.PHONY: all amiga atari glide check dispatch clean
