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

enum alu_flag {
  ALU_NORMAL,
  ALU_WITH_CARRY,
  ALU_DISCARD_RESULT,
  ALU_CLEAR_ZERO
};

static uint8_t alu_srl(struct libyagbe_cpu* const cpu, uint8_t reg) {
  cpu->reg.f &= ~LIBYAGBE_CPU_FLAG_N;
  cpu->reg.f &= ~LIBYAGBE_CPU_FLAG_H;

  if ((reg & 1) != 0) {
    cpu->reg.f |= LIBYAGBE_CPU_FLAG_C;
  } else {
    cpu->reg.f &= ~LIBYAGBE_CPU_FLAG_C;
  }

  reg >>= 1;

  if (reg == 0) {
    cpu->reg.f |= LIBYAGBE_CPU_FLAG_Z;
  } else {
    cpu->reg.f &= ~LIBYAGBE_CPU_FLAG_Z;
  }
  return reg;
}

static uint8_t alu_rr(struct libyagbe_cpu* const cpu, uint8_t reg,
                      const enum alu_flag flag) {
  cpu->reg.f &= ~LIBYAGBE_CPU_FLAG_N;
  cpu->reg.f &= ~LIBYAGBE_CPU_FLAG_H;

  const uint8_t old_carry_flag_value =
      ((cpu->reg.f & LIBYAGBE_CPU_FLAG_C) != 0) ? 0x80 : 0x00;

  if ((reg & 1) != 0) {
    cpu->reg.f |= LIBYAGBE_CPU_FLAG_C;
  } else {
    cpu->reg.f &= ~LIBYAGBE_CPU_FLAG_C;
  }

  reg >>= 1;
  reg |= old_carry_flag_value;

  if (flag == ALU_CLEAR_ZERO) {
    cpu->reg.f &= ~LIBYAGBE_CPU_FLAG_Z;
    return reg;
  }

  if (reg == 0) {
    cpu->reg.f |= LIBYAGBE_CPU_FLAG_Z;
  } else {
    cpu->reg.f &= ~LIBYAGBE_CPU_FLAG_Z;
  }
  return reg;
}

static void alu_add(struct libyagbe_cpu* const cpu, const uint8_t addend,
                    const enum alu_flag flag) {
  assert(cpu != NULL);

  cpu->reg.f &= ~LIBYAGBE_CPU_FLAG_N;

  int sum = cpu->reg.a + addend;

  if (flag == ALU_WITH_CARRY) {
    sum += (cpu->reg.f & LIBYAGBE_CPU_FLAG_C) != 0;
  }

  const uint8_t s = (uint8_t)sum;

  if (s == 0) {
    cpu->reg.f |= LIBYAGBE_CPU_FLAG_Z;
  } else {
    cpu->reg.f &= ~LIBYAGBE_CPU_FLAG_Z;
  }

  if (sum > 0xFF) {
    cpu->reg.f |= LIBYAGBE_CPU_FLAG_C;
  } else {
    cpu->reg.f &= ~LIBYAGBE_CPU_FLAG_C;
  }
  cpu->reg.a = s;
}

static void alu_add_hl(struct libyagbe_cpu* const cpu, const uint8_t hi,
                       const uint8_t lo) {
  assert(cpu != NULL);

  cpu->reg.f &= ~LIBYAGBE_CPU_FLAG_N;

  const uint16_t hl = (cpu->reg.h << 8) | cpu->reg.l;
  const uint16_t pair = (hi << 8) | lo;

  const int sum = hl + pair;

  if (sum > 0xFFFF) {
    cpu->reg.f |= LIBYAGBE_CPU_FLAG_C;
  } else {
    cpu->reg.f &= ~LIBYAGBE_CPU_FLAG_C;
  }

  const uint16_t result = (uint16_t)sum;

  cpu->reg.h = result >> 8;
  cpu->reg.l = result & 0x00FF;
}

static void alu_sub(struct libyagbe_cpu* const cpu, const uint8_t subtrahend,
                    const enum alu_flag flag) {
  assert(cpu != NULL);

  const uint8_t diff = cpu->reg.a - subtrahend;

  if (diff == 0) {
    cpu->reg.f |= LIBYAGBE_CPU_FLAG_Z;
  } else {
    cpu->reg.f &= ~LIBYAGBE_CPU_FLAG_Z;
  }

  if (subtrahend > cpu->reg.a) {
    cpu->reg.f |= LIBYAGBE_CPU_FLAG_C;
  } else {
    cpu->reg.f &= ~LIBYAGBE_CPU_FLAG_C;
  }

  if (flag != ALU_DISCARD_RESULT) {
    cpu->reg.a = diff;
  }
}

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

  cpu->reg.f |= LIBYAGBE_CPU_FLAG_N;
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

    case LIBYAGBE_CPU_OP_INC_B:
      cpu->reg.b = alu_inc(cpu, cpu->reg.b);
      cpu->reg.pc++;

      return 4;

    case LIBYAGBE_CPU_OP_DEC_B:
      cpu->reg.b = alu_dec(cpu, cpu->reg.b);
      cpu->reg.pc++;

      return 4;

    case LIBYAGBE_CPU_OP_LD_B_IMM8:
      cpu->reg.b = libyagbe_bus_read_memory(bus, cpu->reg.pc + 1);
      cpu->reg.pc += 2;

      return 4;

    case LIBYAGBE_CPU_OP_INC_C:
      cpu->reg.c = alu_inc(cpu, cpu->reg.c);
      cpu->reg.pc++;

      return 4;

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

    case LIBYAGBE_CPU_OP_INC_DE: {
      uint16_t de = (cpu->reg.d << 8) | cpu->reg.e;

      de++;

      cpu->reg.h = de >> 8;
      cpu->reg.l = de & 0x00FF;

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

    case LIBYAGBE_CPU_OP_LD_A_MEM_DE: {
      const uint16_t de = (cpu->reg.d << 8) | cpu->reg.e;

      cpu->reg.a = libyagbe_bus_read_memory(bus, de);
      cpu->reg.pc++;

      return 4;
    }

    case LIBYAGBE_CPU_OP_INC_E:
      cpu->reg.e = alu_inc(cpu, cpu->reg.e);
      cpu->reg.pc++;

      return 4;

    case LIBYAGBE_CPU_OP_DEC_E:
      cpu->reg.e = alu_dec(cpu, cpu->reg.e);
      cpu->reg.pc++;

      return 4;

    case LIBYAGBE_CPU_OP_RRA:
      cpu->reg.a = alu_rr(cpu, cpu->reg.a, ALU_CLEAR_ZERO);
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

    case LIBYAGBE_CPU_OP_LDI_MEM_HL_A: {
      uint16_t hl = (cpu->reg.h << 8) | cpu->reg.l;

      libyagbe_bus_write_memory(bus, hl++, cpu->reg.a);

      cpu->reg.h = hl >> 8;
      cpu->reg.l = hl & 0x00FF;

      cpu->reg.pc++;
      return 4;
    }

    case LIBYAGBE_CPU_OP_INC_HL: {
      uint16_t hl = (cpu->reg.h << 8) | cpu->reg.l;

      hl++;

      cpu->reg.h = hl >> 8;
      cpu->reg.l = hl & 0x00FF;

      cpu->reg.pc++;
      return 4;
    }

    case LIBYAGBE_CPU_OP_INC_H:
      cpu->reg.h = alu_inc(cpu, cpu->reg.h);
      cpu->reg.pc++;

      return 4;

    case LIBYAGBE_CPU_OP_DEC_H:
      cpu->reg.h = alu_dec(cpu, cpu->reg.h);
      cpu->reg.pc++;

      return 4;

    case LIBYAGBE_CPU_OP_LD_H_IMM8:
      cpu->reg.h = libyagbe_bus_read_memory(bus, cpu->reg.pc + 1);
      cpu->reg.pc += 2;

      return 4;

    case LIBYAGBE_CPU_OP_DAA:
      cpu->reg.pc++;
      return 4;

    case LIBYAGBE_CPU_OP_JR_Z_SIMM8:
      jr_if(bus, cpu, (cpu->reg.f & LIBYAGBE_CPU_FLAG_Z) != 0);
      return 4;

    case LIBYAGBE_CPU_OP_ADD_HL_HL:
      alu_add_hl(cpu, cpu->reg.h, cpu->reg.l);
      cpu->reg.pc++;

      return 4;

    case LIBYAGBE_CPU_OP_LDI_A_MEM_HL: {
      uint16_t hl = (cpu->reg.h << 8) | cpu->reg.l;
      cpu->reg.a = libyagbe_bus_read_memory(bus, hl++);

      cpu->reg.l = hl & 0x00FF;
      cpu->reg.h = hl >> 8;

      cpu->reg.pc++;
      return 4;
    }

    case LIBYAGBE_CPU_OP_INC_L:
      cpu->reg.l = alu_inc(cpu, cpu->reg.l);
      cpu->reg.pc++;

      return 4;

    case LIBYAGBE_CPU_OP_DEC_L:
      cpu->reg.l = alu_dec(cpu, cpu->reg.l);
      cpu->reg.pc++;

      return 4;

    case LIBYAGBE_CPU_OP_CPL:
      cpu->reg.a = ~cpu->reg.a;
      cpu->reg.pc++;

      return 4;

    case LIBYAGBE_CPU_OP_JR_NC_SIMM8:
      jr_if(bus, cpu, !(cpu->reg.f & LIBYAGBE_CPU_FLAG_C));
      return 4;

    case LIBYAGBE_CPU_OP_LD_SP_IMM16: {
      const uint8_t lo = libyagbe_bus_read_memory(bus, cpu->reg.pc + 1);
      const uint8_t hi = libyagbe_bus_read_memory(bus, cpu->reg.pc + 2);

      cpu->reg.sp = (hi << 8) | lo;
      cpu->reg.pc += 3;

      return 4;
    }

    case LIBYAGBE_CPU_OP_LDD_HL_A: {
      uint16_t hl = (cpu->reg.h << 8) | cpu->reg.l;

      libyagbe_bus_write_memory(bus, hl--, cpu->reg.a);

      cpu->reg.h = hl >> 8;
      cpu->reg.l = hl & 0x00FF;

      cpu->reg.pc++;
      return 4;
    }

    case LIBYAGBE_CPU_OP_DEC_MEM_HL: {
      const uint16_t hl = (cpu->reg.h << 8) | cpu->reg.l;

      uint8_t data = libyagbe_bus_read_memory(bus, hl);
      data = alu_dec(cpu, data);
      libyagbe_bus_write_memory(bus, hl, data);

      cpu->reg.pc++;
      return 4;
    }

    case LIBYAGBE_CPU_OP_JR_C_SIMM8:
      jr_if(bus, cpu, (cpu->reg.f & LIBYAGBE_CPU_FLAG_C) != 0);
      return 4;

    case LIBYAGBE_CPU_OP_INC_A:
      cpu->reg.a = alu_inc(cpu, cpu->reg.a);
      cpu->reg.pc++;

      return 4;

    case LIBYAGBE_CPU_OP_DEC_A:
      cpu->reg.a = alu_dec(cpu, cpu->reg.a);
      cpu->reg.pc++;

      return 4;

    case LIBYAGBE_CPU_OP_LD_A_IMM8:
      cpu->reg.a = libyagbe_bus_read_memory(bus, cpu->reg.pc + 1);
      cpu->reg.pc += 2;

      return 4;

    case LIBYAGBE_CPU_OP_LD_B_MEM_HL: {
      const uint16_t hl = (cpu->reg.h << 8) | cpu->reg.l;

      cpu->reg.b = libyagbe_bus_read_memory(bus, hl);
      cpu->reg.pc++;

      return 4;
    }

    case LIBYAGBE_CPU_OP_LD_B_A:
      cpu->reg.b = cpu->reg.a;
      cpu->reg.pc++;

      return 4;

    case LIBYAGBE_CPU_OP_LD_C_MEM_HL: {
      const uint16_t hl = (cpu->reg.h << 8) | cpu->reg.l;

      cpu->reg.c = libyagbe_bus_read_memory(bus, hl);
      cpu->reg.pc++;

      return 4;
    }

    case LIBYAGBE_CPU_OP_LD_C_A:
      cpu->reg.c = cpu->reg.a;
      cpu->reg.pc++;

      return 4;

    case LIBYAGBE_CPU_OP_LD_D_MEM_HL: {
      const uint16_t hl = (cpu->reg.h << 8) | cpu->reg.l;

      cpu->reg.d = libyagbe_bus_read_memory(bus, hl);
      cpu->reg.pc++;

      return 4;
    }

    case LIBYAGBE_CPU_OP_LD_D_A:
      cpu->reg.d = cpu->reg.a;
      cpu->reg.pc++;

      return 4;

    case LIBYAGBE_CPU_OP_LD_E_A:
      cpu->reg.e = cpu->reg.a;
      cpu->reg.pc++;

      return 4;

    case LIBYAGBE_CPU_OP_LD_H_A:
      cpu->reg.h = cpu->reg.a;
      cpu->reg.pc++;

      return 4;

    case LIBYAGBE_CPU_OP_LD_L_MEM_HL: {
      const uint16_t hl = (cpu->reg.h << 8) | cpu->reg.l;

      cpu->reg.l = libyagbe_bus_read_memory(bus, hl);
      cpu->reg.pc++;

      return 4;
    }

    case LIBYAGBE_CPU_OP_LD_L_A:
      cpu->reg.l = cpu->reg.a;
      cpu->reg.pc++;

      return 4;

    case LIBYAGBE_CPU_OP_LD_MEM_HL_B: {
      const uint16_t hl = (cpu->reg.h << 8) | cpu->reg.l;

      libyagbe_bus_write_memory(bus, hl, cpu->reg.b);
      cpu->reg.pc++;

      return 4;
    }

    case LIBYAGBE_CPU_OP_LD_MEM_HL_C: {
      const uint16_t hl = (cpu->reg.h << 8) | cpu->reg.l;

      libyagbe_bus_write_memory(bus, hl, cpu->reg.c);
      cpu->reg.pc++;

      return 4;
    }

    case LIBYAGBE_CPU_OP_LD_MEM_HL_D: {
      const uint16_t hl = (cpu->reg.h << 8) | cpu->reg.l;

      libyagbe_bus_write_memory(bus, hl, cpu->reg.d);
      cpu->reg.pc++;

      return 4;
    }

    case LIBYAGBE_CPU_OP_LD_MEM_HL_A: {
      const uint16_t hl = (cpu->reg.h << 8) | cpu->reg.l;

      libyagbe_bus_write_memory(bus, hl, cpu->reg.a);
      cpu->reg.pc++;

      return 4;
    }

    case LIBYAGBE_CPU_OP_LD_A_B:
      cpu->reg.a = cpu->reg.b;
      cpu->reg.pc++;

      return 4;

    case LIBYAGBE_CPU_OP_LD_A_C:
      cpu->reg.a = cpu->reg.c;
      cpu->reg.pc++;

      return 4;

    case LIBYAGBE_CPU_OP_LD_A_D:
      cpu->reg.a = cpu->reg.d;
      cpu->reg.pc++;

      return 4;

    case LIBYAGBE_CPU_OP_LD_A_E:
      cpu->reg.a = cpu->reg.e;
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

    case LIBYAGBE_CPU_OP_LD_A_MEM_HL: {
      const uint16_t hl = (cpu->reg.h << 8) | cpu->reg.l;

      cpu->reg.a = libyagbe_bus_read_memory(bus, hl);
      cpu->reg.pc++;

      return 4;
    }

    case LIBYAGBE_CPU_OP_XOR_C:
      cpu->reg.a ^= cpu->reg.c;
      cpu->reg.f = (cpu->reg.a == 0) ? 0x80 : 0x00;

      cpu->reg.pc++;
      return 4;

    case LIBYAGBE_CPU_OP_XOR_MEM_HL: {
      const uint16_t hl = (cpu->reg.h << 8) | cpu->reg.l;

      cpu->reg.a ^= libyagbe_bus_read_memory(bus, hl);
      cpu->reg.f = (cpu->reg.a == 0) ? 0x80 : 0x00;

      cpu->reg.pc++;
      return 4;
    }

    case LIBYAGBE_CPU_OP_OR_C:
      cpu->reg.a |= cpu->reg.c;
      cpu->reg.f = (cpu->reg.a == 0) ? 0x80 : 0x00;

      cpu->reg.pc++;
      return 4;

    case LIBYAGBE_CPU_OP_OR_MEM_HL: {
      const uint16_t hl = (cpu->reg.h << 8) | cpu->reg.l;
      const uint8_t value = libyagbe_bus_read_memory(bus, hl);

      cpu->reg.a |= value;
      cpu->reg.f = (cpu->reg.a == 0) ? 0xA0 : 0x20;

      cpu->reg.pc++;
      return 4;
    }

    case LIBYAGBE_CPU_OP_OR_A:
      cpu->reg.f = (cpu->reg.a == 0) ? 0x80 : 0x00;

      cpu->reg.pc++;
      return 4;

    case LIBYAGBE_CPU_OP_CP_E:
      alu_sub(cpu, cpu->reg.e, ALU_DISCARD_RESULT);
      cpu->reg.pc++;

      return 4;

    case LIBYAGBE_CPU_OP_POP_BC:
      cpu->reg.c = libyagbe_bus_read_memory(bus, cpu->reg.sp++);
      cpu->reg.b = libyagbe_bus_read_memory(bus, cpu->reg.sp++);

      cpu->reg.pc++;
      return 4;

    case LIBYAGBE_CPU_OP_JP_NZ_IMM16:
      jp_if(cpu, bus, !(cpu->reg.f & LIBYAGBE_CPU_FLAG_Z));
      return 4;

    case LIBYAGBE_CPU_OP_JP_IMM16:
      jp_if(cpu, bus, true);
      return 16;

    case LIBYAGBE_CPU_OP_CALL_NZ_IMM16:
      call_if(cpu, bus, !(cpu->reg.f & LIBYAGBE_CPU_FLAG_Z));
      return 16;

    case LIBYAGBE_CPU_OP_PUSH_BC:
      libyagbe_bus_write_memory(bus, --cpu->reg.sp, cpu->reg.b);
      libyagbe_bus_write_memory(bus, --cpu->reg.sp, cpu->reg.c);

      cpu->reg.pc++;
      return 4;

    case LIBYAGBE_CPU_OP_ADD_A_IMM8: {
      const uint8_t imm = libyagbe_bus_read_memory(bus, cpu->reg.pc + 1);

      alu_add(cpu, imm, ALU_NORMAL);
      cpu->reg.pc += 2;

      return 4;
    }

    case LIBYAGBE_CPU_OP_RET_Z:
      ret_if(cpu, bus, (cpu->reg.f & LIBYAGBE_CPU_FLAG_Z) != 0);
      return 4;

    case LIBYAGBE_CPU_OP_RET:
      ret_if(cpu, bus, true);
      return 4;

    case LIBYAGBE_CPU_OP_PREFIX_CB:
      switch (libyagbe_bus_read_memory(bus, cpu->reg.pc + 1)) {
        case LIBYAGBE_CPU_OP_RR_C:
          cpu->reg.c = alu_rr(cpu, cpu->reg.c, ALU_NORMAL);
          cpu->reg.pc += 2;

          return 4;

        case LIBYAGBE_CPU_OP_RR_D:
          cpu->reg.d = alu_rr(cpu, cpu->reg.d, ALU_NORMAL);
          cpu->reg.pc += 2;

          return 4;

        case LIBYAGBE_CPU_OP_SRL_B:
          cpu->reg.b = alu_srl(cpu, cpu->reg.b);
          cpu->reg.pc += 2;

          return 4;

        case LIBYAGBE_CPU_OP_SWAP_A:
          cpu->reg.a = ((cpu->reg.a & 0x0F) << 4) | cpu->reg.a >> 4;
          cpu->reg.f = (cpu->reg.a == 0) ? 0x80 : 0x00;

          cpu->reg.pc += 2;
          return 4;

        default:
          return 0;
      }

    case LIBYAGBE_CPU_OP_CALL_IMM16:
      call_if(cpu, bus, true);
      return 16;

    case LIBYAGBE_CPU_OP_ADC_A_IMM8: {
      const uint8_t imm = libyagbe_bus_read_memory(bus, cpu->reg.pc + 1);

      alu_add(cpu, imm, ALU_WITH_CARRY);
      cpu->reg.pc += 2;

      return 4;
    }

    case LIBYAGBE_CPU_OP_RET_NC:
      ret_if(cpu, bus, !(cpu->reg.f & LIBYAGBE_CPU_FLAG_C));
      return 4;

    case LIBYAGBE_CPU_OP_PUSH_DE:
      libyagbe_bus_write_memory(bus, --cpu->reg.sp, cpu->reg.d);
      libyagbe_bus_write_memory(bus, --cpu->reg.sp, cpu->reg.e);

      cpu->reg.pc++;
      return 4;

    case LIBYAGBE_CPU_OP_POP_DE:
      cpu->reg.e = libyagbe_bus_read_memory(bus, cpu->reg.sp++);
      cpu->reg.d = libyagbe_bus_read_memory(bus, cpu->reg.sp++);

      cpu->reg.pc++;
      return 4;

    case LIBYAGBE_CPU_OP_SUB_IMM8: {
      const uint8_t imm = libyagbe_bus_read_memory(bus, cpu->reg.pc + 1);

      alu_sub(cpu, imm, ALU_NORMAL);
      cpu->reg.pc += 2;

      return 4;
    }

    case LIBYAGBE_CPU_OP_RET_C:
      ret_if(cpu, bus, (cpu->reg.f & LIBYAGBE_CPU_FLAG_C) != 0);
      return 4;

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

    case LIBYAGBE_CPU_OP_AND_IMM8: {
      const uint8_t imm = libyagbe_bus_read_memory(bus, cpu->reg.pc + 1);

      cpu->reg.a &= imm;
      cpu->reg.f = (cpu->reg.a == 0) ? 0xA0 : 0x20;

      cpu->reg.pc += 2;
      return 4;
    }

    case LIBYAGBE_CPU_OP_JP_HL:
      cpu->reg.pc = (cpu->reg.h << 8) | cpu->reg.l;
      return 4;

    case LIBYAGBE_CPU_OP_LD_MEM_IMM16_A: {
      const uint8_t lo = libyagbe_bus_read_memory(bus, cpu->reg.pc + 1);
      const uint8_t hi = libyagbe_bus_read_memory(bus, cpu->reg.pc + 2);

      libyagbe_bus_write_memory(bus, (hi << 8) | lo, cpu->reg.a);
      cpu->reg.pc += 3;

      return 4;
    }

    case LIBYAGBE_CPU_OP_XOR_IMM8: {
      const uint8_t imm = libyagbe_bus_read_memory(bus, cpu->reg.pc + 1);

      cpu->reg.a ^= imm;
      cpu->reg.f = (cpu->reg.a == 0) ? 0x80 : 0x00;

      cpu->reg.pc += 2;
      return 4;
    }

    case LIBYAGBE_CPU_OP_LDH_A_IMM8: {
      const uint8_t imm = libyagbe_bus_read_memory(bus, cpu->reg.pc + 1);

      cpu->reg.a = libyagbe_bus_read_memory(bus, 0xFF00 + imm);
      cpu->reg.pc += 2;

      return 4;
    }

    case LIBYAGBE_CPU_OP_POP_AF:
      cpu->reg.f = libyagbe_bus_read_memory(bus, cpu->reg.sp++) & ~0x0F;
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

    case LIBYAGBE_CPU_OP_LD_A_MEM_IMM16: {
      const uint8_t lo = libyagbe_bus_read_memory(bus, cpu->reg.pc + 1);
      const uint8_t hi = libyagbe_bus_read_memory(bus, cpu->reg.pc + 2);

      cpu->reg.a = libyagbe_bus_read_memory(bus, ((hi << 8) | lo));

      cpu->reg.pc += 3;
      return 4;
    }

    case LIBYAGBE_CPU_OP_CP_IMM8: {
      const uint8_t imm = libyagbe_bus_read_memory(bus, cpu->reg.pc + 1);

      alu_sub(cpu, imm, ALU_DISCARD_RESULT);
      cpu->reg.pc += 2;

      return 4;
    }

    default:
      return 0;
  }
  return 0;
}
