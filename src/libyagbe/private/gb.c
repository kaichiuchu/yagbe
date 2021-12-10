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

#include "libyagbe/gb.h"

#include <assert.h>
#include <stddef.h>

void libyagbe_system_init(struct libyagbe_system* const gb,
                          const uint8_t* const cart_data) {
  assert(gb != NULL);

  gb->bus.cart.data = cart_data;
  libyagbe_system_reset(gb);
}

void libyagbe_system_reset(struct libyagbe_system* const gb) {
  assert(gb != NULL);
  libyagbe_cpu_reset(&gb->cpu);
}

unsigned int libyagbe_system_step(struct libyagbe_system* const gb) {
  assert(gb != NULL);
  return libyagbe_cpu_step(&gb->cpu, &gb->bus);
}
