#ifndef MELO_PARSER_H
#define MELO_PARSER_H

#include "melo_lexer.h"
#include "melo_ast.h"

typedef struct {
    MeloLexer lexer;
    MeloToken current;
    MeloToken previous;
    int had_error;
    char error_msg[256];
    int error_line;
} MeloParser;

void melo_parser_init(MeloParser *p, const char *source);
MeloProgram *melo_parse_program(MeloParser *p);

#endif
