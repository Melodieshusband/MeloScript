#ifndef MELO_OPCODES_H
#define MELO_OPCODES_H

#include <stdint.h>

typedef enum {
    OP_NOP = 0,

    OP_LOADK,
    OP_LOADNIL,
    OP_MOVE,

    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_MOD,
    OP_NEG,
    OP_NOT,

    OP_ADDI,
    OP_SUBI,
    OP_MULI,
    OP_ADDF,
    OP_SUBF,
    OP_MULF,
    OP_DIVF,

    OP_VADD,
    OP_VSUB,
    OP_VMUL,
    OP_VDOT,
    OP_VCROSS,
    OP_VNORM,
    OP_VSCALE,
    OP_VGET,
    OP_VSET,
    OP_VZERO,

    OP_EQ,
    OP_LT,
    OP_LE,

    OP_JMP,
    OP_JMPIF,
    OP_JMPIFNOT,

    OP_CALL,
    OP_NATIVECALL,
    OP_YIELD,
    OP_RET,
    OP_RETNIL,

    OP_NEWOBJECT,
    OP_GETFIELD,
    OP_SETFIELD,
    OP_NEWARRAY,
    OP_GETINDEX,
    OP_SETINDEX,
    OP_LEN,

    OP_NEWOBJECT_SCRATCH,
    OP_NEWARRAY_SCRATCH,

    OP_VRAM_ALLOC,
    OP_VRAM_UPLOAD,
    OP_VRAM_EVICT,
    OP_VRAM_RESIDENT,

    OP_NEWARRAY_SOA,
    OP_SOA_GETFIELD,
    OP_SOA_SETFIELD,
    OP_SOA_LEN,

    OP_ARENA_ENTER,
    OP_ARENA_EXIT,

    OP_COROCREATE,
    OP_RESUME,
    OP_CORO_STATUS,

    OP_PRINT,

    OP_HALT,

    OP_COUNT
} MeloOpcode;

typedef uint32_t MeloInstr;

#define INSTR_OP(i)   ((i) & 0xFF)
#define INSTR_A(i)    (((i) >> 8) & 0xFF)
#define INSTR_B(i)    (((i) >> 16) & 0xFF)
#define INSTR_C(i)    (((i) >> 24) & 0xFF)
#define INSTR_BX(i)   (((i) >> 16) & 0xFFFF)

#define MAKE_ABC(op, a, b, c) \
    ((uint32_t)(op) | ((uint32_t)(a) << 8) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 24))

#define MAKE_ABX(op, a, bx) \
    ((uint32_t)(op) | ((uint32_t)(a) << 8) | ((uint32_t)(bx) << 16))

static inline __attribute__((unused)) const char *melo_opcode_name(MeloOpcode op) {
    static const char *names[] = {
        "NOP", "LOADK", "LOADNIL", "MOVE",
        "ADD", "SUB", "MUL", "DIV", "MOD", "NEG", "NOT",
        "ADDI", "SUBI", "MULI", "ADDF", "SUBF", "MULF", "DIVF",
        "VADD", "VSUB", "VMUL", "VDOT", "VCROSS", "VNORM", "VSCALE", "VGET", "VSET", "VZERO",
        "EQ", "LT", "LE",
        "JMP", "JMPIF", "JMPIFNOT",
        "CALL", "NATIVECALL", "YIELD", "RET", "RETNIL",
        "NEWOBJECT", "GETFIELD", "SETFIELD", "NEWARRAY", "GETINDEX", "SETINDEX", "LEN",
        "NEWOBJECT_SCRATCH", "NEWARRAY_SCRATCH",
        "VRAM_ALLOC", "VRAM_UPLOAD", "VRAM_EVICT", "VRAM_RESIDENT",
        "NEWARRAY_SOA", "SOA_GETFIELD", "SOA_SETFIELD", "SOA_LEN",
        "ARENA_ENTER", "ARENA_EXIT",
        "COROCREATE", "RESUME", "CORO_STATUS",
        "PRINT",
        "HALT"
    };
    if (op < OP_COUNT) return names[op];
    return "UNKNOWN";
}

#endif
