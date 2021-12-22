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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libyagbe/disasm.h"
#include "libyagbe/gb.h"

static uint8_t* open_rom(const char* const file) {
  FILE* const rom_file = fopen(file, "rb");
  long rom_file_size;
  uint8_t* data;

  if (rom_file == NULL) {
    fprintf(stderr, "unable to open ROM file %s: %s\n", file, strerror(errno));
    exit(EXIT_FAILURE);
  }

  if (fseek(rom_file, 0, SEEK_END) != 0) {
    fprintf(stderr, "fseek() failed: %s", strerror(errno));
    exit(EXIT_FAILURE);
  }

  rom_file_size = ftell(rom_file);
  fseek(rom_file, 0, SEEK_SET);

  data = malloc(sizeof(uint8_t) * (size_t)rom_file_size);

  if (data == NULL) {
    fprintf(stderr, "malloc() failed: %s\n", strerror(errno));
    exit(EXIT_FAILURE);
  }

  fread(data, sizeof(uint8_t), (size_t)rom_file_size, rom_file);

  fclose(rom_file);
  return data;
}

int main(int argc, char* argv[]) {
  uint8_t* rom_data;
  struct libyagbe_system gb;
  /*char* disasm; */

  if (argc < 2) {
    fprintf(stderr, "%s: missing required argument.\n", argv[0]);
    fprintf(stderr, "%s: Syntax: %s romfile\n", argv[0], argv[0]);

    return EXIT_FAILURE;
  }

  rom_data = open_rom(argv[1]);
  libyagbe_system_init(&gb, rom_data);

  for (;;) {
    /*const uint16_t pc = gb.cpu.reg.pc;*/

    /*libyagbe_disasm_prepare(gb.cpu.reg.pc, &gb.bus);*/

    if (libyagbe_system_step(&gb) == 0) {
      /*disasm = libyagbe_disasm_execute(&gb.cpu, &gb.bus); */
      /* printf("$%04X: %s\n", pc, disasm); */

      return EXIT_FAILURE;
    }

    /*disasm = libyagbe_disasm_execute(&gb.cpu, &gb.bus);*/
    /*printf("$%04X: %s\n", pc, disasm);*/
  }
}
