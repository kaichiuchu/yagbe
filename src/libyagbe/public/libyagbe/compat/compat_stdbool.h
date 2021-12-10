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

/* Provides C99 booleans or simulates them depending on the language standard
 * used. */

#ifndef LIBYAGBE_COMPAT_STDBOOL_H
#define LIBYAGBE_COMPAT_STDBOOL_H

#if (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L) || \
    defined(__cplusplus)
#include <stdbool.h>
#else
#define false 0
#define true 1

typedef int bool;
#endif /* (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L) || \
           defined(__cplusplus) */

#endif /* LIBYAGBE_COMPAT_STDBOOL_H */
