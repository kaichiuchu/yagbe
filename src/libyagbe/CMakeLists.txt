# Copyright 2021 Michael Rodriguez aka kaichiuchu <mike@kaichiuchu.dev>
#
# Permission to use, copy, modify, and/or distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
# REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
# AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
# INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
# LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
# OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
# PERFORMANCE OF THIS SOFTWARE.

set(PRIVATE_SRCS private/apu.c
                 private/bus.c
                 private/cart.c
                 private/cpu.c
                 private/disasm.c
                 private/ppu.c
                 private/sched.c
                 private/gb.c
                 private/timer.c)

set(PRIVATE_HDRS private/utility.h)

set(PUBLIC_HDRS public/libyagbe/compat/compat_stdbool.h
                public/libyagbe/compat/compat_stdint.h
                public/libyagbe/apu.h
                public/libyagbe/bus.h
                public/libyagbe/cart.h
                public/libyagbe/cpu.h
                public/libyagbe/disasm.h
                public/libyagbe/gb.h
                public/libyagbe/ppu.h
                public/libyagbe/sched.h
                public/libyagbe/timer.h)

add_library(yagbecore STATIC ${PRIVATE_SRCS} ${PUBLIC_HDRS} ${PRIVATE_HDRS})
target_include_directories(yagbecore PRIVATE public)
yagbe_configure_c_target(yagbecore)
