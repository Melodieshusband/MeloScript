#include "melo_lexer.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

void melo_lexer_init(MeloLexer *lx, const char *source) {
    lx->src = source;
    lx->cur = source;
    lx->line = 1;
}

static int is_at_end(MeloLexer *lx) {
    return *lx->cur == '\0';
}

static char advance(MeloLexer *lx) {
    char c = *lx->cur;
    lx->cur++;
    if (c == '\n') lx->line++;
    return c;
}

static char peek(MeloLexer *lx) {
    return *lx->cur;
}

static char peek_next(MeloLexer *lx) {
    if (is_at_end(lx)) return '\0';
    return lx->cur[1];
}

static int match(MeloLexer *lx, char expected) {
    if (is_at_end(lx)) return 0;
    if (*lx->cur != expected) return 0;
    lx->cur++;
    return 1;
}

static void skip_whitespace(MeloLexer *lx) {
    for (;;) {
        char c = peek(lx);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance(lx);
        } else if (c == '/' && peek_next(lx) == '/') {
            while (!is_at_end(lx) && peek(lx) != '\n') advance(lx);
        } else {
            break;
        }
    }
}

static MeloToken make_token(MeloLexer *lx, MeloTokenType type, const char *start) {
    MeloToken t;
    t.type = type;
    t.start = start;
    t.length = (int)(lx->cur - start);
    t.line = lx->line;
    t.lit.ival = 0;
    return t;
}

static MeloToken error_token(MeloLexer *lx, const char *msg) {
    MeloToken t;
    t.type = TOK_ERROR;
    t.start = msg;
    t.length = (int)strlen(msg);
    t.line = lx->line;
    return t;
}

typedef struct {
    const char *word;
    MeloTokenType type;
} Keyword;

static const Keyword keywords[] = {
    {"func", TOK_FUNC}, {"var", TOK_VAR}, {"let", TOK_LET},
    {"struct", TOK_STRUCT}, {"vukernel", TOK_VUKERNEL},
    {"arena", TOK_ARENA}, {"if", TOK_IF}, {"else", TOK_ELSE},
    {"while", TOK_WHILE}, {"for", TOK_FOR}, {"in", TOK_IN},
    {"return", TOK_RETURN}, {"true", TOK_TRUE}, {"false", TOK_FALSE},
    {"break", TOK_BREAK}, {"continue", TOK_CONTINUE},
    {"yield", TOK_YIELD},
    {NULL, TOK_EOF}
};

static MeloTokenType ident_type(const char *start, int length) {
    for (int i = 0; keywords[i].word != NULL; i++) {
        size_t klen = strlen(keywords[i].word);
        if ((int)klen == length && strncmp(start, keywords[i].word, length) == 0) {
            return keywords[i].type;
        }
    }
    return TOK_IDENT;
}

static MeloToken ident_or_keyword(MeloLexer *lx, const char *start) {
    while (isalnum((unsigned char)peek(lx)) || peek(lx) == '_') advance(lx);
    MeloToken t = make_token(lx, ident_type(start, (int)(lx->cur - start)), start);
    return t;
}

static MeloToken number(MeloLexer *lx, const char *start) {
    int is_float = 0;
    while (isdigit((unsigned char)peek(lx))) advance(lx);
    if (peek(lx) == '.' && isdigit((unsigned char)peek_next(lx))) {
        is_float = 1;
        advance(lx);
        while (isdigit((unsigned char)peek(lx))) advance(lx);
    }
    MeloToken t = make_token(lx, is_float ? TOK_FLOAT_LIT : TOK_INT_LIT, start);
    if (is_float) {
        t.lit.fval = strtod(start, NULL);
    } else {
        t.lit.ival = strtol(start, NULL, 10);
    }
    return t;
}

static MeloToken string_lit(MeloLexer *lx, const char *start) {
    while (!is_at_end(lx) && peek(lx) != '"') {
        if (peek(lx) == '\\' && peek_next(lx) != '\0') advance(lx);
        advance(lx);
    }
    if (is_at_end(lx)) return error_token(lx, "unterminated string");
    advance(lx);
    return make_token(lx, TOK_STRING_LIT, start);
}

MeloToken melo_lexer_next(MeloLexer *lx) {
    skip_whitespace(lx);
    const char *start = lx->cur;

    if (is_at_end(lx)) return make_token(lx, TOK_EOF, start);

    char c = advance(lx);

    if (isalpha((unsigned char)c) || c == '_') return ident_or_keyword(lx, start);
    if (isdigit((unsigned char)c)) return number(lx, start);
    if (c == '"') return string_lit(lx, start);

    switch (c) {
        case '(': return make_token(lx, TOK_LPAREN, start);
        case ')': return make_token(lx, TOK_RPAREN, start);
        case '{': return make_token(lx, TOK_LBRACE, start);
        case '}': return make_token(lx, TOK_RBRACE, start);
        case '[': return make_token(lx, TOK_LBRACKET, start);
        case ']': return make_token(lx, TOK_RBRACKET, start);
        case ',': return make_token(lx, TOK_COMMA, start);
        case ':': return make_token(lx, TOK_COLON, start);
        case ';': return make_token(lx, TOK_SEMI, start);
        case '.': return make_token(lx, TOK_DOT, start);
        case '@': return make_token(lx, TOK_AT, start);
        case '#': return make_token(lx, TOK_HASH, start);
        case '%': return make_token(lx, TOK_PERCENT, start);
        case '+': return make_token(lx, TOK_PLUS, start);
        case '-':
            if (match(lx, '>')) return make_token(lx, TOK_ARROW, start);
            return make_token(lx, TOK_MINUS, start);
        case '*': return make_token(lx, TOK_STAR, start);
        case '/': return make_token(lx, TOK_SLASH, start);
        case '=':
            if (match(lx, '=')) return make_token(lx, TOK_EQ, start);
            return make_token(lx, TOK_ASSIGN, start);
        case '!':
            if (match(lx, '=')) return make_token(lx, TOK_NEQ, start);
            return make_token(lx, TOK_NOT, start);
        case '<':
            if (match(lx, '=')) return make_token(lx, TOK_LE, start);
            return make_token(lx, TOK_LT, start);
        case '>':
            if (match(lx, '=')) return make_token(lx, TOK_GE, start);
            return make_token(lx, TOK_GT, start);
        case '&':
            if (match(lx, '&')) return make_token(lx, TOK_AND, start);
            return error_token(lx, "unexpected '&'");
        case '|':
            if (match(lx, '|')) return make_token(lx, TOK_OR, start);
            return error_token(lx, "unexpected '|'");
        default:
            return error_token(lx, "unexpected character");
    }
}

const char *melo_token_type_name(MeloTokenType t) {
    static const char *names[] = {
        "EOF", "IDENT", "INT_LIT", "FLOAT_LIT", "STRING_LIT",
        "FUNC", "VAR", "LET", "STRUCT", "VUKERNEL",
        "ARENA", "IF", "ELSE", "WHILE", "FOR", "IN",
        "RETURN", "TRUE", "FALSE",
        "YIELD",
        "BREAK", "CONTINUE",
        "LPAREN", "RPAREN", "LBRACE", "RBRACE",
        "LBRACKET", "RBRACKET",
        "COMMA", "COLON", "SEMI", "DOT", "ARROW",
        "AT", "HASH",
        "PLUS", "MINUS", "STAR", "SLASH", "PERCENT",
        "ASSIGN", "EQ", "NEQ",
        "LT", "LE", "GT", "GE",
        "AND", "OR", "NOT",
        "ERROR"
    };
    return names[t];
}
