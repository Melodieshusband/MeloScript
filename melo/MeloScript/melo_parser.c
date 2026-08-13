#include "melo_parser.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static char *str_dup_n(const char *s, int n) {
    char *r = malloc(n + 1);
    memcpy(r, s, n);
    r[n] = '\0';
    return r;
}

static char *str_unescape_n(const char *s, int n) {
    char *r = malloc(n + 1);
    int out = 0;
    for (int i = 0; i < n; i++) {
        if (s[i] == '\\' && i + 1 < n) {
            char next = s[i + 1];
            switch (next) {
                case 'n':  r[out++] = '\n'; i++; break;
                case 't':  r[out++] = '\t'; i++; break;
                case 'r':  r[out++] = '\r'; i++; break;
                case '"':  r[out++] = '"';  i++; break;
                case '\\': r[out++] = '\\'; i++; break;
                case '0':  r[out++] = '\0'; i++; break;
                default:   r[out++] = s[i]; break;
            }
        } else {
            r[out++] = s[i];
        }
    }
    r[out] = '\0';
    return r;
}

static MeloAnnotation *parse_annotations(MeloParser *p, int *out_count);

static void advance_tok(MeloParser *p) {
    p->previous = p->current;
    for (;;) {
        p->current = melo_lexer_next(&p->lexer);
        if (p->current.type != TOK_ERROR) break;
        p->had_error = 1;
        snprintf(p->error_msg, sizeof(p->error_msg), "%.*s", p->current.length, p->current.start);
        p->error_line = p->current.line;
    }
}

static int check(MeloParser *p, MeloTokenType type) {
    return p->current.type == type;
}

static int match_tok(MeloParser *p, MeloTokenType type) {
    if (!check(p, type)) return 0;
    advance_tok(p);
    return 1;
}

static void parser_error(MeloParser *p, const char *msg) {
    if (p->had_error) return;
    p->had_error = 1;
    snprintf(p->error_msg, sizeof(p->error_msg), "%s (got '%.*s')",
              msg, p->current.length, p->current.start);
    p->error_line = p->current.line;
}

static void expect(MeloParser *p, MeloTokenType type, const char *msg) {
    if (check(p, type)) {
        advance_tok(p);
        return;
    }
    parser_error(p, msg);
}

void melo_parser_init(MeloParser *p, const char *source) {
    melo_lexer_init(&p->lexer, source);
    p->had_error = 0;
    p->error_msg[0] = '\0';
    p->error_line = 0;
    advance_tok(p);
}

static MeloTypeNode *parse_type(MeloParser *p) {
    MeloTypeNode *t = calloc(1, sizeof(MeloTypeNode));

    if (match_tok(p, TOK_IDENT)) {
        char *name = str_dup_n(p->previous.start, p->previous.length);
        if (strcmp(name, "int32") == 0) t->kind = TYPE_INT32;
        else if (strcmp(name, "float") == 0) t->kind = TYPE_FLOAT;
        else if (strcmp(name, "vec2") == 0) t->kind = TYPE_VEC2;
        else if (strcmp(name, "vec3") == 0) t->kind = TYPE_VEC3;
        else if (strcmp(name, "vec4") == 0) t->kind = TYPE_VEC4;
        else if (strcmp(name, "mat4") == 0) t->kind = TYPE_MAT4;
        else if (strcmp(name, "string") == 0) t->kind = TYPE_STRING;
        else if (strcmp(name, "bool") == 0) t->kind = TYPE_BOOL;
        else if (strcmp(name, "array") == 0) {
            t->kind = TYPE_ARRAY;
            expect(p, TOK_LT, "expected '<' after 'array'");
            t->elem = parse_type(p);
            t->array_size = -1;
            if (match_tok(p, TOK_COMMA)) {
                expect(p, TOK_INT_LIT, "expected array size");
                t->array_size = (int)p->previous.lit.ival;
            }
            expect(p, TOK_GT, "expected '>' to close array type");
            free(name);
            return t;
        } else {
            t->kind = TYPE_NAMED;
            t->name = name;
            return t;
        }
        free(name);
        return t;
    }

    parser_error(p, "expected type name");
    t->kind = TYPE_VOID;
    return t;
}

static MeloExpr *parse_expr(MeloParser *p);
static MeloExpr *parse_assignment(MeloParser *p);

static MeloExpr *new_expr(MeloExprKind kind, int line) {
    MeloExpr *e = calloc(1, sizeof(MeloExpr));
    e->kind = kind;
    e->line = line;
    return e;
}

static MeloExpr **parse_arg_list(MeloParser *p, int *out_count) {
    int cap = 4;
    MeloExpr **args = malloc(sizeof(MeloExpr *) * cap);
    int count = 0;

    if (!check(p, TOK_RPAREN)) {
        do {
            if (count >= cap) {
                cap *= 2;
                args = realloc(args, sizeof(MeloExpr *) * cap);
            }
            args[count++] = parse_expr(p);
        } while (match_tok(p, TOK_COMMA));
    }

    *out_count = count;
    return args;
}

static int is_vec_ctor_name(const char *s, int len) {
    return (len == 4 && strncmp(s, "vec4", 4) == 0) ||
           (len == 4 && strncmp(s, "vec3", 4) == 0) ||
           (len == 4 && strncmp(s, "vec2", 4) == 0);
}

static MeloExpr *parse_primary(MeloParser *p) {
    int line = p->current.line;

    if (match_tok(p, TOK_INT_LIT)) {
        MeloExpr *e = new_expr(EXPR_INT_LIT, line);
        e->as.ival = p->previous.lit.ival;
        return e;
    }
    if (match_tok(p, TOK_FLOAT_LIT)) {
        MeloExpr *e = new_expr(EXPR_FLOAT_LIT, line);
        e->as.fval = p->previous.lit.fval;
        return e;
    }
    if (match_tok(p, TOK_STRING_LIT)) {
        MeloExpr *e = new_expr(EXPR_STRING_LIT, line);
        e->as.strlit.str = str_unescape_n(p->previous.start + 1, p->previous.length - 2);
        return e;
    }
    if (match_tok(p, TOK_TRUE)) {
        MeloExpr *e = new_expr(EXPR_BOOL_LIT, line);
        e->as.ival = 1;
        return e;
    }
    if (match_tok(p, TOK_FALSE)) {
        MeloExpr *e = new_expr(EXPR_BOOL_LIT, line);
        e->as.ival = 0;
        return e;
    }
    if (check(p, TOK_IDENT) && is_vec_ctor_name(p->current.start, p->current.length)) {
        char *name = str_dup_n(p->current.start, p->current.length);
        advance_tok(p);
        if (check(p, TOK_LPAREN)) {
            advance_tok(p);
            MeloExpr *e = new_expr(EXPR_VEC_CTOR, line);
            e->as.vec_ctor.type_name = name;
            e->as.vec_ctor.args = parse_arg_list(p, &e->as.vec_ctor.arg_count);
            expect(p, TOK_RPAREN, "expected ')' after vector constructor args");
            return e;
        }
        MeloExpr *e = new_expr(EXPR_IDENT, line);
        e->as.ident.name = name;
        return e;
    }
    if (check(p, TOK_IDENT) && p->current.length == 5 && strncmp(p->current.start, "array", 5) == 0) {
        advance_tok(p);
        expect(p, TOK_LT, "expected '<' after 'array'");
        MeloTypeNode *elem_type = parse_type(p);
        int array_size = -1;
        if (match_tok(p, TOK_COMMA)) {
            expect(p, TOK_INT_LIT, "expected array size");
            array_size = (int)p->previous.lit.ival;
        }
        expect(p, TOK_GT, "expected '>' to close array type");
        expect(p, TOK_LPAREN, "expected '(' after array type in constructor");
        MeloExpr *e = new_expr(EXPR_ARRAY_LIT, line);
        e->as.array_lit.elem_type = elem_type;
        e->as.array_lit.args = parse_arg_list(p, &e->as.array_lit.arg_count);
        e->as.array_lit.array_size = array_size;
        expect(p, TOK_RPAREN, "expected ')' after array constructor args");
        return e;
    }
    if (match_tok(p, TOK_IDENT)) {
        MeloExpr *e = new_expr(EXPR_IDENT, line);
        e->as.ident.name = str_dup_n(p->previous.start, p->previous.length);
        return e;
    }
    if (match_tok(p, TOK_LPAREN)) {
        MeloExpr *e = parse_expr(p);
        expect(p, TOK_RPAREN, "expected ')' after expression");
        return e;
    }

    parser_error(p, "expected expression");
    return new_expr(EXPR_INT_LIT, line);
}

static MeloExpr *parse_postfix(MeloParser *p) {
    MeloExpr *e = parse_primary(p);

    for (;;) {
        int line = p->current.line;
        if (match_tok(p, TOK_LPAREN)) {
            MeloExpr *call = new_expr(EXPR_CALL, line);
            call->as.call.callee = e;
            call->as.call.args = parse_arg_list(p, &call->as.call.arg_count);
            expect(p, TOK_RPAREN, "expected ')' after call arguments");
            e = call;
        } else if (match_tok(p, TOK_DOT)) {
            expect(p, TOK_IDENT, "expected field name after '.'");
            MeloExpr *field = new_expr(EXPR_FIELD, line);
            field->as.field.object = e;
            field->as.field.field = str_dup_n(p->previous.start, p->previous.length);
            e = field;
        } else if (match_tok(p, TOK_LBRACKET)) {
            MeloExpr *idx = new_expr(EXPR_INDEX, line);
            idx->as.index.object = e;
            idx->as.index.index = parse_expr(p);
            expect(p, TOK_RBRACKET, "expected ']' after index expression");
            e = idx;
        } else {
            break;
        }
    }

    return e;
}

static MeloExpr *parse_unary(MeloParser *p) {
    int line = p->current.line;
    if (match_tok(p, TOK_MINUS)) {
        MeloExpr *e = new_expr(EXPR_UNARY, line);
        e->as.unary.op = '-';
        e->as.unary.operand = parse_unary(p);
        return e;
    }
    if (match_tok(p, TOK_NOT)) {
        MeloExpr *e = new_expr(EXPR_UNARY, line);
        e->as.unary.op = '!';
        e->as.unary.operand = parse_unary(p);
        return e;
    }
    if (match_tok(p, TOK_YIELD)) {
        MeloExpr *e = new_expr(EXPR_YIELD, line);
        e->as.yield_expr.value = parse_assignment(p);
        return e;
    }
    return parse_postfix(p);
}

static MeloExpr *make_binary(MeloBinOp op, MeloExpr *lhs, MeloExpr *rhs, int line) {
    MeloExpr *e = new_expr(EXPR_BINARY, line);
    e->as.binary.op = op;
    e->as.binary.lhs = lhs;
    e->as.binary.rhs = rhs;
    return e;
}

static MeloExpr *parse_factor(MeloParser *p) {
    MeloExpr *e = parse_unary(p);
    for (;;) {
        int line = p->current.line;
        if (match_tok(p, TOK_STAR)) e = make_binary(BIN_MUL, e, parse_unary(p), line);
        else if (match_tok(p, TOK_SLASH)) e = make_binary(BIN_DIV, e, parse_unary(p), line);
        else if (match_tok(p, TOK_PERCENT)) e = make_binary(BIN_MOD, e, parse_unary(p), line);
        else break;
    }
    return e;
}

static MeloExpr *parse_term(MeloParser *p) {
    MeloExpr *e = parse_factor(p);
    for (;;) {
        int line = p->current.line;
        if (match_tok(p, TOK_PLUS)) e = make_binary(BIN_ADD, e, parse_factor(p), line);
        else if (match_tok(p, TOK_MINUS)) e = make_binary(BIN_SUB, e, parse_factor(p), line);
        else break;
    }
    return e;
}

static MeloExpr *parse_comparison(MeloParser *p) {
    MeloExpr *e = parse_term(p);
    for (;;) {
        int line = p->current.line;
        if (match_tok(p, TOK_LT)) e = make_binary(BIN_LT, e, parse_term(p), line);
        else if (match_tok(p, TOK_LE)) e = make_binary(BIN_LE, e, parse_term(p), line);
        else if (match_tok(p, TOK_GT)) e = make_binary(BIN_GT, e, parse_term(p), line);
        else if (match_tok(p, TOK_GE)) e = make_binary(BIN_GE, e, parse_term(p), line);
        else break;
    }
    return e;
}

static MeloExpr *parse_equality(MeloParser *p) {
    MeloExpr *e = parse_comparison(p);
    for (;;) {
        int line = p->current.line;
        if (match_tok(p, TOK_EQ)) e = make_binary(BIN_EQ, e, parse_comparison(p), line);
        else if (match_tok(p, TOK_NEQ)) e = make_binary(BIN_NEQ, e, parse_comparison(p), line);
        else break;
    }
    return e;
}

static MeloExpr *parse_logic_and(MeloParser *p) {
    MeloExpr *e = parse_equality(p);
    while (check(p, TOK_AND)) {
        int line = p->current.line;
        advance_tok(p);
        e = make_binary(BIN_AND, e, parse_equality(p), line);
    }
    return e;
}

static MeloExpr *parse_logic_or(MeloParser *p) {
    MeloExpr *e = parse_logic_and(p);
    while (check(p, TOK_OR)) {
        int line = p->current.line;
        advance_tok(p);
        e = make_binary(BIN_OR, e, parse_logic_and(p), line);
    }
    return e;
}

static MeloExpr *parse_assignment(MeloParser *p) {
    MeloExpr *e = parse_logic_or(p);
    if (match_tok(p, TOK_ASSIGN)) {
        int line = p->previous.line;
        MeloExpr *value = parse_assignment(p);
        MeloExpr *a = new_expr(EXPR_ASSIGN, line);
        a->as.assign.target = e;
        a->as.assign.value = value;
        return a;
    }
    return e;
}

static MeloExpr *parse_expr(MeloParser *p) {
    return parse_assignment(p);
}

static MeloStmt *new_stmt(MeloStmtKind kind, int line) {
    MeloStmt *s = calloc(1, sizeof(MeloStmt));
    s->kind = kind;
    s->line = line;
    return s;
}

static MeloStmt *parse_statement(MeloParser *p);

static MeloStmt *parse_block(MeloParser *p) {
    int line = p->current.line;
    expect(p, TOK_LBRACE, "expected '{'");
    MeloStmt *block = new_stmt(STMT_BLOCK, line);

    int cap = 8;
    block->as.block.stmts = malloc(sizeof(MeloStmt *) * cap);
    block->as.block.count = 0;

    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        if (block->as.block.count >= cap) {
            cap *= 2;
            block->as.block.stmts = realloc(block->as.block.stmts, sizeof(MeloStmt *) * cap);
        }
        const char *before = p->current.start;
        block->as.block.stmts[block->as.block.count++] = parse_statement(p);
        if (p->had_error && p->current.start == before) {
            advance_tok(p);
        }
    }

    expect(p, TOK_RBRACE, "expected '}' to close block");
    return block;
}

static MeloStmt *parse_var_decl_stmt(MeloParser *p, MeloAnnotation *anns, int ann_count) {
    int line = p->current.line;
    advance_tok(p);
    expect(p, TOK_IDENT, "expected variable name");
    char *name = str_dup_n(p->previous.start, p->previous.length);

    MeloTypeNode *type = NULL;
    if (match_tok(p, TOK_COLON)) {
        type = parse_type(p);
    }

    expect(p, TOK_ASSIGN, "expected '=' in variable declaration");
    MeloExpr *init = parse_expr(p);
    expect(p, TOK_SEMI, "expected ';' after variable declaration");

    int is_scratch = 0;
    int is_vram = 0;
    for (int i = 0; i < ann_count; i++) {
        if (strcmp(anns[i].name, "scratch") == 0) {
            is_scratch = 1;
        }
        if (strcmp(anns[i].name, "vram") == 0) {
            is_vram = 1;
        }
    }

    MeloStmt *s = new_stmt(STMT_VAR_DECL, line);
    s->as.var_decl.name = name;
    s->as.var_decl.type = type;
    s->as.var_decl.init = init;
    s->as.var_decl.is_scratch = is_scratch;
    s->as.var_decl.is_vram = is_vram;
    return s;
}

static MeloStmt *parse_statement(MeloParser *p) {
    int line = p->current.line;

    if (check(p, TOK_AT) || check(p, TOK_HASH)) {
        int ann_count = 0;
        MeloAnnotation *anns = parse_annotations(p, &ann_count);
        if (!check(p, TOK_VAR) && !check(p, TOK_LET)) {
            parser_error(p, "annotations are only allowed before 'var'/'let' declarations here");
            return new_stmt(STMT_EXPR, line);
        }
        return parse_var_decl_stmt(p, anns, ann_count);
    }

    if (check(p, TOK_VAR) || check(p, TOK_LET)) {
        return parse_var_decl_stmt(p, NULL, 0);
    }
    if (match_tok(p, TOK_IF)) {
        expect(p, TOK_LPAREN, "expected '(' after 'if'");
        MeloExpr *cond = parse_expr(p);
        expect(p, TOK_RPAREN, "expected ')' after if condition");
        MeloStmt *then_branch = parse_block(p);
        MeloStmt *else_branch = NULL;
        if (match_tok(p, TOK_ELSE)) {
            if (check(p, TOK_IF)) {
                else_branch = parse_statement(p);
            } else {
                else_branch = parse_block(p);
            }
        }
        MeloStmt *s = new_stmt(STMT_IF, line);
        s->as.if_stmt.cond = cond;
        s->as.if_stmt.then_branch = then_branch;
        s->as.if_stmt.else_branch = else_branch;
        return s;
    }
    if (match_tok(p, TOK_WHILE)) {
        expect(p, TOK_LPAREN, "expected '(' after 'while'");
        MeloExpr *cond = parse_expr(p);
        expect(p, TOK_RPAREN, "expected ')' after while condition");
        MeloStmt *body = parse_block(p);
        MeloStmt *s = new_stmt(STMT_WHILE, line);
        s->as.while_stmt.cond = cond;
        s->as.while_stmt.body = body;
        return s;
    }
    if (match_tok(p, TOK_FOR)) {
        expect(p, TOK_IDENT, "expected loop variable name");
        char *var_name = str_dup_n(p->previous.start, p->previous.length);
        expect(p, TOK_IN, "expected 'in' in for loop");
        MeloExpr *iterable = parse_expr(p);
        MeloStmt *body = parse_block(p);
        MeloStmt *s = new_stmt(STMT_FOR, line);
        s->as.for_stmt.var_name = var_name;
        s->as.for_stmt.iterable = iterable;
        s->as.for_stmt.body = body;
        return s;
    }
    if (match_tok(p, TOK_RETURN)) {
        MeloExpr *value = NULL;
        if (!check(p, TOK_SEMI)) {
            value = parse_expr(p);
        }
        expect(p, TOK_SEMI, "expected ';' after return statement");
        MeloStmt *s = new_stmt(STMT_RETURN, line);
        s->as.return_stmt.value = value;
        return s;
    }
    if (match_tok(p, TOK_YIELD)) {
        MeloExpr *value = parse_expr(p);
        expect(p, TOK_SEMI, "expected ';' after yield statement");
        MeloStmt *s = new_stmt(STMT_YIELD, line);
        s->as.yield_stmt.value = value;
        return s;
    }
    if (match_tok(p, TOK_BREAK)) {
        expect(p, TOK_SEMI, "expected ';' after 'break'");
        return new_stmt(STMT_BREAK, line);
    }
    if (match_tok(p, TOK_CONTINUE)) {
        expect(p, TOK_SEMI, "expected ';' after 'continue'");
        return new_stmt(STMT_CONTINUE, line);
    }
    if (match_tok(p, TOK_ARENA)) {
        char *name = NULL;
        if (check(p, TOK_IDENT)) {
            name = str_dup_n(p->current.start, p->current.length);
            advance_tok(p);
        }
        MeloStmt *body = parse_block(p);
        MeloStmt *s = new_stmt(STMT_ARENA, line);
        s->as.arena.name = name;
        s->as.arena.body = body;
        return s;
    }
    if (check(p, TOK_LBRACE)) {
        return parse_block(p);
    }

    MeloExpr *e = parse_expr(p);
    expect(p, TOK_SEMI, "expected ';' after expression");
    MeloStmt *s = new_stmt(STMT_EXPR, line);
    s->as.expr_stmt.expr = e;
    return s;
}

static MeloAnnotation *parse_annotations(MeloParser *p, int *out_count) {
    int cap = 4;
    MeloAnnotation *anns = malloc(sizeof(MeloAnnotation) * cap);
    int count = 0;

    while (check(p, TOK_AT) || check(p, TOK_HASH)) {
        int is_hash = check(p, TOK_HASH);
        advance_tok(p);
        if (is_hash) {
            expect(p, TOK_LBRACKET, "expected '[' after '#'");
        }
        expect(p, TOK_IDENT, "expected annotation name");
        if (count >= cap) {
            cap *= 2;
            anns = realloc(anns, sizeof(MeloAnnotation) * cap);
        }
        anns[count].name = str_dup_n(p->previous.start, p->previous.length);
        count++;
        if (is_hash) {
            expect(p, TOK_RBRACKET, "expected ']' after annotation");
        }
    }

    *out_count = count;
    return anns;
}

static MeloParam *parse_param_list(MeloParser *p, int *out_count) {
    int cap = 4;
    MeloParam *params = malloc(sizeof(MeloParam) * cap);
    int count = 0;

    if (!check(p, TOK_RPAREN)) {
        do {
            if (count >= cap) {
                cap *= 2;
                params = realloc(params, sizeof(MeloParam) * cap);
            }
            expect(p, TOK_IDENT, "expected parameter name");
            params[count].name = str_dup_n(p->previous.start, p->previous.length);
            expect(p, TOK_COLON, "expected ':' after parameter name");
            params[count].type = parse_type(p);
            count++;
        } while (match_tok(p, TOK_COMMA));
    }

    *out_count = count;
    return params;
}

static MeloTopDecl parse_func_decl(MeloParser *p, MeloAnnotation *anns, int ann_count) {
    advance_tok(p);
    expect(p, TOK_IDENT, "expected function name");
    char *name = str_dup_n(p->previous.start, p->previous.length);

    expect(p, TOK_LPAREN, "expected '(' after function name");
    int param_count;
    MeloParam *params = parse_param_list(p, &param_count);
    expect(p, TOK_RPAREN, "expected ')' after parameters");

    MeloTypeNode *ret_type = NULL;
    if (match_tok(p, TOK_ARROW)) {
        ret_type = parse_type(p);
    }

    MeloStmt *body = parse_block(p);

    MeloTopDecl decl;
    decl.kind = DECL_FUNC;
    decl.as.func.name = name;
    decl.as.func.params = params;
    decl.as.func.param_count = param_count;
    decl.as.func.return_type = ret_type;
    decl.as.func.body = body;
    decl.as.func.annotations = anns;
    decl.as.func.annotation_count = ann_count;
    decl.as.func.is_vukernel = 0;
    decl.as.func.is_hot = 0;
    for (int i = 0; i < ann_count; i++) {
        if (strcmp(anns[i].name, "hot") == 0) {
            decl.as.func.is_hot = 1;
        }
    }
    return decl;
}

static MeloTopDecl parse_vukernel_decl(MeloParser *p) {
    advance_tok(p);
    expect(p, TOK_IDENT, "expected vukernel name");
    char *name = str_dup_n(p->previous.start, p->previous.length);

    expect(p, TOK_LPAREN, "expected '(' after vukernel name");
    int param_count;
    MeloParam *params = parse_param_list(p, &param_count);
    expect(p, TOK_RPAREN, "expected ')' after parameters");

    expect(p, TOK_ARROW, "vukernel requires explicit '->' return type");
    MeloTypeNode *ret_type = parse_type(p);

    MeloStmt *body = parse_block(p);

    MeloTopDecl decl;
    decl.kind = DECL_FUNC;
    decl.as.func.name = name;
    decl.as.func.params = params;
    decl.as.func.param_count = param_count;
    decl.as.func.return_type = ret_type;
    decl.as.func.body = body;
    decl.as.func.annotations = NULL;
    decl.as.func.annotation_count = 0;
    decl.as.func.is_vukernel = 1;
    decl.as.func.is_hot = 0;
    return decl;
}

static MeloTopDecl parse_struct_decl(MeloParser *p, MeloAnnotation *anns, int ann_count) {
    advance_tok(p);
    expect(p, TOK_IDENT, "expected struct name");
    char *name = str_dup_n(p->previous.start, p->previous.length);

    expect(p, TOK_LBRACE, "expected '{' after struct name");

    int cap = 4;
    MeloParam *fields = malloc(sizeof(MeloParam) * cap);
    int count = 0;

    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        if (count >= cap) {
            cap *= 2;
            fields = realloc(fields, sizeof(MeloParam) * cap);
        }
        expect(p, TOK_IDENT, "expected field name");
        fields[count].name = str_dup_n(p->previous.start, p->previous.length);
        expect(p, TOK_COLON, "expected ':' after field name");
        fields[count].type = parse_type(p);
        count++;
        match_tok(p, TOK_COMMA);
    }

    expect(p, TOK_RBRACE, "expected '}' to close struct");

    MeloTopDecl decl;
    decl.kind = DECL_STRUCT;
    decl.as.struct_decl.name = name;
    decl.as.struct_decl.fields = fields;
    decl.as.struct_decl.field_count = count;
    decl.as.struct_decl.annotations = anns;
    decl.as.struct_decl.annotation_count = ann_count;
    return decl;
}

MeloProgram *melo_parse_program(MeloParser *p) {
    int cap = 8;
    MeloTopDecl *decls = malloc(sizeof(MeloTopDecl) * cap);
    int count = 0;

    while (!check(p, TOK_EOF) && !p->had_error) {
        int ann_count = 0;
        MeloAnnotation *anns = parse_annotations(p, &ann_count);

        if (count >= cap) {
            cap *= 2;
            decls = realloc(decls, sizeof(MeloTopDecl) * cap);
        }

        if (check(p, TOK_FUNC)) {
            decls[count++] = parse_func_decl(p, anns, ann_count);
        } else if (check(p, TOK_STRUCT)) {
            decls[count++] = parse_struct_decl(p, anns, ann_count);
        } else if (check(p, TOK_VUKERNEL)) {
            decls[count++] = parse_vukernel_decl(p);
        } else {
            parser_error(p, "expected 'func', 'struct' or 'vukernel' at top level");
            break;
        }
    }

    MeloProgram *prog = malloc(sizeof(MeloProgram));
    prog->decls = decls;
    prog->count = count;
    return prog;
}
