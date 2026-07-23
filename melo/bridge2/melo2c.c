#include "melo_parser.h"
#include "melo_codegen.h"
#include "melo_aot.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(n + 1);
    fread(buf, 1, n, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: melo2c input.melo output.c\n");
        return 1;
    }

    char *src = read_file(argv[1]);
    if (!src) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 1;
    }

    MeloParser parser;
    melo_parser_init(&parser, src);
    MeloProgram *prog = melo_parse_program(&parser);
    if (parser.had_error) {
        fprintf(stderr, "parse error at line %d: %s\n", parser.error_line, parser.error_msg);
        return 1;
    }

    MeloCompiledModule mod;
    if (melo_codegen_compile(prog, &mod)) {
        fprintf(stderr, "codegen error: %s\n", mod.error_msg);
        return 1;
    }

    MeloAotResult aot;
    if (melo_aot_translate_program(prog, &aot)) {
        fprintf(stderr, "aot error: %s\n", aot.error_msg);
        return 1;
    }

    if (melo_aot_write_bundle_file(&aot, argv[2], NULL)) {
        fprintf(stderr, "failed to write %s\n", argv[2]);
        return 1;
    }

    fprintf(stderr, "OK: %s -> %s (%d units)\n", argv[1], argv[2], aot.unit_count);

    melo_aot_free(&aot);
    melo_codegen_free(&mod);
    free(src);
    return 0;
}
