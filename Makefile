# Makefile - builds softgl and GLU as static libraries and runs the tests.
#
#   make            builds build/libsoftgl.a (the renderer and GLU)
#   make check      builds and runs tests/render.c
#   make dispatch   writes src/dispatch.c and src/dispatch.h again
#
# Set CC, CFLAGS and AR for a cross compiler, for example:
#   make CC="clang --target=m68k-apple-macos" AR=llvm-ar
#
# Part of clang-retro softgl, a software implementation of OpenGL.
# SPDX-License-Identifier: AGPL-3.0-only

CC ?= cc
AR ?= ar
CFLAGS ?= -O2
OUT ?= build
PYTHON ?= python3

SRCS := $(wildcard src/*.c)
GLU_SRCS := $(filter-out %/priorityq-heap.c,$(wildcard glu/libtess/*.c glu/libutil/*.c))
OBJS := $(patsubst %.c,$(OUT)/%.o,$(SRCS) $(GLU_SRCS))

SGL_CFLAGS := -std=c99 -Wall -Iinclude -Isrc
GLU_CFLAGS := -std=gnu99 -w -Iinclude -Iglu/include

all: $(OUT)/libsoftgl.a

$(OUT)/libsoftgl.a: $(OBJS)
	rm -f $@
	$(AR) rcs $@ $^

$(OUT)/src/%.o: src/%.c src/sgl.h src/dispatch.h include/softgl.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(SGL_CFLAGS) -c $< -o $@

$(OUT)/glu/%.o: glu/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(GLU_CFLAGS) -c $< -o $@

$(OUT)/render: tests/render.c $(OUT)/libsoftgl.a
	$(CC) $(CFLAGS) $(SGL_CFLAGS) $< $(OUT)/libsoftgl.a -lm -o $@

check: $(OUT)/render
	$(OUT)/render

dispatch:
	$(PYTHON) tools/gen-dispatch.py

clean:
	rm -rf $(OUT)

.PHONY: all check dispatch clean
