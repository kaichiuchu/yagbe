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

/* Some notes:
*
*  a) while we do use branchless versions of some bit operations, they're
*     wrapped around function calls. The overhead of the function call may,
*     assuming it's not inlined negate any performance advantage of the
*     branchless techniques. Must investigate further, but it's too early to
*     worry about that now.
* 
*  b) Due to the fact we can't use anonymous structs/unions from C11 (unless we
*     can figure out a seamless wrapper), there is a readability issue.
*/

#include "libyagbe/cpu.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "libyagbe/bus.h"
#include "libyagbe/compat/compat_stdbool.h"
#include "utility.h"

/** Defines the flag bits for the Flag register. */
enum cpu_flag_bits {
  /** This bit is set if and only if the result of an operation is zero. Used by
     conditional jumps. */
  FLAG_Z = 1 << 7,

  /** This bit is set in these cases:
   *
   * When the result of an 8-bit addition is higher than $FF.
   * When the result of a 16-bit addition is higher than $FFFF.
   * When the result of a subtraction or comparison is lower than zero (like in
   * Z80 and 80x86 CPUs, but unlike in 65XX and ARM CPUs). When a rotate/shift
   * operation shifts out a “1” bit.
   *
   * Used by conditional jumps and instructions such as ADC, SBC, RL, RLA, etc.
   */
  FLAG_N = 1 << 6,
  FLAG_H = 1 << 5,
  FLAG_C = 1 << 4
};

enum alu_flag {
  /** The ALU operation should execute normally. */
  ALU_NORMAL,

  /** The ALU operation should take the carry flag into account. */
  ALU_WITH_CARRY,

  /** The ALU operation should discard the result. */
  ALU_DISCARD_RESULT,

  /** The ALU operation should forcibly clear the Zero flag. */
  ALU_CLEAR_ZERO
};

enum main_opcodes {
  OP_NOP = 0x00,
  OP_LD_BC_IMM16 = 0x01,
  OP_LD_MEM_BC_A = 0x02,
  OP_INC_BC = 0x03,
  OP_INC_B = 0x04,
  OP_DEC_B = 0x05,
  OP_LD_B_IMM8 = 0x06,
  OP_RLCA = 0x07,
  OP_LD_MEM_IMM16_SP = 0x08,
  OP_ADD_HL_BC = 0x09,
  OP_LD_A_MEM_BC = 0x0A,
  OP_DEC_BC = 0x0B,
  OP_INC_C = 0x0C,
  OP_DEC_C = 0x0D,
  OP_LD_C_IMM8 = 0x0E,
  OP_RRCA = 0x0F,
  OP_STOP = 0x10,
  OP_LD_DE_IMM16 = 0x11,
  OP_LD_MEM_DE_A = 0x12,
  OP_INC_DE = 0x13,
  OP_INC_D = 0x14,
  OP_DEC_D = 0x15,
  OP_LD_D_IMM8 = 0x16,
  OP_RLA = 0x17,
  OP_JR_SIMM8 = 0x18,
  OP_ADD_HL_DE = 0x19,
  OP_LD_A_MEM_DE = 0x1A,
  OP_DEC_DE = 0x1B,
  OP_INC_E = 0x1C,
  OP_DEC_E = 0x1D,
  OP_LD_E_IMM8 = 0x1E,
  OP_RRA = 0x1F,
  OP_JR_NZ_SIMM8 = 0x20,
  OP_LD_HL_IMM16 = 0x21,
  OP_LDI_MEM_HL_A = 0x22,
  OP_INC_HL = 0x23,
  OP_INC_H = 0x24,
  OP_DEC_H = 0x25,
  OP_LD_H_IMM8 = 0x26,
  OP_DAA = 0x27,
  OP_JR_Z_SIMM8 = 0x28,
  OP_ADD_HL_HL = 0x29,
  OP_LDI_A_MEM_HL = 0x2A,
  OP_DEC_HL = 0x2B,
  OP_INC_L = 0x2C,
  OP_DEC_L = 0x2D,
  OP_LD_L_IMM8 = 0x2E,
  OP_CPL = 0x2F,
  OP_JR_NC_SIMM8 = 0x30,
  OP_LD_SP_IMM16 = 0x31,
  OP_LDD_HL_A = 0x32,
  OP_INC_SP = 0x33,
  OP_INC_MEM_HL = 0x34,
  OP_DEC_MEM_HL = 0x35,
  OP_LD_MEM_HL_IMM8 = 0x36,
  OP_SCF = 0x37,
  OP_JR_C_SIMM8 = 0x38,
  OP_ADD_HL_SP = 0x39,
  OP_LDD_A_HL = 0x3A,
  OP_DEC_SP = 0x3B,
  OP_INC_A = 0x3C,
  OP_DEC_A = 0x3D,
  OP_LD_A_IMM8 = 0x3E,
  OP_CCF = 0x3F,
  OP_LD_B_B = 0x40,
  OP_LD_B_C = 0x41,
  OP_LD_B_D = 0x42,
  OP_LD_B_E = 0x43,
  OP_LD_B_H = 0x44,
  OP_LD_B_L = 0x45,
  OP_LD_B_MEM_HL = 0x46,
  OP_LD_B_A = 0x47,
  OP_LD_C_B = 0x48,
  OP_LD_C_C = 0x49,
  OP_LD_C_D = 0x4A,
  OP_LD_C_E = 0x4B,
  OP_LD_C_H = 0x4C,
  OP_LD_C_L = 0x4D,
  OP_LD_C_MEM_HL = 0x4E,
  OP_LD_C_A = 0x4F,
  OP_LD_D_B = 0x50,
  OP_LD_D_C = 0x51,
  OP_LD_D_D = 0x52,
  OP_LD_D_E = 0x53,
  OP_LD_D_H = 0x54,
  OP_LD_D_L = 0x55,
  OP_LD_D_MEM_HL = 0x56,
  OP_LD_D_A = 0x57,
  OP_LD_E_B = 0x58,
  OP_LD_E_C = 0x59,
  OP_LD_E_D = 0x5A,
  OP_LD_E_E = 0x5B,
  OP_LD_E_H = 0x5C,
  OP_LD_E_L = 0x5D,
  OP_LD_E_MEM_HL = 0x5E,
  OP_LD_E_A = 0x5F,
  OP_LD_H_B = 0x60,
  OP_LD_H_C = 0x61,
  OP_LD_H_D = 0x62,
  OP_LD_H_E = 0x63,
  OP_LD_H_H = 0x64,
  OP_LD_H_L = 0x65,
  OP_LD_H_MEM_HL = 0x66,
  OP_LD_H_A = 0x67,
  OP_LD_L_B = 0x68,
  OP_LD_L_C = 0x69,
  OP_LD_L_D = 0x6A,
  OP_LD_L_E = 0x6B,
  OP_LD_L_H = 0x6C,
  OP_LD_L_L = 0x6D,
  OP_LD_L_MEM_HL = 0x6E,
  OP_LD_L_A = 0x6F,
  OP_LD_MEM_HL_B = 0x70,
  OP_LD_MEM_HL_C = 0x71,
  OP_LD_MEM_HL_D = 0x72,
  OP_LD_MEM_HL_E = 0x73,
  OP_LD_MEM_HL_H = 0x74,
  OP_LD_MEM_HL_L = 0x75,
  OP_HALT = 0x76,
  OP_LD_MEM_HL_A = 0x77,
  OP_LD_A_B = 0x78,
  OP_LD_A_C = 0x79,
  OP_LD_A_D = 0x7A,
  OP_LD_A_E = 0x7B,
  OP_LD_A_H = 0x7C,
  OP_LD_A_L = 0x7D,
  OP_LD_A_MEM_HL = 0x7E,
  OP_LD_A_A = 0x7F,
  OP_ADD_A_B = 0x80,
  OP_ADD_A_C = 0x81,
  OP_ADD_A_D = 0x82,
  OP_ADD_A_E = 0x83,
  OP_ADD_A_H = 0x84,
  OP_ADD_A_L = 0x85,
  OP_ADD_A_MEM_HL = 0x86,
  OP_ADD_A_A = 0x87,
  OP_ADC_A_B = 0x88,
  OP_ADC_A_C = 0x89,
  OP_ADC_A_D = 0x8A,
  OP_ADC_A_E = 0x8B,
  OP_ADC_A_H = 0x8C,
  OP_ADC_A_L = 0x8D,
  OP_ADC_A_MEM_HL = 0x8E,
  OP_ADC_A_A = 0x8F,
  OP_SUB_B = 0x90,
  OP_SUB_C = 0x91,
  OP_SUB_D = 0x92,
  OP_SUB_E = 0x93,
  OP_SUB_H = 0x94,
  OP_SUB_L = 0x95,
  OP_SUB_MEM_HL = 0x96,
  OP_SUB_A = 0x97,
  OP_SBC_A_B = 0x98,
  OP_SBC_A_C = 0x99,
  OP_SBC_A_D = 0x9A,
  OP_SBC_A_E = 0x9B,
  OP_SBC_A_H = 0x9C,
  OP_SBC_A_L = 0x9D,
  OP_SBC_A_MEM_HL = 0x9E,
  OP_SBC_A_A = 0x9F,
  OP_AND_B = 0xA0,
  OP_AND_C = 0xA1,
  OP_AND_D = 0xA2,
  OP_AND_E = 0xA3,
  OP_AND_H = 0xA4,
  OP_AND_L = 0xA5,
  OP_AND_MEM_HL = 0xA6,
  OP_AND_A = 0xA7,
  OP_XOR_B = 0xA8,
  OP_XOR_C = 0xA9,
  OP_XOR_D = 0xAA,
  OP_XOR_E = 0xAB,
  OP_XOR_H = 0xAC,
  OP_XOR_L = 0xAD,
  OP_XOR_MEM_HL = 0xAE,
  OP_XOR_A = 0xAF,
  OP_OR_B = 0xB0,
  OP_OR_C = 0xB1,
  OP_OR_D = 0xB2,
  OP_OR_E = 0xB3,
  OP_OR_H = 0xB4,
  OP_OR_L = 0xB5,
  OP_OR_MEM_HL = 0xB6,
  OP_OR_A = 0xB7,
  OP_CP_B = 0xB8,
  OP_CP_C = 0xB9,
  OP_CP_D = 0xBA,
  OP_CP_E = 0xBB,
  OP_CP_H = 0xBC,
  OP_CP_L = 0xBD,
  OP_CP_MEM_HL = 0xBE,
  OP_CP_A = 0xBF,
  OP_RET_NZ = 0xC0,
  OP_POP_BC = 0xC1,
  OP_JP_NZ_IMM16 = 0xC2,
  OP_JP_IMM16 = 0xC3,
  OP_CALL_NZ_IMM16 = 0xC4,
  OP_PUSH_BC = 0xC5,
  OP_ADD_A_IMM8 = 0xC6,
  OP_RST_00 = 0xC7,
  OP_RET_Z = 0xC8,
  OP_RET = 0xC9,
  OP_JP_Z_IMM16 = 0xCA,
  OP_PREFIX_CB = 0xCB,
  OP_CALL_Z_IMM16 = 0xCC,
  OP_CALL_IMM16 = 0xCD,
  OP_ADC_A_IMM8 = 0xCE,
  OP_RST_08 = 0xCF,
  OP_RET_NC = 0xD0,
  OP_POP_DE = 0xD1,
  OP_JP_NC_IMM16 = 0xD2,
  OP_CALL_NC_IMM16 = 0xD4,
  OP_PUSH_DE = 0xD5,
  OP_SUB_IMM8 = 0xD6,
  OP_RST_10 = 0xD7,
  OP_RET_C = 0xD8,
  OP_RETI = 0xD9,
  OP_JP_C_IMM16 = 0xDA,
  OP_CALL_C_IMM16 = 0xDC,
  OP_SBC_A_IMM8 = 0xDE,
  OP_RST_18 = 0xDF,
  OP_LDH_IMM8_A = 0xE0,
  OP_POP_HL = 0xE1,
  OP_LD_MEM_FF00_C_A = 0xE2,
  OP_PUSH_HL = 0xE5,
  OP_AND_IMM8 = 0xE6,
  OP_RST_20 = 0xE7,
  OP_ADD_SP_SIMM8 = 0xE8,
  OP_JP_HL = 0xE9,
  OP_LD_MEM_IMM16_A = 0xEA,
  OP_XOR_IMM8 = 0xEE,
  OP_RST_28 = 0xEF,
  OP_LDH_A_IMM8 = 0xF0,
  OP_POP_AF = 0xF1,
  OP_LD_A_MEM_FF00_C = 0xF2,
  OP_DI = 0xF3,
  OP_PUSH_AF = 0xF5,
  OP_OR_IMM8 = 0xF6,
  OP_RST_30 = 0xF7,
  OP_LD_HL_SP_SIMM8 = 0xF8,
  OP_LD_SP_HL = 0xF9,
  OP_LD_A_MEM_IMM16 = 0xFA,
  OP_EI = 0xFB,
  OP_CP_IMM8 = 0xFE,
  OP_RST_38 = 0xFF
};

/* Defines the opcodes within the 0xCB table. */
enum cb_opcodes {
  OP_RLC_B = 0x00,
  OP_RLC_C = 0x01,
  OP_RLC_D = 0x02,
  OP_RLC_E = 0x03,
  OP_RLC_H = 0x04,
  OP_RLC_L = 0x05,
  OP_RLC_HL = 0x06,
  OP_RLC_A = 0x07,
  OP_RRC_B = 0x08,
  OP_RRC_C = 0x09,
  OP_RRC_D = 0x0A,
  OP_RRC_E = 0x0B,
  OP_RRC_H = 0x0C,
  OP_RRC_L = 0x0D,
  OP_RRC_HL = 0x0E,
  OP_RRC_A = 0x0F,
  OP_RL_B = 0x10,
  OP_RL_C = 0x11,
  OP_RL_D = 0x12,
  OP_RL_E = 0x13,
  OP_RL_H = 0x14,
  OP_RL_L = 0x15,
  OP_RL_HL = 0x16,
  OP_RL_A = 0x17,
  OP_RR_B = 0x18,
  OP_RR_C = 0x19,
  OP_RR_D = 0x1A,
  OP_RR_E = 0x1B,
  OP_RR_H = 0x1C,
  OP_RR_L = 0x1D,
  OP_RR_HL = 0x1E,
  OP_RR_A = 0x1F,
  OP_SLA_B = 0x20,
  OP_SLA_C = 0x21,
  OP_SLA_D = 0x22,
  OP_SLA_E = 0x23,
  OP_SLA_H = 0x24,
  OP_SLA_L = 0x25,
  OP_SLA_HL = 0x26,
  OP_SLA_A = 0x27,
  OP_SRA_B = 0x28,
  OP_SRA_C = 0x29,
  OP_SRA_D = 0x2A,
  OP_SRA_E = 0x2B,
  OP_SRA_H = 0x2C,
  OP_SRA_L = 0x2D,
  OP_SRA_HL = 0x2E,
  OP_SRA_A = 0x2F,
  OP_SWAP_B = 0x30,
  OP_SWAP_C = 0x31,
  OP_SWAP_D = 0x32,
  OP_SWAP_E = 0x33,
  OP_SWAP_H = 0x34,
  OP_SWAP_L = 0x35,
  OP_SWAP_HL = 0x36,
  OP_SWAP_A = 0x37,
  OP_SRL_B = 0x38,
  OP_SRL_C = 0x39,
  OP_SRL_D = 0x3A,
  OP_SRL_E = 0x3B,
  OP_SRL_H = 0x3C,
  OP_SRL_L = 0x3D,
  OP_SRL_HL = 0x3E,
  OP_SRL_A = 0x3F,
  OP_BIT_0_B = 0x40,
  OP_BIT_0_C = 0x41,
  OP_BIT_0_D = 0x42,
  OP_BIT_0_E = 0x43,
  OP_BIT_0_H = 0x44,
  OP_BIT_0_L = 0x45,
  OP_BIT_0_HL = 0x46,
  OP_BIT_0_A = 0x47,
  OP_BIT_1_B = 0x48,
  OP_BIT_1_C = 0x49,
  OP_BIT_1_D = 0x4A,
  OP_BIT_1_E = 0x4B,
  OP_BIT_1_H = 0x4C,
  OP_BIT_1_L = 0x4D,
  OP_BIT_1_HL = 0x4E,
  OP_BIT_1_A = 0x4F,
  OP_BIT_2_B = 0x50,
  OP_BIT_2_C = 0x51,
  OP_BIT_2_D = 0x52,
  OP_BIT_2_E = 0x53,
  OP_BIT_2_H = 0x54,
  OP_BIT_2_L = 0x55,
  OP_BIT_2_HL = 0x56,
  OP_BIT_2_A = 0x57,
  OP_BIT_3_B = 0x58,
  OP_BIT_3_C = 0x59,
  OP_BIT_3_D = 0x5A,
  OP_BIT_3_E = 0x5B,
  OP_BIT_3_H = 0x5C,
  OP_BIT_3_L = 0x5D,
  OP_BIT_3_HL = 0x5E,
  OP_BIT_3_A = 0x5F,
  OP_BIT_4_B = 0x60,
  OP_BIT_4_C = 0x61,
  OP_BIT_4_D = 0x62,
  OP_BIT_4_E = 0x63,
  OP_BIT_4_H = 0x64,
  OP_BIT_4_L = 0x65,
  OP_BIT_4_HL = 0x66,
  OP_BIT_4_A = 0x67,
  OP_BIT_5_B = 0x68,
  OP_BIT_5_C = 0x69,
  OP_BIT_5_D = 0x6A,
  OP_BIT_5_E = 0x6B,
  OP_BIT_5_H = 0x6C,
  OP_BIT_5_L = 0x6D,
  OP_BIT_5_HL = 0x6E,
  OP_BIT_5_A = 0x6F,
  OP_BIT_6_B = 0x70,
  OP_BIT_6_C = 0x71,
  OP_BIT_6_D = 0x72,
  OP_BIT_6_E = 0x73,
  OP_BIT_6_H = 0x74,
  OP_BIT_6_L = 0x75,
  OP_BIT_6_HL = 0x76,
  OP_BIT_6_A = 0x77,
  OP_BIT_7_B = 0x78,
  OP_BIT_7_C = 0x79,
  OP_BIT_7_D = 0x7A,
  OP_BIT_7_E = 0x7B,
  OP_BIT_7_H = 0x7C,
  OP_BIT_7_L = 0x7D,
  OP_BIT_7_HL = 0x7E,
  OP_BIT_7_A = 0x7F,
  OP_RES_0_B = 0x80,
  OP_RES_0_C = 0x81,
  OP_RES_0_D = 0x82,
  OP_RES_0_E = 0x83,
  OP_RES_0_H = 0x84,
  OP_RES_0_L = 0x85,
  OP_RES_0_HL = 0x86,
  OP_RES_0_A = 0x87,
  OP_RES_1_B = 0x88,
  OP_RES_1_C = 0x89,
  OP_RES_1_D = 0x8A,
  OP_RES_1_E = 0x8B,
  OP_RES_1_H = 0x8C,
  OP_RES_1_L = 0x8D,
  OP_RES_1_HL = 0x8E,
  OP_RES_1_A = 0x8F,
  OP_RES_2_B = 0x90,
  OP_RES_2_C = 0x91,
  OP_RES_2_D = 0x92,
  OP_RES_2_E = 0x93,
  OP_RES_2_H = 0x94,
  OP_RES_2_L = 0x95,
  OP_RES_2_HL = 0x96,
  OP_RES_2_A = 0x97,
  OP_RES_3_B = 0x98,
  OP_RES_3_C = 0x99,
  OP_RES_3_D = 0x9A,
  OP_RES_3_E = 0x9B,
  OP_RES_3_H = 0x9C,
  OP_RES_3_L = 0x9D,
  OP_RES_3_HL = 0x9E,
  OP_RES_3_A = 0x9F,
  OP_RES_4_B = 0xA0,
  OP_RES_4_C = 0xA1,
  OP_RES_4_D = 0xA2,
  OP_RES_4_E = 0xA3,
  OP_RES_4_H = 0xA4,
  OP_RES_4_L = 0xA5,
  OP_RES_4_HL = 0xA6,
  OP_RES_4_A = 0xA7,
  OP_RES_5_B = 0xA8,
  OP_RES_5_C = 0xA9,
  OP_RES_5_D = 0xAA,
  OP_RES_5_E = 0xAB,
  OP_RES_5_H = 0xAC,
  OP_RES_5_L = 0xAD,
  OP_RES_5_HL = 0xAE,
  OP_RES_5_A = 0xAF,
  OP_RES_6_B = 0xB0,
  OP_RES_6_C = 0xB1,
  OP_RES_6_D = 0xB2,
  OP_RES_6_E = 0xB3,
  OP_RES_6_H = 0xB4,
  OP_RES_6_L = 0xB5,
  OP_RES_6_HL = 0xB6,
  OP_RES_6_A = 0xB7,
  OP_RES_7_B = 0xB8,
  OP_RES_7_C = 0xB9,
  OP_RES_7_D = 0xBA,
  OP_RES_7_E = 0xBB,
  OP_RES_7_H = 0xBC,
  OP_RES_7_L = 0xBD,
  OP_RES_7_HL = 0xBE,
  OP_RES_7_A = 0xBF,
  OP_SET_0_B = 0xC0,
  OP_SET_0_C = 0xC1,
  OP_SET_0_D = 0xC2,
  OP_SET_0_E = 0xC3,
  OP_SET_0_H = 0xC4,
  OP_SET_0_L = 0xC5,
  OP_SET_0_HL = 0xC6,
  OP_SET_0_A = 0xC7,
  OP_SET_1_B = 0xC8,
  OP_SET_1_C = 0xC9,
  OP_SET_1_D = 0xCA,
  OP_SET_1_E = 0xCB,
  OP_SET_1_H = 0xCC,
  OP_SET_1_L = 0xCD,
  OP_SET_1_HL = 0xCE,
  OP_SET_1_A = 0xCF,
  OP_SET_2_B = 0xD0,
  OP_SET_2_C = 0xD1,
  OP_SET_2_D = 0xD2,
  OP_SET_2_E = 0xD3,
  OP_SET_2_H = 0xD4,
  OP_SET_2_L = 0xD5,
  OP_SET_2_HL = 0xD6,
  OP_SET_2_A = 0xD7,
  OP_SET_3_B = 0xD8,
  OP_SET_3_C = 0xD9,
  OP_SET_3_D = 0xDA,
  OP_SET_3_E = 0xDB,
  OP_SET_3_H = 0xDC,
  OP_SET_3_L = 0xDD,
  OP_SET_3_HL = 0xDE,
  OP_SET_3_A = 0xDF,
  OP_SET_4_B = 0xE0,
  OP_SET_4_C = 0xE1,
  OP_SET_4_D = 0xE2,
  OP_SET_4_E = 0xE3,
  OP_SET_4_H = 0xE4,
  OP_SET_4_L = 0xE5,
  OP_SET_4_HL = 0xE6,
  OP_SET_4_A = 0xE7,
  OP_SET_5_B = 0xE8,
  OP_SET_5_C = 0xE9,
  OP_SET_5_D = 0xEA,
  OP_SET_5_E = 0xEB,
  OP_SET_5_H = 0xEC,
  OP_SET_5_L = 0xED,
  OP_SET_5_HL = 0xEE,
  OP_SET_5_A = 0xEF,
  OP_SET_6_B = 0xF0,
  OP_SET_6_C = 0xF1,
  OP_SET_6_D = 0xF2,
  OP_SET_6_E = 0xF3,
  OP_SET_6_H = 0xF4,
  OP_SET_6_L = 0xF5,
  OP_SET_6_HL = 0xF6,
  OP_SET_6_A = 0xF7,
  OP_SET_7_B = 0xF8,
  OP_SET_7_C = 0xF9,
  OP_SET_7_D = 0xFA,
  OP_SET_7_E = 0xFB,
  OP_SET_7_H = 0xFC,
  OP_SET_7_L = 0xFD,
  OP_SET_7_HL = 0xFE,
  OP_SET_7_A = 0xFF
};

static void stack_push(struct libyagbe_cpu* const cpu,
                       struct libyagbe_bus* const bus, const uint8_t hi,
                       const uint8_t lo) {
  assert(cpu != NULL);
  assert(bus != NULL);

  libyagbe_bus_write_memory(bus, --cpu->reg.sp, hi);
  libyagbe_bus_write_memory(bus, --cpu->reg.sp, lo);
}

/* @brief Reads the immediate byte referenced by the program counter, then
*  increments the program counter.
*
* @param cpu The CPU instance.
* @param bus The system bus instance.
* 
* @returns The immediate byte.
*/
static uint8_t read_imm8(struct libyagbe_cpu* const cpu,
                         struct libyagbe_bus* const bus) {
  assert(cpu != NULL);
  assert(bus != NULL);

  return libyagbe_bus_read_memory(bus, cpu->reg.pc++);
}

/* @brief Reads the next two bytes from memory referenced by the program counter. */
static uint16_t read_imm16(struct libyagbe_cpu* const cpu,
                           struct libyagbe_bus* const bus) {
  assert(cpu != NULL);
  assert(bus != NULL);

  const uint8_t lo = read_imm8(cpu, bus);
  const uint8_t hi = read_imm8(cpu, bus);

  return (uint16_t)((hi << 8) | lo);
}

static uint8_t set_zero_flag(uint8_t flag_reg, const uint8_t n) {
  SET_BIT_IF(flag_reg, FLAG_Z, n == 0);
  return flag_reg;
}

static uint8_t set_half_carry_flag(uint8_t flag_reg, const bool condition) {
  SET_BIT_IF(flag_reg, FLAG_H, condition);
  return flag_reg;
}

static uint8_t set_carry_flag(uint8_t flag_reg, const bool condition) {
  SET_BIT_IF(flag_reg, FLAG_C, condition);
  return flag_reg;
}

static uint8_t alu_inc(struct libyagbe_cpu* const cpu, uint8_t value) {
  assert(cpu != NULL);

  cpu->reg.af.byte.lo &= ~FLAG_N;
  cpu->reg.af.byte.lo =
      set_half_carry_flag(cpu->reg.af.byte.lo, (value & 0x0F) == 0xF);

  value++;

  cpu->reg.af.byte.lo = set_zero_flag(cpu->reg.af.byte.lo, value);
  return value;
}

static uint8_t alu_dec(struct libyagbe_cpu* const cpu, uint8_t value) {
  assert(cpu != NULL);

  cpu->reg.af.byte.lo |= FLAG_N;

  cpu->reg.af.byte.lo =
      set_half_carry_flag(cpu->reg.af.byte.lo, (value & 0x0F) == 0);

  value--;

  cpu->reg.af.byte.lo = set_zero_flag(cpu->reg.af.byte.lo, value);
  return value;
}

static void alu_add_hl(struct libyagbe_cpu* const cpu, const uint16_t pair) {
  assert(cpu != NULL);

  cpu->reg.af.byte.lo &= ~FLAG_N;

  const int sum = cpu->reg.hl.value + pair;

  cpu->reg.af.byte.lo = set_half_carry_flag(
    cpu->reg.af.byte.lo, ((cpu->reg.hl.value ^ pair ^ sum) & 0x1000) != 0);

  cpu->reg.af.byte.lo = set_carry_flag(cpu->reg.af.byte.lo, sum > 0xFFFF);
  cpu->reg.hl.value = (uint16_t)sum;
}

static uint8_t alu_rr(struct libyagbe_cpu* const cpu, uint8_t reg,
                      const enum alu_flag flag) {
  cpu->reg.af.byte.lo &= ~FLAG_N;
  cpu->reg.af.byte.lo &= ~FLAG_H;

  const uint8_t old_carry_flag_value =
      ((cpu->reg.af.byte.lo & FLAG_C) != 0) ? 0x80 : 0x00;

  cpu->reg.af.byte.lo = set_carry_flag(cpu->reg.af.byte.lo, (reg & 1) != 0);

  reg >>= 1;
  reg |= old_carry_flag_value;

  if (flag == ALU_CLEAR_ZERO) {
    cpu->reg.af.byte.lo &= ~FLAG_Z;
    return reg;
  }

  cpu->reg.af.byte.lo = set_zero_flag(cpu->reg.af.byte.lo, reg);
  return reg;
}

static uint8_t alu_rl(struct libyagbe_cpu* const cpu, uint8_t reg, const enum alu_flag flag) {
  assert(cpu != NULL);

  cpu->reg.af.byte.lo &= ~FLAG_N;
  cpu->reg.af.byte.lo &= ~FLAG_H;

  const uint8_t old_carry_flag_value = (cpu->reg.af.byte.lo & FLAG_C) != 0;

  cpu->reg.af.byte.lo = set_carry_flag(cpu->reg.af.byte.lo, (reg & 0x80) != 0);
  reg = (uint8_t)((reg << 1) | old_carry_flag_value);

  if (flag == ALU_CLEAR_ZERO) {
    cpu->reg.af.byte.lo &= ~FLAG_Z;
    return reg;
  }

  cpu->reg.af.byte.lo = set_zero_flag(cpu->reg.af.byte.lo, reg);
  return reg;
}

static void alu_add(struct libyagbe_cpu* const cpu, const uint8_t addend,
                    const enum alu_flag flag) {
  assert(cpu != NULL);

  cpu->reg.af.byte.lo &= ~FLAG_N;

  int sum = cpu->reg.af.byte.hi + addend;

  if (flag == ALU_WITH_CARRY) {
    sum += (cpu->reg.af.byte.lo & FLAG_C) != 0;
  }

  const uint8_t result = (uint8_t)sum;

  cpu->reg.af.byte.lo = set_zero_flag(cpu->reg.af.byte.lo, result);

  cpu->reg.af.byte.lo = set_half_carry_flag(
      cpu->reg.af.byte.lo, ((cpu->reg.af.byte.hi ^ addend ^ sum) & 0x10) != 0);

  cpu->reg.af.byte.lo = set_carry_flag(cpu->reg.af.byte.lo, sum > 0xFF);

  cpu->reg.af.byte.hi = result;
}

static void alu_sub(struct libyagbe_cpu* const cpu, uint8_t subtrahend,
                    const enum alu_flag flag) {
  assert(cpu != NULL);

  cpu->reg.af.byte.lo |= FLAG_N;

  int diff = cpu->reg.af.byte.hi - subtrahend;

  if (flag == ALU_WITH_CARRY) {
    diff -= (cpu->reg.af.byte.lo & FLAG_C) != 0;
  }

  const uint8_t result = (uint8_t)diff;

  cpu->reg.af.byte.lo = set_zero_flag(cpu->reg.af.byte.lo, result);

  cpu->reg.af.byte.lo = set_half_carry_flag(
      cpu->reg.af.byte.lo,
      ((cpu->reg.af.byte.hi ^ subtrahend ^ diff) & 0x10) != 0);

  cpu->reg.af.byte.lo =
      set_carry_flag(cpu->reg.af.byte.lo, diff < 0);

  if (flag != ALU_DISCARD_RESULT) {
    cpu->reg.af.byte.hi = result;
  }
}

static uint8_t alu_srl(struct libyagbe_cpu* const cpu, uint8_t reg) {
  cpu->reg.af.byte.lo &= ~FLAG_N;
  cpu->reg.af.byte.lo &= ~FLAG_H;

  cpu->reg.af.byte.lo = set_carry_flag(cpu->reg.af.byte.lo, (reg & 1) != 0);
  reg >>= 1;
  cpu->reg.af.byte.lo = set_zero_flag(cpu->reg.af.byte.lo, reg);

  return reg;
}

static void call_if(struct libyagbe_cpu* const cpu,
                    struct libyagbe_bus* const bus, const bool condition_met) {
  assert(cpu != NULL);
  assert(bus != NULL);

  const uint16_t address = read_imm16(cpu, bus);

  if (condition_met) {
    stack_push(cpu, bus, cpu->reg.pc >> 8, cpu->reg.pc & 0x00FF);
    cpu->reg.pc = address;
  }
}

static void jr_if(struct libyagbe_bus* const bus,
                  struct libyagbe_cpu* const cpu, const bool condition_met) {
  assert(bus != NULL);
  assert(cpu != NULL);

  const int8_t imm = (int8_t)read_imm8(cpu, bus);

  if (condition_met) {
    cpu->reg.pc += imm;
  }
}

static void jp_if(struct libyagbe_cpu* const cpu,
                  struct libyagbe_bus* const bus, const bool condition_met) {
  assert(bus != NULL);
  assert(cpu != NULL);

  const uint16_t address = read_imm16(cpu, bus);

  if (condition_met) {
    cpu->reg.pc = address;
  }
}

static uint16_t stack_pop(struct libyagbe_cpu* const cpu,
                          struct libyagbe_bus* const bus) {
  assert(cpu != NULL);
  assert(bus != NULL);

  const uint8_t lo = libyagbe_bus_read_memory(bus, cpu->reg.sp++);
  const uint8_t hi = libyagbe_bus_read_memory(bus, cpu->reg.sp++);

  return (uint16_t)((hi << 8) | lo);
}

static void ret_if(struct libyagbe_cpu* const cpu,
                   struct libyagbe_bus* const bus, const bool condition_met) {
  assert(bus != NULL);
  assert(cpu != NULL);

  if (condition_met) {
    cpu->reg.pc = stack_pop(cpu, bus);
  }
}

static void rst(struct libyagbe_cpu* const cpu, struct libyagbe_bus* const bus,
    const uint16_t address) {
  assert(bus != NULL);
  assert(cpu != NULL);

  stack_push(cpu, bus, cpu->reg.pc >> 8, cpu->reg.pc & 0x00FF);
  cpu->reg.pc = address;
}

static uint8_t alu_rlc(struct libyagbe_cpu* const cpu, uint8_t n, const enum alu_flag flag) {
  assert(cpu != NULL);

  cpu->reg.af.byte.lo &= ~FLAG_N;
  cpu->reg.af.byte.lo &= ~FLAG_H;

  cpu->reg.af.byte.lo = set_carry_flag(cpu->reg.af.byte.lo, (n & 0x80) != 0);
  n = (uint8_t)((n << 1) | (n >> 7));

  if (flag == ALU_CLEAR_ZERO) {
    cpu->reg.af.byte.lo &= ~FLAG_Z;
    return n;
  }
  cpu->reg.af.byte.lo = set_zero_flag(cpu->reg.af.byte.lo, n);
  return n;
}

static uint8_t alu_rrc(struct libyagbe_cpu* const cpu, uint8_t n, const enum alu_flag flag) {
  assert(cpu != NULL);

  cpu->reg.af.byte.lo &= ~FLAG_N;
  cpu->reg.af.byte.lo &= ~FLAG_H;

  cpu->reg.af.byte.lo = set_carry_flag(cpu->reg.af.byte.lo, (n & 1) != 0);
  n = (uint8_t)((n >> 1) | (n << 7));

  if (flag == ALU_CLEAR_ZERO) {
    cpu->reg.af.byte.lo &= ~FLAG_Z;
    return n;
  }

  cpu->reg.af.byte.lo = set_zero_flag(cpu->reg.af.byte.lo, n);
  return n;
}

static uint8_t alu_sla(struct libyagbe_cpu* const cpu, uint8_t n) {
  assert(cpu != NULL);

  cpu->reg.af.byte.lo &= ~FLAG_N;
  cpu->reg.af.byte.lo &= ~FLAG_H;

  cpu->reg.af.byte.lo = set_carry_flag(cpu->reg.af.byte.lo, (n & 0x80) != 0);
  n <<= 1;
  cpu->reg.af.byte.lo = set_zero_flag(cpu->reg.af.byte.lo, n);

  return n;
}

static uint8_t alu_sra(struct libyagbe_cpu* const cpu, uint8_t n) {
  assert(cpu != NULL);

  cpu->reg.af.byte.lo &= ~FLAG_N;
  cpu->reg.af.byte.lo &= ~FLAG_H;

  cpu->reg.af.byte.lo = set_carry_flag(cpu->reg.af.byte.lo, (n & 1) != 0);
  n = (uint8_t)((n >> 1) | (n & 0x80));
  cpu->reg.af.byte.lo = set_zero_flag(cpu->reg.af.byte.lo, n);

  return n;
}

static uint8_t alu_bit(uint8_t flag_reg, const int bit_mask, const uint8_t n) {
  SET_BIT_IF(flag_reg, FLAG_Z, (n & bit_mask) == 0);
  flag_reg &= ~FLAG_N;
  flag_reg |= FLAG_H;

  return flag_reg;
}

void libyagbe_cpu_reset(struct libyagbe_cpu* const cpu) {
  assert(cpu != NULL);

  cpu->reg.af.value = 0x01B0;
  cpu->reg.bc.value = 0x0013;
  cpu->reg.de.value = 0x00D8;
  cpu->reg.hl.value = 0x014D;

  cpu->reg.sp = 0xFFFE;
  cpu->reg.pc = 0x0100;
}

void libyagbe_cpu_step(struct libyagbe_cpu* const cpu,
                       struct libyagbe_bus* const bus) {
  assert(cpu != NULL);
  assert(bus != NULL);

  cpu->instruction = read_imm8(cpu, bus);

  switch (cpu->instruction) {
    case OP_NOP:
      return;

    case OP_LD_BC_IMM16:
      cpu->reg.bc.value = read_imm16(cpu, bus);
      return;

    case OP_INC_BC:
      cpu->reg.bc.value++;
      return;

    case OP_LD_MEM_BC_A:
      libyagbe_bus_write_memory(bus, cpu->reg.bc.value, cpu->reg.af.byte.hi);
      return;

    case OP_INC_B:
      cpu->reg.bc.byte.hi = alu_inc(cpu, cpu->reg.bc.byte.hi);
      return;

    case OP_DEC_B:
      cpu->reg.bc.byte.hi = alu_dec(cpu, cpu->reg.bc.byte.hi);
      return;

    case OP_LD_B_IMM8:
      cpu->reg.bc.byte.hi = read_imm8(cpu, bus);
      return;

    case OP_RLCA:
      cpu->reg.af.byte.hi = alu_rlc(cpu, cpu->reg.af.byte.hi, ALU_CLEAR_ZERO);
      return;

    case OP_LD_MEM_IMM16_SP: {
      const uint16_t imm16 = read_imm16(cpu, bus);

      libyagbe_bus_write_memory(bus, imm16, cpu->reg.sp & 0x00FF);
      libyagbe_bus_write_memory(bus, imm16 + 1, cpu->reg.sp >> 8);

      return;
    }

    case OP_ADD_HL_BC:
      alu_add_hl(cpu, cpu->reg.bc.value);
      return;

    case OP_LD_A_MEM_BC:
      cpu->reg.af.byte.hi = libyagbe_bus_read_memory(bus, cpu->reg.bc.value);
      return;

    case OP_DEC_BC:
      cpu->reg.bc.value--;
      return;

    case OP_INC_C:
      cpu->reg.bc.byte.lo = alu_inc(cpu, cpu->reg.bc.byte.lo);
      return;

    case OP_DEC_C:
      cpu->reg.bc.byte.lo = alu_dec(cpu, cpu->reg.bc.byte.lo);
      return;

    case OP_LD_C_IMM8:
      cpu->reg.bc.byte.lo = read_imm8(cpu, bus);
      return;

    case OP_RRCA:
      cpu->reg.af.byte.hi = alu_rrc(cpu, cpu->reg.af.byte.hi, ALU_CLEAR_ZERO);
      return;

    case OP_LD_DE_IMM16:
      cpu->reg.de.value = read_imm16(cpu, bus);
      return;

    case OP_LD_MEM_DE_A:
      libyagbe_bus_write_memory(bus, cpu->reg.de.value, cpu->reg.af.byte.hi);
      return;

    case OP_INC_DE:
      cpu->reg.de.value++;
      return;

    case OP_INC_D:
      cpu->reg.de.byte.hi = alu_inc(cpu, cpu->reg.de.byte.hi);
      return;

    case OP_DEC_D:
      cpu->reg.de.byte.hi = alu_dec(cpu, cpu->reg.de.byte.hi);
      return;

    case OP_LD_D_IMM8:
      cpu->reg.de.byte.hi = read_imm8(cpu, bus);
      return;

    case OP_RLA:
      cpu->reg.af.byte.hi = alu_rl(cpu, cpu->reg.af.byte.hi, ALU_CLEAR_ZERO);
      return;

    case OP_JR_SIMM8:
      jr_if(bus, cpu, true);
      return;

    case OP_ADD_HL_DE:
      alu_add_hl(cpu, cpu->reg.de.value);
      return;

    case OP_LD_A_MEM_DE:
      cpu->reg.af.byte.hi = libyagbe_bus_read_memory(bus, cpu->reg.de.value);
      return;

    case OP_DEC_DE:
      cpu->reg.de.value--;
      return;

    case OP_INC_E:
      cpu->reg.de.byte.lo = alu_inc(cpu, cpu->reg.de.byte.lo);
      return;

    case OP_DEC_E:
      cpu->reg.de.byte.lo = alu_dec(cpu, cpu->reg.de.byte.lo);
      return;

    case OP_LD_E_IMM8:
      cpu->reg.de.byte.lo = read_imm8(cpu, bus);
      return;

    case OP_RRA:
      cpu->reg.af.byte.hi = alu_rr(cpu, cpu->reg.af.byte.hi, ALU_CLEAR_ZERO);
      return;

    case OP_JR_NZ_SIMM8:
      jr_if(bus, cpu, !(cpu->reg.af.byte.lo & FLAG_Z));
      return;

    case OP_LD_HL_IMM16:
      cpu->reg.hl.value = read_imm16(cpu, bus);
      return;

    case OP_LDI_MEM_HL_A:
      libyagbe_bus_write_memory(bus, cpu->reg.hl.value++, cpu->reg.af.byte.hi);
      return;

    case OP_INC_HL:
      cpu->reg.hl.value++;
      return;

    case OP_INC_H:
      cpu->reg.hl.byte.hi = alu_inc(cpu, cpu->reg.hl.byte.hi);
      return;

    case OP_DEC_H:
      cpu->reg.hl.byte.hi = alu_dec(cpu, cpu->reg.hl.byte.hi);
      return;

    case OP_LD_H_IMM8:
      cpu->reg.hl.byte.hi = read_imm8(cpu, bus);
      return;

    case OP_DAA:
      return;

    case OP_JR_Z_SIMM8:
      jr_if(bus, cpu, (cpu->reg.af.byte.lo & FLAG_Z) != 0);
      return;

    case OP_ADD_HL_HL:
      alu_add_hl(cpu, cpu->reg.hl.value);
      return;

    case OP_LDI_A_MEM_HL:
      cpu->reg.af.byte.hi = libyagbe_bus_read_memory(bus, cpu->reg.hl.value++);
      return;

    case OP_DEC_HL:
      cpu->reg.hl.value--;
      return;

    case OP_INC_L:
      cpu->reg.hl.byte.lo = alu_inc(cpu, cpu->reg.hl.byte.lo);
      return;

    case OP_DEC_L:
      cpu->reg.hl.byte.lo = alu_dec(cpu, cpu->reg.hl.byte.lo);
      return;

    case OP_LD_L_IMM8:
      cpu->reg.hl.byte.lo = read_imm8(cpu, bus);
      return;

    case OP_CPL:
      cpu->reg.af.byte.hi = ~cpu->reg.af.byte.hi;
      cpu->reg.af.byte.lo |= FLAG_N;
      cpu->reg.af.byte.lo |= FLAG_H;

      return;

    case OP_JR_NC_SIMM8:
      jr_if(bus, cpu, !(cpu->reg.af.byte.lo & FLAG_C));
      return;

    case OP_LD_SP_IMM16:
      cpu->reg.sp = read_imm16(cpu, bus);
      return;

    case OP_LDD_HL_A:
      libyagbe_bus_write_memory(bus, cpu->reg.hl.value--, cpu->reg.af.byte.hi);
      return;

    case OP_INC_SP:
      cpu->reg.sp++;
      return;

    case OP_INC_MEM_HL: {
      uint8_t data = libyagbe_bus_read_memory(bus, cpu->reg.hl.value);
      data = alu_inc(cpu, data);
      libyagbe_bus_write_memory(bus, cpu->reg.hl.value, data);

      return;
    }

    case OP_DEC_MEM_HL: {
      uint8_t data = libyagbe_bus_read_memory(bus, cpu->reg.hl.value);
      data = alu_dec(cpu, data);
      libyagbe_bus_write_memory(bus, cpu->reg.hl.value, data);

      return;
    }

    case OP_LD_MEM_HL_IMM8: {
      const uint8_t imm8 = read_imm8(cpu, bus);
      libyagbe_bus_write_memory(bus, cpu->reg.hl.value, imm8);

      return;
    }

    case OP_SCF:
      cpu->reg.af.byte.lo &= ~FLAG_N;
      cpu->reg.af.byte.lo &= ~FLAG_H;
      cpu->reg.af.byte.lo |= FLAG_C;

      return;

    case OP_JR_C_SIMM8:
      jr_if(bus, cpu, (cpu->reg.af.byte.lo & FLAG_C) != 0);
      return;

    case OP_ADD_HL_SP:
      alu_add_hl(cpu, cpu->reg.sp);
      return;

    case OP_LDD_A_HL:
      cpu->reg.af.byte.hi = libyagbe_bus_read_memory(bus, cpu->reg.hl.value--);
      return;

    case OP_DEC_SP:
      cpu->reg.sp--;
      return;

    case OP_INC_A:
      cpu->reg.af.byte.hi = alu_inc(cpu, cpu->reg.af.byte.hi);
      return;

    case OP_DEC_A:
      cpu->reg.af.byte.hi = alu_dec(cpu, cpu->reg.af.byte.hi);
      return;

    case OP_LD_A_IMM8:
      cpu->reg.af.byte.hi = read_imm8(cpu, bus);
      return;

    case OP_CCF:
      cpu->reg.af.byte.lo &= ~FLAG_N;
      cpu->reg.af.byte.lo &= ~FLAG_H;
      cpu->reg.af.byte.lo =
          set_carry_flag(cpu->reg.af.byte.lo, !(cpu->reg.af.byte.lo & FLAG_C));
      return;

    case OP_LD_B_B:
      return;

    case OP_LD_B_C:
      cpu->reg.bc.byte.hi = cpu->reg.bc.byte.lo;
      return;

    case OP_LD_B_D:
      cpu->reg.bc.byte.hi = cpu->reg.de.byte.hi;
      return;

    case OP_LD_B_E:
      cpu->reg.bc.byte.hi = cpu->reg.de.byte.lo;
      return;

    case OP_LD_B_H:
      cpu->reg.bc.byte.hi = cpu->reg.hl.byte.hi;
      return;

    case OP_LD_B_L:
      cpu->reg.bc.byte.hi = cpu->reg.hl.byte.lo;
      return;

    case OP_LD_B_MEM_HL:
      cpu->reg.bc.byte.hi = libyagbe_bus_read_memory(bus, cpu->reg.hl.value);
      return;

    case OP_LD_B_A:
      cpu->reg.bc.byte.hi = cpu->reg.af.byte.hi;
      return;

    case OP_LD_C_B:
      cpu->reg.bc.byte.lo = cpu->reg.bc.byte.hi;
      return;

    case OP_LD_C_C:
      return;

    case OP_LD_C_D:
      cpu->reg.bc.byte.lo = cpu->reg.de.byte.hi;
      return;

    case OP_LD_C_E:
      cpu->reg.bc.byte.lo = cpu->reg.de.byte.lo;
      return;

    case OP_LD_C_H:
      cpu->reg.bc.byte.lo = cpu->reg.hl.byte.hi;
      return;

    case OP_LD_C_L:
      cpu->reg.bc.byte.lo = cpu->reg.hl.byte.lo;
      return;

    case OP_LD_C_MEM_HL:
      cpu->reg.bc.byte.lo = libyagbe_bus_read_memory(bus, cpu->reg.hl.value);
      return;

    case OP_LD_C_A:
      cpu->reg.bc.byte.lo = cpu->reg.af.byte.hi;
      return;

    case OP_LD_D_B:
      cpu->reg.de.byte.hi = cpu->reg.bc.byte.hi;
      return;

    case OP_LD_D_C:
      cpu->reg.de.byte.hi = cpu->reg.bc.byte.lo;
      return;

    case OP_LD_D_D:
      return;

    case OP_LD_D_E:
      cpu->reg.de.byte.hi = cpu->reg.de.byte.lo;
      return;

    case OP_LD_D_H:
      cpu->reg.de.byte.hi = cpu->reg.hl.byte.hi;
      return;

    case OP_LD_D_L:
      cpu->reg.de.byte.hi = cpu->reg.hl.byte.lo;
      return;

    case OP_LD_D_MEM_HL:
      cpu->reg.de.byte.hi = libyagbe_bus_read_memory(bus, cpu->reg.hl.value);
      return;

    case OP_LD_D_A:
      cpu->reg.de.byte.hi = cpu->reg.af.byte.hi;
      return;

    case OP_LD_E_B:
      cpu->reg.de.byte.lo = cpu->reg.bc.byte.hi;
      return;

    case OP_LD_E_C:
      cpu->reg.de.byte.lo = cpu->reg.bc.byte.lo;
      return;

    case OP_LD_E_D:
      cpu->reg.de.byte.lo = cpu->reg.de.byte.hi;
      return;

    case OP_LD_E_E:
      return;

    case OP_LD_E_H:
      cpu->reg.de.byte.lo = cpu->reg.hl.byte.hi;
      return;

    case OP_LD_E_L:
      cpu->reg.de.byte.lo = cpu->reg.hl.byte.lo;
      return;

    case OP_LD_E_MEM_HL:
      cpu->reg.de.byte.lo = libyagbe_bus_read_memory(bus, cpu->reg.hl.value);
      return;

    case OP_LD_E_A:
      cpu->reg.de.byte.lo = cpu->reg.af.byte.hi;
      return;

    case OP_LD_H_B:
      cpu->reg.hl.byte.hi = cpu->reg.bc.byte.hi;
      return;

    case OP_LD_H_C:
      cpu->reg.hl.byte.hi = cpu->reg.bc.byte.lo;
      return;

    case OP_LD_H_D:
      cpu->reg.hl.byte.hi = cpu->reg.de.byte.hi;
      return;

    case OP_LD_H_E:
      cpu->reg.hl.byte.hi = cpu->reg.de.byte.lo;
      return;

    case OP_LD_H_H:
      return;

    case OP_LD_H_L:
      cpu->reg.hl.byte.hi = cpu->reg.hl.byte.lo;
      return;

    case OP_LD_H_MEM_HL:
      cpu->reg.hl.byte.hi = libyagbe_bus_read_memory(bus, cpu->reg.hl.value);
      return;

    case OP_LD_H_A:
      cpu->reg.hl.byte.hi = cpu->reg.af.byte.hi;
      return;

    case OP_LD_L_B:
      cpu->reg.hl.byte.lo = cpu->reg.bc.byte.hi;
      return;

    case OP_LD_L_C:
      cpu->reg.hl.byte.lo = cpu->reg.bc.byte.lo;
      return;

    case OP_LD_L_D:
      cpu->reg.hl.byte.lo = cpu->reg.de.byte.hi;
      return;

    case OP_LD_L_E:
      cpu->reg.hl.byte.lo = cpu->reg.de.byte.lo;
      return;

    case OP_LD_L_H:
      cpu->reg.hl.byte.lo = cpu->reg.hl.byte.hi;
      return;

    case OP_LD_L_L:
      return;

    case OP_LD_L_MEM_HL:
      cpu->reg.hl.byte.lo = libyagbe_bus_read_memory(bus, cpu->reg.hl.value);
      return;

    case OP_LD_L_A:
      cpu->reg.hl.byte.lo = cpu->reg.af.byte.hi;
      return;

    case OP_LD_MEM_HL_B:
      libyagbe_bus_write_memory(bus, cpu->reg.hl.value, cpu->reg.bc.byte.hi);
      return;

    case OP_LD_MEM_HL_C:
      libyagbe_bus_write_memory(bus, cpu->reg.hl.value, cpu->reg.bc.byte.lo);
      return;

    case OP_LD_MEM_HL_D:
      libyagbe_bus_write_memory(bus, cpu->reg.hl.value, cpu->reg.de.byte.hi);
      return;

    case OP_LD_MEM_HL_E:
      libyagbe_bus_write_memory(bus, cpu->reg.hl.value, cpu->reg.de.byte.lo);
      return;

    case OP_LD_MEM_HL_H:
      libyagbe_bus_write_memory(bus, cpu->reg.hl.value, cpu->reg.hl.byte.hi);
      return;

    case OP_LD_MEM_HL_L:
      libyagbe_bus_write_memory(bus, cpu->reg.hl.value, cpu->reg.hl.byte.lo);
      return;

    case OP_LD_MEM_HL_A:
      libyagbe_bus_write_memory(bus, cpu->reg.hl.value, cpu->reg.af.byte.hi);
      return;

    case OP_LD_A_B:
      cpu->reg.af.byte.hi = cpu->reg.bc.byte.hi;
      return;

    case OP_LD_A_C:
      cpu->reg.af.byte.hi = cpu->reg.bc.byte.lo;
      return;

    case OP_LD_A_D:
      cpu->reg.af.byte.hi = cpu->reg.de.byte.hi;
      return;

    case OP_LD_A_E:
      cpu->reg.af.byte.hi = cpu->reg.de.byte.lo;
      return;

    case OP_LD_A_H:
      cpu->reg.af.byte.hi = cpu->reg.hl.byte.hi;
      return;

    case OP_LD_A_L:
      cpu->reg.af.byte.hi = cpu->reg.hl.byte.lo;
      return;

    case OP_LD_A_MEM_HL:
      cpu->reg.af.byte.hi = libyagbe_bus_read_memory(bus, cpu->reg.hl.value);
      return;

    case OP_LD_A_A:
      return;

    case OP_ADD_A_B:
      alu_add(cpu, cpu->reg.bc.byte.hi, ALU_NORMAL);
      return;

    case OP_ADD_A_C:
      alu_add(cpu, cpu->reg.bc.byte.lo, ALU_NORMAL);
      return;

    case OP_ADD_A_D:
      alu_add(cpu, cpu->reg.de.byte.hi, ALU_NORMAL);
      return;

    case OP_ADD_A_E:
      alu_add(cpu, cpu->reg.de.byte.lo, ALU_NORMAL);
      return;

    case OP_ADD_A_H:
      alu_add(cpu, cpu->reg.hl.byte.hi, ALU_NORMAL);
      return;

    case OP_ADD_A_L:
      alu_add(cpu, cpu->reg.hl.byte.lo, ALU_NORMAL);
      return;

    case OP_ADD_A_MEM_HL: {
      const uint8_t data = libyagbe_bus_read_memory(bus, cpu->reg.hl.value);

      alu_add(cpu, data, ALU_NORMAL);
      return;
    }

    case OP_ADD_A_A:
      alu_add(cpu, cpu->reg.af.byte.hi, ALU_NORMAL);
      return;

    case OP_ADC_A_B:
      alu_add(cpu, cpu->reg.bc.byte.hi, ALU_WITH_CARRY);
      return;

    case OP_ADC_A_C:
      alu_add(cpu, cpu->reg.bc.byte.lo, ALU_WITH_CARRY);
      return;

    case OP_ADC_A_D:
      alu_add(cpu, cpu->reg.de.byte.hi, ALU_WITH_CARRY);
      return;

    case OP_ADC_A_E:
      alu_add(cpu, cpu->reg.de.byte.lo, ALU_WITH_CARRY);
      return;

    case OP_ADC_A_H:
      alu_add(cpu, cpu->reg.hl.byte.hi, ALU_WITH_CARRY);
      return;

    case OP_ADC_A_L:
      alu_add(cpu, cpu->reg.hl.byte.lo, ALU_WITH_CARRY);
      return;

    case OP_ADC_A_MEM_HL: {
      const uint8_t data = libyagbe_bus_read_memory(bus, cpu->reg.hl.value);

      alu_add(cpu, data, ALU_WITH_CARRY);
      return;
    }

    case OP_ADC_A_A:
      alu_add(cpu, cpu->reg.af.byte.hi, ALU_WITH_CARRY);
      return;

    case OP_SUB_B:
      alu_sub(cpu, cpu->reg.bc.byte.hi, ALU_NORMAL);
      return;

    case OP_SUB_C:
      alu_sub(cpu, cpu->reg.bc.byte.lo, ALU_NORMAL);
      return;

    case OP_SUB_D:
      alu_sub(cpu, cpu->reg.de.byte.hi, ALU_NORMAL);
      return;

    case OP_SUB_E:
      alu_sub(cpu, cpu->reg.de.byte.lo, ALU_NORMAL);
      return;

    case OP_SUB_H:
      alu_sub(cpu, cpu->reg.hl.byte.hi, ALU_NORMAL);
      return;

    case OP_SUB_L:
      alu_sub(cpu, cpu->reg.hl.byte.lo, ALU_NORMAL);
      return;

    case OP_SUB_MEM_HL: {
      const uint8_t data = libyagbe_bus_read_memory(bus, cpu->reg.hl.value);

      alu_sub(cpu, data, ALU_NORMAL);
      return;
    }

    case OP_SUB_A:
      alu_sub(cpu, cpu->reg.af.byte.hi, ALU_NORMAL);
      return;

    case OP_SBC_A_B:
      alu_sub(cpu, cpu->reg.bc.byte.hi, ALU_WITH_CARRY);
      return;

    case OP_SBC_A_C:
      alu_sub(cpu, cpu->reg.bc.byte.lo, ALU_WITH_CARRY);
      return;

    case OP_SBC_A_D:
      alu_sub(cpu, cpu->reg.de.byte.hi, ALU_WITH_CARRY);
      return;

    case OP_SBC_A_E:
      alu_sub(cpu, cpu->reg.de.byte.lo, ALU_WITH_CARRY);
      return;

    case OP_SBC_A_H:
      alu_sub(cpu, cpu->reg.hl.byte.hi, ALU_WITH_CARRY);
      return;

    case OP_SBC_A_L:
      alu_sub(cpu, cpu->reg.hl.byte.lo, ALU_WITH_CARRY);
      return;

    case OP_SBC_A_MEM_HL: {
      const uint8_t data = libyagbe_bus_read_memory(bus, cpu->reg.hl.value);

      alu_sub(cpu, data, ALU_WITH_CARRY);
      return;
    }

    case OP_SBC_A_A:
      alu_sub(cpu, cpu->reg.af.byte.hi, ALU_WITH_CARRY);
      return;

    case OP_AND_B:
      cpu->reg.af.byte.hi &= cpu->reg.bc.byte.hi;
      cpu->reg.af.byte.lo = (cpu->reg.af.byte.hi == 0) ? 0xA0 : 0x20;

      return;

    case OP_AND_C:
      cpu->reg.af.byte.hi &= cpu->reg.bc.byte.lo;
      cpu->reg.af.byte.lo = (cpu->reg.af.byte.hi == 0) ? 0xA0 : 0x20;

      return;

    case OP_AND_D:
      cpu->reg.af.byte.hi &= cpu->reg.de.byte.hi;
      cpu->reg.af.byte.lo = (cpu->reg.af.byte.hi == 0) ? 0xA0 : 0x20;

      return;

    case OP_AND_E:
      cpu->reg.af.byte.hi &= cpu->reg.de.byte.lo;
      cpu->reg.af.byte.lo = (cpu->reg.af.byte.hi == 0) ? 0xA0 : 0x20;

      return;

    case OP_AND_H:
      cpu->reg.af.byte.hi &= cpu->reg.hl.byte.hi;
      cpu->reg.af.byte.lo = (cpu->reg.af.byte.hi == 0) ? 0xA0 : 0x20;

      return;

    case OP_AND_L:
      cpu->reg.af.byte.hi &= cpu->reg.hl.byte.lo;
      cpu->reg.af.byte.lo = (cpu->reg.af.byte.hi == 0) ? 0xA0 : 0x20;

      return;

    case OP_AND_MEM_HL: {
      const uint8_t data = libyagbe_bus_read_memory(bus, cpu->reg.hl.value);

      cpu->reg.af.byte.hi &= data;
      cpu->reg.af.byte.lo = (cpu->reg.af.byte.hi == 0) ? 0xA0 : 0x20;

      return;
    }

    case OP_AND_A:
      cpu->reg.af.byte.lo = (cpu->reg.af.byte.hi == 0) ? 0xA0 : 0x20;
      return;

    case OP_XOR_B:
      cpu->reg.af.byte.hi ^= cpu->reg.bc.byte.hi;
      cpu->reg.af.byte.lo = (cpu->reg.af.byte.hi == 0) ? 0x80 : 0x00;

      return;

    case OP_XOR_C:
      cpu->reg.af.byte.hi ^= cpu->reg.bc.byte.lo;
      cpu->reg.af.byte.lo = (cpu->reg.af.byte.hi == 0) ? 0x80 : 0x00;

      return;

    case OP_XOR_D:
      cpu->reg.af.byte.hi ^= cpu->reg.de.byte.hi;
      cpu->reg.af.byte.lo = (cpu->reg.af.byte.hi == 0) ? 0x80 : 0x00;

      return;

    case OP_XOR_E:
      cpu->reg.af.byte.hi ^= cpu->reg.de.byte.lo;
      cpu->reg.af.byte.lo = (cpu->reg.af.byte.hi == 0) ? 0x80 : 0x00;

      return;

    case OP_XOR_H:
      cpu->reg.af.byte.hi ^= cpu->reg.hl.byte.hi;
      cpu->reg.af.byte.lo = (cpu->reg.af.byte.hi == 0) ? 0x80 : 0x00;

      return;

    case OP_XOR_L:
      cpu->reg.af.byte.hi ^= cpu->reg.hl.byte.lo;
      cpu->reg.af.byte.lo = (cpu->reg.af.byte.hi == 0) ? 0x80 : 0x00;

      return;

    case OP_XOR_MEM_HL: {
      const uint8_t data = libyagbe_bus_read_memory(bus, cpu->reg.hl.value);

      cpu->reg.af.byte.hi ^= data;
      cpu->reg.af.byte.lo = (cpu->reg.af.byte.hi == 0) ? 0x80 : 0x00;

      return;
    }

    case OP_XOR_A:
      cpu->reg.af.byte.hi ^= cpu->reg.af.byte.hi;
      cpu->reg.af.byte.lo = 0x80;

      return;

    case OP_OR_B:
      cpu->reg.af.byte.hi |= cpu->reg.bc.byte.hi;
      cpu->reg.af.byte.lo = (cpu->reg.af.byte.hi == 0) ? 0x80 : 0x00;

      return;

    case OP_OR_C:
      cpu->reg.af.byte.hi |= cpu->reg.bc.byte.lo;
      cpu->reg.af.byte.lo = (cpu->reg.af.byte.hi == 0) ? 0x80 : 0x00;

      return;

    case OP_OR_D:
      cpu->reg.af.byte.hi |= cpu->reg.de.byte.hi;
      cpu->reg.af.byte.lo = (cpu->reg.af.byte.hi == 0) ? 0x80 : 0x00;

      return;

    case OP_OR_E:
      cpu->reg.af.byte.hi |= cpu->reg.de.byte.lo;
      cpu->reg.af.byte.lo = (cpu->reg.af.byte.hi == 0) ? 0x80 : 0x00;

      return;

    case OP_OR_H:
      cpu->reg.af.byte.hi |= cpu->reg.hl.byte.hi;
      cpu->reg.af.byte.lo = (cpu->reg.af.byte.hi == 0) ? 0x80 : 0x00;

      return;

    case OP_OR_L:
      cpu->reg.af.byte.hi |= cpu->reg.hl.byte.lo;
      cpu->reg.af.byte.lo = (cpu->reg.af.byte.hi == 0) ? 0x80 : 0x00;

      return;

    case OP_OR_MEM_HL: {
      const uint8_t data = libyagbe_bus_read_memory(bus, cpu->reg.hl.value);

      cpu->reg.af.byte.hi |= data;
      cpu->reg.af.byte.lo = (cpu->reg.af.byte.hi == 0) ? 0x80 : 0x00;

      return;
    }

    case OP_OR_A:
      cpu->reg.af.byte.lo = (cpu->reg.af.byte.hi == 0) ? 0x80 : 0x00;
      return;

    case OP_CP_B:
      alu_sub(cpu, cpu->reg.bc.byte.hi, ALU_DISCARD_RESULT);
      return;

    case OP_CP_C:
      alu_sub(cpu, cpu->reg.bc.byte.lo, ALU_DISCARD_RESULT);
      return;

    case OP_CP_D:
      alu_sub(cpu, cpu->reg.de.byte.hi, ALU_DISCARD_RESULT);
      return;

    case OP_CP_E:
      alu_sub(cpu, cpu->reg.de.byte.lo, ALU_DISCARD_RESULT);
      return;

    case OP_CP_H:
      alu_sub(cpu, cpu->reg.hl.byte.hi, ALU_DISCARD_RESULT);
      return;

    case OP_CP_L:
      alu_sub(cpu, cpu->reg.hl.byte.lo, ALU_DISCARD_RESULT);
      return;

    case OP_CP_MEM_HL: {
      const uint8_t data = libyagbe_bus_read_memory(bus, cpu->reg.hl.value);

      alu_sub(cpu, data, ALU_DISCARD_RESULT);
      return;
    }

    case OP_CP_A:
      alu_sub(cpu, cpu->reg.af.byte.hi, ALU_DISCARD_RESULT);
      return;

    case OP_RET_NZ:
      ret_if(cpu, bus, !(cpu->reg.af.byte.lo & FLAG_Z));
      return;

    case OP_POP_BC:
      cpu->reg.bc.value = stack_pop(cpu, bus);
      return;

    case OP_JP_NZ_IMM16:
      jp_if(cpu, bus, !(cpu->reg.af.byte.lo & FLAG_Z));
      return;

    case OP_JP_IMM16:
      jp_if(cpu, bus, true);
      return;

    case OP_CALL_NZ_IMM16:
      call_if(cpu, bus, !(cpu->reg.af.byte.lo & FLAG_Z));
      return;

    case OP_PUSH_BC:
      stack_push(cpu, bus, cpu->reg.bc.byte.hi, cpu->reg.bc.byte.lo);
      return;

    case OP_ADD_A_IMM8: {
      const uint8_t imm8 = read_imm8(cpu, bus);

      alu_add(cpu, imm8, ALU_NORMAL);
      return;
    }

    case OP_RST_00:
      rst(cpu, bus, 0x0000);
      return;

    case OP_RET_Z:
      ret_if(cpu, bus, (cpu->reg.af.byte.lo & FLAG_Z) != 0);
      return;

    case OP_RET:
      ret_if(cpu, bus, true);
      return;

    case OP_JP_Z_IMM16:
      jp_if(cpu, bus, (cpu->reg.af.byte.lo & FLAG_Z) != 0);
      return;

    case OP_PREFIX_CB: {
      const uint8_t cb_instruction = read_imm8(cpu, bus);

      switch (cb_instruction) {
        case OP_RLC_B:
          cpu->reg.bc.byte.hi = alu_rlc(cpu, cpu->reg.bc.byte.hi, ALU_NORMAL);
          return;

        case OP_RLC_C:
          cpu->reg.bc.byte.lo = alu_rlc(cpu, cpu->reg.bc.byte.lo, ALU_NORMAL);
          return;

        case OP_RLC_D:
          cpu->reg.de.byte.hi = alu_rlc(cpu, cpu->reg.de.byte.hi, ALU_NORMAL);
          return;

        case OP_RLC_E:
          cpu->reg.de.byte.lo = alu_rlc(cpu, cpu->reg.de.byte.lo, ALU_NORMAL);
          return;

        case OP_RLC_H:
          cpu->reg.hl.byte.hi = alu_rlc(cpu, cpu->reg.hl.byte.hi, ALU_NORMAL);
          return;

        case OP_RLC_L:
          cpu->reg.hl.byte.lo = alu_rlc(cpu, cpu->reg.hl.byte.lo, ALU_NORMAL);
          return;

        case OP_RLC_HL: {
          uint8_t data = libyagbe_bus_read_memory(bus, cpu->reg.hl.value);
          data = alu_rlc(cpu, data, ALU_NORMAL);
          libyagbe_bus_write_memory(bus, cpu->reg.hl.value, data);

          return;
        }

        case OP_RLC_A:
          cpu->reg.af.byte.hi =
              alu_rlc(cpu, cpu->reg.af.byte.hi, ALU_NORMAL);
          return;

        case OP_RRC_B:
          cpu->reg.bc.byte.hi = alu_rrc(cpu, cpu->reg.bc.byte.hi, ALU_NORMAL);
          return;

        case OP_RRC_C:
          cpu->reg.bc.byte.lo = alu_rrc(cpu, cpu->reg.bc.byte.lo, ALU_NORMAL);
          return;

        case OP_RRC_D:
          cpu->reg.de.byte.hi = alu_rrc(cpu, cpu->reg.de.byte.hi, ALU_NORMAL);
          return;

        case OP_RRC_E:
          cpu->reg.de.byte.lo = alu_rrc(cpu, cpu->reg.de.byte.lo, ALU_NORMAL);
          return;

        case OP_RRC_H:
          cpu->reg.hl.byte.hi = alu_rrc(cpu, cpu->reg.hl.byte.hi, ALU_NORMAL);
          return;

        case OP_RRC_L:
          cpu->reg.hl.byte.lo = alu_rrc(cpu, cpu->reg.hl.byte.lo, ALU_NORMAL);
          return;

        case OP_RRC_HL: {
          uint8_t data = libyagbe_bus_read_memory(bus, cpu->reg.hl.value);
          data = alu_rrc(cpu, data, ALU_NORMAL);
          libyagbe_bus_write_memory(bus, cpu->reg.hl.value, data);

          return;
        }

        case OP_RRC_A:
          cpu->reg.af.byte.hi = alu_rrc(cpu, cpu->reg.af.byte.hi, ALU_NORMAL);
          return;

        case OP_RL_B:
          cpu->reg.bc.byte.hi = alu_rl(cpu, cpu->reg.bc.byte.hi, ALU_NORMAL);
          return;

        case OP_RL_C:
          cpu->reg.bc.byte.lo = alu_rl(cpu, cpu->reg.bc.byte.lo, ALU_NORMAL);
          return;

        case OP_RL_D:
          cpu->reg.de.byte.hi = alu_rl(cpu, cpu->reg.de.byte.hi, ALU_NORMAL);
          return;

        case OP_RL_E:
          cpu->reg.de.byte.lo = alu_rl(cpu, cpu->reg.de.byte.lo, ALU_NORMAL);
          return;

        case OP_RL_H:
          cpu->reg.hl.byte.hi = alu_rl(cpu, cpu->reg.hl.byte.hi, ALU_NORMAL);
          return;

        case OP_RL_L:
          cpu->reg.hl.byte.lo = alu_rl(cpu, cpu->reg.hl.byte.lo, ALU_NORMAL);
          return;

        case OP_RL_HL: {
          uint8_t data = libyagbe_bus_read_memory(bus, cpu->reg.hl.value);
          data = alu_rl(cpu, data, ALU_NORMAL);
          libyagbe_bus_write_memory(bus, cpu->reg.hl.value, data);

          return;
        }

        case OP_RL_A:
          cpu->reg.af.byte.hi = alu_rl(cpu, cpu->reg.af.byte.hi, ALU_NORMAL);
          return;

        case OP_RR_B:
          cpu->reg.bc.byte.hi = alu_rr(cpu, cpu->reg.bc.byte.hi, ALU_NORMAL);
          return;

        case OP_RR_C:
          cpu->reg.bc.byte.lo = alu_rr(cpu, cpu->reg.bc.byte.lo, ALU_NORMAL);
          return;

        case OP_RR_D:
          cpu->reg.de.byte.hi = alu_rr(cpu, cpu->reg.de.byte.hi, ALU_NORMAL);
          return;

        case OP_RR_E:
          cpu->reg.de.byte.lo = alu_rr(cpu, cpu->reg.de.byte.lo, ALU_NORMAL);
          return;

        case OP_RR_H:
          cpu->reg.hl.byte.hi = alu_rr(cpu, cpu->reg.hl.byte.hi, ALU_NORMAL);
          return;

        case OP_RR_L:
          cpu->reg.hl.byte.lo = alu_rr(cpu, cpu->reg.hl.byte.lo, ALU_NORMAL);
          return;

        case OP_RR_HL: {
          uint8_t data = libyagbe_bus_read_memory(bus, cpu->reg.hl.value);
          data = alu_rr(cpu, data, ALU_NORMAL);
          libyagbe_bus_write_memory(bus, cpu->reg.hl.value, data);

          return;
        }

        case OP_RR_A:
          cpu->reg.af.byte.hi = alu_rr(cpu, cpu->reg.af.byte.hi, ALU_NORMAL);
          return;

        case OP_SLA_B:
          cpu->reg.bc.byte.hi = alu_sla(cpu, cpu->reg.bc.byte.hi);
          return;

        case OP_SLA_C:
          cpu->reg.bc.byte.lo = alu_sla(cpu, cpu->reg.bc.byte.lo);
          return;

        case OP_SLA_D:
          cpu->reg.de.byte.hi = alu_sla(cpu, cpu->reg.de.byte.hi);
          return;

        case OP_SLA_E:
          cpu->reg.de.byte.lo = alu_sla(cpu, cpu->reg.de.byte.lo);
          return;

        case OP_SLA_H:
          cpu->reg.hl.byte.hi = alu_sla(cpu, cpu->reg.hl.byte.hi);
          return;

        case OP_SLA_L:
          cpu->reg.hl.byte.lo = alu_sla(cpu, cpu->reg.hl.byte.lo);
          return;

        case OP_SLA_HL: {
          uint8_t data = libyagbe_bus_read_memory(bus, cpu->reg.hl.value);
          data = alu_sla(cpu, data);
          libyagbe_bus_write_memory(bus, cpu->reg.hl.value, data);

          return;
        }

        case OP_SLA_A:
          cpu->reg.af.byte.hi = alu_sla(cpu, cpu->reg.af.byte.hi);
          return;

        case OP_SRA_B:
          cpu->reg.bc.byte.hi = alu_sra(cpu, cpu->reg.bc.byte.hi);
          return;

        case OP_SRA_C:
          cpu->reg.bc.byte.lo = alu_sra(cpu, cpu->reg.bc.byte.lo);
          return;

        case OP_SRA_D:
          cpu->reg.de.byte.hi = alu_sra(cpu, cpu->reg.de.byte.hi);
          return;

        case OP_SRA_E:
          cpu->reg.de.byte.lo = alu_sra(cpu, cpu->reg.de.byte.lo);
          return;

        case OP_SRA_H:
          cpu->reg.hl.byte.hi = alu_sra(cpu, cpu->reg.hl.byte.hi);
          return;

        case OP_SRA_L:
          cpu->reg.hl.byte.lo = alu_sra(cpu, cpu->reg.hl.byte.lo);
          return;

        case OP_SRA_HL: {
          uint8_t data = libyagbe_bus_read_memory(bus, cpu->reg.hl.value);
          data = alu_sra(cpu, data);
          libyagbe_bus_write_memory(bus, cpu->reg.hl.value, data);

          return;
        }

        case OP_SRA_A:
          cpu->reg.af.byte.hi = alu_sra(cpu, cpu->reg.af.byte.hi);
          return;

        case OP_SWAP_B:
          cpu->reg.bc.byte.hi = (uint8_t)((cpu->reg.bc.byte.hi & 0x0F) << 4) |
                                (cpu->reg.bc.byte.hi >> 4);
          cpu->reg.af.byte.lo = (cpu->reg.bc.byte.hi == 0) ? 0x80 : 0x00;

          return;

        case OP_SWAP_C:
          cpu->reg.bc.byte.lo = (uint8_t)((cpu->reg.bc.byte.lo & 0x0F) << 4) |
                                (cpu->reg.bc.byte.lo >> 4);
          cpu->reg.af.byte.lo = (cpu->reg.bc.byte.lo == 0) ? 0x80 : 0x00;

          return;

        case OP_SWAP_D:
          cpu->reg.de.byte.hi = (uint8_t)((cpu->reg.de.byte.hi & 0x0F) << 4) |
                                (cpu->reg.de.byte.hi >> 4);
          cpu->reg.af.byte.lo = (cpu->reg.de.byte.hi == 0) ? 0x80 : 0x00;

          return;

        case OP_SWAP_E:
          cpu->reg.de.byte.lo = (uint8_t)((cpu->reg.de.byte.lo & 0x0F) << 4) |
                                (cpu->reg.de.byte.lo >> 4);
          cpu->reg.af.byte.lo = (cpu->reg.de.byte.lo == 0) ? 0x80 : 0x00;

          return;

        case OP_SWAP_H:
          cpu->reg.hl.byte.hi = (uint8_t)((cpu->reg.hl.byte.hi & 0x0F) << 4) |
                                (cpu->reg.hl.byte.hi >> 4);
          cpu->reg.af.byte.lo = (cpu->reg.hl.byte.hi == 0) ? 0x80 : 0x00;

          return;

        case OP_SWAP_L:
          cpu->reg.hl.byte.lo = (uint8_t)((cpu->reg.hl.byte.lo & 0x0F) << 4) |
                                (cpu->reg.hl.byte.lo >> 4);
          cpu->reg.af.byte.lo = (cpu->reg.hl.byte.lo == 0) ? 0x80 : 0x00;

          return;

        case OP_SWAP_HL: {
          uint8_t data = libyagbe_bus_read_memory(bus, cpu->reg.hl.value);

          data = (uint8_t)((data & 0x0F) << 4) | (data >> 4);
          cpu->reg.af.byte.lo = (data == 0) ? 0x80 : 0x00;

          libyagbe_bus_write_memory(bus, cpu->reg.hl.value, data);
          return;
        }

        case OP_SWAP_A:
          cpu->reg.af.byte.hi = (uint8_t)((cpu->reg.af.byte.hi & 0x0F) << 4) |
                                (cpu->reg.af.byte.hi >> 4);
          cpu->reg.af.byte.lo = (cpu->reg.af.byte.hi == 0) ? 0x80 : 0x00;

          return;

        case OP_SRL_B:
          cpu->reg.bc.byte.hi = alu_srl(cpu, cpu->reg.bc.byte.hi);
          return;

        case OP_SRL_C:
          cpu->reg.bc.byte.lo = alu_srl(cpu, cpu->reg.bc.byte.lo);
          return;

        case OP_SRL_D:
          cpu->reg.de.byte.hi = alu_srl(cpu, cpu->reg.de.byte.hi);
          return;

        case OP_SRL_E:
          cpu->reg.de.byte.lo = alu_srl(cpu, cpu->reg.de.byte.lo);
          return;

        case OP_SRL_H:
          cpu->reg.hl.byte.hi = alu_srl(cpu, cpu->reg.hl.byte.hi);
          return;

        case OP_SRL_L:
          cpu->reg.hl.byte.lo = alu_srl(cpu, cpu->reg.hl.byte.lo);
          return;

        case OP_SRL_HL: {
          uint8_t data = libyagbe_bus_read_memory(bus, cpu->reg.hl.value);
          data = alu_srl(cpu, data);
          libyagbe_bus_write_memory(bus, cpu->reg.hl.value, data);

          return;
        }

        case OP_SRL_A:
          cpu->reg.af.byte.hi = alu_srl(cpu, cpu->reg.af.byte.hi);
          return;

        case OP_BIT_0_B:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 0), cpu->reg.bc.byte.hi);
          return;

        case OP_BIT_0_C:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 0), cpu->reg.bc.byte.lo);
          return;

        case OP_BIT_0_D:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 0), cpu->reg.de.byte.hi);
          return;

        case OP_BIT_0_E:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 0), cpu->reg.de.byte.lo);
          return;

        case OP_BIT_0_H:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 0), cpu->reg.hl.byte.hi);
          return;

        case OP_BIT_0_L:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 0), cpu->reg.hl.byte.lo);
          return;

        case OP_BIT_0_HL: {
          const uint8_t data = libyagbe_bus_read_memory(bus, cpu->reg.hl.value);
          cpu->reg.af.byte.lo = alu_bit(cpu->reg.af.byte.lo, (1 << 0), data);

          return;
        }

        case OP_BIT_0_A:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 0), cpu->reg.af.byte.hi);
          return;

        case OP_BIT_1_B:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 1), cpu->reg.bc.byte.hi);
          return;

        case OP_BIT_1_C:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 1), cpu->reg.bc.byte.lo);
          return;

        case OP_BIT_1_D:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 1), cpu->reg.de.byte.hi);
          return;

        case OP_BIT_1_E:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 1), cpu->reg.de.byte.lo);
          return;

        case OP_BIT_1_H:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 1), cpu->reg.hl.byte.hi);
          return;

        case OP_BIT_1_L:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 1), cpu->reg.hl.byte.lo);
          return;

        case OP_BIT_1_HL: {
          const uint8_t data = libyagbe_bus_read_memory(bus, cpu->reg.hl.value);
          cpu->reg.af.byte.lo = alu_bit(cpu->reg.af.byte.lo, (1 << 1), data);

          return;
        }

        case OP_BIT_1_A:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 1), cpu->reg.af.byte.hi);
          return;

        case OP_BIT_2_B:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 2), cpu->reg.bc.byte.hi);
          return;

        case OP_BIT_2_C:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 2), cpu->reg.bc.byte.lo);
          return;

        case OP_BIT_2_D:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 2), cpu->reg.de.byte.hi);
          return;

        case OP_BIT_2_E:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 2), cpu->reg.de.byte.lo);
          return;

        case OP_BIT_2_H:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 2), cpu->reg.hl.byte.hi);
          return;

        case OP_BIT_2_L:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 2), cpu->reg.hl.byte.lo);
          return;

        case OP_BIT_2_HL: {
          const uint8_t data = libyagbe_bus_read_memory(bus, cpu->reg.hl.value);
          cpu->reg.af.byte.lo = alu_bit(cpu->reg.af.byte.lo, (1 << 2), data);

          return;
        }

        case OP_BIT_2_A:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 2), cpu->reg.af.byte.hi);
          return;

        case OP_BIT_3_B:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 3), cpu->reg.bc.byte.hi);
          return;

        case OP_BIT_3_C:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 3), cpu->reg.bc.byte.lo);
          return;

        case OP_BIT_3_D:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 3), cpu->reg.de.byte.hi);
          return;

        case OP_BIT_3_E:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 3), cpu->reg.de.byte.lo);
          return;

        case OP_BIT_3_H:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 3), cpu->reg.hl.byte.hi);
          return;

        case OP_BIT_3_L:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 3), cpu->reg.hl.byte.lo);
          return;

        case OP_BIT_3_HL: {
          const uint8_t data = libyagbe_bus_read_memory(bus, cpu->reg.hl.value);
          cpu->reg.af.byte.lo = alu_bit(cpu->reg.af.byte.lo, (1 << 3), data);

          return;
        }

        case OP_BIT_3_A:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 3), cpu->reg.af.byte.hi);
          return;

        case OP_BIT_4_B:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 4), cpu->reg.bc.byte.hi);
          return;

        case OP_BIT_4_C:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 4), cpu->reg.bc.byte.lo);
          return;

        case OP_BIT_4_D:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 4), cpu->reg.de.byte.hi);
          return;

        case OP_BIT_4_E:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 4), cpu->reg.de.byte.lo);
          return;

        case OP_BIT_4_H:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 4), cpu->reg.hl.byte.hi);
          return;

        case OP_BIT_4_L:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 4), cpu->reg.hl.byte.lo);
          return;

        case OP_BIT_4_HL: {
          const uint8_t data = libyagbe_bus_read_memory(bus, cpu->reg.hl.value);
          cpu->reg.af.byte.lo = alu_bit(cpu->reg.af.byte.lo, (1 << 4), data);

          return;
        }

        case OP_BIT_4_A:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 4), cpu->reg.af.byte.hi);
          return;

        case OP_BIT_5_B:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 5), cpu->reg.bc.byte.hi);
          return;

        case OP_BIT_5_C:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 5), cpu->reg.bc.byte.lo);
          return;

        case OP_BIT_5_D:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 5), cpu->reg.de.byte.hi);
          return;

        case OP_BIT_5_E:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 5), cpu->reg.de.byte.lo);
          return;

        case OP_BIT_5_H:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 5), cpu->reg.hl.byte.hi);
          return;

        case OP_BIT_5_L:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 5), cpu->reg.hl.byte.lo);
          return;

        case OP_BIT_5_HL: {
          const uint8_t data = libyagbe_bus_read_memory(bus, cpu->reg.hl.value);
          cpu->reg.af.byte.lo = alu_bit(cpu->reg.af.byte.lo, (1 << 5), data);

          return;
        }

        case OP_BIT_5_A:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 5), cpu->reg.af.byte.hi);
          return;

        case OP_BIT_6_B:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 6), cpu->reg.bc.byte.hi);
          return;

        case OP_BIT_6_C:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 6), cpu->reg.bc.byte.lo);
          return;

        case OP_BIT_6_D:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 6), cpu->reg.de.byte.hi);
          return;

        case OP_BIT_6_E:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 6), cpu->reg.de.byte.lo);
          return;

        case OP_BIT_6_H:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 6), cpu->reg.hl.byte.hi);
          return;

        case OP_BIT_6_L:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 6), cpu->reg.hl.byte.lo);
          return;

        case OP_BIT_6_HL: {
          const uint8_t data = libyagbe_bus_read_memory(bus, cpu->reg.hl.value);
          cpu->reg.af.byte.lo = alu_bit(cpu->reg.af.byte.lo, (1 << 6), data);

          return;
        }

        case OP_BIT_6_A:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 6), cpu->reg.af.byte.hi);
          return;

        case OP_BIT_7_B:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 7), cpu->reg.bc.byte.hi);
          return;

        case OP_BIT_7_C:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 7), cpu->reg.bc.byte.lo);
          return;

        case OP_BIT_7_D:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 7), cpu->reg.de.byte.hi);
          return;

        case OP_BIT_7_E:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 7), cpu->reg.de.byte.lo);
          return;

        case OP_BIT_7_H:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 7), cpu->reg.hl.byte.hi);
          return;

        case OP_BIT_7_L:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 7), cpu->reg.hl.byte.lo);
          return;

        case OP_BIT_7_HL: {
          const uint8_t data = libyagbe_bus_read_memory(bus, cpu->reg.hl.value);
          cpu->reg.af.byte.lo = alu_bit(cpu->reg.af.byte.lo, (1 << 7), data);

          return;
        }

        case OP_BIT_7_A:
          cpu->reg.af.byte.lo =
              alu_bit(cpu->reg.af.byte.lo, (1 << 7), cpu->reg.af.byte.hi);
          return;

        case OP_RES_0_B:
          cpu->reg.bc.byte.hi &= ~(1 << 0);
          return;

        case OP_RES_0_C:
          cpu->reg.bc.byte.lo &= ~(1 << 0);
          return;

        case OP_RES_0_D:
          cpu->reg.de.byte.hi &= ~(1 << 0);
          return;

        case OP_RES_0_E:
          cpu->reg.de.byte.lo &= ~(1 << 0);
          return;

        case OP_RES_0_H:
          cpu->reg.hl.byte.hi &= ~(1 << 0);
          return;

        case OP_RES_0_L:
          cpu->reg.hl.byte.lo &= ~(1 << 0);
          return;

        case OP_RES_0_HL: {
          uint8_t data = libyagbe_bus_read_memory(bus, cpu->reg.hl.value);
          data &= ~(1 << 0);
          libyagbe_bus_write_memory(bus, cpu->reg.hl.value, data);

          return;
        }

        case OP_RES_0_A:
          cpu->reg.af.byte.hi &= ~(1 << 0);
          return;

        case OP_RES_1_B:
          cpu->reg.bc.byte.hi &= ~(1 << 1);
          return;

        case OP_RES_1_C:
          cpu->reg.bc.byte.lo &= ~(1 << 1);
          return;

        case OP_RES_1_D:
          cpu->reg.de.byte.hi &= ~(1 << 1);
          return;

        case OP_RES_1_E:
          cpu->reg.de.byte.lo &= ~(1 << 1);
          return;

        case OP_RES_1_H:
          cpu->reg.hl.byte.hi &= ~(1 << 1);
          return;

        case OP_RES_1_L:
          cpu->reg.hl.byte.lo &= ~(1 << 1);
          return;

        case OP_RES_1_HL: {
          uint8_t data = libyagbe_bus_read_memory(bus, cpu->reg.hl.value);
          data &= ~(1 << 1);
          libyagbe_bus_write_memory(bus, cpu->reg.hl.value, data);

          return;
        }

        case OP_RES_1_A:
          cpu->reg.af.byte.hi &= ~(1 << 1);
          return;

        case OP_RES_2_B:
          cpu->reg.bc.byte.hi &= ~(1 << 2);
          return;

        case OP_RES_2_C:
          cpu->reg.bc.byte.lo &= ~(1 << 2);
          return;

        case OP_RES_2_D:
          cpu->reg.de.byte.hi &= ~(1 << 2);
          return;

        case OP_RES_2_E:
          cpu->reg.de.byte.lo &= ~(1 << 2);
          return;

        case OP_RES_2_H:
          cpu->reg.hl.byte.hi &= ~(1 << 2);
          return;

        case OP_RES_2_L:
          cpu->reg.hl.byte.lo &= ~(1 << 2);
          return;

        case OP_RES_2_HL: {
          uint8_t data = libyagbe_bus_read_memory(bus, cpu->reg.hl.value);
          data &= ~(1 << 2);
          libyagbe_bus_write_memory(bus, cpu->reg.hl.value, data);

          return;
        }

        case OP_RES_2_A:
          cpu->reg.af.byte.hi &= ~(1 << 2);
          return;

        case OP_RES_3_B:
          cpu->reg.bc.byte.hi &= ~(1 << 3);
          return;

        case OP_RES_3_C:
          cpu->reg.bc.byte.lo &= ~(1 << 3);
          return;

        case OP_RES_3_D:
          cpu->reg.de.byte.hi &= ~(1 << 3);
          return;

        case OP_RES_3_E:
          cpu->reg.de.byte.lo &= ~(1 << 3);
          return;

        case OP_RES_3_H:
          cpu->reg.hl.byte.hi &= ~(1 << 3);
          return;

        case OP_RES_3_L:
          cpu->reg.hl.byte.lo &= ~(1 << 3);
          return;

        case OP_RES_3_HL: {
          uint8_t data = libyagbe_bus_read_memory(bus, cpu->reg.hl.value);
          data &= ~(1 << 3);
          libyagbe_bus_write_memory(bus, cpu->reg.hl.value, data);

          return;
        }

        case OP_RES_3_A:
          cpu->reg.af.byte.hi &= ~(1 << 3);
          return;

        case OP_RES_4_B:
          cpu->reg.bc.byte.hi &= ~(1 << 4);
          return;

        case OP_RES_4_C:
          cpu->reg.bc.byte.lo &= ~(1 << 4);
          return;

        case OP_RES_4_D:
          cpu->reg.de.byte.hi &= ~(1 << 4);
          return;

        case OP_RES_4_E:
          cpu->reg.de.byte.lo &= ~(1 << 4);
          return;

        case OP_RES_4_H:
          cpu->reg.hl.byte.hi &= ~(1 << 4);
          return;

        case OP_RES_4_L:
          cpu->reg.hl.byte.lo &= ~(1 << 4);
          return;

        case OP_RES_4_HL: {
          uint8_t data = libyagbe_bus_read_memory(bus, cpu->reg.hl.value);
          data &= ~(1 << 4);
          libyagbe_bus_write_memory(bus, cpu->reg.hl.value, data);

          return;
        }

        case OP_RES_4_A:
          cpu->reg.af.byte.hi &= ~(1 << 4);
          return;

        case OP_RES_5_B:
          cpu->reg.bc.byte.hi &= ~(1 << 5);
          return;

        case OP_RES_5_C:
          cpu->reg.bc.byte.lo &= ~(1 << 5);
          return;

        case OP_RES_5_D:
          cpu->reg.de.byte.hi &= ~(1 << 5);
          return;

        case OP_RES_5_E:
          cpu->reg.de.byte.lo &= ~(1 << 5);
          return;

        case OP_RES_5_H:
          cpu->reg.hl.byte.hi &= ~(1 << 5);
          return;

        case OP_RES_5_L:
          cpu->reg.hl.byte.lo &= ~(1 << 5);
          return;

        case OP_RES_5_HL: {
          uint8_t data = libyagbe_bus_read_memory(bus, cpu->reg.hl.value);
          data &= ~(1 << 5);
          libyagbe_bus_write_memory(bus, cpu->reg.hl.value, data);

          return;
        }

        case OP_RES_5_A:
          cpu->reg.af.byte.hi &= ~(1 << 5);
          return;

        case OP_RES_6_B:
          cpu->reg.bc.byte.hi &= ~(1 << 6);
          return;

        case OP_RES_6_C:
          cpu->reg.bc.byte.lo &= ~(1 << 6);
          return;

        case OP_RES_6_D:
          cpu->reg.de.byte.hi &= ~(1 << 6);
          return;

        case OP_RES_6_E:
          cpu->reg.de.byte.lo &= ~(1 << 6);
          return;

        case OP_RES_6_H:
          cpu->reg.hl.byte.hi &= ~(1 << 6);
          return;

        case OP_RES_6_L:
          cpu->reg.hl.byte.lo &= ~(1 << 6);
          return;

        case OP_RES_6_HL: {
          uint8_t data = libyagbe_bus_read_memory(bus, cpu->reg.hl.value);
          data &= ~(1 << 6);
          libyagbe_bus_write_memory(bus, cpu->reg.hl.value, data);

          return;
        }

        case OP_RES_6_A:
          cpu->reg.af.byte.hi &= ~(1 << 6);
          return;

        case OP_RES_7_B:
          cpu->reg.bc.byte.hi &= ~(1 << 7);
          return;

        case OP_RES_7_C:
          cpu->reg.bc.byte.lo &= ~(1 << 7);
          return;

        case OP_RES_7_D:
          cpu->reg.de.byte.hi &= ~(1 << 7);
          return;

        case OP_RES_7_E:
          cpu->reg.de.byte.lo &= ~(1 << 7);
          return;

        case OP_RES_7_H:
          cpu->reg.hl.byte.hi &= ~(1 << 7);
          return;

        case OP_RES_7_L:
          cpu->reg.hl.byte.lo &= ~(1 << 7);
          return;

        case OP_RES_7_HL: {
          uint8_t data = libyagbe_bus_read_memory(bus, cpu->reg.hl.value);
          data &= ~(1 << 7);
          libyagbe_bus_write_memory(bus, cpu->reg.hl.value, data);

          return;
        }

        case OP_RES_7_A:
          cpu->reg.af.byte.hi &= ~(1 << 7);
          return;

        case OP_SET_0_B:
          cpu->reg.bc.byte.hi |= (1 << 0);
          return;

        case OP_SET_0_C:
          cpu->reg.bc.byte.lo |= (1 << 0);
          return;

        case OP_SET_0_D:
          cpu->reg.de.byte.hi |= (1 << 0);
          return;

        case OP_SET_0_E:
          cpu->reg.de.byte.lo |= (1 << 0);
          return;

        case OP_SET_0_H:
          cpu->reg.hl.byte.hi |= (1 << 0);
          return;

        case OP_SET_0_L:
          cpu->reg.hl.byte.lo |= (1 << 0);
          return;

        case OP_SET_0_HL: {
          uint8_t data = libyagbe_bus_read_memory(bus, cpu->reg.hl.value);
          data |= (1 << 0);
          libyagbe_bus_write_memory(bus, cpu->reg.hl.value, data);

          return;
        }

        case OP_SET_0_A:
          cpu->reg.af.byte.hi |= (1 << 0);
          return;

        case OP_SET_1_B:
          cpu->reg.bc.byte.hi |= (1 << 1);
          return;

        case OP_SET_1_C:
          cpu->reg.bc.byte.lo |= (1 << 1);
          return;

        case OP_SET_1_D:
          cpu->reg.de.byte.hi |= (1 << 1);
          return;

        case OP_SET_1_E:
          cpu->reg.de.byte.lo |= (1 << 1);
          return;

        case OP_SET_1_H:
          cpu->reg.hl.byte.hi |= (1 << 1);
          return;

        case OP_SET_1_L:
          cpu->reg.hl.byte.lo |= (1 << 1);
          return;

        case OP_SET_1_HL: {
          uint8_t data = libyagbe_bus_read_memory(bus, cpu->reg.hl.value);
          data |= (1 << 1);
          libyagbe_bus_write_memory(bus, cpu->reg.hl.value, data);

          return;
        }

        case OP_SET_1_A:
          cpu->reg.af.byte.hi |= (1 << 1);
          return;

        case OP_SET_2_B:
          cpu->reg.bc.byte.hi |= (1 << 2);
          return;

        case OP_SET_2_C:
          cpu->reg.bc.byte.lo |= (1 << 2);
          return;

        case OP_SET_2_D:
          cpu->reg.de.byte.hi |= (1 << 2);
          return;

        case OP_SET_2_E:
          cpu->reg.de.byte.lo |= (1 << 2);
          return;

        case OP_SET_2_H:
          cpu->reg.hl.byte.hi |= (1 << 2);
          return;

        case OP_SET_2_L:
          cpu->reg.hl.byte.lo |= (1 << 2);
          return;

        case OP_SET_2_HL: {
          uint8_t data = libyagbe_bus_read_memory(bus, cpu->reg.hl.value);
          data |= (1 << 2);
          libyagbe_bus_write_memory(bus, cpu->reg.hl.value, data);

          return;
        }

        case OP_SET_2_A:
          cpu->reg.af.byte.hi |= (1 << 2);
          return;

        case OP_SET_3_B:
          cpu->reg.bc.byte.hi |= (1 << 3);
          return;

        case OP_SET_3_C:
          cpu->reg.bc.byte.lo |= (1 << 3);
          return;

        case OP_SET_3_D:
          cpu->reg.de.byte.hi |= (1 << 3);
          return;

        case OP_SET_3_E:
          cpu->reg.de.byte.lo |= (1 << 3);
          return;

        case OP_SET_3_H:
          cpu->reg.hl.byte.hi |= (1 << 3);
          return;

        case OP_SET_3_L:
          cpu->reg.hl.byte.lo |= (1 << 3);
          return;

        case OP_SET_3_HL: {
          uint8_t data = libyagbe_bus_read_memory(bus, cpu->reg.hl.value);
          data |= (1 << 3);
          libyagbe_bus_write_memory(bus, cpu->reg.hl.value, data);

          return;
        }

        case OP_SET_3_A:
          cpu->reg.af.byte.hi |= (1 << 3);
          return;

        case OP_SET_4_B:
          cpu->reg.bc.byte.hi |= (1 << 4);
          return;

        case OP_SET_4_C:
          cpu->reg.bc.byte.lo |= (1 << 4);
          return;

        case OP_SET_4_D:
          cpu->reg.de.byte.hi |= (1 << 4);
          return;

        case OP_SET_4_E:
          cpu->reg.de.byte.lo |= (1 << 4);
          return;

        case OP_SET_4_H:
          cpu->reg.hl.byte.hi |= (1 << 4);
          return;

        case OP_SET_4_L:
          cpu->reg.hl.byte.lo |= (1 << 4);
          return;

        case OP_SET_4_HL: {
          uint8_t data = libyagbe_bus_read_memory(bus, cpu->reg.hl.value);
          data |= (1 << 4);
          libyagbe_bus_write_memory(bus, cpu->reg.hl.value, data);

          return;
        }

        case OP_SET_4_A:
          cpu->reg.af.byte.hi |= (1 << 4);
          return;

        case OP_SET_5_B:
          cpu->reg.bc.byte.hi |= (1 << 5);
          return;

        case OP_SET_5_C:
          cpu->reg.bc.byte.lo |= (1 << 5);
          return;

        case OP_SET_5_D:
          cpu->reg.de.byte.hi |= (1 << 5);
          return;

        case OP_SET_5_E:
          cpu->reg.de.byte.lo |= (1 << 5);
          return;

        case OP_SET_5_H:
          cpu->reg.hl.byte.hi |= (1 << 5);
          return;

        case OP_SET_5_L:
          cpu->reg.hl.byte.lo |= (1 << 5);
          return;

        case OP_SET_5_HL: {
          uint8_t data = libyagbe_bus_read_memory(bus, cpu->reg.hl.value);
          data |= (1 << 5);
          libyagbe_bus_write_memory(bus, cpu->reg.hl.value, data);

          return;
        }

        case OP_SET_5_A:
          cpu->reg.af.byte.hi |= (1 << 5);
          return;

        case OP_SET_6_B:
          cpu->reg.bc.byte.hi |= (1 << 6);
          return;

        case OP_SET_6_C:
          cpu->reg.bc.byte.lo |= (1 << 6);
          return;

        case OP_SET_6_D:
          cpu->reg.de.byte.hi |= (1 << 6);
          return;

        case OP_SET_6_E:
          cpu->reg.de.byte.lo |= (1 << 6);
          return;

        case OP_SET_6_H:
          cpu->reg.hl.byte.hi |= (1 << 6);
          return;

        case OP_SET_6_L:
          cpu->reg.hl.byte.lo |= (1 << 6);
          return;

        case OP_SET_6_HL: {
          uint8_t data = libyagbe_bus_read_memory(bus, cpu->reg.hl.value);
          data |= (1 << 6);
          libyagbe_bus_write_memory(bus, cpu->reg.hl.value, data);

          return;
        }

        case OP_SET_6_A:
          cpu->reg.af.byte.hi |= (1 << 6);
          return;

        case OP_SET_7_B:
          cpu->reg.bc.byte.hi |= (1 << 7);
          return;

        case OP_SET_7_C:
          cpu->reg.bc.byte.lo |= (1 << 7);
          return;

        case OP_SET_7_D:
          cpu->reg.de.byte.hi |= (1 << 7);
          return;

        case OP_SET_7_E:
          cpu->reg.de.byte.lo |= (1 << 7);
          return;

        case OP_SET_7_H:
          cpu->reg.hl.byte.hi |= (1 << 7);
          return;

        case OP_SET_7_L:
          cpu->reg.hl.byte.lo |= (1 << 7);
          return;

        case OP_SET_7_HL: {
          uint8_t data = libyagbe_bus_read_memory(bus, cpu->reg.hl.value);
          data |= (1 << 7);
          libyagbe_bus_write_memory(bus, cpu->reg.hl.value, data);

          return;
        }

        case OP_SET_7_A:
          cpu->reg.af.byte.hi |= (1 << 7);
          return;

        default:
          break;
      }
      break;

      case OP_CALL_Z_IMM16:
        call_if(cpu, bus, (cpu->reg.af.byte.lo & FLAG_Z) != 0);
        return;

      case OP_CALL_IMM16:
        call_if(cpu, bus, true);
        return;

      case OP_ADC_A_IMM8: {
        const uint8_t imm8 = read_imm8(cpu, bus);

        alu_add(cpu, imm8, ALU_WITH_CARRY);
        return;
      }

      case OP_RST_08:
        rst(cpu, bus, 0x0008);
        return;

      case OP_RET_NC:
        ret_if(cpu, bus, !(cpu->reg.af.byte.lo & FLAG_C));
        return;

      case OP_CALL_NC_IMM16:
        call_if(cpu, bus, !(cpu->reg.af.byte.lo & FLAG_C));
        return;

      case OP_PUSH_DE:
        stack_push(cpu, bus, cpu->reg.de.byte.hi, cpu->reg.de.byte.lo);
        return;

      case OP_POP_DE:
        cpu->reg.de.value = stack_pop(cpu, bus);
        return;

      case OP_JP_NC_IMM16:
        jp_if(cpu, bus, !(cpu->reg.af.byte.lo & FLAG_C));
        return;

      case OP_SUB_IMM8: {
        const uint8_t imm8 = read_imm8(cpu, bus);

        alu_sub(cpu, imm8, ALU_NORMAL);
        return;
      }

      case OP_RST_10:
        rst(cpu, bus, 0x0010);
        return;

      case OP_RET_C:
        ret_if(cpu, bus, (cpu->reg.af.byte.lo & FLAG_C) != 0);
        return;

      case OP_RETI:
        ret_if(cpu, bus, true);
        return;

      case OP_JP_C_IMM16:
        jp_if(cpu, bus, (cpu->reg.af.byte.lo & FLAG_C) != 0);
        return;

      case OP_CALL_C_IMM16:
        call_if(cpu, bus, (cpu->reg.af.byte.lo & FLAG_C) != 0);
        return;

      case OP_SBC_A_IMM8: {
        const uint8_t imm = read_imm8(cpu, bus);

        alu_sub(cpu, imm, ALU_WITH_CARRY);
        return;
      }

      case OP_RST_18:
        rst(cpu, bus, 0x0018);
        return;

      case OP_LDH_IMM8_A: {
        const uint8_t imm8 = read_imm8(cpu, bus);
        libyagbe_bus_write_memory(bus, 0xFF00 + imm8, cpu->reg.af.byte.hi);

        return;
      }

      case OP_POP_HL:
        cpu->reg.hl.value = stack_pop(cpu, bus);
        return;

      case OP_LD_MEM_FF00_C_A:
        libyagbe_bus_write_memory(bus, 0xFF00 + cpu->reg.bc.byte.lo,
                                  cpu->reg.af.byte.hi);
        return;

      case OP_PUSH_HL:
        stack_push(cpu, bus, cpu->reg.hl.byte.hi, cpu->reg.hl.byte.lo);
        return;

      case OP_AND_IMM8: {
        const uint8_t imm8 = read_imm8(cpu, bus);

        cpu->reg.af.byte.hi &= imm8;
        cpu->reg.af.byte.lo = (cpu->reg.af.byte.hi == 0) ? 0xA0 : 0x20;

        return;
      }

      case OP_RST_20:
        rst(cpu, bus, 0x0020);
        return;

      case OP_ADD_SP_SIMM8: {
        const int8_t simm8 = (int8_t)read_imm8(cpu, bus);
        const uint16_t sum = (uint16_t)(cpu->reg.sp + simm8);

        const int result = cpu->reg.sp ^ simm8 ^ sum;

        cpu->reg.af.byte.lo &= ~FLAG_Z;
        cpu->reg.af.byte.lo &= ~FLAG_N;

        cpu->reg.af.byte.lo =
            set_half_carry_flag(cpu->reg.af.byte.lo, (result & 0x10) != 0);

        cpu->reg.af.byte.lo =
            set_carry_flag(cpu->reg.af.byte.lo, (result & 0x100) != 0);

        cpu->reg.sp = sum;
        return;
      }

      case OP_JP_HL:
        cpu->reg.pc = cpu->reg.hl.value;
        return;

      case OP_LD_MEM_IMM16_A: {
        const uint16_t imm16 = read_imm16(cpu, bus);

        libyagbe_bus_write_memory(bus, imm16, cpu->reg.af.byte.hi);
        return;
      }

      case OP_XOR_IMM8: {
        const uint8_t imm8 = read_imm8(cpu, bus);

        cpu->reg.af.byte.hi ^= imm8;
        cpu->reg.af.byte.lo = (cpu->reg.af.byte.hi == 0) ? 0x80 : 0x00;

        return;
      }

      case OP_RST_28:
        rst(cpu, bus, 0x0028);
        return;

      case OP_LDH_A_IMM8: {
        const uint8_t imm8 = read_imm8(cpu, bus);

        cpu->reg.af.byte.hi = libyagbe_bus_read_memory(bus, 0xFF00 + imm8);
        return;
      }

      case OP_POP_AF:
        cpu->reg.af.value = stack_pop(cpu, bus) & ~0x000F;
        return;

      case OP_LD_A_MEM_FF00_C:
        cpu->reg.af.byte.hi =
            libyagbe_bus_read_memory(bus, 0xFF00 + cpu->reg.bc.byte.lo);
        return;

      case OP_DI:
        return;

      case OP_PUSH_AF:
        stack_push(cpu, bus, cpu->reg.af.byte.hi, cpu->reg.af.byte.lo);
        return;

      case OP_OR_IMM8: {
        const uint8_t imm8 = read_imm8(cpu, bus);

        cpu->reg.af.byte.hi |= imm8;
        cpu->reg.af.byte.lo = (cpu->reg.af.byte.hi == 0) ? 0x80 : 0x00;

        return;
      }

      case OP_RST_30:
        rst(cpu, bus, 0x0030);
        return;

      case OP_LD_HL_SP_SIMM8: {
        const int8_t simm8 = (int8_t)read_imm8(cpu, bus);
        const uint16_t sum = (uint16_t)(cpu->reg.sp + simm8);

        const int result = cpu->reg.sp ^ simm8 ^ sum;

        cpu->reg.af.byte.lo &= ~FLAG_Z;
        cpu->reg.af.byte.lo &= ~FLAG_N;

        cpu->reg.af.byte.lo =
            set_half_carry_flag(cpu->reg.af.byte.lo, (result & 0x10) != 0);

        cpu->reg.af.byte.lo =
            set_carry_flag(cpu->reg.af.byte.lo, (result & 0x100) != 0);

        cpu->reg.hl.value = sum;
        return;
      }

      case OP_LD_SP_HL:
        cpu->reg.sp = cpu->reg.hl.value;
        return;

      case OP_LD_A_MEM_IMM16: {
        const uint16_t imm16 = read_imm16(cpu, bus);

        cpu->reg.af.byte.hi = libyagbe_bus_read_memory(bus, imm16);
        return;
      }

      case OP_CP_IMM8: {
        const uint8_t imm8 = read_imm8(cpu, bus);

        alu_sub(cpu, imm8, ALU_DISCARD_RESULT);
        return;
      }

      case OP_RST_38:
        rst(cpu, bus, 0x0038);
        return;

      default:
        break;
    }
  }
  __debugbreak();
}
