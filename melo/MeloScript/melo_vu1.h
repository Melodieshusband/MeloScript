#ifndef MELO_VU1_H
#define MELO_VU1_H

#include "melo_ast.h"
#include <stdint.h>
#include <stddef.h>

#define MELO_VU1_MAX_INSTR   1024
#define MELO_VU1_VF_COUNT    32
#define MELO_VU1_VI_COUNT    16

typedef enum {
    VU_UPPER_NOP,
    VU_UPPER_ADD,
    VU_UPPER_SUB,
    VU_UPPER_MUL,
    VU_UPPER_MULA,
    VU_UPPER_MADD,
    VU_UPPER_OPMULA,
    VU_UPPER_OPMSUB,
    VU_UPPER_MOVE,
    VU_UPPER_MR32
} MeloVuUpperOp;

typedef enum {
    VU_LOWER_NOP,
    VU_LOWER_IADDIU,
    VU_LOWER_ILW,
    VU_LOWER_ISW,
    VU_LOWER_LQ,
    VU_LOWER_SQ,
    VU_LOWER_FSSET,
    VU_LOWER_XTOP,
    VU_LOWER_XGKICK,
    VU_LOWER_B,
    VU_LOWER_IBEQ,
    VU_LOWER_ERET
} MeloVuLowerOp;

#define VU_DEST_X 1
#define VU_DEST_Y 2
#define VU_DEST_Z 4
#define VU_DEST_W 8
#define VU_DEST_XYZW (VU_DEST_X|VU_DEST_Y|VU_DEST_Z|VU_DEST_W)

typedef struct {
    MeloVuUpperOp upper_op;
    int dest_mask;
    int fd;
    int fs;
    int ft;

    MeloVuLowerOp lower_op;
    int id;
    int is_;
    int it;
    int16_t imm;

    int is_branch_target;
    int has_i_flag;
    int has_e_flag;
} MeloVuInstr;

typedef struct {
    char name[64];
    MeloVuInstr instrs[MELO_VU1_MAX_INSTR];
    int instr_count;

    int param_count;
    char param_names[8][32];
    int param_vf[8];

    float const_pool[64][4];
    int const_count;

    int result_vf;
    int result_addr;
    int has_error;
    char error_msg[256];

    size_t code_bytes;
    size_t data_bytes;
} MeloVu1Program;

int melo_vu1_compile(MeloFuncDecl *fn, MeloVu1Program *out);

void melo_vu1_emit_asm_listing(const MeloVu1Program *prog, char *buf, size_t buf_size);

void melo_vu1_emit_c_bridge(const MeloVu1Program *prog, char *buf, size_t buf_size);

void melo_vu1_encode_microcode(const MeloVu1Program *prog, uint64_t *out_words, int *out_count);

#endif
