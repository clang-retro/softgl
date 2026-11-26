/* glide_dos.c - finds the 3D card of a 32-bit DOS computer for the
 * Glide back end: it tries each built-in driver in turn.
 *
 * Part of clang-retro softgl, a software implementation of OpenGL.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#if defined(__MSDOS__) && defined(__i386__)

#include <stdint.h>
#include <string.h>

#include "softgl_glide.h"

/* A built-in driver: its name and its fill function. */
typedef struct {
  const char *name;
  int (*fill)(sgl_glide_api *api);
} driver;

/* The built-in drivers, the Voodoo first. */
static const driver drivers[] = {{"voodoo", sgl_glide_sst1}, {"virge", sgl_glide_virge},
                                      {"matrox", sgl_glide_mga}};

/* Gives 1 when the driver in api finds its card. */
static int has_card(const sgl_glide_api *api) {
  int32_t hw[64];
  int ok;
  memset(hw, 0, sizeof hw);
  api->GlideInit();
  ok = api->SstQueryHardware(hw) && hw[0] > 0;
  api->GlideShutdown();
  return ok;
}

/* Fills api with the driver of the first card that is found (see
 * softgl_glide.h). */
const char *sgl_glide_dos(sgl_glide_api *api, const char *name) {
  size_t i;
  for (i = 0; i < sizeof drivers / sizeof drivers[0]; i++) {
    if (name && strcmp(name, drivers[i].name) != 0) continue;
    if (drivers[i].fill(api) && has_card(api)) return drivers[i].name;
  }
  memset(api, 0, sizeof *api);
  return NULL;
}

#endif
