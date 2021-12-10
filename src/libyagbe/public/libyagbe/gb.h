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

#ifndef LIBYAGBE_GB_H
#define LIBYAGBE_GB_H

#include "bus.h"
#include "cpu.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** Defines a YAGBE system instance. */
struct libyagbe_system {
  struct libyagbe_bus bus;
  struct libyagbe_cpu cpu;
};

/**
 * @brief Initializes a YAGBE instance.
 *
 * @param gb The YAGBE instance to initialize.
 * @param cart_data The cart data.
 */
void libyagbe_system_init(struct libyagbe_system* const gb,
                          const uint8_t* const cart_data);

/**
 * @brief Resets a YAGBE instance to the startup state.
 *
 * This will still keep the cartridge (if any) inserted, but any internal state
 * within the cartridge will be cleared as appropriate.
 *
 * @param gb The YAGBE instance.
 */
void libyagbe_system_reset(struct libyagbe_system* const gb);

/**
 * @brief Advances a YAGBE instance by one step.
 *
 * @param gb The YAGBE instance.
 */
unsigned int libyagbe_system_step(struct libyagbe_system* const gb);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* LIBYAGBE_GB_H */
