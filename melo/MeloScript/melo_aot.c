#define _POSIX_C_SOURCE 200809L
#include "melo_aot.h"
#include "melo_vu1.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define SBUF_INIT_CAP 4096

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} SBuf;

static void sbuf_init(SBuf *sb) {
    sb->cap = SBUF_INIT_CAP;
    sb->len = 0;
    sb->data = (char *)malloc(sb->cap);
    sb->data[0] = '\0';
}

static void sbuf_free(SBuf *sb) {
    free(sb->data);
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

static void sbuf_ensure(SBuf *sb, size_t extra) {
    if (sb->len + extra + 1 <= sb->cap) return;
    while (sb->len + extra + 1 > sb->cap) sb->cap *= 2;
    sb->data = (char *)realloc(sb->data, sb->cap);
}

static void sbuf_append(SBuf *sb, const char *s) {
    size_t n = strlen(s);
    sbuf_ensure(sb, n);
    memcpy(sb->data + sb->len, s, n + 1);
    sb->len += n;
}

static void sbuf_appendf(SBuf *sb, const char *fmt, ...) {
    char tmp[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    sbuf_append(sb, tmp);
}

typedef struct {
    MeloProgram *prog;
    MeloFuncDecl *fn;
    SBuf out;
    int indent;
    int had_error;
    char error_msg[256];
    int tmp_counter;
} AotCtx;

static void aot_error(AotCtx *cx, int line, const char *msg) {
    if (cx->had_error) return;
    cx->had_error = 1;
    snprintf(cx->error_msg, sizeof(cx->error_msg),
             "AOT translate error in '%s' (line %d): %s",
             cx->fn->name, line, msg);
}

static void emit_indent(AotCtx *cx) {
    for (int i = 0; i < cx->indent; i++) sbuf_append(&cx->out, "    ");
}

static const char *c_type_name(MeloTypeKind k) {
    switch (k) {
        case TYPE_INT32: return "int32_t";
        case TYPE_FLOAT:  return "float";
        case TYPE_VEC2:   return "MeloVec2";
        case TYPE_VEC3:   return "MeloVec3";
        case TYPE_VEC4:   return "MeloVec4";
        case TYPE_BOOL:   return "int32_t";
        default:          return NULL;
    }
}

static int type_is_vec(MeloTypeKind k) {
    return k == TYPE_VEC2 || k == TYPE_VEC3 || k == TYPE_VEC4;
}

static MeloFuncDecl *find_func(MeloProgram *prog, const char *name) {
    for (int i = 0; i < prog->count; i++) {
        if (prog->decls[i].kind == DECL_FUNC &&
            strcmp(prog->decls[i].as.func.name, name) == 0) {
            return &prog->decls[i].as.func;
        }
    }
    return NULL;
}

typedef struct {
    char *name;
    MeloTypeKind type;
} AotLocal;

#define AOT_MAX_LOCALS 64

typedef struct {
    AotLocal locals[AOT_MAX_LOCALS];
    int count;
} AotScope;

static void scope_init(AotScope *sc) { sc->count = 0; }

static void scope_add(AotScope *sc, const char *name, MeloTypeKind type) {
    if (sc->count >= AOT_MAX_LOCALS) return;
    sc->locals[sc->count].name = (char *)name;
    sc->locals[sc->count].type = type;
    sc->count++;
}

static MeloTypeKind scope_lookup(AotScope *sc, const char *name, int *found) {
    for (int i = sc->count - 1; i >= 0; i--) {
        if (strcmp(sc->locals[i].name, name) == 0) {
            *found = 1;
            return sc->locals[i].type;
        }
    }
    *found = 0;
    return TYPE_VOID;
}

static MeloTypeKind gen_expr(AotCtx *cx, AotScope *sc, MeloExpr *e, SBuf *out);
static void gen_stmt(AotCtx *cx, AotScope *sc, MeloStmt *s);

static MeloTypeKind gen_binary(AotCtx *cx, AotScope *sc, MeloExpr *e, SBuf *out) {
    SBuf lhs, rhs;
    sbuf_init(&lhs);
    sbuf_init(&rhs);
    MeloTypeKind lt = gen_expr(cx, sc, e->as.binary.lhs, &lhs);
    MeloTypeKind rt = gen_expr(cx, sc, e->as.binary.rhs, &rhs);

    MeloBinOp op = e->as.binary.op;
    int is_logic = (op == BIN_AND || op == BIN_OR);
    int is_cmp = (op == BIN_EQ || op == BIN_NEQ || op == BIN_LT ||
                  op == BIN_LE || op == BIN_GT || op == BIN_GE);

    if (type_is_vec(lt) || type_is_vec(rt)) {
        if (lt != rt) {
            aot_error(cx, e->line, "vector binary op requires matching vec types on both sides");
            sbuf_free(&lhs);
            sbuf_free(&rhs);
            return TYPE_VOID;
        }
        if (is_cmp || is_logic) {
            aot_error(cx, e->line, "comparison/logic operators are not supported on vector types in @hot");
            sbuf_free(&lhs);
            sbuf_free(&rhs);
            return TYPE_VOID;
        }
        const char *fn = NULL;
        switch (op) {
            case BIN_ADD: fn = lt == TYPE_VEC2 ? "melo_aot_vec2_add" : lt == TYPE_VEC3 ? "melo_aot_vec3_add" : "melo_aot_vec4_add"; break;
            case BIN_SUB: fn = lt == TYPE_VEC2 ? "melo_aot_vec2_sub" : lt == TYPE_VEC3 ? "melo_aot_vec3_sub" : "melo_aot_vec4_sub"; break;
            case BIN_MUL: fn = lt == TYPE_VEC2 ? "melo_aot_vec2_mul" : lt == TYPE_VEC3 ? "melo_aot_vec3_mul" : "melo_aot_vec4_mul"; break;
            default:
                aot_error(cx, e->line, "unsupported vector binary operator in @hot");
                sbuf_free(&lhs);
                sbuf_free(&rhs);
                return TYPE_VOID;
        }
        sbuf_appendf(out, "%s(%s, %s)", fn, lhs.data, rhs.data);
        sbuf_free(&lhs);
        sbuf_free(&rhs);
        return lt;
    }

    const char *csym = NULL;
    switch (op) {
        case BIN_ADD: csym = "+"; break;
        case BIN_SUB: csym = "-"; break;
        case BIN_MUL: csym = "*"; break;
        case BIN_DIV: csym = "/"; break;
        case BIN_MOD: csym = "%"; break;
        case BIN_EQ:  csym = "=="; break;
        case BIN_NEQ: csym = "!="; break;
        case BIN_LT:  csym = "<"; break;
        case BIN_LE:  csym = "<="; break;
        case BIN_GT:  csym = ">"; break;
        case BIN_GE:  csym = ">="; break;
        case BIN_AND: csym = "&&"; break;
        case BIN_OR:  csym = "||"; break;
    }

    if (op == BIN_MOD && (lt == TYPE_FLOAT || rt == TYPE_FLOAT)) {
        sbuf_appendf(out, "fmodf((float)(%s), (float)(%s))", lhs.data, rhs.data);
        sbuf_free(&lhs);
        sbuf_free(&rhs);
        return TYPE_FLOAT;
    }

    sbuf_appendf(out, "(%s %s %s)", lhs.data, csym, rhs.data);
    sbuf_free(&lhs);
    sbuf_free(&rhs);

    if (is_cmp || is_logic) return TYPE_BOOL;
    if (lt == TYPE_FLOAT || rt == TYPE_FLOAT) return TYPE_FLOAT;
    return TYPE_INT32;
}

static MeloTypeKind gen_expr(AotCtx *cx, AotScope *sc, MeloExpr *e, SBuf *out) {
    switch (e->kind) {
        case EXPR_INT_LIT:
            sbuf_appendf(out, "%ld", e->as.ival);
            return TYPE_INT32;
        case EXPR_FLOAT_LIT:
            sbuf_appendf(out, "%.9gf", e->as.fval);
            return TYPE_FLOAT;
        case EXPR_BOOL_LIT:
            sbuf_append(out, e->as.ival ? "1" : "0");
            return TYPE_BOOL;
        case EXPR_STRING_LIT:
            aot_error(cx, e->line, "string literals are not supported in @hot functions");
            return TYPE_VOID;
        case EXPR_IDENT: {
            int found = 0;
            MeloTypeKind t = scope_lookup(sc, e->as.ident.name, &found);
            if (!found) {
                aot_error(cx, e->line, "unknown identifier (globals are not supported in @hot)");
                return TYPE_VOID;
            }
            sbuf_append(out, e->as.ident.name);
            return t;
        }
        case EXPR_VEC_CTOR: {
            MeloTypeKind vt;
            const char *ctor;
            int expect_n;
            if (strcmp(e->as.vec_ctor.type_name, "vec2") == 0) { vt = TYPE_VEC2; ctor = "melo_aot_mk_vec2"; expect_n = 2; }
            else if (strcmp(e->as.vec_ctor.type_name, "vec3") == 0) { vt = TYPE_VEC3; ctor = "melo_aot_mk_vec3"; expect_n = 3; }
            else { vt = TYPE_VEC4; ctor = "melo_aot_mk_vec4"; expect_n = 4; }
            if (e->as.vec_ctor.arg_count != expect_n) {
                aot_error(cx, e->line, "vector constructor arity mismatch in @hot function");
                return TYPE_VOID;
            }
            sbuf_appendf(out, "%s(", ctor);
            for (int i = 0; i < e->as.vec_ctor.arg_count; i++) {
                if (i > 0) sbuf_append(out, ", ");
                SBuf arg;
                sbuf_init(&arg);
                MeloTypeKind at = gen_expr(cx, sc, e->as.vec_ctor.args[i], &arg);
                if (at != TYPE_FLOAT && at != TYPE_INT32) {
                    aot_error(cx, e->line, "vector constructor components must be numeric");
                }
                sbuf_appendf(out, "(float)(%s)", arg.data);
                sbuf_free(&arg);
            }
            sbuf_append(out, ")");
            return vt;
        }
        case EXPR_ARRAY_LIT:
            aot_error(cx, e->line, "array literals are not supported in @hot functions");
            return TYPE_VOID;
        case EXPR_BINARY:
            return gen_binary(cx, sc, e, out);
        case EXPR_UNARY: {
            SBuf operand;
            sbuf_init(&operand);
            MeloTypeKind t = gen_expr(cx, sc, e->as.unary.operand, &operand);
            if (e->as.unary.op == '!') {
                sbuf_appendf(out, "(!(%s))", operand.data);
                sbuf_free(&operand);
                return TYPE_BOOL;
            }
            if (type_is_vec(t)) {
                aot_error(cx, e->line, "unary minus on vector types is not supported in @hot");
                sbuf_free(&operand);
                return TYPE_VOID;
            }
            sbuf_appendf(out, "(-(%s))", operand.data);
            sbuf_free(&operand);
            return t;
        }
        case EXPR_ASSIGN: {
            if (e->as.assign.target->kind != EXPR_IDENT) {
                aot_error(cx, e->line, "only simple identifier assignment is supported in @hot");
                return TYPE_VOID;
            }
            int found = 0;
            MeloTypeKind lt = scope_lookup(sc, e->as.assign.target->as.ident.name, &found);
            if (!found) {
                aot_error(cx, e->line, "assignment to unknown identifier in @hot");
                return TYPE_VOID;
            }
            SBuf rhs;
            sbuf_init(&rhs);
            gen_expr(cx, sc, e->as.assign.value, &rhs);
            sbuf_appendf(out, "(%s = %s)", e->as.assign.target->as.ident.name, rhs.data);
            sbuf_free(&rhs);
            return lt;
        }
        case EXPR_CALL: {
            if (e->as.call.callee->kind != EXPR_IDENT) {
                aot_error(cx, e->line, "only direct function calls are supported in @hot");
                return TYPE_VOID;
            }
            const char *callee_name = e->as.call.callee->as.ident.name;
            MeloFuncDecl *callee = find_func(cx->prog, callee_name);
            if (!callee || !callee->is_hot) {
                aot_error(cx, e->line, "@hot functions may only call other @hot functions");
                return TYPE_VOID;
            }
            if (callee->param_count != e->as.call.arg_count) {
                aot_error(cx, e->line, "argument count mismatch in @hot call");
                return TYPE_VOID;
            }
            sbuf_appendf(out, "melo_hot_%s(", callee_name);
            for (int i = 0; i < e->as.call.arg_count; i++) {
                if (i > 0) sbuf_append(out, ", ");
                SBuf arg;
                sbuf_init(&arg);
                gen_expr(cx, sc, e->as.call.args[i], &arg);
                sbuf_append(out, arg.data);
                sbuf_free(&arg);
            }
            sbuf_append(out, ")");
            return callee->return_type ? callee->return_type->kind : TYPE_VOID;
        }
        case EXPR_FIELD: {
            SBuf obj;
            sbuf_init(&obj);
            MeloTypeKind ot = gen_expr(cx, sc, e->as.field.object, &obj);
            if (!type_is_vec(ot)) {
                aot_error(cx, e->line, "field access in @hot is only supported on vector types (x/y/z/w)");
                sbuf_free(&obj);
                return TYPE_VOID;
            }
            const char *f = e->as.field.field;
            if (strcmp(f, "x") && strcmp(f, "y") && strcmp(f, "z") && strcmp(f, "w")) {
                aot_error(cx, e->line, "unknown vector field in @hot (expected x/y/z/w)");
                sbuf_free(&obj);
                return TYPE_VOID;
            }
            sbuf_appendf(out, "(%s).%s", obj.data, f);
            sbuf_free(&obj);
            return TYPE_FLOAT;
        }
        case EXPR_INDEX:
            aot_error(cx, e->line, "array indexing is not supported in @hot functions yet");
            return TYPE_VOID;
    }
    aot_error(cx, e->line, "unsupported expression kind in @hot function");
    return TYPE_VOID;
}

static void gen_block(AotCtx *cx, AotScope *sc, MeloStmt *block) {
    AotScope inner = *sc;
    for (int i = 0; i < block->as.block.count && !cx->had_error; i++) {
        gen_stmt(cx, &inner, block->as.block.stmts[i]);
    }
}

static void gen_stmt(AotCtx *cx, AotScope *sc, MeloStmt *s) {
    if (cx->had_error) return;
    switch (s->kind) {
        case STMT_EXPR: {
            SBuf e;
            sbuf_init(&e);
            gen_expr(cx, sc, s->as.expr_stmt.expr, &e);
            emit_indent(cx);
            sbuf_appendf(&cx->out, "(void)(%s);\n", e.data);
            sbuf_free(&e);
            break;
        }
        case STMT_VAR_DECL: {
            if (s->as.var_decl.is_scratch) {
                aot_error(cx, s->line, "@scratch variables are not supported inside @hot functions");
                return;
            }
            if (!s->as.var_decl.type) {
                aot_error(cx, s->line, "local variables in @hot functions require an explicit type");
                return;
            }
            MeloTypeKind t = s->as.var_decl.type->kind;
            const char *ct = c_type_name(t);
            if (!ct) {
                aot_error(cx, s->line, "unsupported local variable type in @hot function");
                return;
            }
            SBuf init;
            sbuf_init(&init);
            if (s->as.var_decl.init) {
                gen_expr(cx, sc, s->as.var_decl.init, &init);
            } else {
                sbuf_append(&init, t == TYPE_FLOAT ? "0.0f" : type_is_vec(t) ? "{0}" : "0");
            }
            emit_indent(cx);
            sbuf_appendf(&cx->out, "%s %s = %s;\n", ct, s->as.var_decl.name, init.data);
            sbuf_free(&init);
            scope_add(sc, s->as.var_decl.name, t);
            break;
        }
        case STMT_IF: {
            SBuf cond;
            sbuf_init(&cond);
            gen_expr(cx, sc, s->as.if_stmt.cond, &cond);
            emit_indent(cx);
            sbuf_appendf(&cx->out, "if (%s) {\n", cond.data);
            sbuf_free(&cond);
            cx->indent++;
            gen_block(cx, sc, s->as.if_stmt.then_branch);
            cx->indent--;
            if (s->as.if_stmt.else_branch) {
                emit_indent(cx);
                sbuf_append(&cx->out, "} else {\n");
                cx->indent++;
                if (s->as.if_stmt.else_branch->kind == STMT_IF) {
                    gen_stmt(cx, sc, s->as.if_stmt.else_branch);
                } else {
                    gen_block(cx, sc, s->as.if_stmt.else_branch);
                }
                cx->indent--;
            }
            emit_indent(cx);
            sbuf_append(&cx->out, "}\n");
            break;
        }
        case STMT_WHILE: {
            SBuf cond;
            sbuf_init(&cond);
            gen_expr(cx, sc, s->as.while_stmt.cond, &cond);
            emit_indent(cx);
            sbuf_appendf(&cx->out, "while (%s) {\n", cond.data);
            sbuf_free(&cond);
            cx->indent++;
            gen_block(cx, sc, s->as.while_stmt.body);
            cx->indent--;
            emit_indent(cx);
            sbuf_append(&cx->out, "}\n");
            break;
        }
        case STMT_FOR:
            aot_error(cx, s->line, "for-in loops are not supported inside @hot functions yet (arrays not supported)");
            break;
        case STMT_RETURN: {
            emit_indent(cx);
            if (s->as.return_stmt.value) {
                SBuf v;
                sbuf_init(&v);
                gen_expr(cx, sc, s->as.return_stmt.value, &v);
                MeloTypeKind rt = cx->fn->return_type ? cx->fn->return_type->kind : TYPE_VOID;
                switch (rt) {
                    case TYPE_INT32: sbuf_appendf(&cx->out, "return melo_int((int32_t)(%s));\n", v.data); break;
                    case TYPE_FLOAT: sbuf_appendf(&cx->out, "return melo_float((float)(%s));\n", v.data); break;
                    case TYPE_BOOL:  sbuf_appendf(&cx->out, "return melo_int((%s) ? 1 : 0);\n", v.data); break;
                    case TYPE_VEC2:  sbuf_appendf(&cx->out, "return melo_aot_wrap_vec2(%s);\n", v.data); break;
                    case TYPE_VEC3:  sbuf_appendf(&cx->out, "return melo_aot_wrap_vec3(%s);\n", v.data); break;
                    case TYPE_VEC4:  sbuf_appendf(&cx->out, "return melo_aot_wrap_vec4(%s);\n", v.data); break;
                    default:
                        aot_error(cx, s->line, "unsupported @hot return type");
                        break;
                }
                sbuf_free(&v);
            } else {
                sbuf_append(&cx->out, "return melo_nil();\n");
            }
            break;
        }
        case STMT_BLOCK:
            emit_indent(cx);
            sbuf_append(&cx->out, "{\n");
            cx->indent++;
            gen_block(cx, sc, s);
            cx->indent--;
            emit_indent(cx);
            sbuf_append(&cx->out, "}\n");
            break;
        case STMT_ARENA:
            aot_error(cx, s->line, "arena blocks are not supported inside @hot functions");
            break;
        case STMT_BREAK:
            emit_indent(cx);
            sbuf_append(&cx->out, "break;\n");
            break;
        case STMT_CONTINUE:
            emit_indent(cx);
            sbuf_append(&cx->out, "continue;\n");
            break;
        case STMT_YIELD:
            aot_error(cx, s->line, "yield is not supported inside @hot functions");
            break;
    }
}

static int translate_one(MeloProgram *prog, MeloFuncDecl *fn, MeloAotUnit *unit, char *errbuf, size_t errbuf_len) {
    AotCtx cx;
    memset(&cx, 0, sizeof(cx));
    cx.prog = prog;
    cx.fn = fn;
    sbuf_init(&cx.out);
    cx.indent = 0;

    for (int i = 0; i < fn->param_count; i++) {
        if (!fn->params[i].type || !c_type_name(fn->params[i].type->kind)) {
            snprintf(errbuf, errbuf_len,
                     "AOT translate error in '%s': parameter '%s' has unsupported type for @hot",
                     fn->name, fn->params[i].name);
            sbuf_free(&cx.out);
            return 1;
        }
    }
    if (!fn->return_type || !c_type_name(fn->return_type->kind)) {
        snprintf(errbuf, errbuf_len,
                 "AOT translate error in '%s': unsupported or missing return type for @hot",
                 fn->name);
        sbuf_free(&cx.out);
        return 1;
    }

    sbuf_appendf(&cx.out, "MeloValue melo_hot_%s(const MeloValue *melo_args) {\n", fn->name);
    cx.indent = 1;
    for (int i = 0; i < fn->param_count; i++) {
        MeloTypeKind pt = fn->params[i].type->kind;
        emit_indent(&cx);
        switch (pt) {
            case TYPE_INT32:
                sbuf_appendf(&cx.out, "int32_t %s = melo_args[%d].as.i32;\n", fn->params[i].name, i);
                break;
            case TYPE_BOOL:
                sbuf_appendf(&cx.out, "int32_t %s = melo_args[%d].as.i32;\n", fn->params[i].name, i);
                break;
            case TYPE_FLOAT:
                sbuf_appendf(&cx.out, "float %s = melo_args[%d].as.f32;\n", fn->params[i].name, i);
                break;
            case TYPE_VEC2:
                sbuf_appendf(&cx.out, "MeloVec2 %s = melo_args[%d].as.v2;\n", fn->params[i].name, i);
                break;
            case TYPE_VEC3:
                sbuf_appendf(&cx.out, "MeloVec3 %s = melo_args[%d].as.v3;\n", fn->params[i].name, i);
                break;
            case TYPE_VEC4:
                sbuf_appendf(&cx.out, "MeloVec4 %s = melo_args[%d].as.v4;\n", fn->params[i].name, i);
                break;
            default:
                break;
        }
    }

    AotScope sc;
    scope_init(&sc);
    for (int i = 0; i < fn->param_count; i++) {
        scope_add(&sc, fn->params[i].name, fn->params[i].type->kind);
    }

    gen_block(&cx, &sc, fn->body);

    cx.indent = 0;
    sbuf_append(&cx.out, "}\n");

    if (cx.had_error) {
        snprintf(errbuf, errbuf_len, "%s", cx.error_msg);
        sbuf_free(&cx.out);
        return 1;
    }

    unit->name = strdup(fn->name);
    unit->c_symbol = (char *)malloc(strlen(fn->name) + 16);
    sprintf(unit->c_symbol, "melo_hot_%s", fn->name);
    unit->c_source = cx.out.data;
    return 0;
}

static int translate_vukernel(MeloFuncDecl *fn, MeloAotUnit *unit, char *errbuf, size_t errbuf_len) {
    MeloVu1Program vu;
    if (melo_vu1_compile(fn, &vu)) {
        snprintf(errbuf, errbuf_len, "%s", vu.error_msg);
        return 1;
    }

    char *asm_buf = (char *)malloc(65536);
    char *bridge_buf = (char *)malloc(65536);
    if (!asm_buf || !bridge_buf) {
        free(asm_buf);
        free(bridge_buf);
        snprintf(errbuf, errbuf_len, "out of memory generating vukernel unit '%s'", fn->name);
        return 1;
    }

    melo_vu1_emit_asm_listing(&vu, asm_buf, 65536);

    uint64_t words[MELO_VU1_MAX_INSTR];
    int word_count = 0;
    melo_vu1_encode_microcode(&vu, words, &word_count);

    size_t off = 0;
    off += snprintf(bridge_buf + off, 65536 - off, "/*\n%s*/\n\n", asm_buf);
    off += snprintf(bridge_buf + off, 65536 - off,
        "u64 melo_vu1_%s_microcode[] MELO_ALIGN16 = {\n", fn->name);
    for (int i = 0; i < word_count; i++) {
        off += snprintf(bridge_buf + off, 65536 - off, "    0x%016llxULL,\n",
                         (unsigned long long)words[i]);
    }
    off += snprintf(bridge_buf + off, 65536 - off, "};\n");
    off += snprintf(bridge_buf + off, 65536 - off,
        "const int melo_vu1_%s_microcode_qwc = %d;\n\n", fn->name, word_count);

    char c_bridge[16384];
    melo_vu1_emit_c_bridge(&vu, c_bridge, sizeof(c_bridge));
    off += snprintf(bridge_buf + off, 65536 - off, "%s", c_bridge);

    unit->name = strdup(fn->name);
    unit->c_symbol = (char *)malloc(strlen(fn->name) + 24);
    sprintf(unit->c_symbol, "melo_vu1_call_%s", fn->name);
    unit->c_source = bridge_buf;

    free(asm_buf);
    return 0;
}

int melo_aot_translate_program(MeloProgram *prog, MeloAotResult *out) {
    memset(out, 0, sizeof(*out));

    for (int i = 0; i < prog->count; i++) {
        if (prog->decls[i].kind != DECL_FUNC) continue;
        MeloFuncDecl *fn = &prog->decls[i].as.func;
        if (!fn->is_hot && !fn->is_vukernel) continue;

        if (out->unit_count >= MELO_AOT_MAX_UNITS) {
            out->had_error = 1;
            snprintf(out->error_msg, sizeof(out->error_msg), "too many @hot functions for AOT translation");
            return 1;
        }

        MeloAotUnit unit;
        memset(&unit, 0, sizeof(unit));
        char errbuf[256];

        int rc;
        if (fn->is_vukernel) {
            rc = translate_vukernel(fn, &unit, errbuf, sizeof(errbuf));
        } else {
            rc = translate_one(prog, fn, &unit, errbuf, sizeof(errbuf));
        }

        if (rc) {
            out->had_error = 1;
            snprintf(out->error_msg, sizeof(out->error_msg), "%s", errbuf);
            return 1;
        }
        out->units[out->unit_count++] = unit;
    }

    return 0;
}

void melo_aot_free(MeloAotResult *out) {
    for (int i = 0; i < out->unit_count; i++) {
        free(out->units[i].name);
        free(out->units[i].c_source);
        free(out->units[i].c_symbol);
    }
    out->unit_count = 0;
}

static const char *melo_aot_runtime_preamble =
    "#include <stdint.h>\n"
    "#include \"melo_value.h\"\n"
    "#include <math.h>\n"
    "\n"
    "static inline MeloVec2 melo_aot_mk_vec2(float x, float y) { MeloVec2 v; v.x = x; v.y = y; return v; }\n"
    "static inline MeloVec3 melo_aot_mk_vec3(float x, float y, float z) { MeloVec3 v; v.x = x; v.y = y; v.z = z; return v; }\n"
    "static inline MeloVec4 melo_aot_mk_vec4(float x, float y, float z, float w) { MeloVec4 v; v.x = x; v.y = y; v.z = z; v.w = w; return v; }\n"
    "\n"
    "static inline MeloVec2 melo_aot_vec2_add(MeloVec2 a, MeloVec2 b) { return melo_aot_mk_vec2(a.x + b.x, a.y + b.y); }\n"
    "static inline MeloVec2 melo_aot_vec2_sub(MeloVec2 a, MeloVec2 b) { return melo_aot_mk_vec2(a.x - b.x, a.y - b.y); }\n"
    "static inline MeloVec2 melo_aot_vec2_mul(MeloVec2 a, MeloVec2 b) { return melo_aot_mk_vec2(a.x * b.x, a.y * b.y); }\n"
    "\n"
    "static inline MeloVec3 melo_aot_vec3_add(MeloVec3 a, MeloVec3 b) { return melo_aot_mk_vec3(a.x + b.x, a.y + b.y, a.z + b.z); }\n"
    "static inline MeloVec3 melo_aot_vec3_sub(MeloVec3 a, MeloVec3 b) { return melo_aot_mk_vec3(a.x - b.x, a.y - b.y, a.z - b.z); }\n"
    "static inline MeloVec3 melo_aot_vec3_mul(MeloVec3 a, MeloVec3 b) { return melo_aot_mk_vec3(a.x * b.x, a.y * b.y, a.z * b.z); }\n"
    "static inline MeloVec4 melo_aot_vec4_add(MeloVec4 a, MeloVec4 b) {\n"
    "#if defined(__mips__) || defined(MELO_PS2_TARGET)\n"
    "    MeloVec4 r;\n"
    "    __asm__ __volatile__(\"lqc2 $vf1, 0(%1)\\n lqc2 $vf2, 0(%2)\\n vadd.xyzw $vf3, $vf1, $vf2\\n sqc2 $vf3, 0(%0)\\n\" : : \"r\"(&r), \"r\"(&a), \"r\"(&b) : \"memory\");\n"
    "    return r;\n"
    "#else\n"
    "    return melo_aot_mk_vec4(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w);\n"
    "#endif\n"
    "}\n"
    "static inline MeloVec4 melo_aot_vec4_sub(MeloVec4 a, MeloVec4 b) {\n"
    "#if defined(__mips__) || defined(MELO_PS2_TARGET)\n"
    "    MeloVec4 r;\n"
    "    __asm__ __volatile__(\"lqc2 $vf1, 0(%1)\\n lqc2 $vf2, 0(%2)\\n vsub.xyzw $vf3, $vf1, $vf2\\n sqc2 $vf3, 0(%0)\\n\" : : \"r\"(&r), \"r\"(&a), \"r\"(&b) : \"memory\");\n"
    "    return r;\n"
    "#else\n"
    "    return melo_aot_mk_vec4(a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w);\n"
    "#endif\n"
    "}\n"
    "static inline MeloVec4 melo_aot_vec4_mul(MeloVec4 a, MeloVec4 b) {\n"
    "#if defined(__mips__) || defined(MELO_PS2_TARGET)\n"
    "    MeloVec4 r;\n"
    "    __asm__ __volatile__(\"lqc2 $vf1, 0(%1)\\n lqc2 $vf2, 0(%2)\\n vmul.xyzw $vf3, $vf1, $vf2\\n sqc2 $vf3, 0(%0)\\n\" : : \"r\"(&r), \"r\"(&a), \"r\"(&b) : \"memory\");\n"
    "    return r;\n"
    "#else\n"
    "    return melo_aot_mk_vec4(a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w);\n"
    "#endif\n"
    "}\n"
    "\n"
    "static inline MeloValue melo_aot_wrap_vec2(MeloVec2 v) { MeloValue r; r.type = MELO_VEC2; r.as.v2 = v; return r; }\n"
    "static inline MeloValue melo_aot_wrap_vec3(MeloVec3 v) { MeloValue r; r.type = MELO_VEC3; r.as.v3 = v; return r; }\n"
    "static inline MeloValue melo_aot_wrap_vec4(MeloVec4 v) { MeloValue r; r.type = MELO_VEC4; r.as.v4 = v; return r; }\n"
    "\n";

int melo_aot_write_unit_file(const MeloAotUnit *unit, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return 1;
    fputs(melo_aot_runtime_preamble, f);
    fputs(unit->c_source, f);
    fclose(f);
    return 0;
}

int melo_aot_write_bundle_file(const MeloAotResult *result, const char *path, const char *header_includes) {
    FILE *f = fopen(path, "w");
    if (!f) return 1;
    fputs(melo_aot_runtime_preamble, f);
    if (header_includes) fputs(header_includes, f);
    for (int i = 0; i < result->unit_count; i++) {
        fputs(result->units[i].c_source, f);
        fputs("\n", f);
    }
    fclose(f);
    return 0;
}
