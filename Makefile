# Makefile - builds softgl and GLU as static libraries and runs the tests.
#
#   make            builds build/libsoftgl.a (the renderer and GLU)
#   make check      builds and runs the tests in tests/
#   make dispatch   writes src/dispatch.c and src/dispatch.h again
#   make amiga      builds build/libsoftgl-amiga.a (the Amiga back ends)
#                   and the Amiga examples, with the AmigaOS compiler
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
AMIGA_SRCS := $(wildcard backends/amiga/*.c)
AMIGA_OBJS := $(patsubst %.c,$(OUT)/%.o,$(AMIGA_SRCS))

amiga: $(OUT)/libsoftgl-amiga.a $(OUT)/examples/amiga/cube

$(OUT)/examples/amiga/%: examples/amiga/%.c $(OUT)/libsoftgl-amiga.a $(OUT)/libsoftgl.a
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -std=gnu99 -Wall -Iinclude $< $(OUT)/libsoftgl-amiga.a $(OUT)/libsoftgl.a -lm -o $@

$(OUT)/libsoftgl-amiga.a: $(AMIGA_OBJS)
	rm -f $@
	$(AR) rcs $@ $^

$(OUT)/backends/%.o: backends/%.c src/sgl.h include/softgl.h include/softgl_amiga.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(SGL_CFLAGS) -c $< -o $@

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

$(OUT)/tests/%: tests/%.c tests/harness.h $(OUT)/libsoftgl.a
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(SGL_CFLAGS) -c $< -o $@.o
	$(if $(NURBS_SRCS),$(CXX),$(CC)) $(CFLAGS) $@.o $(OUT)/libsoftgl.a -lm -o $@

check: $(TESTS)
	@set -e; for t in $(TESTS); do $$t; done

dispatch:
	$(PYTHON) tools/gen-dispatch.py

clean:
	rm -rf $(OUT)

.PHONY: all amiga check dispatch clean
