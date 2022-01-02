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

#include "libyagbe/sched.h"

#include <assert.h>
#include <stddef.h>

#include "libyagbe/compat/compat_stdint.h"
#include "utility.h"

/** @brief The maximum number of events possible. */
#define MAX_EVENTS 10

/** @brief The total number of T-cycles which have passed since the start of
 * the emulation.
 */
static uintmax_t current_timestamp = 0;

/** @brief The current number of events queued. */
static size_t heap_size = 0;

/** @brief An array containing \ref libyagbe_sched_event structures. */
static struct libyagbe_sched_event events[MAX_EVENTS];

static size_t get_parent_node(const size_t index) { return (index - 1) / 2; }

static size_t get_left_child_of_node(const size_t index) {
  return (index * 2) + 1;
}

static size_t get_right_child_of_node(const size_t index) {
  return (index * 2) + 2;
}

/** @brief Returns the expiry time of the root event. */
static unsigned int find_min(void) { return events[0].expiry_time; }

static void heapify_top_bottom(const size_t parent_node) {
  size_t left_node;
  size_t right_node;
  size_t smallest_node;

  left_node = get_left_child_of_node(parent_node);
  right_node = get_right_child_of_node(parent_node);
  smallest_node = parent_node;

  if ((smallest_node < heap_size) &&
      (events[left_node].expiry_time < events[smallest_node].expiry_time)) {
    smallest_node = left_node;
  }

  if ((right_node < heap_size) &&
      events[right_node].expiry_time < events[smallest_node].expiry_time) {
    smallest_node = right_node;
  }

  if (smallest_node != parent_node) {
    SWAP(events[smallest_node], events[parent_node],
         struct libyagbe_sched_event);

    heapify_top_bottom(smallest_node);
  }
}

/** @brief Returns the node of minimum value after removing it from the heap. */
static struct libyagbe_sched_event extract_min(void) {
  struct libyagbe_sched_event event;
  assert(heap_size != 0);

  event = events[0];
  heap_size--;

  if (heap_size == 0) {
    memset(&events[0], 0, sizeof(struct libyagbe_sched_event));
  } else {
    events[0] = events[heap_size];
  }
  heapify_top_bottom(0);
  return event;
}

static void heapify_bottom_top(const size_t index) {
  size_t parent_node;

  parent_node = get_parent_node(index);

  if (events[parent_node].expiry_time > events[index].expiry_time) {
    SWAP(events[parent_node], events[index], struct libyagbe_sched_event);
    heapify_bottom_top(parent_node);
  }
}

void libyagbe_sched_insert(struct libyagbe_sched_event* const event) {
  assert(heap_size != (MAX_EVENTS - 1));

  events[heap_size] = *event;
  events[heap_size].expiry_time += current_timestamp;

  heapify_bottom_top(heap_size++);
}

void libyagbe_sched_reset(void) {
  current_timestamp = 0;
  memset(&events, 0, sizeof(events));
  heap_size = 0;
}

void libyagbe_sched_step(void) {
  unsigned int expiry_time;
  current_timestamp += 4;

  if (heap_size == 0) {
    return;
  }

  expiry_time = find_min();

  if (current_timestamp == expiry_time) {
    struct libyagbe_sched_event event = extract_min();
    event.cb_func(event.userdata);
  }
}
