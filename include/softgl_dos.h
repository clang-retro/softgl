/* softgl_dos.h - the hardware functions of the 32-bit DOS back ends:
 * PCI configuration space, physical memory and the VBE video BIOS.
 *
 * The functions are for flat 32-bit DOS programs (clang-retro
 * i386-pc-msdos, DOS/32A). The code is in backends/dos/dos_hw.c.
 *
 * Part of clang-retro softgl, a software implementation of OpenGL.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#ifndef SOFTGL_DOS_H
#define SOFTGL_DOS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A PCI function: its bus, device and function numbers, and its IDs. */
typedef struct sgl_pci_device {
  int bus, dev, fn;
  uint16_t vendor, device;
  uint8_t revision;
} sgl_pci_device;

/* An ID that sgl_pci_find looks for. A device of 0xFFFF agrees with each
 * device of the vendor. The list ends with a vendor of 0. */
typedef struct sgl_pci_id {
  uint16_t vendor, device;
} sgl_pci_id;

/* Finds the index-th PCI function (from 0) whose IDs are in ids. Gives 1
 * and fills out, or 0 when there is no such function. It uses PCI
 * configuration mechanism 1 (ports 0CF8h and 0CFCh). */
int sgl_pci_find(const sgl_pci_id *ids, int index, sgl_pci_device *out);

/* Reads a configuration register of d. reg is a byte offset; the 16-bit
 * and 32-bit reads must be aligned. */
uint8_t sgl_pci_read8(const sgl_pci_device *d, int reg);
uint16_t sgl_pci_read16(const sgl_pci_device *d, int reg);
uint32_t sgl_pci_read32(const sgl_pci_device *d, int reg);

/* Writes a configuration register of d. */
void sgl_pci_write8(const sgl_pci_device *d, int reg, uint8_t value);
void sgl_pci_write16(const sgl_pci_device *d, int reg, uint16_t value);
void sgl_pci_write32(const sgl_pci_device *d, int reg, uint32_t value);

/* Gives the memory address of base address register bar (0 to 5) of d,
 * without the flag bits, or 0 when the BAR is an I/O BAR or is empty. */
uint32_t sgl_pci_bar(const sgl_pci_device *d, int bar);

/* Gives the I/O port of base address register bar of d, or 0 when the
 * BAR is a memory BAR or is empty. */
uint16_t sgl_pci_bar_io(const sgl_pci_device *d, int bar);

/* Makes a linear address for size bytes of physical memory at phys
 * (DPMI function 0800h), so that the program can use it as a pointer.
 * Gives NULL on failure. A program that ends frees the mapping. */
volatile void *sgl_map_physical(uint32_t phys, uint32_t size);

/* Frees a mapping of sgl_map_physical (DPMI function 0801h). */
void sgl_unmap_physical(volatile void *p);

/* The information of a VBE mode (function 4F01h). */
typedef struct sgl_vbe_mode {
  uint16_t attributes;     /* bit 0: the mode is available; bit 7: LFB */
  uint16_t width, height;  /* in pixels */
  uint16_t stride;         /* bytes in each scan line */
  uint8_t bits;            /* bits in each pixel (15 for 5:5:5) */
  uint32_t lfb;            /* the physical address of the LFB */
} sgl_vbe_mode;

/* Gives the information of VBE mode mode in *out. Gives 1 on success.
 * The VBE functions call the video BIOS through DPMI function 0300h. */
int sgl_vbe_mode_info(int mode, sgl_vbe_mode *out);

/* Gives the video memory of the card in bytes, or 0 without VBE 2.0. */
uint32_t sgl_vbe_memory(void);

/* Sets VBE mode mode, with its linear frame buffer when lfb is 1 (VBE
 * 2.0). A VBE 1.2 BIOS has no linear frame buffer: then use lfb 0 and
 * turn on the linear frame buffer with the registers of the card.
 * Gives 1 on success. */
int sgl_vbe_set_mode(int mode, int lfb);

/* Shows the frame buffer from scan line line. With vsync, the change
 * waits for the vertical retrace. Gives 1 on success. */
int sgl_vbe_display_start(int line, int vsync);

/* Sets the text mode 3 (80 x 25) again. */
void sgl_vbe_text_mode(void);

/* Gives the time in milliseconds from an unknown start. */
uint32_t sgl_dos_ms(void);

#ifdef __cplusplus
}
#endif

#endif
