#include "melo_vu1.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct {
    MeloVu1Program *prog;
    int next_vf;
    int next_vi;
    char slot_names[MELO_VU1_VF_COUNT][32];
    int slot_used[MELO_VU1_VF_COUNT];
    char local_names[32][32];
    int local_vf[32];
    int local_count;
} Vu1Ctx;

static void vu_error(MeloVu1Program *p, int line, const char *msg) {
    if (p->has_error) return;
    p->has_error = 1;
    snprintf(p->error_msg, sizeof(p->error_msg),
             "VU1 codegen error in '%s' (line %d): %s", p->name, line, msg);
}

static int vu_alloc_vf(Vu1Ctx *cx) {
    for (int i = cx->next_vf; i < MELO_VU1_VF_COUNT; i++) {
        if (!cx->slot_used[i]) {
            cx->slot_used[i] = 1;
            cx->next_vf = i + 1;
            return i;
        }
    }
    return -1;
}

static MeloVuInstr *push_instr(MeloVu1Program *p) {
    if (p->instr_count >= MELO_VU1_MAX_INSTR) return NULL;
    MeloVuInstr *ins = &p->instrs[p->instr_count++];
    memset(ins, 0, sizeof(*ins));
    return ins;
}

static void emit_vadd(MeloVu1Program *p, int fd, int fs, int ft) {
    MeloVuInstr *i = push_instr(p);
    if (!i) return;
    i->upper_op = VU_UPPER_ADD;
    i->dest_mask = VU_DEST_XYZW;
    i->fd = fd; i->fs = fs; i->ft = ft;
    i->lower_op = VU_LOWER_NOP;
}

static void emit_vsub(MeloVu1Program *p, int fd, int fs, int ft) {
    MeloVuInstr *i = push_instr(p);
    if (!i) return;
    i->upper_op = VU_UPPER_SUB;
    i->dest_mask = VU_DEST_XYZW;
    i->fd = fd; i->fs = fs; i->ft = ft;
    i->lower_op = VU_LOWER_NOP;
}

static void emit_vmul(MeloVu1Program *p, int fd, int fs, int ft) {
    MeloVuInstr *i = push_instr(p);
    if (!i) return;
    i->upper_op = VU_UPPER_MUL;
    i->dest_mask = VU_DEST_XYZW;
    i->fd = fd; i->fs = fs; i->ft = ft;
    i->lower_op = VU_LOWER_NOP;
}

static void emit_vmove(MeloVu1Program *p, int fd, int fs) {
    MeloVuInstr *i = push_instr(p);
    if (!i) return;
    i->upper_op = VU_UPPER_MOVE;
    i->dest_mask = VU_DEST_XYZW;
    i->fd = fd; i->fs = fs; i->ft = 0;
    i->lower_op = VU_LOWER_NOP;
}

static void emit_vmove_masked(MeloVu1Program *p, int fd, int fs, int mask) {
    MeloVuInstr *i = push_instr(p);
    if (!i) return;
    i->upper_op = VU_UPPER_MOVE;
    i->dest_mask = mask;
    i->fd = fd; i->fs = fs; i->ft = 0;
    i->lower_op = VU_LOWER_NOP;
}

static void emit_lq(MeloVu1Program *p, int ft, int is_, int16_t imm) {
    MeloVuInstr *i = push_instr(p);
    if (!i) return;
    i->upper_op = VU_UPPER_NOP;
    i->lower_op = VU_LOWER_LQ;
    i->ft = ft;
    i->is_ = is_;
    i->imm = imm;
}

static int eval_const_component(MeloVu1Program *p, MeloExpr *e, float *out) {
    if (e->kind == EXPR_INT_LIT) {
        *out = (float)e->as.ival;
        return 0;
    }
    if (e->kind == EXPR_FLOAT_LIT) {
        *out = (float)e->as.fval;
        return 0;
    }
    if (e->kind == EXPR_UNARY && e->as.unary.op == '-') {
        float inner;
        if (eval_const_component(p, e->as.unary.operand, &inner)) return 1;
        *out = -inner;
        return 0;
    }
    vu_error(p, e->line, "vukernel vec4 literal components must be constant numeric expressions");
    return 1;
}

static int alloc_const(MeloVu1Program *p, float x, float y, float z, float w) {
    if (p->const_count >= 64) {
        vu_error(p, 0, "vukernel constant pool full (max 64 vec4 constants)");
        return -1;
    }
    int idx = p->const_count++;
    p->const_pool[idx][0] = x;
    p->const_pool[idx][1] = y;
    p->const_pool[idx][2] = z;
    p->const_pool[idx][3] = w;
    return idx;
}

static void emit_sq(MeloVu1Program *p, int fs, int it, int16_t imm) {
    MeloVuInstr *i = push_instr(p);
    if (!i) return;
    i->upper_op = VU_UPPER_NOP;
    i->lower_op = VU_LOWER_SQ;
    i->fs = fs;
    i->it = it;
    i->imm = imm;
}

static void emit_xtop(MeloVu1Program *p, int it) {
    MeloVuInstr *i = push_instr(p);
    if (!i) return;
    i->lower_op = VU_LOWER_XTOP;
    i->it = it;
}

static void emit_xgkick(MeloVu1Program *p, int is_) {
    MeloVuInstr *i = push_instr(p);
    if (!i) return;
    i->lower_op = VU_LOWER_XGKICK;
    i->is_ = is_;
}

static void emit_eret(MeloVu1Program *p) {
    MeloVuInstr *i = push_instr(p);
    if (!i) return;
    i->lower_op = VU_LOWER_ERET;
    i->has_e_flag = 1;
}

static int find_param_vf(Vu1Ctx *cx, const char *name) {
    for (int i = 0; i < cx->prog->param_count; i++) {
        if (strcmp(cx->prog->param_names[i], name) == 0) {
            return cx->prog->param_vf[i];
        }
    }
    for (int i = 0; i < cx->local_count; i++) {
        if (strcmp(cx->local_names[i], name) == 0) {
            return cx->local_vf[i];
        }
    }
    return -1;
}

static void bind_local(Vu1Ctx *cx, const char *name, int vf) {
    if (cx->local_count >= 32) return;
    snprintf(cx->local_names[cx->local_count], 32, "%s", name);
    cx->local_vf[cx->local_count] = vf;
    cx->local_count++;
}

static int compile_vec_lit(Vu1Ctx *cx, MeloExpr *e) {
    if (e->as.vec_ctor.arg_count != 4) {
        vu_error(cx->prog, e->line, "vukernel vec4 literal must have exactly 4 components");
        return -1;
    }
    float comps[4];
    for (int i = 0; i < 4; i++) {
        if (eval_const_component(cx->prog, e->as.vec_ctor.args[i], &comps[i])) return -1;
    }
    int const_idx = alloc_const(cx->prog, comps[0], comps[1], comps[2], comps[3]);
    if (const_idx < 0) return -1;

    int dst = vu_alloc_vf(cx);
    if (dst < 0) {
        vu_error(cx->prog, e->line, "out of VU1 vector registers");
        return -1;
    }
    int16_t addr = (int16_t)(cx->prog->param_count + const_idx);
    emit_lq(cx->prog, dst, 0, addr);
    return dst;
}

static int compile_expr(Vu1Ctx *cx, MeloExpr *e) {
    if (cx->prog->has_error) return -1;

    switch (e->kind) {
        case EXPR_IDENT: {
            int vf = find_param_vf(cx, e->as.ident.name);
            if (vf < 0) {
                vu_error(cx->prog, e->line, "unknown identifier in vukernel (only vec4 params supported)");
                return -1;
            }
            return vf;
        }
        case EXPR_VEC_CTOR:
            return compile_vec_lit(cx, e);
        case EXPR_FIELD: {
            int src = compile_expr(cx, e->as.field.object);
            if (src < 0) return -1;
            int mask;
            if (strcmp(e->as.field.field, "x") == 0) mask = VU_DEST_X;
            else if (strcmp(e->as.field.field, "y") == 0) mask = VU_DEST_Y;
            else if (strcmp(e->as.field.field, "z") == 0) mask = VU_DEST_Z;
            else if (strcmp(e->as.field.field, "w") == 0) mask = VU_DEST_W;
            else {
                vu_error(cx->prog, e->line, "vukernel field access only supports .x/.y/.z/.w on vec4");
                return -1;
            }
            int dst = vu_alloc_vf(cx);
            if (dst < 0) {
                vu_error(cx->prog, e->line, "out of VU1 vector registers");
                return -1;
            }
            emit_vmove_masked(cx->prog, dst, src, mask);
            return dst;
        }
        case EXPR_BINARY: {
            int lhs = compile_expr(cx, e->as.binary.lhs);
            int rhs = compile_expr(cx, e->as.binary.rhs);
            if (lhs < 0 || rhs < 0) return -1;
            int dst = vu_alloc_vf(cx);
            if (dst < 0) {
                vu_error(cx->prog, e->line, "out of VU1 vector registers");
                return -1;
            }
            switch (e->as.binary.op) {
                case BIN_ADD: emit_vadd(cx->prog, dst, lhs, rhs); break;
                case BIN_SUB: emit_vsub(cx->prog, dst, lhs, rhs); break;
                case BIN_MUL: emit_vmul(cx->prog, dst, lhs, rhs); break;
                default:
                    vu_error(cx->prog, e->line, "unsupported operator in vukernel body");
                    return -1;
            }
            return dst;
        }
        default:
            vu_error(cx->prog, e->line, "unsupported expression kind in vukernel body");
            return -1;
    }
}

static void compile_stmt(Vu1Ctx *cx, MeloStmt *s) {
    if (cx->prog->has_error || !s) return;

    switch (s->kind) {
        case STMT_BLOCK:
            for (int i = 0; i < s->as.block.count; i++) {
                compile_stmt(cx, s->as.block.stmts[i]);
            }
            break;
        case STMT_RETURN: {
            int vf = compile_expr(cx, s->as.return_stmt.value);
            if (vf < 0) return;
            cx->prog->result_vf = vf;
            break;
        }
        case STMT_VAR_DECL: {
            int vf = compile_expr(cx, s->as.var_decl.init);
            if (vf < 0) return;
            bind_local(cx, s->as.var_decl.name, vf);
            break;
        }
        default:
            vu_error(cx->prog, s->line, "unsupported statement kind in vukernel body (only var/return supported)");
            break;
    }
}

int melo_vu1_compile(MeloFuncDecl *fn, MeloVu1Program *out) {
    memset(out, 0, sizeof(*out));
    snprintf(out->name, sizeof(out->name), "%s", fn->name);

    if (fn->param_count > 8) {
        vu_error(out, 0, "vukernel supports at most 8 parameters");
        return 1;
    }

    Vu1Ctx cx;
    memset(&cx, 0, sizeof(cx));
    cx.prog = out;
    cx.next_vf = 1;
    cx.next_vi = 1;

    for (int i = 0; i < fn->param_count; i++) {
        if (fn->params[i].type->kind != TYPE_VEC4) {
            vu_error(out, 0, "vukernel parameters must be vec4 (VU1 register width)");
            return 1;
        }
        int vf = vu_alloc_vf(&cx);
        emit_lq(out, vf, 0, (int16_t)i);
        snprintf(out->param_names[out->param_count], 32, "%s", fn->params[i].name);
        out->param_vf[out->param_count] = vf;
        out->param_count++;
    }

    compile_stmt(&cx, fn->body);

    if (out->has_error) return 1;

    if (out->result_vf <= 0) {
        vu_error(out, 0, "vukernel must return a vec4 value");
        return 1;
    }

    int result_addr = out->param_count + out->const_count;
    out->result_addr = result_addr;

    emit_sq(out, out->result_vf, 0, (int16_t)result_addr);
    emit_eret(out);

    out->code_bytes = (size_t)out->instr_count * 8;
    out->data_bytes = (size_t)(out->param_count + out->const_count + 1) * 16;

    return 0;
}

static const char *upper_mnemonic(MeloVuUpperOp op) {
    switch (op) {
        case VU_UPPER_ADD:    return "vadd";
        case VU_UPPER_SUB:    return "vsub";
        case VU_UPPER_MUL:    return "vmul";
        case VU_UPPER_MULA:   return "vmula";
        case VU_UPPER_MADD:   return "vmadd";
        case VU_UPPER_OPMULA: return "vopmula";
        case VU_UPPER_OPMSUB: return "vopmsub";
        case VU_UPPER_MOVE:   return "vmove";
        case VU_UPPER_MR32:   return "vmr32";
        default:              return "nop";
    }
}

static void mask_suffix(int mask, char *out, size_t out_size) {
    size_t off = 0;
    if (mask & VU_DEST_X) out[off++] = 'x';
    if (mask & VU_DEST_Y) out[off++] = 'y';
    if (mask & VU_DEST_Z) out[off++] = 'z';
    if (mask & VU_DEST_W) out[off++] = 'w';
    if (off == 0 || off >= out_size) { snprintf(out, out_size, "xyzw"); return; }
    out[off] = '\0';
}

void melo_vu1_emit_asm_listing(const MeloVu1Program *p, char *buf, size_t buf_size) {
    size_t off = 0;
    off += snprintf(buf + off, buf_size - off, "--vu\n");
    off += snprintf(buf + off, buf_size - off, "-- vukernel %s : VLIW microcode (%d instructions, %zu bytes)\n",
                     p->name, p->instr_count, p->code_bytes);

    for (int i = 0; i < p->instr_count && off < buf_size; i++) {
        const MeloVuInstr *ins = &p->instrs[i];
        char upper_str[64];
        char lower_str[64];

        if (ins->upper_op == VU_UPPER_NOP) {
            snprintf(upper_str, sizeof(upper_str), "nop");
        } else if (ins->upper_op == VU_UPPER_MOVE) {
            char suffix[8];
            mask_suffix(ins->dest_mask, suffix, sizeof(suffix));
            snprintf(upper_str, sizeof(upper_str), "%s.%s vf%02d, vf%02d",
                      upper_mnemonic(ins->upper_op), suffix, ins->fd, ins->fs);
        } else {
            snprintf(upper_str, sizeof(upper_str), "%s.xyzw vf%02d, vf%02d, vf%02d",
                      upper_mnemonic(ins->upper_op), ins->fd, ins->fs, ins->ft);
        }

        switch (ins->lower_op) {
            case VU_LOWER_LQ:
                snprintf(lower_str, sizeof(lower_str), "lq vf%02d, %d(vi00)", ins->ft, ins->imm);
                break;
            case VU_LOWER_SQ:
                snprintf(lower_str, sizeof(lower_str), "sq vf%02d, %d(vi%02d)", ins->fs, ins->imm, ins->it);
                break;
            case VU_LOWER_XTOP:
                snprintf(lower_str, sizeof(lower_str), "xtop vi%02d", ins->it);
                break;
            case VU_LOWER_XGKICK:
                snprintf(lower_str, sizeof(lower_str), "xgkick vi%02d", ins->is_);
                break;
            case VU_LOWER_ERET:
                snprintf(lower_str, sizeof(lower_str), "eret");
                break;
            default:
                snprintf(lower_str, sizeof(lower_str), "nop");
                break;
        }

        off += snprintf(buf + off, buf_size - off, "    %-38s %s%s\n",
                          upper_str, lower_str,
                          ins->has_e_flag ? " [E]" : "");
    }
    off += snprintf(buf + off, buf_size - off, "--endvu\n");
    (void)off;
}

static uint32_t encode_upper(const MeloVuInstr *ins) {
    uint32_t op;
    switch (ins->upper_op) {
        case VU_UPPER_ADD:  op = 0x28; break;
        case VU_UPPER_SUB:  op = 0x2C; break;
        case VU_UPPER_MUL:  op = 0x1A; break;
        case VU_UPPER_MOVE: op = 0x00; break;
        default:            op = 0x2F; break;
    }
    uint32_t word = 0;
    word |= (op & 0x3F) << 25;
    word |= ((uint32_t)ins->dest_mask & 0xF) << 21;
    word |= ((uint32_t)ins->ft & 0x1F) << 16;
    word |= ((uint32_t)ins->fs & 0x1F) << 11;
    word |= ((uint32_t)ins->fd & 0x1F) << 6;
    return word;
}

static uint32_t encode_lower(const MeloVuInstr *ins) {
    uint32_t word = 0;
    switch (ins->lower_op) {
        case VU_LOWER_LQ:
            word |= (0x01u << 25);
            word |= ((uint32_t)ins->ft & 0x1F) << 16;
            word |= ((uint32_t)ins->is_ & 0x1F) << 11;
            word |= ((uint16_t)ins->imm);
            break;
        case VU_LOWER_SQ:
            word |= (0x03u << 25);
            word |= ((uint32_t)ins->fs & 0x1F) << 16;
            word |= ((uint32_t)ins->it & 0x1F) << 11;
            word |= ((uint16_t)ins->imm);
            break;
        case VU_LOWER_XTOP:
            word |= (0x3Fu << 25) | 0x30;
            word |= ((uint32_t)ins->it & 0x1F) << 16;
            break;
        case VU_LOWER_XGKICK:
            word |= (0x3Fu << 25) | 0x31;
            word |= ((uint32_t)ins->is_ & 0x1F) << 11;
            break;
        case VU_LOWER_ERET:
            word |= (0x3Fu << 25) | 0x3B;
            break;
        default:
            break;
    }
    if (ins->has_e_flag) word |= (1u << 31);
    return word;
}

void melo_vu1_encode_microcode(const MeloVu1Program *p, uint64_t *out_words, int *out_count) {
    for (int i = 0; i < p->instr_count; i++) {
        uint32_t upper = encode_upper(&p->instrs[i]);
        uint32_t lower = encode_lower(&p->instrs[i]);
        out_words[i] = ((uint64_t)upper << 32) | (uint64_t)lower;
    }
    *out_count = p->instr_count;
}

void melo_vu1_emit_c_bridge(const MeloVu1Program *p, char *buf, size_t buf_size) {
    size_t off = 0;

    off += snprintf(buf + off, buf_size - off,
        "#include <tamtypes.h>\n"
        "#include <kernel.h>\n"
        "#include <vif_codes.h>\n"
        "#include <packet2.h>\n"
        "#include <packet2_utils.h>\n"
        "#include \"melo_value.h\"\n\n");

    off += snprintf(buf + off, buf_size - off,
        "extern u64 melo_vu1_%s_microcode[];\n"
        "extern const int melo_vu1_%s_microcode_qwc;\n\n",
        p->name, p->name);

    off += snprintf(buf + off, buf_size - off,
        "/* VU1 instruction memory is mapped on the EE at 0x11008000 (16KB);\n"
        " * VU1 data memory is mapped at 0x1100C000 (16KB). Both are only safe\n"
        " * to touch from the EE while VU1 is idle, i.e. after the DMA transfer\n"
        " * that kicked the microprogram has completed and VU1 has finished\n"
        " * executing (vu1 has no path back to the EE other than these mapped\n"
        " * windows or an explicit XGKICK to the GS, which this function does\n"
        " * not use since it returns a value to the caller rather than\n"
        " * rendering geometry). */\n"
        "#define MELO_VU1_DATA_MEM_BASE 0x1100C000u\n"
        "#define MELO_VU1_RESULT_ADDR   %d\n\n",
        p->result_addr);

    off += snprintf(buf + off, buf_size - off,
        "MeloValue melo_vu1_call_%s(const MeloValue *args, int argc) {\n"
        "    packet2_t *pkt = packet2_create(64, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);\n\n"
        "    packet2_vif_add_micro_program(pkt, 0, melo_vu1_%s_microcode,\n"
        "        melo_vu1_%s_microcode + melo_vu1_%s_microcode_qwc * 2);\n\n"
        "    for (int i = 0; i < argc && i < %d; i++) {\n"
        "        packet2_utils_vu_add_unpack_data(pkt, i, (void *)&args[i].as.v4, 1, 0);\n"
        "    }\n\n"
        "    dma_channel_send_packet2(pkt, DMA_CHANNEL_VIF1, 1);\n"
        "    dma_wait_fast();\n"
        "    packet2_free(pkt);\n\n"
        "    /* VU1 is idle again now that the DMA transfer (which included the\n"
        "     * MSCAL that ran the microprogram to completion) has finished, so\n"
        "     * the mapped data memory window can be read directly. */\n"
        "    volatile float *vu1_data = (volatile float *)MELO_VU1_DATA_MEM_BASE;\n"
        "    volatile float *slot = vu1_data + (MELO_VU1_RESULT_ADDR * 4);\n"
        "    return melo_vec4(slot[0], slot[1], slot[2], slot[3]);\n"
        "}\n",
        p->name, p->name, p->name, p->name, p->param_count);

    (void)off;
}
