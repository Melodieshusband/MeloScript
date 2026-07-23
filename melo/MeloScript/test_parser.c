#include "melo_parser.h"
#include <stdio.h>

static void print_type(MeloTypeNode *t) {
    if (!t) { printf("void"); return; }
    switch (t->kind) {
        case TYPE_INT32: printf("int32"); break;
        case TYPE_FLOAT: printf("float"); break;
        case TYPE_VEC2: printf("vec2"); break;
        case TYPE_VEC3: printf("vec3"); break;
        case TYPE_VEC4: printf("vec4"); break;
        case TYPE_MAT4: printf("mat4"); break;
        case TYPE_STRING: printf("string"); break;
        case TYPE_BOOL: printf("bool"); break;
        case TYPE_VOID: printf("void"); break;
        case TYPE_NAMED: printf("%s", t->name); break;
        case TYPE_ARRAY:
            printf("array<");
            print_type(t->elem);
            if (t->array_size >= 0) printf(", %d", t->array_size);
            printf(">");
            break;
    }
}

static void print_expr(MeloExpr *e, int indent) {
    printf("%*s", indent, "");
    switch (e->kind) {
        case EXPR_INT_LIT: printf("Int(%ld)\n", e->as.ival); break;
        case EXPR_FLOAT_LIT: printf("Float(%.3f)\n", e->as.fval); break;
        case EXPR_STRING_LIT: printf("String(\"%s\")\n", e->as.strlit.str); break;
        case EXPR_BOOL_LIT: printf("Bool(%s)\n", e->as.ival ? "true" : "false"); break;
        case EXPR_IDENT: printf("Ident(%s)\n", e->as.ident.name); break;
        case EXPR_VEC_CTOR:
            printf("VecCtor(%s)\n", e->as.vec_ctor.type_name);
            for (int i = 0; i < e->as.vec_ctor.arg_count; i++)
                print_expr(e->as.vec_ctor.args[i], indent + 2);
            break;
        case EXPR_ARRAY_LIT:
            printf("ArrayLit(");
            print_type(e->as.array_lit.elem_type);
            printf(", size=%d)\n", e->as.array_lit.array_size);
            for (int i = 0; i < e->as.array_lit.arg_count; i++)
                print_expr(e->as.array_lit.args[i], indent + 2);
            break;
        case EXPR_BINARY:
            printf("Binary(op=%d)\n", e->as.binary.op);
            print_expr(e->as.binary.lhs, indent + 2);
            print_expr(e->as.binary.rhs, indent + 2);
            break;
        case EXPR_UNARY:
            printf("Unary(%c)\n", e->as.unary.op);
            print_expr(e->as.unary.operand, indent + 2);
            break;
        case EXPR_ASSIGN:
            printf("Assign\n");
            print_expr(e->as.assign.target, indent + 2);
            print_expr(e->as.assign.value, indent + 2);
            break;
        case EXPR_CALL:
            printf("Call\n");
            print_expr(e->as.call.callee, indent + 2);
            for (int i = 0; i < e->as.call.arg_count; i++)
                print_expr(e->as.call.args[i], indent + 2);
            break;
        case EXPR_FIELD:
            printf("Field(.%s)\n", e->as.field.field);
            print_expr(e->as.field.object, indent + 2);
            break;
        case EXPR_INDEX:
            printf("Index\n");
            print_expr(e->as.index.object, indent + 2);
            print_expr(e->as.index.index, indent + 2);
            break;
    }
}

static void print_stmt(MeloStmt *s, int indent) {
    printf("%*s", indent, "");
    switch (s->kind) {
        case STMT_EXPR:
            printf("ExprStmt\n");
            print_expr(s->as.expr_stmt.expr, indent + 2);
            break;
        case STMT_VAR_DECL:
            printf("VarDecl(%s: ", s->as.var_decl.name);
            print_type(s->as.var_decl.type);
            printf(")\n");
            print_expr(s->as.var_decl.init, indent + 2);
            break;
        case STMT_IF:
            printf("If\n");
            print_expr(s->as.if_stmt.cond, indent + 2);
            print_stmt(s->as.if_stmt.then_branch, indent + 2);
            if (s->as.if_stmt.else_branch) print_stmt(s->as.if_stmt.else_branch, indent + 2);
            break;
        case STMT_WHILE:
            printf("While\n");
            print_expr(s->as.while_stmt.cond, indent + 2);
            print_stmt(s->as.while_stmt.body, indent + 2);
            break;
        case STMT_FOR:
            printf("For(%s)\n", s->as.for_stmt.var_name);
            print_expr(s->as.for_stmt.iterable, indent + 2);
            print_stmt(s->as.for_stmt.body, indent + 2);
            break;
        case STMT_RETURN:
            printf("Return\n");
            if (s->as.return_stmt.value) print_expr(s->as.return_stmt.value, indent + 2);
            break;
        case STMT_BLOCK:
            printf("Block\n");
            for (int i = 0; i < s->as.block.count; i++)
                print_stmt(s->as.block.stmts[i], indent + 2);
            break;
        case STMT_ARENA:
            printf("Arena(%s)\n", s->as.arena.name ? s->as.arena.name : "");
            print_stmt(s->as.arena.body, indent + 2);
            break;
    }
}

int main(void) {
    const char *src =
        "struct Particle {\n"
        "    position: vec3,\n"
        "    velocity: vec3,\n"
        "}\n"
        "\n"
        "@hot\n"
        "func update(p: Particle, dt: float) -> vec3 {\n"
        "    var v = p.velocity;\n"
        "    return p.position + v * dt;\n"
        "}\n"
        "\n"
        "func main() {\n"
        "    var a = vec4(1.0, 2.0, 3.0, 0.0);\n"
        "    var b = vec4(4.0, 5.0, 6.0, 0.0);\n"
        "    var d = vdot(a, b);\n"
        "    if (d > 0.0) {\n"
        "        return;\n"
        "    }\n"
        "    return;\n"
        "}\n";

    MeloParser parser;
    melo_parser_init(&parser, src);

    MeloProgram *prog = melo_parse_program(&parser);

    if (parser.had_error) {
        printf("PARSE ERROR at line %d: %s\n", parser.error_line, parser.error_msg);
        return 1;
    }

    printf("Parsed %d top-level declarations:\n\n", prog->count);

    for (int i = 0; i < prog->count; i++) {
        MeloTopDecl *d = &prog->decls[i];
        if (d->kind == DECL_STRUCT) {
            printf("struct %s {\n", d->as.struct_decl.name);
            for (int f = 0; f < d->as.struct_decl.field_count; f++) {
                printf("  %s: ", d->as.struct_decl.fields[f].name);
                print_type(d->as.struct_decl.fields[f].type);
                printf("\n");
            }
            printf("}\n\n");
        } else {
            MeloFuncDecl *fn = &d->as.func;
            printf("func %s(", fn->name);
            for (int a = 0; a < fn->param_count; a++) {
                if (a > 0) printf(", ");
                printf("%s: ", fn->params[a].name);
                print_type(fn->params[a].type);
            }
            printf(") -> ");
            print_type(fn->return_type);
            printf(fn->is_vukernel ? " [vukernel]\n" : "\n");
            print_stmt(fn->body, 2);
            printf("\n");
        }
    }

    printf("OK: parsed without errors\n");
    return 0;
}
