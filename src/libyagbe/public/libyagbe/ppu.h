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

#ifndef LIBYAGBE_PPU_H
#define LIBYAGBE_PPU_H

#include "compat/compat_stdint.h"

enum libyagbe_ppu_io_registers {
  LIBYAGBE_PPU_IO_LCDC = 0x0,
  LIBYAGBE_PPU_IO_SCY = 0x2,
  LIBYAGBE_PPU_IO_SCX = 0x3,
  LIBYAGBE_PPU_IO_LY = 0x4,
  LIBYAGBE_PPU_IO_BGP = 0x7
};

enum libyagbe_ppu_mem_size { LIBYAGBE_PPU_MEM_SIZE_VRAM = 8192 };

struct libyagbe_ppu {
  uint8_t lcdc;
  uint8_t scy;
  uint8_t scx;
  uint8_t ly;
  uint8_t bgp;

  uint8_t vram[LIBYAGBE_PPU_MEM_SIZE_VRAM];
};

#endif /* LIBYAGBE_PPU_H */
