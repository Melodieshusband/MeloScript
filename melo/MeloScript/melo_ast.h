#ifndef MELO_AST_H
#define MELO_AST_H

typedef enum {
    TYPE_INT32, TYPE_FLOAT, TYPE_VEC2, TYPE_VEC3, TYPE_VEC4,
    TYPE_MAT4, TYPE_STRING, TYPE_BOOL, TYPE_VOID,
    TYPE_ARRAY, TYPE_NAMED
} MeloTypeKind;

typedef struct MeloTypeNode {
    MeloTypeKind kind;
    char *name;
    struct MeloTypeNode *elem;
    int array_size;
} MeloTypeNode;

typedef enum {
    EXPR_INT_LIT, EXPR_FLOAT_LIT, EXPR_STRING_LIT, EXPR_BOOL_LIT,
    EXPR_IDENT,
    EXPR_VEC_CTOR,
    EXPR_ARRAY_LIT,
    EXPR_BINARY,
    EXPR_UNARY,
    EXPR_ASSIGN,
    EXPR_CALL,
    EXPR_FIELD,
    EXPR_INDEX,
    EXPR_YIELD
} MeloExprKind;

typedef enum {
    BIN_ADD, BIN_SUB, BIN_MUL, BIN_DIV, BIN_MOD,
    BIN_EQ, BIN_NEQ, BIN_LT, BIN_LE, BIN_GT, BIN_GE,
    BIN_AND, BIN_OR
} MeloBinOp;

typedef struct MeloExpr {
    MeloExprKind kind;
    int line;
    union {
        long ival;
        double fval;
        struct { char *str; } strlit;
        struct { char *name; } ident;
        struct { char *type_name; struct MeloExpr **args; int arg_count; } vec_ctor;
        struct { struct MeloTypeNode *elem_type; struct MeloExpr **args; int arg_count; int array_size; } array_lit;
        struct { MeloBinOp op; struct MeloExpr *lhs; struct MeloExpr *rhs; } binary;
        struct { char op; struct MeloExpr *operand; } unary;
        struct { struct MeloExpr *target; struct MeloExpr *value; } assign;
        struct { struct MeloExpr *callee; struct MeloExpr **args; int arg_count; } call;
        struct { struct MeloExpr *object; char *field; } field;
        struct { struct MeloExpr *object; struct MeloExpr *index; } index;
        struct { struct MeloExpr *value; } yield_expr;
    } as;
} MeloExpr;

typedef enum {
    STMT_EXPR, STMT_VAR_DECL, STMT_IF, STMT_WHILE, STMT_FOR,
    STMT_RETURN, STMT_BLOCK, STMT_ARENA, STMT_BREAK, STMT_CONTINUE,
    STMT_YIELD
} MeloStmtKind;

typedef struct MeloStmt {
    MeloStmtKind kind;
    int line;
    union {
        struct { MeloExpr *expr; } expr_stmt;
        struct { char *name; MeloTypeNode *type; MeloExpr *init; int is_scratch; int is_vram; } var_decl;
        struct { MeloExpr *cond; struct MeloStmt *then_branch; struct MeloStmt *else_branch; } if_stmt;
        struct { MeloExpr *cond; struct MeloStmt *body; } while_stmt;
        struct { char *var_name; MeloExpr *iterable; struct MeloStmt *body; } for_stmt;
        struct { MeloExpr *value; } return_stmt;
        struct { MeloExpr *value; } yield_stmt;
        struct { struct MeloStmt **stmts; int count; } block;
        struct { char *name; struct MeloStmt *body; } arena;
    } as;
} MeloStmt;

typedef struct {
    char *name;
    MeloTypeNode *type;
} MeloParam;

typedef struct {
    char *name;
} MeloAnnotation;

typedef struct {
    char *name;
    MeloParam *params;
    int param_count;
    MeloTypeNode *return_type;
    MeloStmt *body;
    MeloAnnotation *annotations;
    int annotation_count;
    int is_vukernel;
    int is_hot;
} MeloFuncDecl;

typedef struct {
    char *name;
    MeloParam *fields;
    int field_count;
    MeloAnnotation *annotations;
    int annotation_count;
} MeloStructDecl;

typedef enum { DECL_FUNC, DECL_STRUCT } MeloDeclKind;

typedef struct {
    MeloDeclKind kind;
    union {
        MeloFuncDecl func;
        MeloStructDecl struct_decl;
    } as;
} MeloTopDecl;

typedef struct {
    MeloTopDecl *decls;
    int count;
} MeloProgram;

#endif
