/*
 * yagbe - Yet Another Game Boy Emulator
 *
 * Copyright 2021 Michael Rodriguez aka kaichiuchu <mike@kaichiuchu.dev>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef LIBYAGBE_BUS_H
#define LIBYAGBE_BUS_H

#include "apu.h"
#include "cart.h"
#include "ppu.h"
#include "timer.h"

enum libyagbe_bus_memory_size {
  LIBYAGBE_BUS_MEM_SIZE_WRAM = 8192,
  LIBYAGBE_BUS_MEM_SIZE_HRAM = 128
};

enum libyagbe_bus_registers {
  /** $FF0F */
  LIBYAGBE_BUS_IO_IF = 0xF,

  /** $FFFF */
  LIBYAGBE_BUS_IO_IE = 0xF
};

/** Defines the system bus.
 *
 * The system bus is really just the interconnect between the CPU, memory, and
 * peripherals.
 */
struct libyagbe_bus {
  struct libyagbe_apu apu;
  struct libyagbe_cart cart;
  struct libyagbe_timer timer;
  struct libyagbe_ppu ppu;

  uint8_t wram[LIBYAGBE_BUS_MEM_SIZE_WRAM];
  uint8_t hram[LIBYAGBE_BUS_MEM_SIZE_HRAM];

  uint8_t interrupt_flag;
  uint8_t interrupt_enable;
};

/** Reads a byte from the system bus.
 *
 * @param bus The current system bus.
 * @param address The address to read from the system bus.
 *
 * @returns The byte from system bus.
 */
uint8_t libyagbe_bus_read_memory(struct libyagbe_bus* const bus,
                                 const uint16_t address);

/** Writes a byte to the system bus.
 *
 * @param bus The current system bus.
 * @param address The address to write to.
 * @param data The data to write.
 */
void libyagbe_bus_write_memory(struct libyagbe_bus* const bus,
                               const uint16_t address, const uint8_t data);

#endif /* LIBYAGBE_BUS_H */
