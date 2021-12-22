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

#include "libyagbe/disasm.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "libyagbe/bus.h"
#include "libyagbe/cpu.h"

/* Defines the registers that should be shown in the disassembly output *after*
 * the instruction has been executed. */
enum post_execution_regs {
  /** No post-execution disassembly is necessary. */
  REG_UNUSED,
  REG_B = (1 << 0),
  REG_C = (1 << 1),
  REG_D = (1 << 2),
  REG_E = (1 << 3),
  REG_F = (1 << 4),
  REG_H = (1 << 5),
  REG_L = (1 << 6),
  REG_A = (1 << 7),
  REG_BC = (1 << 8),
  REG_DE = (1 << 9),
  REG_HL = (1 << 10),
  REG_AF = (1 << 11),
  REG_SP = (1 << 12),
  REG_HL_MEM = (1 << 13),
  REG_MEM_IMM16 = (1 << 14)
};

/** Defines what data the format string should be interpolated with. */
enum instr_op { OP_NONE, OP_IMM8, OP_SIMM8, OP_IMM16 };

struct disasm_data {
  const char* const format_str;
  enum instr_op op;
  int flags;
};

static const struct disasm_data main_opcodes[256] = {
    {"NOP", OP_NONE, REG_UNUSED},             /* 0x00 */
    {"LD BC, $%04X", OP_IMM16, REG_BC},       /* 0x01 */
    {"LD (BC), A", OP_NONE, REG_BC},          /* 0x02 */
    {"INC BC", OP_NONE, REG_BC},              /* 0x03 */
    {"INC B", OP_NONE, REG_B},                /* 0x04 */
    {"DEC B", OP_NONE, REG_B},                /* 0x05 */
    {"LD B, $%02X", OP_IMM8, REG_B},          /* 0x06 */
    {"RLCA", OP_NONE, REG_UNUSED},            /* 0x07 */
    {"LD ($%04X), SP", OP_IMM16, REG_UNUSED}, /* 0x08 */
    {"ADD HL, BC", OP_NONE, REG_UNUSED},      /* 0x09 */
    {"LD A, (BC)", OP_NONE, REG_BC},          /* 0x0A */
    {"DEC BC", OP_NONE, REG_BC},              /* 0x0B */
    {"INC C", OP_NONE, REG_C},                /* 0x0C */
    {"DEC C", OP_NONE, REG_C},                /* 0x0D */
    {"LD C, $%02X", OP_IMM8, REG_C},          /* 0x0E */
    {"RRCA", OP_NONE, REG_UNUSED},            /* 0x0F */
    {"STOP", OP_NONE, REG_UNUSED},            /* 0x10 */
    {"LD DE, $%04X", OP_IMM16, REG_DE},       /* 0x11 */
    {"LD (DE), A", OP_NONE, REG_A | REG_DE},  /* 0x12 */
    {"INC DE", OP_NONE, REG_DE},              /* 0x13 */
    {"INC D", OP_NONE, REG_D | REG_F},        /* 0x14 */
    {"DEC D", OP_NONE, REG_D},                /* 0x15 */
    {"LD D, $%02X", OP_IMM8, REG_D},          /* 0x16 */
    {"RLA", OP_NONE, REG_UNUSED},             /* 0x17 */
    {"JR $%04X", OP_SIMM8, REG_UNUSED},       /* 0x18 */
    {"ADD HL, DE", OP_NONE, REG_UNUSED},      /* 0x19 */
    {"LD A, (DE)", OP_NONE, REG_DE},          /* 0x1A */
    {"DEC DE", OP_NONE, REG_DE},              /* 0x1B */
    {"INC E", OP_NONE, REG_E | REG_F},        /* 0x1C */
    {"DEC E", OP_NONE, REG_E},                /* 0x1D */
    {"LD E, $%02X", OP_IMM8, REG_E},          /* 0x1E */
    {"RRA", OP_NONE, REG_A | REG_F},          /* 0x1F */
    {"JR NZ, $%04X", OP_SIMM8, REG_UNUSED},   /* 0x20 */
    {"LD HL, $%04X", OP_IMM16, REG_HL},       /* 0x21 */
    {"LDI (HL), A", OP_NONE, REG_HL},         /* 0x22 */
    {"INC HL", OP_NONE, REG_HL},              /* 0x23 */
    {"INC H", OP_NONE, REG_H},                /* 0x24 */
    {"DEC H", OP_NONE, REG_H},                /* 0x25 */
    {"LD H, $%02X", OP_IMM8, REG_H},          /* 0x26 */
    {"DAA", OP_NONE, REG_A},                  /* 0x27 */
    {"JR Z, $%04X", OP_SIMM8, REG_UNUSED},    /* 0x28 */
    {"ADD HL, HL", OP_NONE, REG_HL | REG_F},  /* 0x29 */
    {"LDI A, (HL)", OP_NONE, REG_A | REG_HL}, /* 0x2A */
    {"DEC HL", OP_NONE, REG_HL},              /* 0x2B */
    {"INC L", OP_NONE, REG_L},                /* 0x2C */
    {"DEC L", OP_NONE, REG_L},                /* 0x2D */
    {"LD L, $%02X", OP_IMM8, REG_L},          /* 0x2E */
    {"CPL", OP_NONE, REG_UNUSED},             /* 0x2F */
    {"JR NC, $%04X", OP_SIMM8, REG_UNUSED},   /* 0x30 */
    {"LD SP, $%04X", OP_IMM16, REG_SP},       /* 0x31 */
    {"LDD (HL), A", OP_NONE, REG_HL},         /* 0x32 */
    {"INC SP", OP_NONE, REG_SP},              /* 0x33 */
    {"INC (HL)", OP_NONE, REG_HL_MEM},        /* 0x34 */
    {"DEC (HL)", OP_NONE, REG_HL_MEM},        /* 0x35 */
    {"LD (HL), $%02X", OP_IMM8, REG_HL_MEM},  /* 0x36 */
    {"SCF", OP_NONE, REG_UNUSED},             /* 0x37 */
    {"JR C, $%04X", OP_SIMM8, REG_UNUSED},    /* 0x38 */
    {"ADD HL, SP", OP_NONE, REG_UNUSED},      /* 0x39 */
    {"LDD, A, (HL)", OP_NONE, REG_HL},        /* 0x3A */
    {"DEC SP", OP_NONE, REG_UNUSED},          /* 0x3B */
    {"INC A", OP_NONE, REG_A},                /* 0x3C */
    {"DEC A", OP_NONE, REG_A},                /* 0x3D */
    {"LD A, $%02X", OP_IMM8, REG_A},          /* 0x3E */
    {"CCF", OP_NONE, REG_UNUSED},             /* 0x3F */
    {"LD B, B", OP_NONE, REG_B},              /* 0x40 */
    {"LD B, C", OP_NONE, REG_B},              /* 0x41 */
    {"LD B, D", OP_NONE, REG_B},              /* 0x42 */
    {"LD B, E", OP_NONE, REG_B},              /* 0x43 */
    {"LD B, H", OP_NONE, REG_B},              /* 0x44 */
    {"LD B, L", OP_NONE, REG_B},              /* 0x45 */
    {"LD B, (HL)", OP_NONE, REG_B},           /* 0x46 */
    {"LD B, A", OP_NONE, REG_B},              /* 0x47 */
    {"LD C, B", OP_NONE, REG_C},              /* 0x48 */
    {"LD C, C", OP_NONE, REG_C},              /* 0x49 */
    {"LD C, D", OP_NONE, REG_C},              /* 0x4A */
    {"LD C, E", OP_NONE, REG_C},              /* 0x4B */
    {"LD C, H", OP_NONE, REG_C},              /* 0x4C */
    {"LD C, L", OP_NONE, REG_C},              /* 0x4D */
    {"LD C, (HL)", OP_NONE, REG_C},           /* 0x4E */
    {"LD C, A", OP_NONE, REG_C},              /* 0x4F */
    {"LD D, B", OP_NONE, REG_D},              /* 0x50 */
    {"LD D, C", OP_NONE, REG_D},              /* 0x51 */
    {"LD D, D", OP_NONE, REG_D},              /* 0x52 */
    {"LD D, E", OP_NONE, REG_D},              /* 0x53 */
    {"LD D, H", OP_NONE, REG_D},              /* 0x54 */
    {"LD D, L", OP_NONE, REG_D},              /* 0x55 */
    {"LD D, (HL)", OP_NONE, REG_D},           /* 0x56 */
    {"LD D, A", OP_NONE, REG_D},              /* 0x57 */
    {"LD E, B", OP_NONE, REG_E},              /* 0x58 */
    {"LD E, C", OP_NONE, REG_E},              /* 0x59 */
    {"LD E, D", OP_NONE, REG_E},              /* 0x5A */
    {"LD E, E", OP_NONE, REG_E},              /* 0x5B */
    {"LD E, H", OP_NONE, REG_E},              /* 0x5C */
    {"LD E, L", OP_NONE, REG_E},              /* 0x5D */
    {"LD E, (HL)", OP_NONE, REG_E},           /* 0x5E */
    {"LD E, A", OP_NONE, REG_E},              /* 0x5F */
    {"LD H, B", OP_NONE, REG_H},              /* 0x60 */
    {"LD H, C", OP_NONE, REG_H},              /* 0x61 */
    {"LD H, D", OP_NONE, REG_H},              /* 0x62 */
    {"LD H, E", OP_NONE, REG_H},              /* 0x63 */
    {"LD H, H", OP_NONE, REG_H},              /* 0x64 */
    {"LD H, L", OP_NONE, REG_H},              /* 0x65 */
    {"LD H, (HL)", OP_NONE, REG_H},           /* 0x66 */
    {"LD H, A", OP_NONE, REG_H},              /* 0x67 */
    {"LD L, B", OP_NONE, REG_L},              /* 0x68 */
    {"LD L, C", OP_NONE, REG_L},              /* 0x69 */
    {"LD L, D", OP_NONE, REG_L},              /* 0x6A */
    {"LD L, E", OP_NONE, REG_L},              /* 0x6B */
    {"LD L, H", OP_NONE, REG_L},              /* 0x6C */
    {"LD L, L", OP_NONE, REG_L},              /* 0x6D */
    {"LD L, (HL)", OP_NONE, REG_L},           /* 0x6E */
    {"LD L, A", OP_NONE, REG_L},              /* 0x6F */
    {"LD (HL), B", OP_NONE, REG_HL},          /* 0x70 */
    {"LD (HL), C", OP_NONE, REG_HL},          /* 0x71 */
    {"LD (HL), D", OP_NONE, REG_HL},          /* 0x72 */
    {"LD (HL), E", OP_NONE, REG_HL},          /* 0x73 */
    {"LD (HL), H", OP_NONE, REG_HL},          /* 0x74 */
    {"LD (HL), L", OP_NONE, REG_HL},          /* 0x75 */
    {"HALT", OP_NONE, REG_UNUSED},            /* 0x76 */
    {"LD (HL), A", OP_NONE, REG_HL},          /* 0x77 */
    {"LD A, B", OP_NONE, REG_A},              /* 0x78 */
    {"LD A, C", OP_NONE, REG_A},              /* 0x79 */
    {"LD A, D", OP_NONE, REG_A},              /* 0x7A */
    {"LD A, E", OP_NONE, REG_A},              /* 0x7B */
    {"LD A, H", OP_NONE, REG_A},              /* 0x7C */
    {"LD A, L", OP_NONE, REG_A},              /* 0x7D */
    {"LD A, (HL)", OP_NONE, REG_A},           /* 0x7E */
    {"LD A, A", OP_NONE, REG_A},              /* 0x7F */
    {"ADD A, B", OP_NONE, REG_A},             /* 0x80 */
    {"ADD A, C", OP_NONE, REG_A},             /* 0x81 */
    {"ADD A, D", OP_NONE, REG_A},             /* 0x82 */
    {"ADD A, E", OP_NONE, REG_A},             /* 0x83 */
    {"ADD A, H", OP_NONE, REG_A},             /* 0x84 */
    {"ADD A, L", OP_NONE, REG_A},             /* 0x85 */
    {"ADD A, (HL)", OP_NONE, REG_A},          /* 0x86 */
    {"ADD A, A", OP_NONE, REG_A},             /* 0x87 */
    {"ADC A, B", OP_NONE, REG_A},             /* 0x88 */
    {"ADC A, C", OP_NONE, REG_A},             /* 0x89 */
    {"ADC A, D", OP_NONE, REG_A},             /* 0x8A */
    {"ADC A, E", OP_NONE, REG_A},             /* 0x8B */
    {"ADC A, H", OP_NONE, REG_A},             /* 0x8C */
    {"ADC A, L", OP_NONE, REG_A},             /* 0x8D */
    {"ADC A, (HL)", OP_NONE, REG_A},          /* 0x8E */
    {"ADC A, A", OP_NONE, REG_A},             /* 0x8F */
    {"SUB B", OP_NONE, REG_A},                /* 0x90 */
    {"SUB C", OP_NONE, REG_A},                /* 0x91 */
    {"SUB D", OP_NONE, REG_A},                /* 0x92 */
    {"SUB E", OP_NONE, REG_A},                /* 0x93 */
    {"SUB H", OP_NONE, REG_A},                /* 0x94 */
    {"SUB L", OP_NONE, REG_A},                /* 0x95 */
    {"SUB (HL)", OP_NONE, REG_A},             /* 0x96 */
    {"SUB A", OP_NONE, REG_A},                /* 0x97 */
    {"SBC A, B", OP_NONE, REG_A},             /* 0x98 */
    {"SBC A, C", OP_NONE, REG_A},             /* 0x99 */
    {"SBC A, D", OP_NONE, REG_A},             /* 0x9A */
    {"SBC A, E", OP_NONE, REG_A},             /* 0x9B */
    {"SBC A, H", OP_NONE, REG_A},             /* 0x9C */
    {"SBC A, L", OP_NONE, REG_A},             /* 0x9D */
    {"SBC A, (HL)", OP_NONE, REG_A},          /* 0x9E */
    {"SBC A, A", OP_NONE, REG_A},             /* 0x9F */
    {"AND B", OP_NONE, REG_A},                /* 0xA0 */
    {"AND C", OP_NONE, REG_A},                /* 0xA1 */
    {"AND D", OP_NONE, REG_A},                /* 0xA2 */
    {"AND E", OP_NONE, REG_A},                /* 0xA3 */
    {"AND H", OP_NONE, REG_A},                /* 0xA4 */
    {"AND L", OP_NONE, REG_A},                /* 0xA5 */
    {"AND (HL)", OP_NONE, REG_A},             /* 0xA6 */
    {"AND A", OP_NONE, REG_A},                /* 0xA7 */
    {"XOR B", OP_NONE, REG_A},                /* 0xA8 */
    {"XOR C", OP_NONE, REG_A},                /* 0xA9 */
    {"XOR D", OP_NONE, REG_A},                /* 0xAA */
    {"XOR E", OP_NONE, REG_A},                /* 0xAB */
    {"XOR H", OP_NONE, REG_A},                /* 0xAC */
    {"XOR L", OP_NONE, REG_A},                /* 0xAD */
    {"XOR (HL)", OP_NONE, REG_A},             /* 0xAE */
    {"XOR A", OP_NONE, REG_A},                /* 0xAF */
    {"OR B", OP_NONE, REG_A},                 /* 0xB0 */
    {"OR C", OP_NONE, REG_A | REG_F},         /* 0xB1 */
    {"OR D", OP_NONE, REG_A},                 /* 0xB2 */
    {"OR E", OP_NONE, REG_A},                 /* 0xB3 */
    {"OR H", OP_NONE, REG_A},                 /* 0xB4 */
    {"OR L", OP_NONE, REG_A},                 /* 0xB5 */
    {"OR (HL)", OP_NONE, REG_A},              /* 0xB6 */
    {"OR A", OP_NONE, REG_A},                 /* 0xB7 */
    {"CP B", OP_NONE, REG_A},                 /* 0xB8 */
    {"CP C", OP_NONE, REG_A},                 /* 0xB9 */
    {"CP D", OP_NONE, REG_A},                 /* 0xBA */
    {"CP E", OP_NONE, REG_A},                 /* 0xBB */
    {"CP H", OP_NONE, REG_A},                 /* 0xBC */
    {"CP L", OP_NONE, REG_A},                 /* 0xBD */
    {"CP (HL)", OP_NONE, REG_A},              /* 0xBE */
    {"CP A", OP_NONE, REG_A},                 /* 0xBF */
    {"RET NZ", OP_NONE, REG_UNUSED},          /* 0xC0 */
    {"POP BC", OP_NONE, REG_UNUSED},          /* 0xC1 */
    {"JP NZ, $%04X", OP_IMM16, REG_UNUSED},   /* 0xC2 */
    {"JP $%04X", OP_IMM16, REG_UNUSED},       /* 0xC3 */
    {"CALL NZ, $%04X", OP_IMM16, REG_UNUSED}, /* 0xC4 */
    {"PUSH BC", OP_NONE, REG_SP | REG_BC},    /* 0xC5 */
    {"ADD A, $%02X", OP_IMM8, REG_A},         /* 0xC6 */
    {"RST $00", OP_NONE, REG_UNUSED},         /* 0xC7 */
    {"RET Z", OP_NONE, REG_UNUSED},           /* 0xC8 */
    {"RET", OP_NONE, REG_SP},                 /* 0xC9 */
    {"JP Z, $%04X", OP_IMM16, REG_UNUSED},    /* 0xCA */
    {"PREFIX CB", OP_NONE, REG_UNUSED},      /* 0xCB (should never be called) */
    {"CALL Z, $%04X", OP_IMM16, REG_UNUSED}, /* 0xCC */
    {"CALL $%04X", OP_IMM16, REG_SP},        /* 0xCD */
    {"ADC A, $%02X", OP_IMM8, REG_A},        /* 0xCE */
    {"RST $08", OP_NONE, REG_UNUSED},        /* 0xCF */
    {"RET NC", OP_NONE, REG_UNUSED},         /* 0xD0 */
    {"POP DE", OP_NONE, REG_UNUSED},         /* 0xD1 */
    {"JP NC, $%04X", OP_IMM16, REG_UNUSED},  /* 0xD2 */
    {"ILLEGAL $D3", OP_NONE, REG_UNUSED},    /* 0xD3 */
    {"CALL NC, $%04X", OP_IMM16, REG_UNUSED},           /* 0xD4 */
    {"PUSH DE", OP_NONE, REG_UNUSED},                   /* 0xD5 */
    {"SUB $%02X", OP_IMM8, REG_A},                      /* 0xD6 */
    {"RST $0010", OP_NONE, REG_UNUSED},                 /* 0xD7 */
    {"RET C", OP_NONE, REG_UNUSED},                     /* 0xD8 */
    {"RETI", OP_NONE, REG_UNUSED},                      /* 0xD9 */
    {"JP C, $%04X", OP_IMM16, REG_UNUSED},              /* 0xDA */
    {"ILLEGAL $DB", OP_NONE, REG_UNUSED},               /* 0xDB */
    {"CALL C, $%04X", OP_IMM16, REG_UNUSED},            /* 0xDC */
    {"ILLEGAL $DD", OP_NONE, REG_UNUSED},               /* 0xDD */
    {"SBC A, $%02X", OP_IMM8, REG_A},                   /* 0xDE */
    {"RST $0018", OP_NONE, REG_UNUSED},                 /* 0xDF */
    {"LDH ($FF%02X), A", OP_IMM8, REG_UNUSED},          /* 0xE0 */
    {"POP HL", OP_NONE, REG_HL | REG_SP},               /* 0xE1 */
    {"LD (C), A", OP_NONE, REG_UNUSED},                 /* 0xE2 */
    {"ILLEGAL $E3", OP_NONE, REG_UNUSED},               /* 0xE3 */
    {"ILLEGAL $E4", OP_NONE, REG_UNUSED},               /* 0xE4 */
    {"PUSH HL", OP_NONE, REG_HL | REG_SP},              /* 0xE5 */
    {"AND $%02X", OP_IMM8, REG_UNUSED},                 /* 0xE6 */
    {"RST $0020", OP_NONE, REG_UNUSED},                 /* 0xE7 */
    {"ADD SP, $%02X", OP_SIMM8, REG_SP},                /* 0xE8 */
    {"JP (HL)", OP_NONE, REG_UNUSED},                   /* 0xE9 */
    {"LD ($%04X), A", OP_IMM16, REG_A | REG_MEM_IMM16}, /* 0xEA */
    {"ILLEGAL $EB", OP_NONE, REG_UNUSED},               /* 0xEB */
    {"ILLEGAL $EC", OP_NONE, REG_UNUSED},               /* 0xEC */
    {"ILLEGAL $ED", OP_NONE, REG_UNUSED},               /* 0xED */
    {"XOR $%02X", OP_IMM8, REG_A},                      /* 0xEE */
    {"RST $0028", OP_NONE, REG_UNUSED},                 /* 0xEF */
    {"LDH A, ($%02X)", OP_IMM8, REG_A},                 /* 0xF0 */
    {"POP AF", OP_NONE, REG_AF},                        /* 0xF1 */
    {"LD A, (C)", OP_NONE, REG_UNUSED},                 /* 0xF2 */
    {"DI", OP_NONE, REG_UNUSED},                        /* 0xF3 */
    {"ILLEGAL $F4", OP_NONE, REG_UNUSED},               /* 0xF4 */
    {"PUSH AF", OP_NONE, REG_UNUSED},                   /* 0xF5 */
    {"OR $%02X", OP_IMM8, REG_A},                       /* 0xF6 */
    {"RST $0030", OP_NONE, REG_UNUSED},                 /* 0xF7 */
    {"LD HL, SP+$%02X", OP_SIMM8, REG_UNUSED},          /* 0xF8 */
    {"LD SP, HL", OP_NONE, REG_UNUSED},                 /* 0xF9 */
    {"LD A, ($%04X)", OP_IMM16, REG_A},                 /* 0xFA */
    {"EI", OP_NONE, REG_UNUSED},                        /* 0xFB */
    {"ILLEGAL $FC", OP_NONE, REG_UNUSED},               /* 0xFC */
    {"ILLEGAL $FD", OP_NONE, REG_UNUSED},               /* 0xFD */
    {"CP $%02X", OP_IMM8, REG_UNUSED},                  /* 0xFE */
    {"RST $0038", OP_NONE, REG_UNUSED}                  /* 0xFF */
};

static const struct disasm_data cb_opcodes[256] = {
    {"RLC B", OP_NONE, REG_B | REG_F},            /* 0x00 */
    {"RLC C", OP_NONE, REG_C | REG_F},            /* 0x01 */
    {"RLC D", OP_NONE, REG_D | REG_F},            /* 0x02 */
    {"RLC E", OP_NONE, REG_E | REG_F},            /* 0x03 */
    {"RLC H", OP_NONE, REG_H | REG_F},            /* 0x04 */
    {"RLC L", OP_NONE, REG_L | REG_F},            /* 0x05 */
    {"RLC (HL)", OP_NONE, REG_HL_MEM | REG_F},    /* 0x06 */
    {"RLC A", OP_NONE, REG_A | REG_F},            /* 0x07 */
    {"RRC B", OP_NONE, REG_B | REG_F},            /* 0x08 */
    {"RRC C", OP_NONE, REG_C | REG_F},            /* 0x09 */
    {"RRC D", OP_NONE, REG_D | REG_F},            /* 0x0A */
    {"RRC E", OP_NONE, REG_E | REG_F},            /* 0x0B */
    {"RRC H", OP_NONE, REG_H | REG_F},            /* 0x0C */
    {"RRC L", OP_NONE, REG_L | REG_F},            /* 0x0D */
    {"RRC (HL)", OP_NONE, REG_HL_MEM | REG_F},    /* 0x0E */
    {"RRC A", OP_NONE, REG_A | REG_F},            /* 0x0F */
    {"RL B", OP_NONE, REG_B | REG_F},             /* 0x10 */
    {"RL C", OP_NONE, REG_C | REG_F},             /* 0x11 */
    {"RL D", OP_NONE, REG_D | REG_F},             /* 0x12 */
    {"RL E", OP_NONE, REG_E | REG_F},             /* 0x13 */
    {"RL H", OP_NONE, REG_H | REG_F},             /* 0x14 */
    {"RL L", OP_NONE, REG_L | REG_F},             /* 0x15 */
    {"RL (HL)", OP_NONE, REG_HL_MEM | REG_F},     /* 0x16 */
    {"RL A", OP_NONE, REG_A | REG_F},             /* 0x17 */
    {"RR B", OP_NONE, REG_B | REG_F},             /* 0x18 */
    {"RR C", OP_NONE, REG_C | REG_F},             /* 0x19 */
    {"RR D", OP_NONE, REG_D | REG_F},             /* 0x1A */
    {"RR E", OP_NONE, REG_E | REG_F},             /* 0x1B */
    {"RR H", OP_NONE, REG_H | REG_F},             /* 0x1C */
    {"RR L", OP_NONE, REG_L | REG_F},             /* 0x1D */
    {"RR (HL)", OP_NONE, REG_HL_MEM | REG_F},     /* 0x1E */
    {"RR A", OP_NONE, REG_A | REG_F},             /* 0x1F */
    {"SLA B", OP_NONE, REG_B | REG_F},            /* 0x20 */
    {"SLA C", OP_NONE, REG_C | REG_F},            /* 0x21 */
    {"SLA D", OP_NONE, REG_D | REG_F},            /* 0x22 */
    {"SLA E", OP_NONE, REG_E | REG_F},            /* 0x23 */
    {"SLA H", OP_NONE, REG_H | REG_F},            /* 0x24 */
    {"SLA L", OP_NONE, REG_L | REG_F},            /* 0x25 */
    {"SLA (HL)", OP_NONE, REG_HL_MEM | REG_F},    /* 0x26 */
    {"SLA A", OP_NONE, REG_A | REG_F},            /* 0x27 */
    {"SRA B", OP_NONE, REG_B | REG_F},            /* 0x28 */
    {"SRA C", OP_NONE, REG_C | REG_F},            /* 0x29 */
    {"SRA D", OP_NONE, REG_D | REG_F},            /* 0x2A */
    {"SRA E", OP_NONE, REG_E | REG_F},            /* 0x2B */
    {"SRA H", OP_NONE, REG_H | REG_F},            /* 0x2C */
    {"SRA L", OP_NONE, REG_L | REG_F},            /* 0x2D */
    {"SRA (HL)", OP_NONE, REG_HL_MEM | REG_F},    /* 0x2E */
    {"SRA A", OP_NONE, REG_A | REG_F},            /* 0x2F */
    {"SWAP B", OP_NONE, REG_B | REG_F},           /* 0x30 */
    {"SWAP C", OP_NONE, REG_C | REG_F},           /* 0x31 */
    {"SWAP D", OP_NONE, REG_D | REG_F},           /* 0x32 */
    {"SWAP E", OP_NONE, REG_E | REG_F},           /* 0x33 */
    {"SWAP H", OP_NONE, REG_H | REG_F},           /* 0x34 */
    {"SWAP L", OP_NONE, REG_L | REG_F},           /* 0x35 */
    {"SWAP (HL)", OP_NONE, REG_HL_MEM | REG_F},   /* 0x36 */
    {"SWAP A", OP_NONE, REG_A | REG_F},           /* 0x37 */
    {"SRL B", OP_NONE, REG_B | REG_F},            /* 0x38 */
    {"SRL C", OP_NONE, REG_C | REG_F},            /* 0x39 */
    {"SRL D", OP_NONE, REG_D | REG_F},            /* 0x3A */
    {"SRL E", OP_NONE, REG_E | REG_F},            /* 0x3B */
    {"SRL H", OP_NONE, REG_H | REG_F},            /* 0x3C */
    {"SRL L", OP_NONE, REG_L | REG_F},            /* 0x3D */
    {"SRL (HL)", OP_NONE, REG_HL_MEM | REG_F},    /* 0x3E */
    {"SRL A", OP_NONE, REG_A | REG_F},            /* 0x3F */
    {"BIT 0, B", OP_NONE, REG_F},                 /* 0x40 */
    {"BIT 0, C", OP_NONE, REG_F},                 /* 0x41 */
    {"BIT 0, D", OP_NONE, REG_F},                 /* 0x42 */
    {"BIT 0, E", OP_NONE, REG_F},                 /* 0x43 */
    {"BIT 0, H", OP_NONE, REG_F},                 /* 0x44 */
    {"BIT 0, L", OP_NONE, REG_F},                 /* 0x45 */
    {"BIT 0, (HL)", OP_NONE, REG_HL_MEM | REG_F}, /* 0x46 */
    {"BIT 0, A", OP_NONE, REG_F},                 /* 0x47 */
    {"BIT 1, B", OP_NONE, REG_F},                 /* 0x48 */
    {"BIT 1, C", OP_NONE, REG_F},                 /* 0x49 */
    {"BIT 1, D", OP_NONE, REG_F},                 /* 0x4A */
    {"BIT 1, E", OP_NONE, REG_F},                 /* 0x4B */
    {"BIT 1, H", OP_NONE, REG_F},                 /* 0x4C */
    {"BIT 1, L", OP_NONE, REG_F},                 /* 0x4D */
    {"BIT 1, (HL)", OP_NONE, REG_HL_MEM | REG_F}, /* 0x4E */
    {"BIT 1, A", OP_NONE, REG_F},                 /* 0x4F */
    {"BIT 2, B", OP_NONE, REG_F},                 /* 0x50 */
    {"BIT 2, C", OP_NONE, REG_F},                 /* 0x51 */
    {"BIT 2, D", OP_NONE, REG_F},                 /* 0x52 */
    {"BIT 2, E", OP_NONE, REG_F},                 /* 0x53 */
    {"BIT 2, H", OP_NONE, REG_F},                 /* 0x54 */
    {"BIT 2, L", OP_NONE, REG_F},                 /* 0x55 */
    {"BIT 2, (HL)", OP_NONE, REG_HL_MEM | REG_F}, /* 0x56 */
    {"BIT 2, A", OP_NONE, REG_F},                 /* 0x57 */
    {"BIT 3, B", OP_NONE, REG_F},                 /* 0x58 */
    {"BIT 3, C", OP_NONE, REG_F},                 /* 0x59 */
    {"BIT 3, D", OP_NONE, REG_F},                 /* 0x5A */
    {"BIT 3, E", OP_NONE, REG_F},                 /* 0x5B */
    {"BIT 3, H", OP_NONE, REG_F},                 /* 0x5C */
    {"BIT 3, L", OP_NONE, REG_F},                 /* 0x5D */
    {"BIT 3, (HL)", OP_NONE, REG_HL_MEM | REG_F}, /* 0x5E */
    {"BIT 3, A", OP_NONE, REG_F},                 /* 0x5F */
    {"BIT 4, B", OP_NONE, REG_F},                 /* 0x60 */
    {"BIT 4, C", OP_NONE, REG_F},                 /* 0x61 */
    {"BIT 4, D", OP_NONE, REG_F},                 /* 0x62 */
    {"BIT 4, E", OP_NONE, REG_F},                 /* 0x63 */
    {"BIT 4, H", OP_NONE, REG_F},                 /* 0x64 */
    {"BIT 4, L", OP_NONE, REG_F},                 /* 0x65 */
    {"BIT 4, (HL)", OP_NONE, REG_HL_MEM | REG_F}, /* 0x66 */
    {"BIT 4, A", OP_NONE, REG_F},                 /* 0x67 */
    {"BIT 5, B", OP_NONE, REG_F},                 /* 0x68 */
    {"BIT 5, C", OP_NONE, REG_F},                 /* 0x69 */
    {"BIT 5, D", OP_NONE, REG_F},                 /* 0x6A */
    {"BIT 5, E", OP_NONE, REG_F},                 /* 0x6B */
    {"BIT 5, H", OP_NONE, REG_F},                 /* 0x6C */
    {"BIT 5, L", OP_NONE, REG_F},                 /* 0x6D */
    {"BIT 5, (HL)", OP_NONE, REG_HL_MEM | REG_F}, /* 0x6E */
    {"BIT 5, A", OP_NONE, REG_F},                 /* 0x6F */
    {"BIT 6, B", OP_NONE, REG_F},                 /* 0x70 */
    {"BIT 6, C", OP_NONE, REG_F},                 /* 0x71 */
    {"BIT 6, D", OP_NONE, REG_F},                 /* 0x72 */
    {"BIT 6, E", OP_NONE, REG_F},                 /* 0x73 */
    {"BIT 6, H", OP_NONE, REG_F},                 /* 0x74 */
    {"BIT 6, L", OP_NONE, REG_F},                 /* 0x75 */
    {"BIT 6, (HL)", OP_NONE, REG_HL_MEM | REG_F}, /* 0x76 */
    {"BIT 6, A", OP_NONE, REG_F},                 /* 0x77 */
    {"BIT 7, B", OP_NONE, REG_F},                 /* 0x78 */
    {"BIT 7, C", OP_NONE, REG_F},                 /* 0x79 */
    {"BIT 7, D", OP_NONE, REG_F},                 /* 0x7A */
    {"BIT 7, E", OP_NONE, REG_F},                 /* 0x7B */
    {"BIT 7, H", OP_NONE, REG_F},                 /* 0x7C */
    {"BIT 7, L", OP_NONE, REG_F},                 /* 0x7D */
    {"BIT 7, (HL)", OP_NONE, REG_HL_MEM | REG_F}, /* 0x7E */
    {"BIT 7, A", OP_NONE, REG_F},                 /* 0x7F */
    {"RES 0, B", OP_NONE, REG_B | REG_F},         /* 0x80 */
    {"RES 0, C", OP_NONE, REG_C | REG_F},         /* 0x81 */
    {"RES 0, D", OP_NONE, REG_D | REG_F},         /* 0x82 */
    {"RES 0, E", OP_NONE, REG_E | REG_F},         /* 0x83 */
    {"RES 0, H", OP_NONE, REG_H | REG_F},         /* 0x84 */
    {"RES 0, L", OP_NONE, REG_L | REG_F},         /* 0x85 */
    {"RES 0, (HL)", OP_NONE, REG_HL_MEM | REG_F}, /* 0x86 */
    {"RES 0, A", OP_NONE, REG_A | REG_F},         /* 0x87 */
    {"RES 1, B", OP_NONE, REG_B | REG_F},         /* 0x88 */
    {"RES 1, C", OP_NONE, REG_C | REG_F},         /* 0x89 */
    {"RES 1, D", OP_NONE, REG_D | REG_F},         /* 0x8A */
    {"RES 1, E", OP_NONE, REG_E | REG_F},         /* 0x8B */
    {"RES 1, H", OP_NONE, REG_H | REG_F},         /* 0x8C */
    {"RES 1, L", OP_NONE, REG_L | REG_F},         /* 0x8D */
    {"RES 1, (HL)", OP_NONE, REG_HL_MEM | REG_F}, /* 0x8E */
    {"RES 1, A", OP_NONE, REG_A | REG_F},         /* 0x8F */
    {"RES 2, B", OP_NONE, REG_B | REG_F},         /* 0x90 */
    {"RES 2, C", OP_NONE, REG_C | REG_F},         /* 0x91 */
    {"RES 2, D", OP_NONE, REG_D | REG_F},         /* 0x92 */
    {"RES 2, E", OP_NONE, REG_E | REG_F},         /* 0x93 */
    {"RES 2, H", OP_NONE, REG_H | REG_F},         /* 0x94 */
    {"RES 2, L", OP_NONE, REG_L | REG_F},         /* 0x95 */
    {"RES 2, (HL)", OP_NONE, REG_HL_MEM | REG_F}, /* 0x96 */
    {"RES 2, A", OP_NONE, REG_A | REG_F},         /* 0x97 */
    {"RES 3, B", OP_NONE, REG_B | REG_F},         /* 0x98 */
    {"RES 3, C", OP_NONE, REG_C | REG_F},         /* 0x99 */
    {"RES 3, D", OP_NONE, REG_D | REG_F},         /* 0x9A */
    {"RES 3, E", OP_NONE, REG_E | REG_F},         /* 0x9B */
    {"RES 3, H", OP_NONE, REG_H | REG_F},         /* 0x9C */
    {"RES 3, L", OP_NONE, REG_L | REG_F},         /* 0x9D */
    {"RES 3, (HL)", OP_NONE, REG_HL_MEM | REG_F}, /* 0x9E */
    {"RES 3, A", OP_NONE, REG_A | REG_F},         /* 0x9F */
    {"RES 4, B", OP_NONE, REG_B | REG_F},         /* 0xA0 */
    {"RES 4, C", OP_NONE, REG_C | REG_F},         /* 0xA1 */
    {"RES 4, D", OP_NONE, REG_D | REG_F},         /* 0xA2 */
    {"RES 4, E", OP_NONE, REG_E | REG_F},         /* 0xA3 */
    {"RES 4, H", OP_NONE, REG_H | REG_F},         /* 0xA4 */
    {"RES 4, L", OP_NONE, REG_L | REG_F},         /* 0xA5 */
    {"RES 4, (HL)", OP_NONE, REG_HL_MEM | REG_F}, /* 0xA6 */
    {"RES 4, A", OP_NONE, REG_A | REG_F},         /* 0xA7 */
    {"RES 5, B", OP_NONE, REG_B | REG_F},         /* 0xA8 */
    {"RES 5, C", OP_NONE, REG_C | REG_F},         /* 0xA9 */
    {"RES 5, D", OP_NONE, REG_D | REG_F},         /* 0xAA */
    {"RES 5, E", OP_NONE, REG_E | REG_F},         /* 0xAB */
    {"RES 5, H", OP_NONE, REG_H | REG_F},         /* 0xAC */
    {"RES 5, L", OP_NONE, REG_L | REG_F},         /* 0xAD */
    {"RES 5, (HL)", OP_NONE, REG_HL_MEM | REG_F}, /* 0xAE */
    {"RES 5, A", OP_NONE, REG_A | REG_F},         /* 0xAF */
    {"RES 6, B", OP_NONE, REG_B | REG_F},         /* 0xB0 */
    {"RES 6, C", OP_NONE, REG_C | REG_F},         /* 0xB1 */
    {"RES 6, D", OP_NONE, REG_D | REG_F},         /* 0xB2 */
    {"RES 6, E", OP_NONE, REG_E | REG_F},         /* 0xB3 */
    {"RES 6, H", OP_NONE, REG_H | REG_F},         /* 0xB4 */
    {"RES 6, L", OP_NONE, REG_L | REG_F},         /* 0xB5 */
    {"RES 6, (HL)", OP_NONE, REG_HL_MEM | REG_F}, /* 0xB6 */
    {"RES 6, A", OP_NONE, REG_A | REG_F},         /* 0xB7 */
    {"RES 7, B", OP_NONE, REG_B | REG_F},         /* 0xB8 */
    {"RES 7, C", OP_NONE, REG_C | REG_F},         /* 0xB9 */
    {"RES 7, D", OP_NONE, REG_D | REG_F},         /* 0xBA */
    {"RES 7, E", OP_NONE, REG_E | REG_F},         /* 0xBB */
    {"RES 7, H", OP_NONE, REG_H | REG_F},         /* 0xBC */
    {"RES 7, L", OP_NONE, REG_L | REG_F},         /* 0xBD */
    {"RES 7, (HL)", OP_NONE, REG_HL_MEM | REG_F}, /* 0xBE */
    {"RES 7, A", OP_NONE, REG_A | REG_F},         /* 0xBF */
    {"SET 0, B", OP_NONE, REG_B | REG_F},         /* 0xC0 */
    {"SET 0, C", OP_NONE, REG_C | REG_F},         /* 0xC1 */
    {"SET 0, D", OP_NONE, REG_D | REG_F},         /* 0xC2 */
    {"SET 0, E", OP_NONE, REG_E | REG_F},         /* 0xC3 */
    {"SET 0, H", OP_NONE, REG_H | REG_F},         /* 0xC4 */
    {"SET 0, L", OP_NONE, REG_L | REG_F},         /* 0xC5 */
    {"SET 0, (HL)", OP_NONE, REG_HL_MEM | REG_F}, /* 0xC6 */
    {"SET 0, A", OP_NONE, REG_A | REG_F},         /* 0xC7 */
    {"SET 1, B", OP_NONE, REG_B | REG_F},         /* 0xC8 */
    {"SET 1, C", OP_NONE, REG_C | REG_F},         /* 0xC9 */
    {"SET 1, D", OP_NONE, REG_D | REG_F},         /* 0xCA */
    {"SET 1, E", OP_NONE, REG_E | REG_F},         /* 0xCB */
    {"SET 1, H", OP_NONE, REG_H | REG_F},         /* 0xCC */
    {"SET 1, L", OP_NONE, REG_L | REG_F},         /* 0xCD */
    {"SET 1, (HL)", OP_NONE, REG_HL_MEM | REG_F}, /* 0xCE */
    {"SET 1, A", OP_NONE, REG_A | REG_F},         /* 0xCF */
    {"SET 2, B", OP_NONE, REG_B | REG_F},         /* 0xD0 */
    {"SET 2, C", OP_NONE, REG_C | REG_F},         /* 0xD1 */
    {"SET 2, D", OP_NONE, REG_D | REG_F},         /* 0xD2 */
    {"SET 2, E", OP_NONE, REG_E | REG_F},         /* 0xD3 */
    {"SET 2, H", OP_NONE, REG_H | REG_F},         /* 0xD4 */
    {"SET 2, L", OP_NONE, REG_L | REG_F},         /* 0xD5 */
    {"SET 2, (HL)", OP_NONE, REG_HL_MEM | REG_F}, /* 0xD6 */
    {"SET 2, A", OP_NONE, REG_A | REG_F},         /* 0xD7 */
    {"SET 3, B", OP_NONE, REG_B | REG_F},         /* 0xD8 */
    {"SET 3, C", OP_NONE, REG_C | REG_F},         /* 0xD9 */
    {"SET 3, D", OP_NONE, REG_D | REG_F},         /* 0xDA */
    {"SET 3, E", OP_NONE, REG_E | REG_F},         /* 0xDB */
    {"SET 3, H", OP_NONE, REG_H | REG_F},         /* 0xDC */
    {"SET 3, L", OP_NONE, REG_L | REG_F},         /* 0xDD */
    {"SET 3, (HL)", OP_NONE, REG_HL_MEM | REG_F}, /* 0xDE */
    {"SET 3, A", OP_NONE, REG_A | REG_F},         /* 0xDF */
    {"SET 4, B", OP_NONE, REG_B | REG_F},         /* 0xE0 */
    {"SET 4, C", OP_NONE, REG_C | REG_F},         /* 0xE1 */
    {"SET 4, D", OP_NONE, REG_D | REG_F},         /* 0xE2 */
    {"SET 4, E", OP_NONE, REG_E | REG_F},         /* 0xE3 */
    {"SET 4, H", OP_NONE, REG_H | REG_F},         /* 0xE4 */
    {"SET 4, L", OP_NONE, REG_L | REG_F},         /* 0xE5 */
    {"SET 4, (HL)", OP_NONE, REG_HL_MEM | REG_F}, /* 0xE6 */
    {"SET 4, A", OP_NONE, REG_A | REG_F},         /* 0xE7 */
    {"SET 5, B", OP_NONE, REG_B | REG_F},         /* 0xE8 */
    {"SET 5, C", OP_NONE, REG_C | REG_F},         /* 0xE9 */
    {"SET 5, D", OP_NONE, REG_D | REG_F},         /* 0xEA */
    {"SET 5, E", OP_NONE, REG_E | REG_F},         /* 0xEB */
    {"SET 5, H", OP_NONE, REG_H | REG_F},         /* 0xEC */
    {"SET 5, L", OP_NONE, REG_L | REG_F},         /* 0xED */
    {"SET 5, (HL)", OP_NONE, REG_HL_MEM | REG_F}, /* 0xEE */
    {"SET 5, A", OP_NONE, REG_A | REG_F},         /* 0xEF */
    {"SET 6, B", OP_NONE, REG_B | REG_F},         /* 0xF0 */
    {"SET 6, C", OP_NONE, REG_C | REG_F},         /* 0xF1 */
    {"SET 6, D", OP_NONE, REG_D | REG_F},         /* 0xF2 */
    {"SET 6, E", OP_NONE, REG_E | REG_F},         /* 0xF3 */
    {"SET 6, H", OP_NONE, REG_H | REG_F},         /* 0xF4 */
    {"SET 6, L", OP_NONE, REG_L | REG_F},         /* 0xF5 */
    {"SET 6, (HL)", OP_NONE, REG_HL_MEM | REG_F}, /* 0xF6 */
    {"SET 6, A", OP_NONE, REG_A | REG_F},         /* 0xF7 */
    {"SET 7, B", OP_NONE, REG_B | REG_F},         /* 0xF8 */
    {"SET 7, C", OP_NONE, REG_C | REG_F},         /* 0xF9 */
    {"SET 7, D", OP_NONE, REG_D | REG_F},         /* 0xFA */
    {"SET 7, E", OP_NONE, REG_E | REG_F},         /* 0xFB */
    {"SET 7, H", OP_NONE, REG_H | REG_F},         /* 0xFC */
    {"SET 7, L", OP_NONE, REG_L | REG_F},         /* 0xFD */
    {"SET 7, (HL)", OP_NONE, REG_HL_MEM | REG_F}, /* 0xFE */
    {"SET 7, A", OP_NONE, REG_A | REG_F}          /* 0xFF */
};

static struct current_disasm {
  char disasm_result[256];
  int post_op_flags;
} current_disasm;

void libyagbe_disasm_prepare(const uint16_t pc,
                             struct libyagbe_bus* const bus) {
  uint8_t instruction;
  const struct disasm_data* data;

  assert(bus != NULL);

  memset(&current_disasm, 0, sizeof(current_disasm));

  instruction = libyagbe_bus_read_memory(bus, pc);

  if (instruction == 0xCB) {
    instruction = libyagbe_bus_read_memory(bus, pc + 1);
    data = &cb_opcodes[instruction];
  } else {
    data = &main_opcodes[instruction];
  }

  switch (data->op) {
    case OP_NONE:
      sprintf(current_disasm.disasm_result, "%s", data->format_str);
      break;

    case OP_IMM8: {
      const uint8_t imm = libyagbe_bus_read_memory(bus, pc + 1);
      sprintf(current_disasm.disasm_result, data->format_str, imm);

      break;
    }

    case OP_IMM16: {
      const uint8_t lo = libyagbe_bus_read_memory(bus, pc + 1);
      const uint8_t hi = libyagbe_bus_read_memory(bus, pc + 2);

      sprintf(current_disasm.disasm_result, data->format_str, (hi << 8) | lo);
      break;
    }

    case OP_SIMM8: {
      const int8_t imm = (int8_t)libyagbe_bus_read_memory(bus, pc + 1);

      sprintf(current_disasm.disasm_result, data->format_str, pc + imm + 2);
      break;
    }
  }
  current_disasm.post_op_flags = data->flags;
}

char* libyagbe_disasm_execute(struct libyagbe_cpu* const cpu,
                              struct libyagbe_bus* const bus) {
  int bit_counter;

  assert(cpu != NULL);
  assert(bus != NULL);

  if (current_disasm.post_op_flags == REG_UNUSED) {
    /* No post instruction execution disassembly has to take place. We're
     * done.
     */
    return current_disasm.disasm_result;
  }

  strcat(current_disasm.disasm_result, "          ; ");

  for (bit_counter = 0; bit_counter != 15; ++bit_counter) {
    const int counter = (1 << bit_counter);
    char buf[64];

    if (counter & current_disasm.post_op_flags) {
      switch (counter) {
        case REG_B:
          sprintf(buf, "B=$%02X", cpu->reg.bc.byte.hi);
          break;

        case REG_C:
          sprintf(buf, "C=$%02X", cpu->reg.bc.byte.lo);
          break;

        case REG_D:
          sprintf(buf, "D=$%02X", cpu->reg.de.byte.hi);
          break;

        case REG_E:
          sprintf(buf, "E=$%02X", cpu->reg.de.byte.lo);
          break;

        case REG_F:
          sprintf(buf, "F=$%02X", cpu->reg.af.byte.lo);
          break;

        case REG_H:
          sprintf(buf, "H=$%02X", cpu->reg.hl.byte.hi);
          break;

        case REG_L:
          sprintf(buf, "L=$%02X", cpu->reg.hl.byte.lo);
          break;

        case REG_A:
          sprintf(buf, "A=$%02X", cpu->reg.af.byte.hi);
          break;

        case REG_BC:
          sprintf(buf, "BC=$%04X", cpu->reg.bc.value);
          break;

        case REG_DE:
          sprintf(buf, "DE=$%04X", cpu->reg.de.value);
          break;

        case REG_HL:
          sprintf(buf, "HL=$%04X", cpu->reg.hl.value);
          break;

        case REG_AF:
          sprintf(buf, "AF=$%04X", cpu->reg.af.value);
          break;

        case REG_SP:
          sprintf(buf, "SP=$%04X", cpu->reg.sp);
          break;

        case REG_MEM_IMM16: {
          const uint16_t npc = cpu->reg.pc - 2;

          const uint8_t lo = libyagbe_bus_read_memory(bus, npc);
          const uint8_t hi = libyagbe_bus_read_memory(bus, npc + 1);

          const uint16_t address = (uint16_t)((hi << 8) | lo);
          const uint8_t data = libyagbe_bus_read_memory(bus, address);

          sprintf(buf, "[$%04X]=$%02X", address, data);
          break;
        }
      }
      strcat(current_disasm.disasm_result, buf);
      strcat(current_disasm.disasm_result, ", ");
    }
  }
  return current_disasm.disasm_result;
}
