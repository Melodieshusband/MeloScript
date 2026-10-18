#include "melo_parser.h"
#include "melo_codegen.h"
#include "melo_aot.h"
#include "melo_mips_codegen.h"
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

static int run_cmd(const char *cmd, int verbose) {
    if (verbose) {
        fprintf(stderr, "[meloc] %s\n", cmd);
    }
    return system(cmd);
}

static int emit_legacy_c(MeloProgram *prog, MeloCompiledModule *mod, const char *out_path) {
    MeloAotResult aot;
    int has_aot = 0;
    if (melo_aot_translate_program(prog, &aot) == 0 && aot.unit_count > 0) {
        has_aot = 1;
    }

    FILE *out = fopen(out_path, "w");
    if (!out) {
        fprintf(stderr, "failed to open %s for writing\n", out_path);
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

    fprintf(out, "static const MeloInstr melo_game_code[%d] = {\n", mod->code_count);
    for (int i = 0; i < mod->code_count; i++) {
        fprintf(out, "    0x%08Xu%s\n", mod->code[i], (i + 1 < mod->code_count) ? "," : "");
    }
    fputs("};\n\n", out);

    fprintf(out, "static const int melo_game_code_count = %d;\n", mod->code_count);
    fprintf(out, "static const int melo_game_main_id = %d;\n\n", mod->main_func_id);

    fputs("int melo_load_game_module(MeloVM *vm) {\n", out);
    fprintf(out, "    melo_vm_init(vm, (MeloInstr *)melo_game_code, melo_game_code_count);\n");
    fprintf(out, "    vm->const_count = %d;\n", mod->const_count);

    for (int i = 0; i < mod->const_count; i++) {
        if (mod->consts[i].type == MELO_INT32) {
            fprintf(out, "    vm->consts[%d] = melo_int(%d);\n", i, (int)mod->consts[i].as.i32);
        } else if (mod->consts[i].type == MELO_FLOAT) {
            fprintf(out, "    vm->consts[%d] = melo_float(%.9gf);\n", i, mod->consts[i].as.f32);
        } else if (mod->consts[i].type == MELO_STRING && mod->consts[i].as.str) {
            uint32_t slen = mod->consts[i].as.str->length;
            fprintf(out, "    vm->consts[%d] = melo_string(melo_strpool_intern(&vm->strpool, ", i);
            print_escaped_string(out, mod->consts[i].as.str->data);
            fprintf(out, ", %uu));\n", slen);
        } else {
            fprintf(out, "    vm->consts[%d] = melo_nil();\n", i);
        }
    }

    fprintf(out, "    vm->func_count = %d;\n", mod->func_count);
    for (int i = 0; i < mod->func_count; i++) {
        int entry_pc = (int)(mod->funcs[i].entry - mod->code);
        fprintf(out, "    vm->funcs[%d].entry = (MeloInstr *)&melo_game_code[%d];\n", i, entry_pc);
        fprintf(out, "    vm->funcs[%d].num_params = %d;\n", i, mod->funcs[i].num_params);
        fprintf(out, "    vm->funcs[%d].num_regs = %d;\n", i, mod->funcs[i].num_regs);
        fprintf(out, "    vm->funcs[%d].is_vukernel = %d;\n", i, mod->funcs[i].is_vukernel);
        fprintf(out, "    vm->funcs[%d].is_vu0 = %d;\n", i, mod->funcs[i].is_vu0);
        fprintf(out, "    vm->funcs[%d].is_hot = %d;\n", i, mod->funcs[i].is_hot);
        fprintf(out, "    strncpy(vm->funcs[%d].name, ", i);
        print_escaped_string(out, mod->funcs[i].name);
        fprintf(out, ", %d);\n", MELO_FUNC_NAME_LEN - 1);
    }

    fputs("    if (melo_game_main_id >= 0 && melo_game_main_id < vm->func_count) {\n", out);
    fputs("        vm->code = vm->funcs[melo_game_main_id].entry;\n", out);
    fputs("    }\n", out);
    fputs("    return 0;\n", out);
    fputs("}\n", out);

    fclose(out);
    return 0;
}

int main(int argc, char **argv) {
    const char *input_path = NULL;
    const char *output_path = NULL;
    int emit_assembly_only = 0;
    int emit_object_only = 0;
    int backend_c = 0;
    int verbose = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_path = argv[++i];
        } else if (strcmp(argv[i], "-S") == 0) {
            emit_assembly_only = 1;
        } else if (strcmp(argv[i], "-c") == 0) {
            emit_object_only = 1;
        } else if (strcmp(argv[i], "--backend=c") == 0 || strcmp(argv[i], "-backend=c") == 0) {
            backend_c = 1;
        } else if (strcmp(argv[i], "--backend=mips") == 0 || strcmp(argv[i], "-backend=mips") == 0) {
            backend_c = 0;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose = 1;
        } else if (argv[i][0] != '-') {
            input_path = argv[i];
        } else {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            return 1;
        }
    }

    if (!input_path) {
        fprintf(stderr, "usage: meloc <input.melo> [-o output.elf] [-S] [-c] [--backend=mips|c] [-v]\n");
        return 1;
    }

    char default_out[256];
    if (!output_path) {
        const char *dot = strrchr(input_path, '.');
        size_t stem_len = dot ? (size_t)(dot - input_path) : strlen(input_path);
        if (stem_len >= sizeof(default_out) - 8) stem_len = sizeof(default_out) - 8;
        memcpy(default_out, input_path, stem_len);
        if (emit_assembly_only) {
            strcpy(default_out + stem_len, ".s");
        } else if (emit_object_only) {
            strcpy(default_out + stem_len, ".o");
        } else {
            strcpy(default_out + stem_len, ".elf");
        }
        output_path = default_out;
    }

    char *src = read_file(input_path);
    if (!src) {
        fprintf(stderr, "cannot read %s\n", input_path);
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

    const char *ps2sdk = getenv("PS2SDK");
    if (!ps2sdk) ps2sdk = "/usr/local/ps2dev/ps2sdk";

    if (backend_c) {
        char gen_c_path[256];
        snprintf(gen_c_path, sizeof(gen_c_path), "%s_gen.c", output_path);
        if (emit_legacy_c(prog, &mod, gen_c_path) != 0) {
            melo_codegen_free(&mod);
            free(src);
            return 1;
        }

        char cmd[1024];
        snprintf(cmd, sizeof(cmd),
                 "mips64r5900el-ps2-elf-gcc -D_EE -G0 -O2 -I../MeloScript "
                 "-I%s/ee/include -I%s/common/include -L%s/ee/lib "
                 "-Wl,-zmax-page-size=128 "
                 "melo_main.c %s ../MeloScript/melo_vm.c ../MeloScript/melo_strpool.c "
                 "../MeloScript/melo_stdlib_vram.c -o %s "
                 "-ldebug -lkernel -lpad -lm",
                 ps2sdk, ps2sdk, ps2sdk, gen_c_path, output_path);

        int rc = run_cmd(cmd, verbose);
        melo_codegen_free(&mod);
        free(src);
        return rc;
    }

    char asm_path[256];
    if (emit_assembly_only) {
        snprintf(asm_path, sizeof(asm_path), "%s", output_path);
    } else {
        snprintf(asm_path, sizeof(asm_path), "%s.tmp.s", output_path);
    }

    FILE *asm_out = fopen(asm_path, "w");
    if (!asm_out) {
        fprintf(stderr, "cannot open %s for writing\n", asm_path);
        melo_codegen_free(&mod);
        free(src);
        return 1;
    }

    char err_buf[256];
    if (melo_mips_emit_assembly(&mod, known_game_natives, native_count, asm_out, err_buf, sizeof(err_buf)) != 0) {
        fprintf(stderr, "mips codegen error: %s\n", err_buf);
        fclose(asm_out);
        melo_codegen_free(&mod);
        free(src);
        return 1;
    }
    fclose(asm_out);

    if (emit_assembly_only) {
        if (verbose) {
            fprintf(stderr, "OK: %s -> %s (assembly)\n", input_path, output_path);
        }
        melo_codegen_free(&mod);
        free(src);
        return 0;
    }

    if (emit_object_only) {
        char cmd[1024];
        snprintf(cmd, sizeof(cmd),
                 "mips64r5900el-ps2-elf-gcc -c -D_EE -G0 -O2 %s -o %s",
                 asm_path, output_path);
        int rc = run_cmd(cmd, verbose);
        remove(asm_path);
        melo_codegen_free(&mod);
        free(src);
        return rc;
    }

    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "mips64r5900el-ps2-elf-gcc -D_EE -G0 -O2 -I../MeloScript "
             "-I%s/ee/include -I%s/common/include -L%s/ee/lib "
             "-Wl,-zmax-page-size=128 "
             "%s melo_ps2_runtime.c -o %s "
             "-ldebug -lkernel -lpad -lm",
             ps2sdk, ps2sdk, ps2sdk, asm_path, output_path);

    int rc = run_cmd(cmd, verbose);
    remove(asm_path);

    if (rc == 0) {
        fprintf(stderr, "OK: %s -> %s (direct MIPS -> ELF, funcs=%d, code=%d)\n",
                input_path, output_path, mod.func_count, mod.code_count);
    }

    melo_codegen_free(&mod);
    free(src);
    return rc;
}
