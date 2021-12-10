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

#ifndef LIBYAGBE_CPU_H
#define LIBYAGBE_CPU_H

#include "compat/compat_stdint.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct libyagbe_bus;

/** Defines the flag bits for the Flag register. */
enum libyagbe_cpu_flag {
  /** This bit is set if and only if the result of an operation is zero. Used by
     conditional jumps. */
  LIBYAGBE_CPU_FLAG_Z = (1 << 7),

  /** This bit is set in these cases:
   *
   * When the result of an 8-bit addition is higher than $FF.
   * When the result of a 16-bit addition is higher than $FFFF.
   * When the result of a subtraction or comparison is lower than zero (like in
   * Z80 and 80x86 CPUs, but unlike in 65XX and ARM CPUs). When a rotate/shift
   * operation shifts out a “1” bit.
   *
   * Used by conditional jumps and instructions such as ADC, SBC, RL, RLA, etc.
   */
  LIBYAGBE_CPU_FLAG_N = (1 << 6),
  LIBYAGBE_CPU_FLAG_H = (1 << 5),
  LIBYAGBE_CPU_FLAG_C = (1 << 4)
};

/* Defines the structure of an SM83 CPU. */
struct libyagbe_cpu {
  struct libyagbe_cpu_registers {
    uint8_t b, c, d, e, f, h, l, a;

    uint16_t pc;
    uint16_t sp;
  } reg;

  /** The current instruction being processed. */
  uint8_t instruction;
};

/** Resets an SM83 CPU to the startup state.
 *
 * @param cpu The SM83 CPU instance.
 */
void libyagbe_cpu_reset(struct libyagbe_cpu* const cpu);

/** Advances the CPU by one cycle.
 *
 * @param cpu The SM83 CPU instance.
 */
unsigned int libyagbe_cpu_step(struct libyagbe_cpu* const cpu,
                               struct libyagbe_bus* const bus);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* LIBYAGBE_CPU_H */
