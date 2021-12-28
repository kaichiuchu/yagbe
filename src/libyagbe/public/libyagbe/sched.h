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

#ifndef LIBYAGBE_SCHED_H
#define LIBYAGBE_SCHED_H

typedef void (*libyagbe_sched_event_cb)(void* const userdata);

struct libyagbe_sched_event {
  /** @brief When should this event be triggered? */
  unsigned int expiry_time;

  /** What function should be called when the event has expired? */
  libyagbe_sched_event_cb cb_func;

  void* userdata;
};

void libyagbe_sched_insert(struct libyagbe_sched_event* const event);

void libyagbe_sched_reset(void);

/** @brief Advances the scheduler by 1 m-cycle.
*
* This function *must* be called once every m-cycle.
*/
void libyagbe_sched_step(void);

#endif /* LIBYAGBE_SCHED_H */
