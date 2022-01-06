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

#ifndef LIBYAGBE_DISASM_H
#define LIBYAGBE_DISASM_H

#include "compat/compat_stdint.h"

/* Forward declaration of the system bus. */
struct libyagbe_cpu;
struct libyagbe_bus;

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** Prepares to disassemble the current instruction.
 */
void libyagbe_disasm_prepare(const uint16_t pc, struct libyagbe_cpu* const cpu,
                             struct libyagbe_bus* const bus);

/**
 * Disassembles a given instruction.
 *
 * @returns The disassembled string. It is your responsibility to free the
 * memory held by the string.
 */
char* libyagbe_disasm_execute(struct libyagbe_cpu* const cpu,
                              struct libyagbe_bus* const bus);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* LIBYAGBE_DISASM_H */
