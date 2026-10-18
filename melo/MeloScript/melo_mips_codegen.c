#include "melo_mips_codegen.h"
#include "melo_opcodes.h"
#include "melo_value.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static void emit_escaped_string(FILE *out, const char *s) {
    fputc('"', out);
    while (*s) {
        if (*s == '"') fputs("\\\"", out);
        else if (*s == '\\') fputs("\\\\", out);
        else if (*s == '\n') fputs("\\n", out);
        else if (*s == '\r') fputs("\\r", out);
        else if (*s == '\t') fputs("\\t", out);
        else fputc(*s, out);
        s++;
    }
    fputc('"', out);
}

int melo_mips_emit_assembly(const MeloCompiledModule *mod,
                            const char **native_names,
                            int native_count,
                            FILE *out,
                            char *err,
                            size_t err_cap) {
    (void)native_names;
    (void)native_count;

    if (!mod || !out) {
        if (err && err_cap > 0) snprintf(err, err_cap, "invalid module or output file");
        return -1;
    }

    fputs("    .set at\n", out);
    fputs("    .set noreorder\n\n", out);

    fputs("    .data\n", out);
    fputs("    .align 4\n", out);

    for (int i = 0; i < mod->const_count; i++) {
        if (mod->consts[i].type == MELO_STRING && mod->consts[i].as.str) {
            uint32_t slen = mod->consts[i].as.str->length;
            uint32_t shash = mod->consts[i].as.str->hash;
            fprintf(out, "melo_str_raw_%d:\n", i);
            fputs("    .asciiz ", out);
            emit_escaped_string(out, mod->consts[i].as.str->data);
            fputs("\n    .align 4\n", out);

            fprintf(out, "melo_str_obj_%d:\n", i);
            fprintf(out, "    .word %u\n", slen);
            fprintf(out, "    .word %u\n", shash);
            fputs("    .word 1\n", out);
            fprintf(out, "    .word melo_str_raw_%d\n", i);
            fputs("    .align 4\n\n", out);
        }
    }

    fputs("    .globl melo_func_table\n", out);
    fputs("melo_func_table:\n", out);
    for (int f = 0; f < mod->func_count; f++) {
        fprintf(out, "    .word melo_func_%d\n", f);
    }
    if (mod->func_count == 0) {
        fputs("    .word 0\n", out);
    }
    fputs("    .align 4\n\n", out);

    fputs("    .text\n\n", out);

    for (int f = 0; f < mod->func_count; f++) {
        int start_pc = (int)(mod->funcs[f].entry - mod->code);
        int end_pc = (f + 1 < mod->func_count) ? (int)(mod->funcs[f + 1].entry - mod->code) : mod->code_count;
        int num_regs = mod->funcs[f].num_regs;
        if (num_regs < 1) num_regs = 1;

        int frame_size = ((num_regs * 32) + 32 + 15) & ~15;
        int ra_offset = frame_size - 4;
        int fp_offset = frame_size - 8;

        int is_main = (f == mod->main_func_id || strcmp(mod->funcs[f].name, "main") == 0);

        if (is_main) {
            fputs("    .globl melo_user_main\n", out);
            fprintf(out, "    .globl melo_func_%d\n", f);
            fputs("    .ent melo_user_main\n", out);
            fputs("melo_user_main:\n", out);
            fprintf(out, "melo_func_%d:\n", f);
        } else {
            fprintf(out, "    .globl melo_func_%d\n", f);
            fprintf(out, "    .ent melo_func_%d\n", f);
            fprintf(out, "melo_func_%d:\n", f);
            if (mod->funcs[f].name[0] != '\0' && strcmp(mod->funcs[f].name, "main") != 0) {
                fprintf(out, "    .globl melo_f_%s\n", mod->funcs[f].name);
                fprintf(out, "melo_f_%s:\n", mod->funcs[f].name);
            }
        }

        fprintf(out, "    addiu $sp, $sp, -%d\n", frame_size);
        fprintf(out, "    sw $ra, %d($sp)\n", ra_offset);
        fprintf(out, "    sw $fp, %d($sp)\n", fp_offset);
        fputs("    move $fp, $sp\n", out);

        int num_params = mod->funcs[f].num_params;
        for (int p = 0; p < num_params; p++) {
            fprintf(out, "    lw $t0, %d($a0)\n", p * 32);
            fprintf(out, "    sw $t0, %d($fp)\n", p * 32);
            fprintf(out, "    lw $t1, %d($a0)\n", p * 32 + 16);
            fprintf(out, "    sw $t1, %d($fp)\n", p * 32 + 16);
            fprintf(out, "    lqc2 $vf1, %d($a0)\n", p * 32 + 16);
            fprintf(out, "    sqc2 $vf1, %d($fp)\n", p * 32 + 16);
        }

        for (int pc = start_pc; pc < end_pc; pc++) {
            fprintf(out, "melo_pc_%d:\n", pc);
            MeloInstr instr = mod->code[pc];
            MeloOpcode op = (MeloOpcode)INSTR_OP(instr);
            int a = INSTR_A(instr);
            int b = INSTR_B(instr);
            int c = INSTR_C(instr);
            int16_t bx = (int16_t)INSTR_BX(instr);

            switch (op) {
                case OP_NOP:
                    fputs("    nop\n", out);
                    break;

                case OP_LOADK: {
                    uint16_t k = (uint16_t)bx;
                    if (k < mod->const_count) {
                        if (mod->consts[k].type == MELO_INT32) {
                            fputs("    li $t0, 1\n", out);
                            fprintf(out, "    sw $t0, %d($fp)\n", a * 32);
                            fprintf(out, "    li $t1, %d\n", (int)mod->consts[k].as.i32);
                            fprintf(out, "    sw $t1, %d($fp)\n", a * 32 + 16);
                        } else if (mod->consts[k].type == MELO_FLOAT) {
                            union { float f; uint32_t u; } fu;
                            fu.f = mod->consts[k].as.f32;
                            fputs("    li $t0, 2\n", out);
                            fprintf(out, "    sw $t0, %d($fp)\n", a * 32);
                            fprintf(out, "    li $t1, 0x%08X\n", fu.u);
                            fprintf(out, "    sw $t1, %d($fp)\n", a * 32 + 16);
                        } else if (mod->consts[k].type == MELO_STRING) {
                            fputs("    li $t0, 6\n", out);
                            fprintf(out, "    sw $t0, %d($fp)\n", a * 32);
                            fprintf(out, "    la $t1, melo_str_obj_%d\n", k);
                            fprintf(out, "    sw $t1, %d($fp)\n", a * 32 + 16);
                        } else {
                            fprintf(out, "    sw $zero, %d($fp)\n", a * 32);
                            fprintf(out, "    sw $zero, %d($fp)\n", a * 32 + 16);
                        }
                    } else {
                        fprintf(out, "    sw $zero, %d($fp)\n", a * 32);
                        fprintf(out, "    sw $zero, %d($fp)\n", a * 32 + 16);
                    }
                    break;
                }

                case OP_LOADNIL:
                    fprintf(out, "    sw $zero, %d($fp)\n", a * 32);
                    fprintf(out, "    sw $zero, %d($fp)\n", a * 32 + 16);
                    break;

                case OP_MOVE:
                    fprintf(out, "    lw $t0, %d($fp)\n", b * 32);
                    fprintf(out, "    sw $t0, %d($fp)\n", a * 32);
                    fprintf(out, "    lw $t1, %d($fp)\n", b * 32 + 16);
                    fprintf(out, "    sw $t1, %d($fp)\n", a * 32 + 16);
                    fprintf(out, "    lqc2 $vf1, %d($fp)\n", b * 32 + 16);
                    fprintf(out, "    sqc2 $vf1, %d($fp)\n", a * 32 + 16);
                    break;

                case OP_ADD:
                case OP_ADDI:
                    fprintf(out, "    lw $t0, %d($fp)\n", b * 32 + 16);
                    fprintf(out, "    lw $t1, %d($fp)\n", c * 32 + 16);
                    fputs("    addu $t2, $t0, $t1\n", out);
                    fputs("    li $t3, 1\n", out);
                    fprintf(out, "    sw $t3, %d($fp)\n", a * 32);
                    fprintf(out, "    sw $t2, %d($fp)\n", a * 32 + 16);
                    break;

                case OP_SUB:
                case OP_SUBI:
                    fprintf(out, "    lw $t0, %d($fp)\n", b * 32 + 16);
                    fprintf(out, "    lw $t1, %d($fp)\n", c * 32 + 16);
                    fputs("    subu $t2, $t0, $t1\n", out);
                    fputs("    li $t3, 1\n", out);
                    fprintf(out, "    sw $t3, %d($fp)\n", a * 32);
                    fprintf(out, "    sw $t2, %d($fp)\n", a * 32 + 16);
                    break;

                case OP_MUL:
                case OP_MULI:
                    fprintf(out, "    lw $t0, %d($fp)\n", b * 32 + 16);
                    fprintf(out, "    lw $t1, %d($fp)\n", c * 32 + 16);
                    fputs("    mult $t0, $t1\n", out);
                    fputs("    mflo $t2\n", out);
                    fputs("    li $t3, 1\n", out);
                    fprintf(out, "    sw $t3, %d($fp)\n", a * 32);
                    fprintf(out, "    sw $t2, %d($fp)\n", a * 32 + 16);
                    break;

                case OP_DIV:
                    fprintf(out, "    lw $t0, %d($fp)\n", b * 32 + 16);
                    fprintf(out, "    lw $t1, %d($fp)\n", c * 32 + 16);
                    fprintf(out, "    bnez $t1, melo_div_ok_%d\n", pc);
                    fputs("    nop\n", out);
                    fputs("    move $t2, $zero\n", out);
                    fprintf(out, "    b melo_div_done_%d\n", pc);
                    fputs("    nop\n", out);
                    fprintf(out, "melo_div_ok_%d:\n", pc);
                    fputs("    div $zero, $t0, $t1\n", out);
                    fputs("    mflo $t2\n", out);
                    fprintf(out, "melo_div_done_%d:\n", pc);
                    fputs("    li $t3, 1\n", out);
                    fprintf(out, "    sw $t3, %d($fp)\n", a * 32);
                    fprintf(out, "    sw $t2, %d($fp)\n", a * 32 + 16);
                    break;

                case OP_MOD:
                    fprintf(out, "    lw $t0, %d($fp)\n", b * 32 + 16);
                    fprintf(out, "    lw $t1, %d($fp)\n", c * 32 + 16);
                    fprintf(out, "    bnez $t1, melo_mod_ok_%d\n", pc);
                    fputs("    nop\n", out);
                    fputs("    move $t2, $zero\n", out);
                    fprintf(out, "    b melo_mod_done_%d\n", pc);
                    fputs("    nop\n", out);
                    fprintf(out, "melo_mod_ok_%d:\n", pc);
                    fputs("    div $zero, $t0, $t1\n", out);
                    fputs("    mfhi $t2\n", out);
                    fprintf(out, "melo_mod_done_%d:\n", pc);
                    fputs("    li $t3, 1\n", out);
                    fprintf(out, "    sw $t3, %d($fp)\n", a * 32);
                    fprintf(out, "    sw $t2, %d($fp)\n", a * 32 + 16);
                    break;

                case OP_NEG:
                    fprintf(out, "    lw $t0, %d($fp)\n", b * 32);
                    fprintf(out, "    sw $t0, %d($fp)\n", a * 32);
                    fprintf(out, "    lw $t1, %d($fp)\n", b * 32 + 16);
                    fputs("    negu $t2, $t1\n", out);
                    fprintf(out, "    sw $t2, %d($fp)\n", a * 32 + 16);
                    break;

                case OP_NOT:
                    fprintf(out, "    lw $t0, %d($fp)\n", b * 32);
                    fprintf(out, "    beqz $t0, melo_not_true_%d\n", pc);
                    fputs("    nop\n", out);
                    fprintf(out, "    lw $t1, %d($fp)\n", b * 32 + 16);
                    fprintf(out, "    beqz $t1, melo_not_true_%d\n", pc);
                    fputs("    nop\n", out);
                    fputs("    move $t2, $zero\n", out);
                    fprintf(out, "    b melo_not_done_%d\n", pc);
                    fputs("    nop\n", out);
                    fprintf(out, "melo_not_true_%d:\n", pc);
                    fputs("    li $t2, 1\n", out);
                    fprintf(out, "melo_not_done_%d:\n", pc);
                    fputs("    li $t3, 1\n", out);
                    fprintf(out, "    sw $t3, %d($fp)\n", a * 32);
                    fprintf(out, "    sw $t2, %d($fp)\n", a * 32 + 16);
                    break;

                case OP_ADDF:
                    fprintf(out, "    lwc1 $f0, %d($fp)\n", b * 32 + 16);
                    fprintf(out, "    lwc1 $f1, %d($fp)\n", c * 32 + 16);
                    fputs("    add.s $f2, $f0, $f1\n", out);
                    fputs("    li $t0, 2\n", out);
                    fprintf(out, "    sw $t0, %d($fp)\n", a * 32);
                    fprintf(out, "    swc1 $f2, %d($fp)\n", a * 32 + 16);
                    break;

                case OP_SUBF:
                    fprintf(out, "    lwc1 $f0, %d($fp)\n", b * 32 + 16);
                    fprintf(out, "    lwc1 $f1, %d($fp)\n", c * 32 + 16);
                    fputs("    sub.s $f2, $f0, $f1\n", out);
                    fputs("    li $t0, 2\n", out);
                    fprintf(out, "    sw $t0, %d($fp)\n", a * 32);
                    fprintf(out, "    swc1 $f2, %d($fp)\n", a * 32 + 16);
                    break;

                case OP_MULF:
                    fprintf(out, "    lwc1 $f0, %d($fp)\n", b * 32 + 16);
                    fprintf(out, "    lwc1 $f1, %d($fp)\n", c * 32 + 16);
                    fputs("    mul.s $f2, $f0, $f1\n", out);
                    fputs("    li $t0, 2\n", out);
                    fprintf(out, "    sw $t0, %d($fp)\n", a * 32);
                    fprintf(out, "    swc1 $f2, %d($fp)\n", a * 32 + 16);
                    break;

                case OP_DIVF:
                    fprintf(out, "    lwc1 $f0, %d($fp)\n", b * 32 + 16);
                    fprintf(out, "    lwc1 $f1, %d($fp)\n", c * 32 + 16);
                    fputs("    div.s $f2, $f0, $f1\n", out);
                    fputs("    li $t0, 2\n", out);
                    fprintf(out, "    sw $t0, %d($fp)\n", a * 32);
                    fprintf(out, "    swc1 $f2, %d($fp)\n", a * 32 + 16);
                    break;

                case OP_EQ:
                    fprintf(out, "    lw $t0, %d($fp)\n", b * 32 + 16);
                    fprintf(out, "    lw $t1, %d($fp)\n", c * 32 + 16);
                    fputs("    xor $t2, $t0, $t1\n", out);
                    fputs("    sltiu $t2, $t2, 1\n", out);
                    fputs("    li $t3, 1\n", out);
                    fprintf(out, "    sw $t3, %d($fp)\n", a * 32);
                    fprintf(out, "    sw $t2, %d($fp)\n", a * 32 + 16);
                    break;

                case OP_LT:
                    fprintf(out, "    lw $t0, %d($fp)\n", b * 32 + 16);
                    fprintf(out, "    lw $t1, %d($fp)\n", c * 32 + 16);
                    fputs("    slt $t2, $t0, $t1\n", out);
                    fputs("    li $t3, 1\n", out);
                    fprintf(out, "    sw $t3, %d($fp)\n", a * 32);
                    fprintf(out, "    sw $t2, %d($fp)\n", a * 32 + 16);
                    break;

                case OP_LE:
                    fprintf(out, "    lw $t0, %d($fp)\n", b * 32 + 16);
                    fprintf(out, "    lw $t1, %d($fp)\n", c * 32 + 16);
                    fputs("    slt $t2, $t1, $t0\n", out);
                    fputs("    xori $t2, $t2, 1\n", out);
                    fputs("    li $t3, 1\n", out);
                    fprintf(out, "    sw $t3, %d($fp)\n", a * 32);
                    fprintf(out, "    sw $t2, %d($fp)\n", a * 32 + 16);
                    break;

                case OP_JMP: {
                    int target = pc + bx;
                    fprintf(out, "    j melo_pc_%d\n", target);
                    fputs("    nop\n", out);
                    break;
                }

                case OP_JMPIF: {
                    int target = pc + bx;
                    fprintf(out, "    lw $t0, %d($fp)\n", a * 32);
                    fprintf(out, "    beqz $t0, melo_jmp_skip_%d\n", pc);
                    fputs("    nop\n", out);
                    fprintf(out, "    lw $t1, %d($fp)\n", a * 32 + 16);
                    fprintf(out, "    bnez $t1, melo_pc_%d\n", target);
                    fputs("    nop\n", out);
                    fprintf(out, "melo_jmp_skip_%d:\n", pc);
                    break;
                }

                case OP_JMPIFNOT: {
                    int target = pc + bx;
                    fprintf(out, "    lw $t0, %d($fp)\n", a * 32);
                    fprintf(out, "    beqz $t0, melo_pc_%d\n", target);
                    fputs("    nop\n", out);
                    fprintf(out, "    lw $t1, %d($fp)\n", a * 32 + 16);
                    fprintf(out, "    beqz $t1, melo_pc_%d\n", target);
                    fputs("    nop\n", out);
                    break;
                }

                case OP_CALL:
                    fprintf(out, "    addiu $a0, $fp, %d\n", c * 32);
                    fprintf(out, "    jal melo_func_%d\n", b);
                    fputs("    nop\n", out);
                    fprintf(out, "    sw $v0, %d($fp)\n", a * 32);
                    fprintf(out, "    sw $v1, %d($fp)\n", a * 32 + 16);
                    fprintf(out, "    sqc2 $vf1, %d($fp)\n", a * 32 + 16);
                    break;

                case OP_NATIVECALL: {
                    uint32_t arg_count = mod->code[pc + 1];
                    pc++;
                    fprintf(out, "    li $a0, %d\n", b);
                    fprintf(out, "    addiu $a1, $fp, %d\n", c * 32);
                    fprintf(out, "    li $a2, %u\n", arg_count);
                    fprintf(out, "    addiu $a3, $fp, %d\n", a * 32);
                    fputs("    jal melo_runtime_native_call\n", out);
                    fputs("    nop\n", out);
                    break;
                }

                case OP_RET:
                    fprintf(out, "    lw $v0, %d($fp)\n", a * 32);
                    fprintf(out, "    lw $v1, %d($fp)\n", a * 32 + 16);
                    fprintf(out, "    lqc2 $vf1, %d($fp)\n", a * 32 + 16);
                    fprintf(out, "    j melo_func_%d_ret\n", f);
                    fputs("    nop\n", out);
                    break;

                case OP_RETNIL:
                    fputs("    move $v0, $zero\n", out);
                    fputs("    move $v1, $zero\n", out);
                    fprintf(out, "    j melo_func_%d_ret\n", f);
                    fputs("    nop\n", out);
                    break;

                case OP_NEWARRAY:
                case OP_NEWARRAY_SCRATCH:
                    fprintf(out, "    li $a0, %d\n", (int)bx);
                    fputs("    jal melo_runtime_alloc_array\n", out);
                    fputs("    nop\n", out);
                    fputs("    li $t0, 8\n", out);
                    fprintf(out, "    sw $t0, %d($fp)\n", a * 32);
                    fprintf(out, "    sw $v0, %d($fp)\n", a * 32 + 16);
                    break;

                case OP_GETINDEX:
                    fprintf(out, "    lw $t0, %d($fp)\n", b * 32 + 16);
                    fprintf(out, "    lw $t1, %d($fp)\n", c * 32 + 16);
                    fputs("    sll $t2, $t1, 5\n", out);
                    fputs("    addu $t0, $t0, $t2\n", out);
                    fputs("    addiu $t0, $t0, 16\n", out);
                    fputs("    lw $t1, 0($t0)\n", out);
                    fprintf(out, "    sw $t1, %d($fp)\n", a * 32);
                    fputs("    lw $t2, 16($t0)\n", out);
                    fprintf(out, "    sw $t2, %d($fp)\n", a * 32 + 16);
                    fputs("    lqc2 $vf1, 16($t0)\n", out);
                    fprintf(out, "    sqc2 $vf1, %d($fp)\n", a * 32 + 16);
                    break;

                case OP_SETINDEX:
                    fprintf(out, "    lw $t0, %d($fp)\n", a * 32 + 16);
                    fprintf(out, "    lw $t1, %d($fp)\n", b * 32 + 16);
                    fputs("    sll $t2, $t1, 5\n", out);
                    fputs("    addu $t0, $t0, $t2\n", out);
                    fputs("    addiu $t0, $t0, 16\n", out);
                    fprintf(out, "    lw $t1, %d($fp)\n", c * 32);
                    fputs("    sw $t1, 0($t0)\n", out);
                    fprintf(out, "    lw $t2, %d($fp)\n", c * 32 + 16);
                    fputs("    sw $t2, 16($t0)\n", out);
                    fprintf(out, "    lqc2 $vf1, %d($fp)\n", c * 32 + 16);
                    fputs("    sqc2 $vf1, 16($t0)\n", out);
                    break;

                case OP_LEN:
                    fprintf(out, "    lw $t0, %d($fp)\n", b * 32 + 16);
                    fputs("    lw $t1, 0($t0)\n", out);
                    fputs("    li $t2, 1\n", out);
                    fprintf(out, "    sw $t2, %d($fp)\n", a * 32);
                    fprintf(out, "    sw $t1, %d($fp)\n", a * 32 + 16);
                    break;

                case OP_NEWOBJECT:
                case OP_NEWOBJECT_SCRATCH: {
                    int fld_count = (b >= 0 && b < mod->struct_count) ? mod->structs[b].field_count : 0;
                    fprintf(out, "    li $a0, %d\n", b);
                    fprintf(out, "    li $a1, %d\n", fld_count);
                    fputs("    jal melo_runtime_alloc_object\n", out);
                    fputs("    nop\n", out);
                    fputs("    li $t0, 7\n", out);
                    fprintf(out, "    sw $t0, %d($fp)\n", a * 32);
                    fprintf(out, "    sw $v0, %d($fp)\n", a * 32 + 16);
                    break;
                }

                case OP_GETFIELD:
                    fprintf(out, "    lw $t0, %d($fp)\n", b * 32 + 16);
                    fprintf(out, "    li $t1, %d\n", c);
                    fputs("    sll $t1, $t1, 5\n", out);
                    fputs("    addu $t0, $t0, $t1\n", out);
                    fputs("    addiu $t0, $t0, 16\n", out);
                    fputs("    lw $t1, 0($t0)\n", out);
                    fprintf(out, "    sw $t1, %d($fp)\n", a * 32);
                    fputs("    lw $t2, 16($t0)\n", out);
                    fprintf(out, "    sw $t2, %d($fp)\n", a * 32 + 16);
                    fputs("    lqc2 $vf1, 16($t0)\n", out);
                    fprintf(out, "    sqc2 $vf1, %d($fp)\n", a * 32 + 16);
                    break;

                case OP_SETFIELD:
                    fprintf(out, "    lw $t0, %d($fp)\n", a * 32 + 16);
                    fprintf(out, "    li $t1, %d\n", b);
                    fputs("    sll $t1, $t1, 5\n", out);
                    fputs("    addu $t0, $t0, $t1\n", out);
                    fputs("    addiu $t0, $t0, 16\n", out);
                    fprintf(out, "    lw $t1, %d($fp)\n", c * 32);
                    fputs("    sw $t1, 0($t0)\n", out);
                    fprintf(out, "    lw $t2, %d($fp)\n", c * 32 + 16);
                    fputs("    sw $t2, 16($t0)\n", out);
                    fprintf(out, "    lqc2 $vf1, %d($fp)\n", c * 32 + 16);
                    fputs("    sqc2 $vf1, 16($t0)\n", out);
                    break;

                case OP_VZERO:
                    fprintf(out, "    li $t0, %d\n", (b == 2 ? 3 : (b == 3 ? 4 : 5)));
                    fprintf(out, "    sw $t0, %d($fp)\n", a * 32);
                    fputs("    vsub.xyzw $vf1, $vf0, $vf0\n", out);
                    fprintf(out, "    sqc2 $vf1, %d($fp)\n", a * 32 + 16);
                    break;

                case OP_VADD:
                    fprintf(out, "    lqc2 $vf1, %d($fp)\n", b * 32 + 16);
                    fprintf(out, "    lqc2 $vf2, %d($fp)\n", c * 32 + 16);
                    fputs("    vadd.xyzw $vf3, $vf1, $vf2\n", out);
                    fputs("    li $t0, 5\n", out);
                    fprintf(out, "    sw $t0, %d($fp)\n", a * 32);
                    fprintf(out, "    sqc2 $vf3, %d($fp)\n", a * 32 + 16);
                    break;

                case OP_VSUB:
                    fprintf(out, "    lqc2 $vf1, %d($fp)\n", b * 32 + 16);
                    fprintf(out, "    lqc2 $vf2, %d($fp)\n", c * 32 + 16);
                    fputs("    vsub.xyzw $vf3, $vf1, $vf2\n", out);
                    fputs("    li $t0, 5\n", out);
                    fprintf(out, "    sw $t0, %d($fp)\n", a * 32);
                    fprintf(out, "    sqc2 $vf3, %d($fp)\n", a * 32 + 16);
                    break;

                case OP_VMUL:
                    fprintf(out, "    lqc2 $vf1, %d($fp)\n", b * 32 + 16);
                    fprintf(out, "    lqc2 $vf2, %d($fp)\n", c * 32 + 16);
                    fputs("    vmul.xyzw $vf3, $vf1, $vf2\n", out);
                    fputs("    li $t0, 5\n", out);
                    fprintf(out, "    sw $t0, %d($fp)\n", a * 32);
                    fprintf(out, "    sqc2 $vf3, %d($fp)\n", a * 32 + 16);
                    break;

                case OP_VDOT:
                    fprintf(out, "    addiu $a0, $fp, %d\n", b * 32);
                    fprintf(out, "    addiu $a1, $fp, %d\n", c * 32);
                    fprintf(out, "    addiu $a2, $fp, %d\n", a * 32);
                    fputs("    jal melo_runtime_vdot\n", out);
                    fputs("    nop\n", out);
                    break;

                case OP_VCROSS:
                    fprintf(out, "    addiu $a0, $fp, %d\n", b * 32);
                    fprintf(out, "    addiu $a1, $fp, %d\n", c * 32);
                    fprintf(out, "    addiu $a2, $fp, %d\n", a * 32);
                    fputs("    jal melo_runtime_vcross\n", out);
                    fputs("    nop\n", out);
                    break;

                case OP_VNORM:
                    fprintf(out, "    addiu $a0, $fp, %d\n", b * 32);
                    fprintf(out, "    addiu $a1, $fp, %d\n", a * 32);
                    fputs("    jal melo_runtime_vnorm\n", out);
                    fputs("    nop\n", out);
                    break;

                case OP_VSCALE:
                    fprintf(out, "    addiu $a0, $fp, %d\n", b * 32);
                    fprintf(out, "    addiu $a1, $fp, %d\n", c * 32);
                    fprintf(out, "    addiu $a2, $fp, %d\n", a * 32);
                    fputs("    jal melo_runtime_vscale\n", out);
                    fputs("    nop\n", out);
                    break;

                case OP_VGET:
                    fprintf(out, "    lwc1 $f0, %d($fp)\n", b * 32 + 16 + c * 4);
                    fputs("    li $t0, 2\n", out);
                    fprintf(out, "    sw $t0, %d($fp)\n", a * 32);
                    fprintf(out, "    swc1 $f0, %d($fp)\n", a * 32 + 16);
                    break;

                case OP_VSET:
                    fprintf(out, "    lw $t0, %d($fp)\n", c * 32 + 16);
                    fprintf(out, "    sw $t0, %d($fp)\n", a * 32 + 16 + b * 4);
                    break;

                case OP_ARENA_ENTER:
                    fputs("    jal melo_runtime_arena_enter\n", out);
                    fputs("    nop\n", out);
                    break;

                case OP_ARENA_EXIT:
                    fputs("    jal melo_runtime_arena_exit\n", out);
                    fputs("    nop\n", out);
                    break;

                case OP_NEWARRAY_SOA: {
                    uint32_t capacity = mod->code[pc + 1];
                    pc++;
                    int fld_count = (b >= 0 && b < mod->struct_count) ? mod->structs[b].field_count : 0;
                    fprintf(out, "    li $a0, %d\n", b);
                    fprintf(out, "    li $a1, %d\n", fld_count);
                    fprintf(out, "    li $a2, %u\n", capacity);
                    fputs("    jal melo_runtime_alloc_soa\n", out);
                    fputs("    nop\n", out);
                    fputs("    li $t0, 9\n", out);
                    fprintf(out, "    sw $t0, %d($fp)\n", a * 32);
                    fprintf(out, "    sw $v0, %d($fp)\n", a * 32 + 16);
                    break;
                }

                case OP_SOA_GETFIELD: {
                    uint32_t field_idx = mod->code[pc + 1];
                    pc++;
                    int field_type = 2;
                    if (mod && b >= 0 && b < mod->struct_count && (int)field_idx < mod->structs[b].field_count) {
                        field_type = mod->structs[b].field_types[field_idx];
                    }
                    fprintf(out, "    lw $a0, %d($fp)\n", b * 32 + 16);
                    fprintf(out, "    lw $a1, %d($fp)\n", c * 32 + 16);
                    fprintf(out, "    li $a2, %u\n", field_idx);
                    fprintf(out, "    li $a3, %d\n", field_type);
                    fprintf(out, "    addiu $t0, $fp, %d\n", a * 32);
                    fputs("    addiu $sp, $sp, -16\n", out);
                    fputs("    sw $t0, 16($sp)\n", out);
                    fputs("    jal melo_runtime_soa_get\n", out);
                    fputs("    nop\n", out);
                    fputs("    addiu $sp, $sp, 16\n", out);
                    break;
                }

                case OP_SOA_SETFIELD: {
                    uint32_t field_idx = mod->code[pc + 1];
                    pc++;
                    int field_type = 2;
                    fprintf(out, "    lw $a0, %d($fp)\n", a * 32 + 16);
                    fprintf(out, "    lw $a1, %d($fp)\n", b * 32 + 16);
                    fprintf(out, "    li $a2, %u\n", field_idx);
                    fprintf(out, "    li $a3, %d\n", field_type);
                    fprintf(out, "    addiu $t0, $fp, %d\n", c * 32);
                    fputs("    addiu $sp, $sp, -16\n", out);
                    fputs("    sw $t0, 16($sp)\n", out);
                    fputs("    jal melo_runtime_soa_set\n", out);
                    fputs("    nop\n", out);
                    fputs("    addiu $sp, $sp, 16\n", out);
                    break;
                }

                case OP_SOA_LEN:
                    fprintf(out, "    lw $t0, %d($fp)\n", b * 32 + 16);
                    fputs("    lw $t1, 8($t0)\n", out);
                    fputs("    li $t2, 1\n", out);
                    fprintf(out, "    sw $t2, %d($fp)\n", a * 32);
                    fprintf(out, "    sw $t1, %d($fp)\n", a * 32 + 16);
                    break;

                case OP_VRAM_ALLOC:
                    fprintf(out, "    lw $a0, %d($fp)\n", b * 32 + 16);
                    fprintf(out, "    lw $a1, %d($fp)\n", c * 32 + 16);
                    fputs("    jal melo_runtime_vram_alloc\n", out);
                    fputs("    nop\n", out);
                    fputs("    li $t0, 10\n", out);
                    fprintf(out, "    sw $t0, %d($fp)\n", a * 32);
                    fprintf(out, "    sw $v0, %d($fp)\n", a * 32 + 16);
                    break;

                case OP_VRAM_UPLOAD:
                    fprintf(out, "    lw $a0, %d($fp)\n", a * 32 + 16);
                    fputs("    jal melo_runtime_vram_upload\n", out);
                    fputs("    nop\n", out);
                    break;

                case OP_VRAM_EVICT:
                    fprintf(out, "    lw $a0, %d($fp)\n", a * 32 + 16);
                    fputs("    jal melo_runtime_vram_evict\n", out);
                    fputs("    nop\n", out);
                    break;

                case OP_VRAM_RESIDENT:
                    fprintf(out, "    lw $a0, %d($fp)\n", b * 32 + 16);
                    fputs("    jal melo_runtime_vram_resident\n", out);
                    fputs("    nop\n", out);
                    fputs("    li $t0, 1\n", out);
                    fprintf(out, "    sw $t0, %d($fp)\n", a * 32);
                    fprintf(out, "    sw $v0, %d($fp)\n", a * 32 + 16);
                    break;

                case OP_SCRATCH_DMA_IN:
                    fprintf(out, "    lw $a0, %d($fp)\n", b * 32 + 16);
                    fprintf(out, "    lw $a1, %d($fp)\n", c * 32 + 16);
                    fputs("    jal melo_runtime_scratch_dma_in\n", out);
                    fputs("    nop\n", out);
                    fputs("    li $t0, 1\n", out);
                    fprintf(out, "    sw $t0, %d($fp)\n", a * 32);
                    fprintf(out, "    sw $v0, %d($fp)\n", a * 32 + 16);
                    break;

                case OP_SCRATCH_DMA_OUT:
                    fprintf(out, "    lw $a0, %d($fp)\n", b * 32 + 16);
                    fprintf(out, "    lw $a1, %d($fp)\n", c * 32 + 16);
                    fputs("    jal melo_runtime_scratch_dma_out\n", out);
                    fputs("    nop\n", out);
                    fputs("    li $t0, 1\n", out);
                    fprintf(out, "    sw $t0, %d($fp)\n", a * 32);
                    fprintf(out, "    sw $v0, %d($fp)\n", a * 32 + 16);
                    break;

                case OP_CORO_CREATE:
                    if (c == 1) {
                        fprintf(out, "    lw $a0, %d($fp)\n", b * 32 + 16);
                    } else {
                        fprintf(out, "    li $a0, %d\n", b);
                    }
                    fputs("    jal melo_runtime_coro_create\n", out);
                    fputs("    nop\n", out);
                    fputs("    li $t0, 1\n", out);
                    fprintf(out, "    sw $t0, %d($fp)\n", a * 32);
                    fprintf(out, "    sw $v0, %d($fp)\n", a * 32 + 16);
                    break;

                case OP_CORO_RESUME:
                    fprintf(out, "    lw $a0, %d($fp)\n", b * 32 + 16);
                    if (c != 255) {
                        fprintf(out, "    addiu $a1, $fp, %d\n", c * 32);
                    } else {
                        fputs("    move $a1, $zero\n", out);
                    }
                    fprintf(out, "    addiu $a2, $fp, %d\n", a * 32);
                    fputs("    jal melo_runtime_coro_resume\n", out);
                    fputs("    nop\n", out);
                    break;

                case OP_CORO_STATUS:
                    fprintf(out, "    lw $a0, %d($fp)\n", b * 32 + 16);
                    fputs("    jal melo_runtime_coro_status\n", out);
                    fputs("    nop\n", out);
                    fputs("    li $t0, 1\n", out);
                    fprintf(out, "    sw $t0, %d($fp)\n", a * 32);
                    fprintf(out, "    sw $v0, %d($fp)\n", a * 32 + 16);
                    break;

                case OP_YIELD:
                    fprintf(out, "    addiu $a0, $fp, %d\n", b * 32);
                    fprintf(out, "    addiu $a1, $fp, %d\n", a * 32);
                    fputs("    jal melo_runtime_coro_yield\n", out);
                    fputs("    nop\n", out);
                    break;

                case OP_PRINT:
                    fprintf(out, "    addiu $a0, $fp, %d\n", a * 32);
                    fputs("    jal melo_runtime_print\n", out);
                    fputs("    nop\n", out);
                    break;

                case OP_HALT:
                    fprintf(out, "    j melo_func_%d_ret\n", f);
                    fputs("    nop\n", out);
                    break;

                default:
                    break;
            }
        }

        fprintf(out, "melo_func_%d_ret:\n", f);
        fputs("    move $sp, $fp\n", out);
        fprintf(out, "    lw $ra, %d($sp)\n", ra_offset);
        fprintf(out, "    lw $fp, %d($sp)\n", fp_offset);
        fprintf(out, "    addiu $sp, $sp, %d\n", frame_size);
        fputs("    jr $ra\n", out);
        fputs("    nop\n", out);
        if (is_main) {
            fputs("    .end melo_user_main\n\n", out);
        } else {
            fprintf(out, "    .end melo_func_%d\n\n", f);
        }
    }

    return 0;
}
