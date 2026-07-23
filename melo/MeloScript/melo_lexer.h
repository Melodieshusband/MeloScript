#ifndef MELO_LEXER_H
#define MELO_LEXER_H

typedef enum {
    TOK_EOF = 0,
    TOK_IDENT,
    TOK_INT_LIT,
    TOK_FLOAT_LIT,
    TOK_STRING_LIT,

    TOK_FUNC, TOK_VAR, TOK_LET, TOK_STRUCT, TOK_VUKERNEL,
    TOK_ARENA, TOK_IF, TOK_ELSE, TOK_WHILE, TOK_FOR, TOK_IN,
    TOK_RETURN, TOK_TRUE, TOK_FALSE,
    TOK_YIELD,
    TOK_BREAK, TOK_CONTINUE,

    TOK_LPAREN, TOK_RPAREN, TOK_LBRACE, TOK_RBRACE,
    TOK_LBRACKET, TOK_RBRACKET,
    TOK_COMMA, TOK_COLON, TOK_SEMI, TOK_DOT, TOK_ARROW,
    TOK_AT, TOK_HASH,

    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH, TOK_PERCENT,
    TOK_ASSIGN, TOK_EQ, TOK_NEQ,
    TOK_LT, TOK_LE, TOK_GT, TOK_GE,
    TOK_AND, TOK_OR, TOK_NOT,

    TOK_ERROR
} MeloTokenType;

typedef struct {
    MeloTokenType type;
    const char *start;
    int length;
    int line;
    union {
        long ival;
        double fval;
    } lit;
} MeloToken;

typedef struct {
    const char *src;
    const char *cur;
    int line;
} MeloLexer;

void melo_lexer_init(MeloLexer *lx, const char *source);
MeloToken melo_lexer_next(MeloLexer *lx);
const char *melo_token_type_name(MeloTokenType t);

#endif
