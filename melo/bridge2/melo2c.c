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

static void print_escaped_string(FILE *f, const char *s) {
    fputc('"', f);
    while (*s) {
        if (*s == '"') fputs("\\\"", f);
        else if (*s == '\\') fputs("\\\\", f);
        else if (*s == '\n') fputs("\\n", f);
        else if (*s == '\r') fputs("\\r", f);
        else if (*s == '\t') fputs("\\t", f);
        else fputc(*s, f);
        s++;
    }
    fputc('"', f);
}

static const char *known_game_natives[] = {
    "scr_clear",
    "scr_set_xy",
    "scr_set_color",
    "scr_print",
    "scr_print_num",
    "pad_poll",
    "pad_btn_left",
    "pad_btn_right",
    "pad_btn_down",
    "pad_btn_rotate",
    "pad_btn_drop",
    "pad_btn_start",
    "sys_sleep_ms",
    "sys_rand"
};

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
        free(src);
        return 1;
    }

    int native_count = sizeof(known_game_natives) / sizeof(known_game_natives[0]);
    MeloCompiledModule mod;
    if (melo_codegen_compile_with_natives(prog, &mod, known_game_natives, native_count)) {
        fprintf(stderr, "codegen error: %s\n", mod.error_msg);
        free(src);
        return 1;
    }

    MeloAotResult aot;
    int has_aot = 0;
    if (melo_aot_translate_program(prog, &aot) == 0 && aot.unit_count > 0) {
        has_aot = 1;
    }

    FILE *out = fopen(argv[2], "w");
    if (!out) {
        fprintf(stderr, "failed to open %s for writing\n", argv[2]);
        melo_codegen_free(&mod);
        free(src);
        return 1;
    }

    fputs("#include \"melo_vm.h\"\n", out);
    fputs("#include \"melo_value.h\"\n", out);
    fputs("#include <string.h>\n\n", out);

    if (has_aot) {
        for (int i = 0; i < aot.unit_count; i++) {
            fputs(aot.units[i].c_source, out);
            fputs("\n", out);
        }
        melo_aot_free(&aot);
    }

    fprintf(out, "static const MeloInstr melo_game_code[%d] = {\n", mod.code_count);
    for (int i = 0; i < mod.code_count; i++) {
        fprintf(out, "    0x%08Xu%s\n", mod.code[i], (i + 1 < mod.code_count) ? "," : "");
    }
    fputs("};\n\n", out);

    fprintf(out, "static const int melo_game_code_count = %d;\n", mod.code_count);
    fprintf(out, "static const int melo_game_main_id = %d;\n\n", mod.main_func_id);

    fputs("int melo_load_game_module(MeloVM *vm) {\n", out);
    fprintf(out, "    melo_vm_init(vm, (MeloInstr *)melo_game_code, melo_game_code_count);\n");
    fprintf(out, "    vm->const_count = %d;\n", mod.const_count);

    for (int i = 0; i < mod.const_count; i++) {
        if (mod.consts[i].type == MELO_INT32) {
            fprintf(out, "    vm->consts[%d] = melo_int(%d);\n", i, (int)mod.consts[i].as.i32);
        } else if (mod.consts[i].type == MELO_FLOAT) {
            fprintf(out, "    vm->consts[%d] = melo_float(%.9gf);\n", i, mod.consts[i].as.f32);
        } else if (mod.consts[i].type == MELO_STRING && mod.consts[i].as.str) {
            uint32_t slen = mod.consts[i].as.str->length;
            fprintf(out, "    vm->consts[%d] = melo_string(melo_strpool_intern(&vm->strpool, ", i);
            print_escaped_string(out, mod.consts[i].as.str->data);
            fprintf(out, ", %uu));\n", slen);
        } else {
            fprintf(out, "    vm->consts[%d] = melo_nil();\n", i);
        }
    }

    fprintf(out, "    vm->func_count = %d;\n", mod.func_count);
    for (int i = 0; i < mod.func_count; i++) {
        int entry_pc = (int)(mod.funcs[i].entry - mod.code);
        fprintf(out, "    vm->funcs[%d].entry = (MeloInstr *)&melo_game_code[%d];\n", i, entry_pc);
        fprintf(out, "    vm->funcs[%d].num_params = %d;\n", i, mod.funcs[i].num_params);
        fprintf(out, "    vm->funcs[%d].num_regs = %d;\n", i, mod.funcs[i].num_regs);
        fprintf(out, "    vm->funcs[%d].is_vukernel = %d;\n", i, mod.funcs[i].is_vukernel);
        fprintf(out, "    vm->funcs[%d].is_vu0 = %d;\n", i, mod.funcs[i].is_vu0);
        fprintf(out, "    vm->funcs[%d].is_hot = %d;\n", i, mod.funcs[i].is_hot);
        fprintf(out, "    strncpy(vm->funcs[%d].name, ", i);
        print_escaped_string(out, mod.funcs[i].name);
        fprintf(out, ", %d);\n", MELO_FUNC_NAME_LEN - 1);
    }

    fputs("    if (melo_game_main_id >= 0 && melo_game_main_id < vm->func_count) {\n", out);
    fputs("        vm->code = vm->funcs[melo_game_main_id].entry;\n", out);
    fputs("    }\n", out);
    fputs("    return 0;\n", out);
    fputs("}\n", out);

    fclose(out);
    fprintf(stderr, "OK: %s -> %s (code=%d, funcs=%d, consts=%d)\n",
            argv[1], argv[2], mod.code_count, mod.func_count, mod.const_count);

    melo_codegen_free(&mod);
    free(src);
    return 0;
}
