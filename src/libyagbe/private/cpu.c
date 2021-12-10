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

#include "libyagbe/cpu.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "libyagbe/bus.h"
#include "libyagbe/compat/compat_stdbool.h"
#include "libyagbe/cpu_defs.h"

static uint8_t alu_inc(struct libyagbe_cpu* const cpu, uint8_t value) {
  assert(cpu != NULL);

  cpu->reg.f &= ~LIBYAGBE_CPU_FLAG_N;
  value++;

  if (value == 0) {
    cpu->reg.f |= LIBYAGBE_CPU_FLAG_Z;
  } else {
    cpu->reg.f &= ~LIBYAGBE_CPU_FLAG_Z;
  }
  return value;
}

static void call_if(struct libyagbe_cpu* const cpu,
                    struct libyagbe_bus* const bus, const bool condition_met) {
  assert(cpu != NULL);
  assert(bus != NULL);

  if (condition_met) {
    const uint8_t lo = libyagbe_bus_read_memory(bus, cpu->reg.pc + 1);
    const uint8_t hi = libyagbe_bus_read_memory(bus, cpu->reg.pc + 2);

    const uint16_t address = (hi << 8) | lo;

    cpu->reg.pc += 3;

    libyagbe_bus_write_memory(bus, --cpu->reg.sp, cpu->reg.pc >> 8);
    libyagbe_bus_write_memory(bus, --cpu->reg.sp, cpu->reg.pc & 0x00FF);

    cpu->reg.pc = address;
    return;
  }
  cpu->reg.pc += 3;
}

static uint8_t alu_dec(struct libyagbe_cpu* const cpu, uint8_t value) {
  assert(cpu != NULL);

  cpu->reg.f |= ~LIBYAGBE_CPU_FLAG_N;
  value--;

  if (value == 0) {
    cpu->reg.f |= LIBYAGBE_CPU_FLAG_Z;
  } else {
    cpu->reg.f &= ~LIBYAGBE_CPU_FLAG_Z;
  }
  return value;
}

static void jr_if(struct libyagbe_bus* const bus,
                  struct libyagbe_cpu* const cpu, const bool condition_met) {
  assert(bus != NULL);
  assert(cpu != NULL);

  if (condition_met) {
    const int8_t imm = (int8_t)libyagbe_bus_read_memory(bus, cpu->reg.pc + 1);
    cpu->reg.pc += (imm + 2);

    return;
  }
  cpu->reg.pc += 2;
}

static void jp_if(struct libyagbe_cpu* const cpu,
                  struct libyagbe_bus* const bus, const bool condition_met) {
  assert(bus != NULL);
  assert(cpu != NULL);

  if (condition_met) {
    const uint8_t lo = libyagbe_bus_read_memory(bus, cpu->reg.pc + 1);
    const uint8_t hi = libyagbe_bus_read_memory(bus, cpu->reg.pc + 2);

    cpu->reg.pc = (hi << 8) | lo;
    return;
  }
  cpu->reg.pc += 3;
}

static void ret_if(struct libyagbe_cpu* const cpu,
                   struct libyagbe_bus* const bus, const bool condition_met) {
  assert(bus != NULL);
  assert(cpu != NULL);

  if (condition_met) {
    const uint8_t lo = libyagbe_bus_read_memory(bus, cpu->reg.sp++);
    const uint8_t hi = libyagbe_bus_read_memory(bus, cpu->reg.sp++);

    cpu->reg.pc = (hi << 8) | lo;
    return;
  }
  cpu->reg.pc++;
}

void libyagbe_cpu_reset(struct libyagbe_cpu* const cpu) {
  assert(cpu != NULL);

  cpu->reg.a = 0x01;
  cpu->reg.f = 0xB0;

  cpu->reg.b = 0x00;
  cpu->reg.c = 0x13;

  cpu->reg.d = 0x00;
  cpu->reg.e = 0xD8;

  cpu->reg.h = 0x01;
  cpu->reg.l = 0x4D;

  cpu->reg.sp = 0xFFFE;
  cpu->reg.pc = 0x0100;
}

unsigned int libyagbe_cpu_step(struct libyagbe_cpu* const cpu,
                               struct libyagbe_bus* const bus) {
  assert(cpu != NULL);
  assert(bus != NULL);

  cpu->instruction = libyagbe_bus_read_memory(bus, cpu->reg.pc);

  switch (cpu->instruction) {
    case LIBYAGBE_CPU_OP_NOP:
      cpu->reg.pc++;
      return 4;

    case LIBYAGBE_CPU_OP_LD_BC_IMM16:
      cpu->reg.c = libyagbe_bus_read_memory(bus, cpu->reg.pc + 1);
      cpu->reg.b = libyagbe_bus_read_memory(bus, cpu->reg.pc + 2);

      cpu->reg.pc += 3;
      return 4;

    case LIBYAGBE_CPU_OP_INC_BC: {
      uint16_t bc = (cpu->reg.b << 8) | cpu->reg.c;

      bc++;

      cpu->reg.b = bc >> 8;
      cpu->reg.c = bc & 0x00FF;

      cpu->reg.pc++;
      return 4;
    }

    case LIBYAGBE_CPU_OP_DEC_C:
      cpu->reg.c = alu_dec(cpu, cpu->reg.c);
      cpu->reg.pc++;

      return 4;

    case LIBYAGBE_CPU_OP_LD_C_IMM8:
      cpu->reg.c = libyagbe_bus_read_memory(bus, cpu->reg.pc + 1);
      cpu->reg.pc += 2;

      return 4;

    case LIBYAGBE_CPU_OP_LD_DE_IMM16:
      cpu->reg.e = libyagbe_bus_read_memory(bus, cpu->reg.pc + 1);
      cpu->reg.d = libyagbe_bus_read_memory(bus, cpu->reg.pc + 2);

      cpu->reg.pc += 3;
      return 4;

    case LIBYAGBE_CPU_OP_LD_MEM_DE_A: {
      const uint16_t de = (cpu->reg.d << 8) | cpu->reg.e;

      libyagbe_bus_write_memory(bus, de, cpu->reg.a);
      cpu->reg.pc++;

      return 4;
    }

    case LIBYAGBE_CPU_OP_INC_D:
      cpu->reg.d = alu_inc(cpu, cpu->reg.d);
      cpu->reg.pc++;

      return 4;

    case LIBYAGBE_CPU_OP_JR_SIMM8:
      jr_if(bus, cpu, true);
      return 4;

    case LIBYAGBE_CPU_OP_INC_E:
      cpu->reg.e = alu_inc(cpu, cpu->reg.e);
      cpu->reg.pc++;

      return 4;

    case LIBYAGBE_CPU_OP_JR_NZ_SIMM8:
      jr_if(bus, cpu, !(cpu->reg.f & LIBYAGBE_CPU_FLAG_Z));
      return 4;

    case LIBYAGBE_CPU_OP_LD_HL_IMM16:
      cpu->reg.l = libyagbe_bus_read_memory(bus, cpu->reg.pc + 1);
      cpu->reg.h = libyagbe_bus_read_memory(bus, cpu->reg.pc + 2);

      cpu->reg.pc += 3;
      return 4;

    case LIBYAGBE_CPU_OP_INC_HL: {
      uint16_t hl = ((cpu->reg.h << 8) | cpu->reg.l);

      hl++;

      cpu->reg.h = hl >> 8;
      cpu->reg.l = hl & 0x00FF;

      cpu->reg.pc++;
      return 4;
    }

    case LIBYAGBE_CPU_OP_LDI_A_MEM_HL: {
      uint16_t hl = (cpu->reg.h << 8) | cpu->reg.l;
      cpu->reg.a = libyagbe_bus_read_memory(bus, hl++);

      cpu->reg.l = hl & 0x00FF;
      cpu->reg.h = hl >> 8;

      cpu->reg.pc++;
      return 4;
    }

    case LIBYAGBE_CPU_OP_LD_SP_IMM16: {
      const uint8_t lo = libyagbe_bus_read_memory(bus, cpu->reg.pc + 1);
      const uint8_t hi = libyagbe_bus_read_memory(bus, cpu->reg.pc + 2);

      cpu->reg.sp = (hi << 8) | lo;
      cpu->reg.pc += 3;

      return 4;
    }

    case LIBYAGBE_CPU_OP_LD_A_IMM8:
      cpu->reg.a = libyagbe_bus_read_memory(bus, cpu->reg.pc + 1);
      cpu->reg.pc += 2;

      return 4;

    case LIBYAGBE_CPU_OP_LD_B_A:
      cpu->reg.b = cpu->reg.a;
      cpu->reg.pc++;

      return 4;

    case LIBYAGBE_CPU_OP_LD_A_B:
      cpu->reg.a = cpu->reg.b;
      cpu->reg.pc++;

      return 4;

    case LIBYAGBE_CPU_OP_LD_A_H:
      cpu->reg.a = cpu->reg.h;
      cpu->reg.pc++;

      return 4;

    case LIBYAGBE_CPU_OP_LD_A_L:
      cpu->reg.a = cpu->reg.l;
      cpu->reg.pc++;

      return 4;

    case LIBYAGBE_CPU_OP_JP_IMM16:
      jp_if(cpu, bus, true);
      return 16;

    case LIBYAGBE_CPU_OP_PUSH_BC:
      libyagbe_bus_write_memory(bus, --cpu->reg.sp, cpu->reg.b);
      libyagbe_bus_write_memory(bus, --cpu->reg.sp, cpu->reg.c);

      cpu->reg.pc++;
      return 4;

    case LIBYAGBE_CPU_OP_RET:
      ret_if(cpu, bus, true);
      return 4;

    case LIBYAGBE_CPU_OP_CALL_IMM16:
      call_if(cpu, bus, true);
      return 16;

    case LIBYAGBE_CPU_OP_LDH_IMM8_A: {
      const uint8_t imm = libyagbe_bus_read_memory(bus, cpu->reg.pc + 1);
      libyagbe_bus_write_memory(bus, 0xFF00 + imm, cpu->reg.a);

      cpu->reg.pc += 2;
      return 4;
    }

    case LIBYAGBE_CPU_OP_POP_HL:
      cpu->reg.l = libyagbe_bus_read_memory(bus, cpu->reg.sp++);
      cpu->reg.h = libyagbe_bus_read_memory(bus, cpu->reg.sp++);

      cpu->reg.pc++;
      return 4;

    case LIBYAGBE_CPU_OP_PUSH_HL:
      libyagbe_bus_write_memory(bus, --cpu->reg.sp, cpu->reg.h);
      libyagbe_bus_write_memory(bus, --cpu->reg.sp, cpu->reg.l);

      cpu->reg.pc++;
      return 4;

    case LIBYAGBE_CPU_OP_LD_MEM_IMM16_A: {
      const uint8_t lo = libyagbe_bus_read_memory(bus, cpu->reg.pc + 1);
      const uint8_t hi = libyagbe_bus_read_memory(bus, cpu->reg.pc + 2);

      libyagbe_bus_write_memory(bus, (hi << 8) | lo, cpu->reg.a);
      cpu->reg.pc += 3;

      return 4;
    }

    case LIBYAGBE_CPU_OP_POP_AF:
      cpu->reg.f = libyagbe_bus_read_memory(bus, cpu->reg.sp++);
      cpu->reg.a = libyagbe_bus_read_memory(bus, cpu->reg.sp++);

      cpu->reg.pc++;
      return 4;

    case LIBYAGBE_CPU_OP_DI:
      cpu->reg.pc++;
      return 4;

    case LIBYAGBE_CPU_OP_PUSH_AF:
      libyagbe_bus_write_memory(bus, --cpu->reg.sp, cpu->reg.a);
      libyagbe_bus_write_memory(bus, --cpu->reg.sp, cpu->reg.f);

      cpu->reg.pc++;
      return 4;

    default:
      return 0;
  }
  return 0;
}
