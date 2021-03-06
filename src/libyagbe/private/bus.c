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

#include "libyagbe/bus.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>

#include "libyagbe/compat/compat_stdbool.h"
#include "libyagbe/sched.h"

uint8_t libyagbe_bus_read_memory(struct libyagbe_bus* const bus,
                                 const uint16_t address) {
  assert(bus != NULL);

  libyagbe_sched_step();

  switch (address >> 12) {
    case 0x0:
    case 0x1:
    case 0x2:
    case 0x3:
    case 0x4:
    case 0x5:
    case 0x6:
    case 0x7:
      return bus->cart.data[address];

    case 0xC:
    case 0xD:
      return bus->wram[address - 0xC000];

    case 0xF:
      switch ((address >> 8) & 0x0F) {
        case 0xF:
          switch ((address & 0x00FF) >> 4) {
            case 0x0:
              switch (address & 0x000F) {
                case LIBYAGBE_TIMER_IO_TIMA:
                  return bus->timer.tima;

                case LIBYAGBE_BUS_IO_IF:
                  return bus->interrupt_flag;

                default:
                  break;
              }
              break;

            case 0x4:
              switch (address & 0x000F) {
                case LIBYAGBE_PPU_IO_LY:
                  return bus->ppu.ly;

                default:
                  break;
              }
              break;

            case 0x8:
            case 0x9:
            case 0xA:
            case 0xB:
            case 0xC:
            case 0xD:
            case 0xE:
              return bus->hram[address - 0xFF80];

            case 0xF:
              switch (address & 0x000F) {
                case 0x0:
                case 0x1:
                case 0x2:
                case 0x3:
                case 0x4:
                case 0x5:
                case 0x6:
                case 0x7:
                case 0x9:
                case 0xA:
                case 0xB:
                case 0xC:
                case 0xD:
                case 0xE:
                  return bus->hram[address - 0xFF80];

                case 0xF:
                  return bus->interrupt_enable;

                default:
                  break;
              }
              break;

            default:
              break;
          }
          break;

        default:
          break;
      }
      break;

    default:
      break;
  }
  printf("Unhandled read: $%04X\n", address);
  return 0xFF;
}

void libyagbe_bus_write_memory(struct libyagbe_bus* const bus,
                               const uint16_t address, const uint8_t data) {
  bool good;
  assert(bus != NULL);

  good = true;

  switch (address >> 12) {
    case 0x8:
    case 0x9:
      bus->ppu.vram[address - 0x8000] = data;
      break;

    case 0xC:
    case 0xD:
      bus->wram[address - 0xC000] = data;
      break;

    case 0xF:
      switch ((address >> 8) & 0x0F) {
        case 0xF:
          switch ((address & 0x00FF) >> 4) {
            case 0x0:
              switch (address & 0x000F) {
                case 1:
                  putchar(data);
                  break;

                case 2:
                  break;

                case LIBYAGBE_TIMER_IO_TIMA:
                  bus->timer.tima = data;
                  break;

                case LIBYAGBE_TIMER_IO_TMA:
                  bus->timer.tma = data;
                  break;

                case LIBYAGBE_TIMER_IO_TAC:
                  libyagbe_timer_handle_tac(&bus->timer, data);
                  break;

                case LIBYAGBE_BUS_IO_IF:
                  bus->interrupt_flag = data;
                  break;

                default:
                  good = false;
                  break;
              }
              break;

            case 0x1:
              good = false;
              break;

            case 0x2:
              switch (address & 0x000F) {
                case LIBYAGBE_APU_IO_NR50:
                  bus->apu.nr50 = data;
                  break;

                case LIBYAGBE_APU_IO_NR51:
                  bus->apu.nr51 = data;
                  break;

                case LIBYAGBE_APU_IO_NR52:
                  bus->apu.nr52 = data;
                  break;

                default:
                  break;
              }
              break;

            case 0x3:
              break;

            case 0x4:
              switch (address & 0x000F) {
                case LIBYAGBE_PPU_IO_LCDC:
                  bus->ppu.lcdc = data;
                  break;

                case LIBYAGBE_PPU_IO_SCY:
                  bus->ppu.scy = data;
                  break;

                case LIBYAGBE_PPU_IO_SCX:
                  bus->ppu.scx = data;
                  break;

                case LIBYAGBE_PPU_IO_BGP:
                  bus->ppu.bgp = data;
                  break;

                default:
                  good = false;
                  break;
              }
              break;

            case 0x5:
            case 0x6:
            case 0x7:
              good = false;
              break;

            case 0x8:
            case 0x9:
            case 0xA:
            case 0xB:
            case 0xC:
            case 0xD:
            case 0xE:
              bus->hram[address - 0xFF80] = data;
              break;

            case 0xF:
              switch (address & 0x000F) {
                case 0x0:
                case 0x1:
                case 0x2:
                case 0x3:
                case 0x4:
                case 0x5:
                case 0x6:
                case 0x7:
                case 0x9:
                case 0xA:
                case 0xB:
                case 0xC:
                case 0xD:
                case 0xE:
                  bus->hram[address - 0xFF80] = data;
                  break;

                case LIBYAGBE_BUS_IO_IE:
                  bus->interrupt_enable = data;
                  break;

                default:
                  good = false;
                  break;
              }
              break;

            default:
              good = false;
              break;
          }
          break;

        default:
          good = false;
          break;
      }
      break;

    default:
      good = false;
      break;
  }

  libyagbe_sched_step();

  if (!good) {
    printf("Unhandled write: $%04X <- $%02X\n", address, data);
  }
}
