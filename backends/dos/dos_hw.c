/* dos_hw.c - PCI configuration space and physical memory for the 32-bit
 * DOS back ends (see include/softgl_dos.h).
 *
 * PCI uses configuration mechanism 1: the address goes to port 0CF8h
 * and the data is at port 0CFCh. Each PCI system from 1994 has it.
 * Physical memory uses DPMI functions 0800h and 0801h, which DOS/32A
 * has.
 *
 * Part of clang-retro softgl, a software implementation of OpenGL.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
/* CLOCK_MONOTONIC of picolibc needs _GNU_SOURCE. */
#define _GNU_SOURCE
#include "softgl_dos.h"

#include <dos.h>
#include <dpmi.h>
#include <time.h>

#define PCI_ADDR 0xCF8
#define PCI_DATA 0xCFC

/* Gives the value for port 0CF8h that selects register reg of d. */
static uint32_t cfg_addr(const sgl_pci_device *d, int reg) {
  return 0x80000000ul | ((uint32_t)d->bus << 16) | ((uint32_t)d->dev << 11) | ((uint32_t)d->fn << 8) |
         (uint32_t)(reg & 0xFC);
}

/* Reads a 32-bit configuration register of d (reg is aligned). */
uint32_t sgl_pci_read32(const sgl_pci_device *d, int reg) {
  outportl(PCI_ADDR, cfg_addr(d, reg));
  return inportl(PCI_DATA);
}

/* Reads a 16-bit configuration register of d (reg is aligned). */
uint16_t sgl_pci_read16(const sgl_pci_device *d, int reg) {
  return (uint16_t)(sgl_pci_read32(d, reg) >> ((reg & 2) * 8));
}

/* Reads an 8-bit configuration register of d. */
uint8_t sgl_pci_read8(const sgl_pci_device *d, int reg) {
  return (uint8_t)(sgl_pci_read32(d, reg) >> ((reg & 3) * 8));
}

/* Writes a 32-bit configuration register of d (reg is aligned). */
void sgl_pci_write32(const sgl_pci_device *d, int reg, uint32_t value) {
  outportl(PCI_ADDR, cfg_addr(d, reg));
  outportl(PCI_DATA, value);
}

/* Writes a 16-bit configuration register of d (reg is aligned). */
void sgl_pci_write16(const sgl_pci_device *d, int reg, uint16_t value) {
  outportl(PCI_ADDR, cfg_addr(d, reg));
  outport(PCI_DATA + (reg & 2), value);
}

/* Writes an 8-bit configuration register of d. */
void sgl_pci_write8(const sgl_pci_device *d, int reg, uint8_t value) {
  outportl(PCI_ADDR, cfg_addr(d, reg));
  outportb(PCI_DATA + (reg & 3), value);
}

/* Gives 1 when ids has the vendor and the device. */
static int listed(const sgl_pci_id *ids, uint16_t vendor, uint16_t device) {
  for (; ids->vendor; ids++)
    if (ids->vendor == vendor && (ids->device == 0xFFFF || ids->device == device)) return 1;
  return 0;
}

/* Finds the index-th PCI function whose IDs are in ids. It looks at
 * each bus, device and function, and at the functions 1 to 7 only of a
 * multi-function device. */
int sgl_pci_find(const sgl_pci_id *ids, int index, sgl_pci_device *out) {
  sgl_pci_device d;
  int fns;
  for (d.bus = 0; d.bus < 256; d.bus++)
    for (d.dev = 0; d.dev < 32; d.dev++) {
      fns = 1;
      for (d.fn = 0; d.fn < fns; d.fn++) {
        uint32_t id = sgl_pci_read32(&d, 0);
        if ((id & 0xFFFF) == 0xFFFF || (id & 0xFFFF) == 0) continue;
        if (d.fn == 0 && (sgl_pci_read8(&d, 0x0E) & 0x80)) fns = 8;
        d.vendor = (uint16_t)id;
        d.device = (uint16_t)(id >> 16);
        d.revision = sgl_pci_read8(&d, 0x08);
        if (listed(ids, d.vendor, d.device) && index-- == 0) {
          *out = d;
          return 1;
        }
      }
    }
  return 0;
}

/* Gives the memory address of BAR bar of d, or 0 for an I/O BAR. */
uint32_t sgl_pci_bar(const sgl_pci_device *d, int bar) {
  uint32_t v = sgl_pci_read32(d, 0x10 + bar * 4);
  if (v & 1) return 0;
  return v & 0xFFFFFFF0ul;
}

/* Gives the I/O port of BAR bar of d, or 0 for a memory BAR. */
uint16_t sgl_pci_bar_io(const sgl_pci_device *d, int bar) {
  uint32_t v = sgl_pci_read32(d, 0x10 + bar * 4);
  if (!(v & 1)) return 0;
  return (uint16_t)(v & 0xFFFC);
}

/* Maps size bytes of physical memory at phys and gives the linear
 * address. In the flat model of DOS/32A, a linear address is a
 * pointer. */
volatile void *sgl_map_physical(uint32_t phys, uint32_t size) {
  __dpmi_meminfo m;
  m.handle = 0;
  m.address = phys;
  m.size = size;
  if (__dpmi_physical_address_mapping(&m) != 0) return 0;
  return (volatile void *)(uintptr_t)m.address;
}

/* Frees a mapping of sgl_map_physical. */
void sgl_unmap_physical(volatile void *p) {
  __dpmi_meminfo m;
  if (!p) return;
  m.handle = 0;
  m.address = (unsigned long)(uintptr_t)p;
  m.size = 0;
  __dpmi_free_physical_address_mapping(&m);
}

/* Gives the time in milliseconds of the monotonic clock. */
uint32_t sgl_dos_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint32_t)ts.tv_sec * 1000u + (uint32_t)(ts.tv_nsec / 1000000);
}
