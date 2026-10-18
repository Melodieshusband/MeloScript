#include "melo_codegen.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define MAX_LOCALS 64
#define MAX_PATCHES 256
#define MAX_LOOP_DEPTH 16
#define MAX_BREAKS_PER_LOOP 32

typedef enum {
    VT_UNKNOWN, VT_INT32, VT_FLOAT, VT_VEC2, VT_VEC3, VT_VEC4, VT_BOOL, VT_STRUCT, VT_ARRAY, VT_OTHER
} ValueTypeHint;

typedef struct {
    char *name;
    int reg;
    ValueTypeHint type_hint;
    int struct_id;
    ValueTypeHint elem_type_hint;
    int elem_struct_id;
} LocalVar;

typedef struct {
    char *name;
    int func_id;
    int entry_pc;
} FuncEntry;

typedef struct {
    int patch_pos;
} JumpPatch;

typedef struct {
    int continue_target;
    int continue_patches[MAX_BREAKS_PER_LOOP];
    int continue_count;
    int break_patches[MAX_BREAKS_PER_LOOP];
    int break_count;
    int arena_depth_at_entry;
} LoopCtx;

typedef struct {
    MeloCompiledModule *mod;

    LocalVar locals[MAX_LOCALS];
    int local_count;
    int next_reg;
    int max_reg_used;

    LoopCtx loop_stack[MAX_LOOP_DEPTH];
    int loop_depth;
    int arena_depth;

    FuncEntry funcs[MELO_MAX_FUNCS];
    int func_entry_count;

    char struct_names[MELO_MAX_STRUCTS][32];
    int struct_entry_count;
    ValueTypeHint struct_field_hints[MELO_MAX_STRUCTS][MELO_MAX_STRUCT_FIELDS];
    int struct_field_struct_ids[MELO_MAX_STRUCTS][MELO_MAX_STRUCT_FIELDS];

    int last_struct_id;
    ValueTypeHint last_elem_hint;
    int last_elem_struct_id;

    int in_hot_func;
    const char *hot_func_name;

    int in_scratch_init;

    const char **native_names;
    int native_count;

    int had_error;
    char error_msg[256];
} Codegen;

static void cg_error(Codegen *cg, const char *msg) {
    if (cg->had_error) return;
    cg->had_error = 1;
    snprintf(cg->error_msg, sizeof(cg->error_msg), "%s", msg);
}

static void emit(Codegen *cg, MeloInstr instr) {
    MeloCompiledModule *mod = cg->mod;
    if (mod->code_count >= mod->code_cap) {
        mod->code_cap = mod->code_cap ? mod->code_cap * 2 : 256;
        mod->code = realloc(mod->code, sizeof(MeloInstr) * mod->code_cap);
    }
    mod->code[mod->code_count++] = instr;
}

static int emit_placeholder(Codegen *cg) {
    emit(cg, 0);
    return cg->mod->code_count - 1;
}

static void emit_raw(Codegen *cg, uint32_t raw) {
    emit(cg, (MeloInstr)raw);
}

static void patch_jump(Codegen *cg, int pos, int target) {
    int16_t offset = (int16_t)(target - pos);
    MeloInstr instr = cg->mod->code[pos];
    MeloOpcode op = (MeloOpcode)INSTR_OP(instr);
    int a = INSTR_A(instr);
    cg->mod->code[pos] = MAKE_ABX(op, a, (uint16_t)offset);
}

static void push_loop(Codegen *cg, int continue_target) {
    if (cg->loop_depth >= MAX_LOOP_DEPTH) {
        cg_error(cg, "loops nested too deeply");
        return;
    }
    LoopCtx *lc = &cg->loop_stack[cg->loop_depth++];
    lc->continue_target = continue_target;
    lc->continue_count = 0;
    lc->break_count = 0;
    lc->arena_depth_at_entry = cg->arena_depth;
}

static void pop_loop_and_patch_breaks(Codegen *cg, int break_target) {
    if (cg->loop_depth <= 0) return;
    LoopCtx *lc = &cg->loop_stack[--cg->loop_depth];
    for (int i = 0; i < lc->break_count; i++) {
        patch_jump(cg, lc->break_patches[i], break_target);
    }
}

static void resolve_pending_continues(Codegen *cg, int continue_target) {
    if (cg->loop_depth <= 0) return;
    LoopCtx *lc = &cg->loop_stack[cg->loop_depth - 1];
    for (int i = 0; i < lc->continue_count; i++) {
        patch_jump(cg, lc->continue_patches[i], continue_target);
    }
    lc->continue_count = 0;
}

static void register_break(Codegen *cg, int patch_pos) {
    if (cg->loop_depth <= 0) {
        cg_error(cg, "'break' used outside of a loop");
        return;
    }
    LoopCtx *lc = &cg->loop_stack[cg->loop_depth - 1];
    if (lc->break_count >= MAX_BREAKS_PER_LOOP) {
        cg_error(cg, "too many 'break' statements in a single loop");
        return;
    }
    lc->break_patches[lc->break_count++] = patch_pos;
}

static void emit_continue(Codegen *cg) {
    if (cg->loop_depth <= 0) {
        cg_error(cg, "'continue' used outside of a loop");
        return;
    }
    LoopCtx *lc = &cg->loop_stack[cg->loop_depth - 1];
    for (int i = cg->arena_depth; i > lc->arena_depth_at_entry; i--) {
        emit(cg, MAKE_ABC(OP_ARENA_EXIT, 0, 0, 0));
    }
    int jmp = emit_placeholder(cg);
    cg->mod->code[jmp] = MAKE_ABX(OP_JMP, 0, 0);
    if (lc->continue_target >= 0) {
        patch_jump(cg, jmp, lc->continue_target);
    } else {
        if (lc->continue_count >= MAX_BREAKS_PER_LOOP) {
            cg_error(cg, "too many 'continue' statements in a single loop");
            return;
        }
        lc->continue_patches[lc->continue_count++] = jmp;
    }
}

static int add_const(Codegen *cg, MeloValue v) {
    MeloCompiledModule *mod = cg->mod;
    for (int i = 0; i < mod->const_count; i++) {
        if (mod->consts[i].type != v.type) continue;
        if (v.type == MELO_INT32 && mod->consts[i].as.i32 == v.as.i32) return i;
        if (v.type == MELO_FLOAT && mod->consts[i].as.f32 == v.as.f32) return i;
        if (v.type == MELO_STRING && mod->consts[i].as.str == v.as.str) return i;
    }
    if (mod->const_count >= MELO_MAX_CONSTS) {
        cg_error(cg, "too many constants in module");
        return 0;
    }
    mod->consts[mod->const_count] = v;
    return mod->const_count++;
}

static int alloc_reg(Codegen *cg) {
    int r = cg->next_reg++;
    if (cg->next_reg > cg->max_reg_used) cg->max_reg_used = cg->next_reg;
    if (r >= MELO_MAX_REGS) {
        cg_error(cg, "too many registers used in function (register pressure too high)");
    }
    return r;
}

static void free_regs_to(Codegen *cg, int mark) {
    cg->next_reg = mark;
}

static LocalVar *find_local(Codegen *cg, const char *name) {
    for (int i = cg->local_count - 1; i >= 0; i--) {
        if (strcmp(cg->locals[i].name, name) == 0) return &cg->locals[i];
    }
    return NULL;
}

static int find_native_name(Codegen *cg, const char *name) {
    for (int i = 0; i < cg->native_count; i++) {
        if (strcmp(cg->native_names[i], name) == 0) return i;
    }
    return -1;
}

static int find_func(Codegen *cg, const char *name) {
    for (int i = 0; i < cg->func_entry_count; i++) {
        if (strcmp(cg->funcs[i].name, name) == 0) return cg->funcs[i].func_id;
    }
    return -1;
}

static int find_struct(Codegen *cg, const char *name) {
    for (int i = 0; i < cg->struct_entry_count; i++) {
        if (strcmp(cg->struct_names[i], name) == 0) return i;
    }
    return -1;
}

static int find_struct_field(Codegen *cg, int struct_id, const char *field_name) {
    MeloStructInfo *si = &cg->mod->structs[struct_id];
    for (int i = 0; i < si->field_count; i++) {
        if (strcmp(si->field_names[i], field_name) == 0) return i;
    }
    return -1;
}

static ValueTypeHint type_hint_from_node(Codegen *cg, MeloTypeNode *t, int *out_struct_id) {
    if (out_struct_id) *out_struct_id = -1;
    if (!t) return VT_UNKNOWN;
    switch (t->kind) {
        case TYPE_INT32: return VT_INT32;
        case TYPE_FLOAT: return VT_FLOAT;
        case TYPE_VEC2: return VT_VEC2;
        case TYPE_VEC3: return VT_VEC3;
        case TYPE_VEC4: return VT_VEC4;
        case TYPE_BOOL: return VT_BOOL;
        case TYPE_NAMED: {
            int sid = find_struct(cg, t->name);
            if (sid >= 0) {
                if (out_struct_id) *out_struct_id = sid;
                return VT_STRUCT;
            }
            return VT_OTHER;
        }
        case TYPE_ARRAY: {
            int elem_struct_id;
            ValueTypeHint elem_hint = type_hint_from_node(cg, t->elem, &elem_struct_id);
            cg->last_elem_hint = elem_hint;
            cg->last_elem_struct_id = elem_struct_id;
            return VT_ARRAY;
        }
        default: return VT_OTHER;
    }
}

static int is_vec_hint(ValueTypeHint h) {
    return h == VT_VEC2 || h == VT_VEC3 || h == VT_VEC4;
}

static int vec_component_index(const char *field) {
    if (strcmp(field, "x") == 0) return 0;
    if (strcmp(field, "y") == 0) return 1;
    if (strcmp(field, "z") == 0) return 2;
    if (strcmp(field, "w") == 0) return 3;
    return -1;
}

static ValueTypeHint gen_expr(Codegen *cg, MeloExpr *e, int dest_reg);

static ValueTypeHint gen_array_lit(Codegen *cg, MeloExpr *e, int dest_reg) {
    int elem_struct_id;
    ValueTypeHint elem_hint = type_hint_from_node(cg, e->as.array_lit.elem_type, &elem_struct_id);

    int declared_size = e->as.array_lit.array_size;
    int arg_count = e->as.array_lit.arg_count;

    if (declared_size >= 0 && arg_count > 0 && arg_count != declared_size) {
        cg_error(cg, "array initializer count does not match declared array size");
        return VT_UNKNOWN;
    }

    int capacity = declared_size >= 0 ? declared_size : arg_count;
    if (capacity < 0 || capacity > 0xFFFF) {
        cg_error(cg, "array capacity out of range");
        return VT_UNKNOWN;
    }

    int is_soa = (elem_hint == VT_STRUCT && elem_struct_id >= 0 && cg->mod->structs[elem_struct_id].is_soa);

    if (is_soa) {
        if (cg->in_scratch_init) {
            cg_error(cg, "@scratch is not supported for #[soa] arrays");
            return VT_UNKNOWN;
        }

        emit(cg, MAKE_ABC(OP_NEWARRAY_SOA, dest_reg, elem_struct_id, 0));
        emit_raw(cg, (uint32_t)capacity);

        int field_count = cg->mod->structs[elem_struct_id].field_count;
        int mark = cg->next_reg;
        for (int i = 0; i < arg_count; i++) {
            MeloExpr *elem = e->as.array_lit.args[i];
            if (elem->kind != EXPR_CALL) {
                cg_error(cg, "#[soa] array elements must be struct constructor calls");
                return VT_UNKNOWN;
            }
            if (elem->as.call.arg_count != field_count) {
                cg_error(cg, "struct constructor argument count does not match field count");
                return VT_UNKNOWN;
            }

            int idx_reg = alloc_reg(cg);
            int k = add_const(cg, melo_int(i));
            emit(cg, MAKE_ABX(OP_LOADK, idx_reg, k));

            for (int f = 0; f < field_count; f++) {
                int val_reg = alloc_reg(cg);
                gen_expr(cg, elem->as.call.args[f], val_reg);
                emit(cg, MAKE_ABC(OP_SOA_SETFIELD, dest_reg, idx_reg, val_reg));
                emit_raw(cg, (uint32_t)f);
                free_regs_to(cg, idx_reg + 1);
            }
            free_regs_to(cg, mark);
        }

        cg->last_elem_hint = elem_hint;
        cg->last_elem_struct_id = elem_struct_id;
        return VT_ARRAY;
    }

    emit(cg, MAKE_ABX(cg->in_scratch_init ? OP_NEWARRAY_SCRATCH : OP_NEWARRAY, dest_reg, capacity));

    int mark = cg->next_reg;
    for (int i = 0; i < arg_count; i++) {
        int idx_reg = alloc_reg(cg);
        int k = add_const(cg, melo_int(i));
        emit(cg, MAKE_ABX(OP_LOADK, idx_reg, k));

        int val_reg = alloc_reg(cg);
        gen_expr(cg, e->as.array_lit.args[i], val_reg);

        emit(cg, MAKE_ABC(OP_SETINDEX, dest_reg, idx_reg, val_reg));
        free_regs_to(cg, mark);
    }

    cg->last_elem_hint = elem_hint;
    cg->last_elem_struct_id = elem_struct_id;
    return VT_ARRAY;
}

static int vec_rank_from_name(const char *name) {
    if (strcmp(name, "vec2") == 0) return 2;
    if (strcmp(name, "vec3") == 0) return 3;
    return 4;
}

static ValueTypeHint vt_from_vec_rank(int rank) {
    if (rank == 2) return VT_VEC2;
    if (rank == 3) return VT_VEC3;
    return VT_VEC4;
}

static ValueTypeHint gen_vec_ctor(Codegen *cg, MeloExpr *e, int dest_reg) {
    int rank = vec_rank_from_name(e->as.vec_ctor.type_name);
    int n = e->as.vec_ctor.arg_count;

    if (n > rank) {
        cg_error(cg, "too many arguments for vector constructor");
        return vt_from_vec_rank(rank);
    }

    float vals[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    int all_const = 1;

    for (int i = 0; i < n; i++) {
        MeloExpr *arg = e->as.vec_ctor.args[i];
        if (arg->kind == EXPR_FLOAT_LIT) {
            vals[i] = (float)arg->as.fval;
        } else if (arg->kind == EXPR_INT_LIT) {
            vals[i] = (float)arg->as.ival;
        } else {
            all_const = 0;
            break;
        }
    }

    if (all_const) {
        MeloValue v;
        v.type = (rank == 2) ? MELO_VEC2 : (rank == 3) ? MELO_VEC3 : MELO_VEC4;
        v.as.v4.x = vals[0]; v.as.v4.y = vals[1]; v.as.v4.z = vals[2]; v.as.v4.w = vals[3];
        int k = add_const(cg, v);
        emit(cg, MAKE_ABX(OP_LOADK, dest_reg, k));
        return vt_from_vec_rank(rank);
    }

    emit(cg, MAKE_ABC(OP_VZERO, dest_reg, rank, 0));
    int mark = cg->next_reg;
    for (int i = 0; i < n; i++) {
        int val_reg = alloc_reg(cg);
        gen_expr(cg, e->as.vec_ctor.args[i], val_reg);
        emit(cg, MAKE_ABC(OP_VSET, dest_reg, i, val_reg));
        free_regs_to(cg, mark);
    }
    return vt_from_vec_rank(rank);
}

static ValueTypeHint gen_logical(Codegen *cg, MeloExpr *e, int dest_reg) {
    int mark = cg->next_reg;
    int lhs_reg = alloc_reg(cg);
    gen_expr(cg, e->as.binary.lhs, lhs_reg);
    if (lhs_reg != dest_reg) {
        emit(cg, MAKE_ABC(OP_MOVE, dest_reg, lhs_reg, 0));
    }
    free_regs_to(cg, mark);

    int skip_patch = emit_placeholder(cg);
    if (e->as.binary.op == BIN_AND) {
        cg->mod->code[skip_patch] = MAKE_ABX(OP_JMPIFNOT, dest_reg, 0);
    } else {
        cg->mod->code[skip_patch] = MAKE_ABX(OP_JMPIF, dest_reg, 0);
    }

    gen_expr(cg, e->as.binary.rhs, dest_reg);

    patch_jump(cg, skip_patch, cg->mod->code_count);
    return VT_BOOL;
}

static ValueTypeHint gen_binary(Codegen *cg, MeloExpr *e, int dest_reg) {
    if (e->as.binary.op == BIN_AND || e->as.binary.op == BIN_OR) {
        return gen_logical(cg, e, dest_reg);
    }

    int mark = cg->next_reg;

    int lhs_reg = alloc_reg(cg);
    ValueTypeHint lhs_t = gen_expr(cg, e->as.binary.lhs, lhs_reg);

    int rhs_reg = alloc_reg(cg);
    ValueTypeHint rhs_t = gen_expr(cg, e->as.binary.rhs, rhs_reg);

    int vector_mode = is_vec_hint(lhs_t) || is_vec_hint(rhs_t);
    ValueTypeHint result_hint = VT_UNKNOWN;

    switch (e->as.binary.op) {
        case BIN_ADD:
            if (vector_mode) {
                emit(cg, MAKE_ABC(OP_VADD, dest_reg, lhs_reg, rhs_reg));
                result_hint = lhs_t;
            } else if (cg->in_hot_func && lhs_t == VT_INT32 && rhs_t == VT_INT32) {
                emit(cg, MAKE_ABC(OP_ADDI, dest_reg, lhs_reg, rhs_reg));
                result_hint = VT_INT32;
            } else if (cg->in_hot_func && lhs_t == VT_FLOAT && rhs_t == VT_FLOAT) {
                emit(cg, MAKE_ABC(OP_ADDF, dest_reg, lhs_reg, rhs_reg));
                result_hint = VT_FLOAT;
            } else {
                emit(cg, MAKE_ABC(OP_ADD, dest_reg, lhs_reg, rhs_reg));
                result_hint = (lhs_t == VT_FLOAT || rhs_t == VT_FLOAT) ? VT_FLOAT : VT_INT32;
            }
            break;
        case BIN_SUB:
            if (vector_mode) {
                emit(cg, MAKE_ABC(OP_VSUB, dest_reg, lhs_reg, rhs_reg));
                result_hint = lhs_t;
            } else if (cg->in_hot_func && lhs_t == VT_INT32 && rhs_t == VT_INT32) {
                emit(cg, MAKE_ABC(OP_SUBI, dest_reg, lhs_reg, rhs_reg));
                result_hint = VT_INT32;
            } else if (cg->in_hot_func && lhs_t == VT_FLOAT && rhs_t == VT_FLOAT) {
                emit(cg, MAKE_ABC(OP_SUBF, dest_reg, lhs_reg, rhs_reg));
                result_hint = VT_FLOAT;
            } else {
                emit(cg, MAKE_ABC(OP_SUB, dest_reg, lhs_reg, rhs_reg));
                result_hint = (lhs_t == VT_FLOAT || rhs_t == VT_FLOAT) ? VT_FLOAT : VT_INT32;
            }
            break;
        case BIN_MUL:
            if (is_vec_hint(lhs_t) && !is_vec_hint(rhs_t)) {
                emit(cg, MAKE_ABC(OP_VSCALE, dest_reg, lhs_reg, rhs_reg));
                result_hint = lhs_t;
            } else if (is_vec_hint(rhs_t) && !is_vec_hint(lhs_t)) {
                emit(cg, MAKE_ABC(OP_VSCALE, dest_reg, rhs_reg, lhs_reg));
                result_hint = rhs_t;
            } else if (vector_mode) {
                emit(cg, MAKE_ABC(OP_VMUL, dest_reg, lhs_reg, rhs_reg));
                result_hint = lhs_t;
            } else if (cg->in_hot_func && lhs_t == VT_INT32 && rhs_t == VT_INT32) {
                emit(cg, MAKE_ABC(OP_MULI, dest_reg, lhs_reg, rhs_reg));
                result_hint = VT_INT32;
            } else if (cg->in_hot_func && lhs_t == VT_FLOAT && rhs_t == VT_FLOAT) {
                emit(cg, MAKE_ABC(OP_MULF, dest_reg, lhs_reg, rhs_reg));
                result_hint = VT_FLOAT;
            } else {
                emit(cg, MAKE_ABC(OP_MUL, dest_reg, lhs_reg, rhs_reg));
                result_hint = (lhs_t == VT_FLOAT || rhs_t == VT_FLOAT) ? VT_FLOAT : VT_INT32;
            }
            break;
        case BIN_DIV:
            if (cg->in_hot_func && lhs_t == VT_FLOAT && rhs_t == VT_FLOAT) {
                emit(cg, MAKE_ABC(OP_DIVF, dest_reg, lhs_reg, rhs_reg));
            } else {
                emit(cg, MAKE_ABC(OP_DIV, dest_reg, lhs_reg, rhs_reg));
            }
            result_hint = VT_FLOAT;
            break;
        case BIN_MOD:
            emit(cg, MAKE_ABC(OP_MOD, dest_reg, lhs_reg, rhs_reg));
            result_hint = VT_INT32;
            break;
        case BIN_EQ:
            emit(cg, MAKE_ABC(OP_EQ, dest_reg, lhs_reg, rhs_reg));
            result_hint = VT_BOOL;
            break;
        case BIN_NEQ:
            emit(cg, MAKE_ABC(OP_EQ, dest_reg, lhs_reg, rhs_reg));
            emit(cg, MAKE_ABC(OP_NOT, dest_reg, dest_reg, 0));
            result_hint = VT_BOOL;
            break;
        case BIN_LT:
            emit(cg, MAKE_ABC(OP_LT, dest_reg, lhs_reg, rhs_reg));
            result_hint = VT_BOOL;
            break;
        case BIN_LE:
            emit(cg, MAKE_ABC(OP_LE, dest_reg, lhs_reg, rhs_reg));
            result_hint = VT_BOOL;
            break;
        case BIN_GT:
            emit(cg, MAKE_ABC(OP_LT, dest_reg, rhs_reg, lhs_reg));
            result_hint = VT_BOOL;
            break;
        case BIN_GE:
            emit(cg, MAKE_ABC(OP_LE, dest_reg, rhs_reg, lhs_reg));
            result_hint = VT_BOOL;
            break;
        case BIN_AND:
        case BIN_OR:
            break;
    }

    free_regs_to(cg, mark);
    return result_hint;
}

static ValueTypeHint gen_call(Codegen *cg, MeloExpr *e, int dest_reg) {
    if (e->as.call.callee->kind != EXPR_IDENT) {
        cg_error(cg, "only direct function calls are supported");
        return VT_UNKNOWN;
    }

    const char *name = e->as.call.callee->as.ident.name;

    if (strcmp(name, "vdot") == 0 && e->as.call.arg_count == 2) {
        int mark = cg->next_reg;
        int a = alloc_reg(cg);
        gen_expr(cg, e->as.call.args[0], a);
        int b = alloc_reg(cg);
        gen_expr(cg, e->as.call.args[1], b);
        emit(cg, MAKE_ABC(OP_VDOT, dest_reg, a, b));
        free_regs_to(cg, mark);
        return VT_FLOAT;
    }
    if (strcmp(name, "vcross") == 0 && e->as.call.arg_count == 2) {
        int mark = cg->next_reg;
        int a = alloc_reg(cg);
        gen_expr(cg, e->as.call.args[0], a);
        int b = alloc_reg(cg);
        gen_expr(cg, e->as.call.args[1], b);
        emit(cg, MAKE_ABC(OP_VCROSS, dest_reg, a, b));
        free_regs_to(cg, mark);
        return VT_VEC3;
    }
    if (strcmp(name, "vnorm") == 0 && e->as.call.arg_count == 1) {
        int mark = cg->next_reg;
        int a = alloc_reg(cg);
        ValueTypeHint t = gen_expr(cg, e->as.call.args[0], a);
        emit(cg, MAKE_ABC(OP_VNORM, dest_reg, a, 0));
        free_regs_to(cg, mark);
        return t;
    }
    if (strcmp(name, "len") == 0 && e->as.call.arg_count == 1) {
        int mark = cg->next_reg;
        int a = alloc_reg(cg);
        ValueTypeHint t = gen_expr(cg, e->as.call.args[0], a);
        if (t != VT_ARRAY) {
            cg_error(cg, "len() requires an array argument");
            free_regs_to(cg, mark);
            return VT_UNKNOWN;
        }
        emit(cg, MAKE_ABC(OP_LEN, dest_reg, a, 0));
        free_regs_to(cg, mark);
        return VT_INT32;
    }
    if (strcmp(name, "print") == 0 && e->as.call.arg_count == 1) {
        int mark = cg->next_reg;
        int a = alloc_reg(cg);
        gen_expr(cg, e->as.call.args[0], a);
        emit(cg, MAKE_ABC(OP_PRINT, a, 0, 0));
        free_regs_to(cg, mark);
        return VT_OTHER;
    }
    if (strcmp(name, "coro_create") == 0) {
        if (e->as.call.arg_count != 1) {
            cg_error(cg, "coro_create() requires exactly 1 argument (function)");
            return VT_UNKNOWN;
        }
        MeloExpr *arg = e->as.call.args[0];
        int func_id = -1;
        if (arg->kind == EXPR_IDENT) {
            func_id = find_func(cg, arg->as.ident.name);
        } else if (arg->kind == EXPR_STRING_LIT) {
            func_id = find_func(cg, arg->as.strlit.str);
        }
        if (func_id >= 0) {
            emit(cg, MAKE_ABC(OP_CORO_CREATE, dest_reg, func_id, 0));
        } else {
            int mark = cg->next_reg;
            int r = alloc_reg(cg);
            gen_expr(cg, arg, r);
            emit(cg, MAKE_ABC(OP_CORO_CREATE, dest_reg, r, 1));
            free_regs_to(cg, mark);
        }
        return VT_INT32;
    }
    if (strcmp(name, "coro_resume") == 0 || strcmp(name, "resume") == 0) {
        if (e->as.call.arg_count < 1 || e->as.call.arg_count > 2) {
            cg_error(cg, "coro_resume() requires 1 or 2 arguments (coroutine, [value])");
            return VT_UNKNOWN;
        }
        int mark = cg->next_reg;
        int r_coro = alloc_reg(cg);
        gen_expr(cg, e->as.call.args[0], r_coro);
        int r_arg = 255;
        if (e->as.call.arg_count == 2) {
            r_arg = alloc_reg(cg);
            gen_expr(cg, e->as.call.args[1], r_arg);
        }
        emit(cg, MAKE_ABC(OP_CORO_RESUME, dest_reg, r_coro, r_arg));
        free_regs_to(cg, mark);
        return VT_UNKNOWN;
    }
    if (strcmp(name, "coro_status") == 0) {
        if (e->as.call.arg_count != 1) {
            cg_error(cg, "coro_status() requires exactly 1 argument (coroutine)");
            return VT_UNKNOWN;
        }
        int mark = cg->next_reg;
        int r_coro = alloc_reg(cg);
        gen_expr(cg, e->as.call.args[0], r_coro);
        emit(cg, MAKE_ABC(OP_CORO_STATUS, dest_reg, r_coro, 0));
        free_regs_to(cg, mark);
        return VT_INT32;
    }
    if (strcmp(name, "scratch_dma_in") == 0) {
        if (e->as.call.arg_count != 2) {
            cg_error(cg, "scratch_dma_in(dest_scratch, src_ram) requires 2 arguments");
            return VT_UNKNOWN;
        }
        int mark = cg->next_reg;
        int dst = alloc_reg(cg);
        gen_expr(cg, e->as.call.args[0], dst);
        int src = alloc_reg(cg);
        gen_expr(cg, e->as.call.args[1], src);
        emit(cg, MAKE_ABC(OP_SCRATCH_DMA_IN, dest_reg, dst, src));
        free_regs_to(cg, mark);
        return VT_INT32;
    }
    if (strcmp(name, "scratch_dma_out") == 0) {
        if (e->as.call.arg_count != 2) {
            cg_error(cg, "scratch_dma_out(dest_ram, src_scratch) requires 2 arguments");
            return VT_UNKNOWN;
        }
        int mark = cg->next_reg;
        int dst = alloc_reg(cg);
        gen_expr(cg, e->as.call.args[0], dst);
        int src = alloc_reg(cg);
        gen_expr(cg, e->as.call.args[1], src);
        emit(cg, MAKE_ABC(OP_SCRATCH_DMA_OUT, dest_reg, dst, src));
        free_regs_to(cg, mark);
        return VT_INT32;
    }

    int struct_id = find_struct(cg, name);
    if (struct_id >= 0) {
        MeloStructInfo *si = &cg->mod->structs[struct_id];
        if (e->as.call.arg_count != si->field_count) {
            cg_error(cg, "struct constructor argument count does not match field count");
            return VT_UNKNOWN;
        }

        emit(cg, MAKE_ABC(cg->in_scratch_init ? OP_NEWOBJECT_SCRATCH : OP_NEWOBJECT, dest_reg, struct_id, 0));

        int mark = cg->next_reg;
        for (int i = 0; i < e->as.call.arg_count; i++) {
            int val_reg = alloc_reg(cg);
            gen_expr(cg, e->as.call.args[i], val_reg);
            emit(cg, MAKE_ABC(OP_SETFIELD, dest_reg, i, val_reg));
            free_regs_to(cg, mark);
        }

        cg->last_struct_id = struct_id;
        return VT_STRUCT;
    }

    int native_id = find_native_name(cg, name);
    if (native_id >= 0) {
        if (e->as.call.arg_count > MELO_MAX_NATIVE_ARGS) {
            cg_error(cg, "too many arguments in native function call");
            return VT_UNKNOWN;
        }
        int mark = cg->next_reg;
        int arg_base = cg->next_reg;
        for (int i = 0; i < e->as.call.arg_count; i++) {
            int r = alloc_reg(cg);
            gen_expr(cg, e->as.call.args[i], r);
        }
        emit(cg, MAKE_ABC(OP_NATIVECALL, dest_reg, native_id, arg_base));
        emit_raw(cg, (uint32_t)e->as.call.arg_count);
        free_regs_to(cg, mark);
        return VT_OTHER;
    }

    int func_id = find_func(cg, name);
    if (func_id < 0) {
        cg_error(cg, "call to undefined function");
        return VT_UNKNOWN;
    }

    int mark = cg->next_reg;
    int arg_base = cg->next_reg;

    for (int i = 0; i < e->as.call.arg_count; i++) {
        int r = alloc_reg(cg);
        gen_expr(cg, e->as.call.args[i], r);
    }

    emit(cg, MAKE_ABC(OP_CALL, dest_reg, func_id, arg_base));
    free_regs_to(cg, mark);
    return VT_UNKNOWN;
}

static ValueTypeHint gen_expr(Codegen *cg, MeloExpr *e, int dest_reg) {
    switch (e->kind) {
        case EXPR_INT_LIT: {
            int k = add_const(cg, melo_int((int32_t)e->as.ival));
            emit(cg, MAKE_ABX(OP_LOADK, dest_reg, k));
            return VT_INT32;
        }
        case EXPR_FLOAT_LIT: {
            int k = add_const(cg, melo_float((float)e->as.fval));
            emit(cg, MAKE_ABX(OP_LOADK, dest_reg, k));
            return VT_FLOAT;
        }
        case EXPR_BOOL_LIT: {
            int k = add_const(cg, melo_int((int32_t)e->as.ival));
            emit(cg, MAKE_ABX(OP_LOADK, dest_reg, k));
            return VT_BOOL;
        }
        case EXPR_STRING_LIT: {
            const char *s = e->as.strlit.str;
            uint32_t len = (uint32_t)strlen(s);
            MeloString *interned = melo_strpool_intern(&cg->mod->strpool, s, len);
            if (!interned) {
                cg_error(cg, "string pool exhausted");
                return VT_OTHER;
            }
            int k = add_const(cg, melo_string(interned));
            emit(cg, MAKE_ABX(OP_LOADK, dest_reg, k));
            return VT_OTHER;
        }
        case EXPR_IDENT: {
            LocalVar *lv = find_local(cg, e->as.ident.name);
            if (!lv) {
                cg_error(cg, "reference to undefined variable");
                return VT_UNKNOWN;
            }
            if (lv->reg != dest_reg) {
                emit(cg, MAKE_ABC(OP_MOVE, dest_reg, lv->reg, 0));
            }
            if (lv->type_hint == VT_STRUCT) cg->last_struct_id = lv->struct_id;
            if (lv->type_hint == VT_ARRAY) {
                cg->last_elem_hint = lv->elem_type_hint;
                cg->last_elem_struct_id = lv->elem_struct_id;
            }
            return lv->type_hint;
        }
        case EXPR_VEC_CTOR:
            return gen_vec_ctor(cg, e, dest_reg);
        case EXPR_ARRAY_LIT:
            return gen_array_lit(cg, e, dest_reg);
        case EXPR_BINARY:
            return gen_binary(cg, e, dest_reg);
        case EXPR_UNARY: {
            int mark = cg->next_reg;
            int r = alloc_reg(cg);
            ValueTypeHint t = gen_expr(cg, e->as.unary.operand, r);
            if (e->as.unary.op == '-') {
                emit(cg, MAKE_ABC(OP_NEG, dest_reg, r, 0));
            } else {
                emit(cg, MAKE_ABC(OP_NOT, dest_reg, r, 0));
                t = VT_BOOL;
            }
            free_regs_to(cg, mark);
            return t;
        }
        case EXPR_ASSIGN: {
            MeloExpr *target = e->as.assign.target;

            if (target->kind == EXPR_IDENT) {
                LocalVar *lv = find_local(cg, target->as.ident.name);
                if (!lv) {
                    cg_error(cg, "assignment to undefined variable");
                    return VT_UNKNOWN;
                }
                ValueTypeHint t = gen_expr(cg, e->as.assign.value, lv->reg);
                lv->type_hint = t;
                if (t == VT_STRUCT) lv->struct_id = cg->last_struct_id;
                if (t == VT_ARRAY) {
                    lv->elem_type_hint = cg->last_elem_hint;
                    lv->elem_struct_id = cg->last_elem_struct_id;
                }
                if (lv->reg != dest_reg) {
                    emit(cg, MAKE_ABC(OP_MOVE, dest_reg, lv->reg, 0));
                }
                return t;
            }

            if (target->kind == EXPR_INDEX) {
                int mark = cg->next_reg;

                int arr_reg = alloc_reg(cg);
                ValueTypeHint arr_t = gen_expr(cg, target->as.index.object, arr_reg);
                if (arr_t != VT_ARRAY) {
                    cg_error(cg, "index assignment target is not an array value");
                    free_regs_to(cg, mark);
                    return VT_UNKNOWN;
                }

                int idx_reg = alloc_reg(cg);
                gen_expr(cg, target->as.index.index, idx_reg);

                int val_reg = alloc_reg(cg);
                ValueTypeHint val_t = gen_expr(cg, e->as.assign.value, val_reg);

                emit(cg, MAKE_ABC(OP_SETINDEX, arr_reg, idx_reg, val_reg));
                if (dest_reg != val_reg) {
                    emit(cg, MAKE_ABC(OP_MOVE, dest_reg, val_reg, 0));
                }

                free_regs_to(cg, mark);
                return val_t;
            }

            if (target->kind == EXPR_FIELD) {
                if (target->as.field.object->kind == EXPR_IDENT) {
                    LocalVar *lv = find_local(cg, target->as.field.object->as.ident.name);
                    if (lv && is_vec_hint(lv->type_hint)) {
                        int comp = vec_component_index(target->as.field.field);
                        int rank = (lv->type_hint == VT_VEC2) ? 2 : (lv->type_hint == VT_VEC3) ? 3 : 4;
                        if (comp < 0 || comp >= rank) {
                            cg_error(cg, "invalid vector component (expected x/y/z/w within the vector's size)");
                            return VT_UNKNOWN;
                        }
                        int mark = cg->next_reg;
                        int val_reg = alloc_reg(cg);
                        ValueTypeHint val_t = gen_expr(cg, e->as.assign.value, val_reg);
                        emit(cg, MAKE_ABC(OP_VSET, lv->reg, comp, val_reg));
                        if (dest_reg != val_reg) {
                            emit(cg, MAKE_ABC(OP_MOVE, dest_reg, val_reg, 0));
                        }
                        free_regs_to(cg, mark);
                        return val_t;
                    }
                }

                if (target->as.field.object->kind == EXPR_FIELD &&
                    target->as.field.object->as.field.object->kind == EXPR_INDEX) {
                    MeloExpr *comp_field_expr = target;
                    MeloExpr *struct_field_expr = target->as.field.object;
                    MeloExpr *index_expr = struct_field_expr->as.field.object;

                    int mark0 = cg->next_reg;
                    int probe_reg = alloc_reg(cg);
                    ValueTypeHint probe_arr_t = gen_expr(cg, index_expr->as.index.object, probe_reg);
                    int probe_elem_struct_id = cg->last_elem_struct_id;
                    ValueTypeHint probe_elem_hint = cg->last_elem_hint;
                    free_regs_to(cg, mark0);

                    if (probe_arr_t == VT_ARRAY && probe_elem_hint == VT_STRUCT &&
                        probe_elem_struct_id >= 0 && cg->mod->structs[probe_elem_struct_id].is_soa) {
                        int field_idx = find_struct_field(cg, probe_elem_struct_id, struct_field_expr->as.field.field);
                        if (field_idx < 0) {
                            cg_error(cg, "assignment to unknown struct field on #[soa] array element");
                            return VT_UNKNOWN;
                        }
                        int comp = vec_component_index(comp_field_expr->as.field.field);
                        if (comp < 0) {
                            cg_error(cg, "invalid vector component in #[soa] assignment");
                            return VT_UNKNOWN;
                        }

                        int mark = cg->next_reg;
                        int arr_reg = alloc_reg(cg);
                        gen_expr(cg, index_expr->as.index.object, arr_reg);

                        int idx_reg = alloc_reg(cg);
                        gen_expr(cg, index_expr->as.index.index, idx_reg);

                        int vec_reg = alloc_reg(cg);
                        emit(cg, MAKE_ABC(OP_SOA_GETFIELD, vec_reg, arr_reg, idx_reg));
                        emit_raw(cg, (uint32_t)field_idx);

                        int val_reg = alloc_reg(cg);
                        ValueTypeHint val_t = gen_expr(cg, e->as.assign.value, val_reg);

                        emit(cg, MAKE_ABC(OP_VSET, vec_reg, comp, val_reg));
                        emit(cg, MAKE_ABC(OP_SOA_SETFIELD, arr_reg, idx_reg, vec_reg));
                        emit_raw(cg, (uint32_t)field_idx);

                        if (dest_reg != val_reg) {
                            emit(cg, MAKE_ABC(OP_MOVE, dest_reg, val_reg, 0));
                        }
                        free_regs_to(cg, mark);
                        return val_t;
                    }
                }

                if (target->as.field.object->kind == EXPR_INDEX) {
                    MeloExpr *index_expr = target->as.field.object;
                    int mark0 = cg->next_reg;
                    int probe_reg = alloc_reg(cg);
                    ValueTypeHint probe_arr_t = gen_expr(cg, index_expr->as.index.object, probe_reg);
                    int probe_elem_struct_id = cg->last_elem_struct_id;
                    ValueTypeHint probe_elem_hint = cg->last_elem_hint;
                    free_regs_to(cg, mark0);

                    if (probe_arr_t == VT_ARRAY && probe_elem_hint == VT_STRUCT &&
                        probe_elem_struct_id >= 0 && cg->mod->structs[probe_elem_struct_id].is_soa) {

                        int mark = cg->next_reg;
                        int arr_reg = alloc_reg(cg);
                        gen_expr(cg, index_expr->as.index.object, arr_reg);

                        int idx_reg = alloc_reg(cg);
                        gen_expr(cg, index_expr->as.index.index, idx_reg);

                        int field_idx = find_struct_field(cg, probe_elem_struct_id, target->as.field.field);
                        if (field_idx < 0) {
                            cg_error(cg, "assignment to unknown struct field on #[soa] array element");
                            free_regs_to(cg, mark);
                            return VT_UNKNOWN;
                        }

                        int val_reg = alloc_reg(cg);
                        ValueTypeHint val_t = gen_expr(cg, e->as.assign.value, val_reg);

                        emit(cg, MAKE_ABC(OP_SOA_SETFIELD, arr_reg, idx_reg, val_reg));
                        emit_raw(cg, (uint32_t)field_idx);
                        if (dest_reg != val_reg) {
                            emit(cg, MAKE_ABC(OP_MOVE, dest_reg, val_reg, 0));
                        }
                        free_regs_to(cg, mark);
                        return val_t;
                    }
                }

                int mark = cg->next_reg;

                int obj_reg = alloc_reg(cg);
                ValueTypeHint obj_t = gen_expr(cg, target->as.field.object, obj_reg);
                if (obj_t != VT_STRUCT) {
                    cg_error(cg, "field assignment target is not a struct value");
                    free_regs_to(cg, mark);
                    return VT_UNKNOWN;
                }
                int struct_id = cg->last_struct_id;
                int field_idx = find_struct_field(cg, struct_id, target->as.field.field);
                if (field_idx < 0) {
                    cg_error(cg, "assignment to unknown struct field");
                    free_regs_to(cg, mark);
                    return VT_UNKNOWN;
                }

                int val_reg = alloc_reg(cg);
                ValueTypeHint val_t = gen_expr(cg, e->as.assign.value, val_reg);

                emit(cg, MAKE_ABC(OP_SETFIELD, obj_reg, field_idx, val_reg));
                if (dest_reg != val_reg) {
                    emit(cg, MAKE_ABC(OP_MOVE, dest_reg, val_reg, 0));
                }

                free_regs_to(cg, mark);
                return val_t;
            }

            cg_error(cg, "unsupported assignment target");
            return VT_UNKNOWN;
        }
        case EXPR_CALL:
            return gen_call(cg, e, dest_reg);
        case EXPR_FIELD: {
            if (e->as.field.object->kind == EXPR_INDEX) {
                MeloExpr *index_expr = e->as.field.object;
                int mark0 = cg->next_reg;
                int probe_reg = alloc_reg(cg);
                ValueTypeHint probe_arr_t = gen_expr(cg, index_expr->as.index.object, probe_reg);
                int probe_elem_struct_id = cg->last_elem_struct_id;
                ValueTypeHint probe_elem_hint = cg->last_elem_hint;
                free_regs_to(cg, mark0);

                if (probe_arr_t == VT_ARRAY && probe_elem_hint == VT_STRUCT &&
                    probe_elem_struct_id >= 0 && cg->mod->structs[probe_elem_struct_id].is_soa) {

                    int mark = cg->next_reg;
                    int arr_reg = alloc_reg(cg);
                    gen_expr(cg, index_expr->as.index.object, arr_reg);

                    int idx_reg = alloc_reg(cg);
                    gen_expr(cg, index_expr->as.index.index, idx_reg);

                    int field_idx = find_struct_field(cg, probe_elem_struct_id, e->as.field.field);
                    if (field_idx < 0) {
                        cg_error(cg, "access to unknown struct field on #[soa] array element");
                        free_regs_to(cg, mark);
                        return VT_UNKNOWN;
                    }

                    emit(cg, MAKE_ABC(OP_SOA_GETFIELD, dest_reg, arr_reg, idx_reg));
                    emit_raw(cg, (uint32_t)field_idx);
                    free_regs_to(cg, mark);
                    return cg->struct_field_hints[probe_elem_struct_id][field_idx];
                }
            }

            int mark = cg->next_reg;
            int obj_reg = alloc_reg(cg);
            ValueTypeHint obj_t = gen_expr(cg, e->as.field.object, obj_reg);

            if (is_vec_hint(obj_t)) {
                int comp = vec_component_index(e->as.field.field);
                int rank = (obj_t == VT_VEC2) ? 2 : (obj_t == VT_VEC3) ? 3 : 4;
                if (comp < 0 || comp >= rank) {
                    cg_error(cg, "invalid vector component (expected x/y/z/w within the vector's size)");
                    free_regs_to(cg, mark);
                    return VT_UNKNOWN;
                }
                emit(cg, MAKE_ABC(OP_VGET, dest_reg, obj_reg, comp));
                free_regs_to(cg, mark);
                return VT_FLOAT;
            }

            if (obj_t != VT_STRUCT) {
                cg_error(cg, "field access on a non-struct value");
                free_regs_to(cg, mark);
                return VT_UNKNOWN;
            }

            int struct_id = cg->last_struct_id;
            int field_idx = find_struct_field(cg, struct_id, e->as.field.field);
            if (field_idx < 0) {
                cg_error(cg, "access to unknown struct field");
                free_regs_to(cg, mark);
                return VT_UNKNOWN;
            }

            emit(cg, MAKE_ABC(OP_GETFIELD, dest_reg, obj_reg, field_idx));
            free_regs_to(cg, mark);

            ValueTypeHint field_t = cg->struct_field_hints[struct_id][field_idx];
            if (field_t == VT_STRUCT) cg->last_struct_id = cg->struct_field_struct_ids[struct_id][field_idx];
            return field_t;
        }
        case EXPR_INDEX: {
            int mark = cg->next_reg;
            int arr_reg = alloc_reg(cg);
            ValueTypeHint arr_t = gen_expr(cg, e->as.index.object, arr_reg);

            if (arr_t != VT_ARRAY) {
                cg_error(cg, "index access on a non-array value");
                free_regs_to(cg, mark);
                return VT_UNKNOWN;
            }
            ValueTypeHint elem_hint = cg->last_elem_hint;
            int elem_struct_id = cg->last_elem_struct_id;

            int idx_reg = alloc_reg(cg);
            gen_expr(cg, e->as.index.index, idx_reg);

            emit(cg, MAKE_ABC(OP_GETINDEX, dest_reg, arr_reg, idx_reg));
            free_regs_to(cg, mark);

            if (elem_hint == VT_STRUCT) cg->last_struct_id = elem_struct_id;
            return elem_hint;
        }
        case EXPR_YIELD: {
            int mark = cg->next_reg;
            int r = alloc_reg(cg);
            gen_expr(cg, e->as.yield_expr.value, r);
            emit(cg, MAKE_ABC(OP_YIELD, dest_reg, r, 0));
            free_regs_to(cg, mark);
            return VT_UNKNOWN;
        }
    }
    return VT_UNKNOWN;
}

static void gen_stmt(Codegen *cg, MeloStmt *s);

static void gen_block(Codegen *cg, MeloStmt *block) {
    int saved_local_count = cg->local_count;
    int saved_reg_mark = cg->next_reg;

    for (int i = 0; i < block->as.block.count; i++) {
        gen_stmt(cg, block->as.block.stmts[i]);
        if (cg->had_error) return;
    }

    cg->local_count = saved_local_count;
    free_regs_to(cg, saved_reg_mark);
}

static void gen_stmt(Codegen *cg, MeloStmt *s) {
    switch (s->kind) {
        case STMT_VAR_DECL: {
            if (cg->local_count >= MAX_LOCALS) {
                cg_error(cg, "too many local variables in function");
                return;
            }

            int is_scratch = s->as.var_decl.is_scratch;
            int is_vram = s->as.var_decl.is_vram;

            if (is_vram) {
                MeloExpr *init_expr = s->as.var_decl.init;
                if (init_expr->kind != EXPR_CALL ||
                    (strcmp(init_expr->as.call.callee->as.ident.name, "vram_alloc") != 0 &&
                     strcmp(init_expr->as.call.callee->as.ident.name, "vram_alloc_paletted") != 0)) {
                    cg_error(cg, "@vram may only be used on a variable initialized with vram_alloc(...) or vram_alloc_paletted(...)");
                    return;
                }
            }

            if (is_scratch) {
                MeloExprKind init_kind = s->as.var_decl.init->kind;
                if (init_kind != EXPR_ARRAY_LIT && init_kind != EXPR_CALL) {
                    cg_error(cg, "@scratch may only be used on array or struct variable declarations");
                    return;
                }
                cg->in_scratch_init = 1;
            }

            int reg = alloc_reg(cg);
            ValueTypeHint t = gen_expr(cg, s->as.var_decl.init, reg);

            if (is_scratch) {
                cg->in_scratch_init = 0;
                if (t != VT_ARRAY && t != VT_STRUCT) {
                    cg_error(cg, "@scratch may only be used on array or struct variable declarations");
                    return;
                }
            }

            if (is_vram) {
                int upload_id = find_native_name(cg, "upload");
                if (upload_id < 0) {
                    cg_error(cg, "@vram requires the vram stdlib to be registered (missing native 'upload')");
                    return;
                }
                int mark = cg->next_reg;
                int arg_reg = alloc_reg(cg);
                emit(cg, MAKE_ABC(OP_MOVE, arg_reg, reg, 0));
                int discard_reg = alloc_reg(cg);
                emit(cg, MAKE_ABC(OP_NATIVECALL, discard_reg, upload_id, arg_reg));
                emit_raw(cg, (uint32_t)1);
                free_regs_to(cg, mark);
            }

            int struct_id = (t == VT_STRUCT) ? cg->last_struct_id : -1;
            ValueTypeHint elem_hint = (t == VT_ARRAY) ? cg->last_elem_hint : VT_UNKNOWN;
            int elem_struct_id = (t == VT_ARRAY) ? cg->last_elem_struct_id : -1;
            if (s->as.var_decl.type) {
                int annotated_struct_id;
                t = type_hint_from_node(cg, s->as.var_decl.type, &annotated_struct_id);
                if (t == VT_STRUCT) struct_id = annotated_struct_id;
                if (t == VT_ARRAY) {
                    elem_hint = cg->last_elem_hint;
                    elem_struct_id = cg->last_elem_struct_id;
                }
            }

            LocalVar *lv = &cg->locals[cg->local_count++];
            lv->name = s->as.var_decl.name;
            lv->reg = reg;
            lv->type_hint = t;
            lv->struct_id = struct_id;
            lv->elem_type_hint = elem_hint;
            lv->elem_struct_id = elem_struct_id;
            break;
        }
        case STMT_EXPR: {
            int mark = cg->next_reg;
            int r = alloc_reg(cg);
            gen_expr(cg, s->as.expr_stmt.expr, r);
            free_regs_to(cg, mark);
            break;
        }
        case STMT_RETURN: {
            if (s->as.return_stmt.value) {
                int mark = cg->next_reg;
                int r = alloc_reg(cg);
                gen_expr(cg, s->as.return_stmt.value, r);
                emit(cg, MAKE_ABC(OP_RET, r, 0, 0));
                free_regs_to(cg, mark);
            } else {
                emit(cg, MAKE_ABC(OP_RETNIL, 0, 0, 0));
            }
            break;
        }
        case STMT_YIELD: {
            int mark = cg->next_reg;
            int r = alloc_reg(cg);
            gen_expr(cg, s->as.yield_stmt.value, r);
            emit(cg, MAKE_ABC(OP_YIELD, r, r, 0));
            free_regs_to(cg, mark);
            break;
        }
        case STMT_IF: {
            int mark = cg->next_reg;
            int cond_reg = alloc_reg(cg);
            gen_expr(cg, s->as.if_stmt.cond, cond_reg);
            free_regs_to(cg, mark);

            int jmp_else = emit_placeholder(cg);
            MeloInstr saved = cg->mod->code[jmp_else];
            (void)saved;
            cg->mod->code[jmp_else] = MAKE_ABX(OP_JMPIFNOT, cond_reg, 0);

            gen_block(cg, s->as.if_stmt.then_branch);

            if (s->as.if_stmt.else_branch) {
                int jmp_end = emit_placeholder(cg);
                cg->mod->code[jmp_end] = MAKE_ABX(OP_JMP, 0, 0);

                patch_jump(cg, jmp_else, cg->mod->code_count);

                if (s->as.if_stmt.else_branch->kind == STMT_BLOCK) {
                    gen_block(cg, s->as.if_stmt.else_branch);
                } else {
                    gen_stmt(cg, s->as.if_stmt.else_branch);
                }

                patch_jump(cg, jmp_end, cg->mod->code_count);
            } else {
                patch_jump(cg, jmp_else, cg->mod->code_count);
            }
            break;
        }
        case STMT_WHILE: {
            int loop_start = cg->mod->code_count;

            int mark = cg->next_reg;
            int cond_reg = alloc_reg(cg);
            gen_expr(cg, s->as.while_stmt.cond, cond_reg);
            free_regs_to(cg, mark);

            int jmp_exit = emit_placeholder(cg);
            cg->mod->code[jmp_exit] = MAKE_ABX(OP_JMPIFNOT, cond_reg, 0);

            push_loop(cg, loop_start);
            gen_block(cg, s->as.while_stmt.body);

            int jmp_back = emit_placeholder(cg);
            cg->mod->code[jmp_back] = MAKE_ABX(OP_JMP, 0, 0);
            patch_jump(cg, jmp_back, loop_start);

            patch_jump(cg, jmp_exit, cg->mod->code_count);
            pop_loop_and_patch_breaks(cg, cg->mod->code_count);
            break;
        }
        case STMT_FOR: {
            int outer_mark = cg->next_reg;

            int arr_reg = alloc_reg(cg);
            ValueTypeHint arr_t = gen_expr(cg, s->as.for_stmt.iterable, arr_reg);
            if (arr_t != VT_ARRAY) {
                cg_error(cg, "'for..in' requires an array expression");
                free_regs_to(cg, outer_mark);
                return;
            }
            ValueTypeHint elem_hint = cg->last_elem_hint;
            int elem_struct_id = cg->last_elem_struct_id;

            int len_reg = alloc_reg(cg);
            emit(cg, MAKE_ABC(OP_LEN, len_reg, arr_reg, 0));

            int idx_reg = alloc_reg(cg);
            int k_zero = add_const(cg, melo_int(0));
            emit(cg, MAKE_ABX(OP_LOADK, idx_reg, k_zero));

            int loop_start = cg->mod->code_count;

            int cond_reg = alloc_reg(cg);
            emit(cg, MAKE_ABC(OP_LT, cond_reg, idx_reg, len_reg));
            free_regs_to(cg, idx_reg + 1);

            int jmp_exit = emit_placeholder(cg);
            cg->mod->code[jmp_exit] = MAKE_ABX(OP_JMPIFNOT, cond_reg, 0);

            int saved_local_count = cg->local_count;
            int saved_reg_mark = cg->next_reg;

            int elem_reg = alloc_reg(cg);
            emit(cg, MAKE_ABC(OP_GETINDEX, elem_reg, arr_reg, idx_reg));

            if (cg->local_count >= MAX_LOCALS) {
                cg_error(cg, "too many local variables in function");
                free_regs_to(cg, outer_mark);
                return;
            }
            LocalVar *loop_var = &cg->locals[cg->local_count++];
            loop_var->name = s->as.for_stmt.var_name;
            loop_var->reg = elem_reg;
            loop_var->type_hint = elem_hint;
            loop_var->struct_id = elem_struct_id;
            loop_var->elem_type_hint = VT_UNKNOWN;
            loop_var->elem_struct_id = -1;

            push_loop(cg, -1);

            for (int i = 0; i < s->as.for_stmt.body->as.block.count; i++) {
                gen_stmt(cg, s->as.for_stmt.body->as.block.stmts[i]);
                if (cg->had_error) return;
            }

            cg->local_count = saved_local_count;
            free_regs_to(cg, saved_reg_mark);

            int increment_pc = cg->mod->code_count;
            resolve_pending_continues(cg, increment_pc);

            int one_reg = alloc_reg(cg);
            int k_one = add_const(cg, melo_int(1));
            emit(cg, MAKE_ABX(OP_LOADK, one_reg, k_one));
            emit(cg, MAKE_ABC(OP_ADD, idx_reg, idx_reg, one_reg));
            free_regs_to(cg, idx_reg + 1);

            int jmp_back = emit_placeholder(cg);
            cg->mod->code[jmp_back] = MAKE_ABX(OP_JMP, 0, 0);
            patch_jump(cg, jmp_back, loop_start);

            patch_jump(cg, jmp_exit, cg->mod->code_count);
            pop_loop_and_patch_breaks(cg, cg->mod->code_count);

            free_regs_to(cg, outer_mark);
            break;
        }
        case STMT_BLOCK:
            gen_block(cg, s);
            break;
        case STMT_ARENA:
            cg->arena_depth++;
            emit(cg, MAKE_ABC(OP_ARENA_ENTER, 0, 0, 0));
            gen_block(cg, s->as.arena.body);
            emit(cg, MAKE_ABC(OP_ARENA_EXIT, 0, 0, 0));
            cg->arena_depth--;
            break;
        case STMT_BREAK: {
            if (cg->loop_depth <= 0) {
                cg_error(cg, "'break' used outside of a loop");
                break;
            }
            int entry_depth = cg->loop_stack[cg->loop_depth - 1].arena_depth_at_entry;
            for (int i = cg->arena_depth; i > entry_depth; i--) {
                emit(cg, MAKE_ABC(OP_ARENA_EXIT, 0, 0, 0));
            }
            int jmp = emit_placeholder(cg);
            cg->mod->code[jmp] = MAKE_ABX(OP_JMP, 0, 0);
            register_break(cg, jmp);
            break;
        }
        case STMT_CONTINUE:
            emit_continue(cg);
            break;
    }
}

static void register_structs(Codegen *cg, MeloProgram *prog) {
    for (int i = 0; i < prog->count; i++) {
        MeloTopDecl *d = &prog->decls[i];
        if (d->kind != DECL_STRUCT) continue;

        if (cg->struct_entry_count >= MELO_MAX_STRUCTS) {
            cg_error(cg, "too many structs in module");
            return;
        }
        if (d->as.struct_decl.field_count > MELO_MAX_STRUCT_FIELDS) {
            cg_error(cg, "struct has too many fields");
            return;
        }

        int struct_id = cg->struct_entry_count++;
        snprintf(cg->struct_names[struct_id], sizeof(cg->struct_names[struct_id]),
                 "%s", d->as.struct_decl.name);

        MeloStructInfo *si = &cg->mod->structs[struct_id];
        si->field_count = d->as.struct_decl.field_count;
        si->is_soa = 0;
        for (int a = 0; a < d->as.struct_decl.annotation_count; a++) {
            if (strcmp(d->as.struct_decl.annotations[a].name, "soa") == 0) {
                si->is_soa = 1;
            }
        }
        for (int f = 0; f < si->field_count; f++) {
            snprintf(si->field_names[f], sizeof(si->field_names[f]),
                     "%s", d->as.struct_decl.fields[f].name);
        }
    }
    cg->mod->struct_count = cg->struct_entry_count;

    int struct_index = 0;
    for (int i = 0; i < prog->count; i++) {
        MeloTopDecl *d = &prog->decls[i];
        if (d->kind != DECL_STRUCT) continue;

        int struct_id = struct_index++;
        for (int f = 0; f < d->as.struct_decl.field_count; f++) {
            int field_struct_id;
            ValueTypeHint h = type_hint_from_node(cg, d->as.struct_decl.fields[f].type, &field_struct_id);
            cg->struct_field_hints[struct_id][f] = h;
            cg->struct_field_struct_ids[struct_id][f] = field_struct_id;
            cg->mod->structs[struct_id].field_types[f] = (int)h;

            if (cg->mod->structs[struct_id].is_soa && h != VT_FLOAT && h != VT_INT32 &&
                h != VT_VEC2 && h != VT_VEC3 && h != VT_VEC4) {
                cg_error(cg, "#[soa] structs currently only support float/int32/vec2/vec3/vec4 fields");
                return;
            }
        }
    }
}

static void register_functions(Codegen *cg, MeloProgram *prog) {
    for (int i = 0; i < prog->count; i++) {
        MeloTopDecl *d = &prog->decls[i];
        if (d->kind != DECL_FUNC) continue;

        if (cg->func_entry_count >= MELO_MAX_FUNCS) {
            cg_error(cg, "too many functions in module");
            return;
        }

        int func_id = cg->func_entry_count;
        cg->funcs[func_id].name = d->as.func.name;
        cg->funcs[func_id].func_id = func_id;
        cg->func_entry_count++;

        if (strcmp(d->as.func.name, "main") == 0) {
            cg->mod->main_func_id = func_id;
        }
    }
}

static void gen_function(Codegen *cg, MeloFuncDecl *fn, int func_id) {
    cg->local_count = 0;
    cg->next_reg = 0;
    cg->max_reg_used = 0;
    cg->in_hot_func = fn->is_hot;
    cg->hot_func_name = fn->name;
    cg->loop_depth = 0;
    cg->arena_depth = 0;

    int entry_pc = cg->mod->code_count;

    for (int i = 0; i < fn->param_count; i++) {
        int reg = alloc_reg(cg);
        LocalVar *lv = &cg->locals[cg->local_count++];
        lv->name = fn->params[i].name;
        lv->reg = reg;
        lv->type_hint = type_hint_from_node(cg, fn->params[i].type, &lv->struct_id);

        if (fn->is_hot && lv->type_hint == VT_UNKNOWN) {
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "@hot function '%s' requires explicit numeric or vector type for parameter '%s'",
                     fn->name, fn->params[i].name);
            cg_error(cg, msg);
            return;
        }
    }

    if (fn->is_hot && fn->return_type == NULL) {
        char msg[256];
        snprintf(msg, sizeof(msg),
                 "@hot function '%s' requires an explicit return type ('-> type')",
                 fn->name);
        cg_error(cg, msg);
        return;
    }

    gen_stmt(cg, fn->body);

    if (cg->mod->code_count == entry_pc ||
        INSTR_OP(cg->mod->code[cg->mod->code_count - 1]) != OP_RET) {
        emit(cg, MAKE_ABC(OP_RETNIL, 0, 0, 0));
    }

    cg->funcs[func_id].entry_pc = entry_pc;
    cg->mod->funcs[func_id].num_params = fn->param_count;
    cg->mod->funcs[func_id].num_regs = cg->max_reg_used;
    cg->mod->funcs[func_id].is_vukernel = fn->is_vukernel;
    cg->mod->funcs[func_id].is_vu0 = fn->is_vu0;
    cg->mod->funcs[func_id].is_hot = fn->is_hot;
    snprintf(cg->mod->funcs[func_id].name, MELO_FUNC_NAME_LEN, "%s", fn->name);

    cg->in_hot_func = 0;

    if (fn->is_vukernel) {
        size_t instr_bytes = (size_t)(cg->mod->code_count - entry_pc) * sizeof(MeloInstr);
        size_t data_bytes = (size_t)cg->max_reg_used * MELO_VU_REG_SIZE;
        int instr_limit = fn->is_vu0 ? MELO_VU0_INSTR_LIMIT : MELO_VU1_INSTR_LIMIT;
        int data_limit = fn->is_vu0 ? MELO_VU0_DATA_LIMIT : MELO_VU1_DATA_LIMIT;
        const char *vu_name = fn->is_vu0 ? "VU0" : "VU1";

        if (instr_bytes > (size_t)instr_limit) {
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "vukernel '%s' exceeds %s instruction memory: %zu bytes > %d byte limit",
                     fn->name, vu_name, instr_bytes, instr_limit);
            cg_error(cg, msg);
            return;
        }
        if (data_bytes > (size_t)data_limit) {
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "vukernel '%s' exceeds %s data memory: %zu bytes > %d byte limit",
                     fn->name, vu_name, data_bytes, data_limit);
            cg_error(cg, msg);
            return;
        }
    }
}

int melo_codegen_compile(MeloProgram *prog, MeloCompiledModule *out) {
    return melo_codegen_compile_with_natives(prog, out, NULL, 0);
}

int melo_codegen_compile_with_natives(MeloProgram *prog, MeloCompiledModule *out,
                                       const char **native_names, int native_count) {
    memset(out, 0, sizeof(MeloCompiledModule));

    Codegen cg;
    memset(&cg, 0, sizeof(Codegen));
    cg.mod = out;
    cg.native_names = native_names;
    cg.native_count = native_count;
    out->main_func_id = -1;

    register_structs(&cg, prog);
    if (cg.had_error) {
        out->had_error = 1;
        snprintf(out->error_msg, sizeof(out->error_msg), "%s", cg.error_msg);
        return 1;
    }

    register_functions(&cg, prog);
    if (cg.had_error) {
        out->had_error = 1;
        snprintf(out->error_msg, sizeof(out->error_msg), "%s", cg.error_msg);
        return 1;
    }

    int func_index = 0;
    for (int i = 0; i < prog->count; i++) {
        MeloTopDecl *d = &prog->decls[i];
        if (d->kind != DECL_FUNC) continue;

        gen_function(&cg, &d->as.func, func_index);
        func_index++;

        if (cg.had_error) {
            out->had_error = 1;
            snprintf(out->error_msg, sizeof(out->error_msg), "%s", cg.error_msg);
            return 1;
        }
    }

    out->func_count = cg.func_entry_count;

    emit(&cg, MAKE_ABC(OP_HALT, 0, 0, 0));

    for (int i = 0; i < cg.func_entry_count; i++) {
        out->funcs[i].entry = &out->code[cg.funcs[i].entry_pc];
    }

    return 0;
}

void melo_codegen_free(MeloCompiledModule *mod) {
    free(mod->code);
    mod->code = NULL;
}
