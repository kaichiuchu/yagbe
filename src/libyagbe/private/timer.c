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

#include "libyagbe/timer.h"

#include <assert.h>
#include <stddef.h>

#include "libyagbe/sched.h"

static const unsigned int timing[4] = {1024, 16, 64, 256};
enum tac_bits { TAC_ENABLED = 1 << 2 };

static uint8_t* interrupt_flag = NULL;

enum interrupt_flags { FLAG_TIMER = 1 << 2 };

static void handle_timer_update(void* const userdata) {
  struct libyagbe_timer* timer = (struct libyagbe_timer*)userdata;

  struct libyagbe_sched_event event;
  event.expiry_time = timing[timer->tac & 0x03];
  event.cb_func = &handle_timer_update;
  event.userdata = timer;

  if (timer->tima == 0xFF) {
    timer->tima = timer->tma;
    *interrupt_flag |= FLAG_TIMER;
  } else {
    timer->tima++;
  }

  /* Don't schedule an event again if the timer is not enabled. */
  if (timer->tac & TAC_ENABLED) {
    libyagbe_sched_insert(&event);
  }
}

void libyagbe_timer_set_interrupt_flag(uint8_t* const iflag) {
  interrupt_flag = iflag;
}

void libyagbe_timer_reset(struct libyagbe_timer* const timer) {
  assert(timer != NULL);

  timer->tac = 0xF8;
  timer->tima = 0x00;
  timer->tma = 0x00;
}

void libyagbe_timer_handle_tac(struct libyagbe_timer* const timer,
                               const uint8_t tac) {
  assert(timer != NULL);

  /* Is the timer being enabled from a disabled state? */
  if (!(timer->tac & TAC_ENABLED) && (tac & TAC_ENABLED)) {
    /* The timer has now been enabled from a previously disabled state, schedule
     * an event. */
    struct libyagbe_sched_event event;

    event.expiry_time = timing[tac & 0x03];
    event.cb_func = &handle_timer_update;
    event.userdata = timer;

    libyagbe_sched_insert(&event);
    timer->tac = (timer->tac & ~0x07) | (tac & 0x07);

    return;
  }

  /* Is the timer being disabled from an enabled state? */
  if ((timer->tac & TAC_ENABLED) && !(tac & TAC_ENABLED)) {
    /* Delete all events related to the timer. */
    /*libyagbe_sched_delete_events(timer);*/
    timer->tac = (timer->tac & ~0x07) | (tac & 0x07);
    return;
  }
  timer->tac = (timer->tac & ~0x07) | (tac & 0x07);
}