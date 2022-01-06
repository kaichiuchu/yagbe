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

/* XXX: On older compilers, though I seriously doubt it, this may be dangerous.
*  Need to investigate.
*
*  Maybe we can come up with some sort of abstraction layer, e.g. use anonymous
*  structs/unions if the standard is >=C11 or else fall back to this? How would
*  we handle accesses and declarations cleanly among standards?
*/
typedef union _cpu_register_pair {
  uint16_t value;

  struct {
#ifdef BIG_ENDIAN
    uint8_t hi;
    uint8_t lo;
#else
    uint8_t lo;
    uint8_t hi;
#endif /* BIG_ENDIAN */
  } byte;
} cpu_register_pair;

/* Defines the structure of an SM83 CPU. */
struct libyagbe_cpu {
  struct libyagbe_cpu_registers {
    cpu_register_pair af;
    cpu_register_pair bc;
    cpu_register_pair de;
    cpu_register_pair hl;
    cpu_register_pair pc;
    cpu_register_pair sp;
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
void libyagbe_cpu_step(struct libyagbe_cpu* const cpu,
                       struct libyagbe_bus* const bus);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* LIBYAGBE_CPU_H */
