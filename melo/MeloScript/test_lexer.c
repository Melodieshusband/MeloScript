#include "melo_lexer.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    const char *src =
        "@hot\n"
        "func update(p: vec4, dt: float) -> vec4 {\n"
        "    var v = p * dt;\n"
        "    return v + vec4(1.0, 2.0, 3.0, 0.0);\n"
        "}\n";

    MeloLexer lx;
    melo_lexer_init(&lx, src);

    int count = 0;
    for (;;) {
        MeloToken t = melo_lexer_next(&lx);
        printf("[%3d] line %2d  %-10s  '%.*s'\n",
               count++, t.line, melo_token_type_name(t.type), t.length, t.start);
        if (t.type == TOK_EOF || t.type == TOK_ERROR) break;
    }

    return 0;
}
