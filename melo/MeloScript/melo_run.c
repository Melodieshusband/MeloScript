#include "melo_parser.h"
#include "melo_codegen.h"
#include "melo_vm.h"
#include "melo_image.h"
#include "melo_stdlib_vram.h"
#include "libs/melo_stdlib_pad.h"
#include "libs/melo_stdlib_time.h"
#include "libs/melo_stdlib_audio.h"
#include "libs/melo_stdlib_size.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int (*RegisterFn)(MeloVM *);
typedef const char **(*NamesFn)(int *);

static RegisterFn g_regs[] = {
    melo_stdlib_register_vram, melo_stdlib_register_pad, melo_stdlib_register_time,
    melo_stdlib_register_audio, melo_stdlib_register_size
};
static NamesFn g_names[] = {
    melo_stdlib_vram_native_names, melo_stdlib_pad_native_names, melo_stdlib_time_native_names,
    melo_stdlib_audio_native_names, melo_stdlib_size_native_names
};
#define LIB_COUNT ((int)(sizeof(g_regs) / sizeof(g_regs[0])))

static const char *g_all_names[MELO_MAX_NATIVES];
static int g_all_count;
static MeloVM g_vm;
static MeloCompiledModule g_mod;

static void collect_names(void) {
    g_all_count = 0;
    for (int i = 0; i < LIB_COUNT; i++) {
        int n = 0;
        const char **names = g_names[i](&n);
        for (int k = 0; k < n && g_all_count < MELO_MAX_NATIVES; k++) {
            g_all_names[g_all_count++] = names[k];
        }
    }
}

static int register_all(MeloVM *vm) {
    for (int i = 0; i < LIB_COUNT; i++) {
        if (g_regs[i](vm) != 0) return -1;
    }
    return 0;
}

static char *read_text(const char *path) {
    uint8_t *data = NULL;
    size_t size = 0;
    if (melo_image_read_file(path, &data, &size) != 0) return NULL;
    data[size] = 0;
    return (char *)data;
}

static int compile_file(const char *path) {
    char *src = read_text(path);
    if (!src) {
        fprintf(stderr, "cannot read %s\n", path);
        return 1;
    }
    MeloParser parser;
    melo_parser_init(&parser, src);
    MeloProgram *prog = melo_parse_program(&parser);
    if (parser.had_error) {
        fprintf(stderr, "%s:%d: parse error: %s\n", path, parser.error_line, parser.error_msg);
        return 1;
    }
    collect_names();
    if (melo_codegen_compile_with_natives(prog, &g_mod, g_all_names, g_all_count)) {
        fprintf(stderr, "%s: codegen error: %s\n", path, g_mod.error_msg);
        return 1;
    }
    if (g_mod.main_func_id < 0) {
        fprintf(stderr, "%s: no main() function found\n", path);
        return 1;
    }
    return 0;
}

static int run_vm(void) {
    int err = melo_vm_run(&g_vm);
    if (err) {
        fprintf(stderr, "VM error: %s\n", g_vm.error_msg);
        return 1;
    }
    MeloValue r = g_vm.reg_stack[0];
    switch (r.type) {
        case MELO_INT32: printf("exit value: %d\n", r.as.i32); break;
        case MELO_FLOAT: printf("exit value: %g\n", r.as.f32); break;
        default: break;
    }
    return 0;
}

static int build_image(const char *path, uint8_t **data, size_t *size) {
    if (compile_file(path)) return 1;
    char err[160];
    if (melo_image_serialize(&g_mod, g_all_names, g_all_count, data, size, err, sizeof(err))) {
        fprintf(stderr, "image error: %s\n", err);
        return 1;
    }
    return 0;
}

static int cmd_run(const char *path) {
    if (compile_file(path)) return 1;
    melo_vm_init(&g_vm, g_mod.code, g_mod.code_count);
    memcpy(g_vm.consts, g_mod.consts, sizeof(MeloValue) * g_mod.const_count);
    g_vm.const_count = g_mod.const_count;
    memcpy(g_vm.funcs, g_mod.funcs, sizeof(MeloFuncInfo) * g_mod.func_count);
    g_vm.func_count = g_mod.func_count;
    g_vm.code = g_mod.funcs[g_mod.main_func_id].entry;
    if (register_all(&g_vm)) {
        fprintf(stderr, "native registration failed\n");
        return 1;
    }
    return run_vm();
}

static int cmd_exec(const char *path) {
    uint8_t *data = NULL;
    size_t size = 0;
    if (melo_image_read_file(path, &data, &size)) {
        fprintf(stderr, "cannot read %s\n", path);
        return 1;
    }
    MeloImageInfo info;
    char err[160];
    if (melo_image_load(&g_vm, data, size, &info, err, sizeof(err))) {
        fprintf(stderr, "image error: %s\n", err);
        return 1;
    }
    if (register_all(&g_vm) || melo_image_verify_natives(&g_vm, &info, err, sizeof(err))) {
        fprintf(stderr, "native binding failed: %s\n", err);
        return 1;
    }
    return run_vm();
}

static int cmd_build(const char *src, const char *out) {
    uint8_t *data = NULL;
    size_t size = 0;
    if (build_image(src, &data, &size)) return 1;
    if (melo_image_write_file(data, size, out)) {
        fprintf(stderr, "cannot write %s\n", out);
        return 1;
    }
    printf("wrote %s (%zu bytes)\n", out, size);
    return 0;
}

static int cmd_embed(const char *src, const char *out, const char *symbol) {
    uint8_t *data = NULL;
    size_t size = 0;
    if (build_image(src, &data, &size)) return 1;
    if (melo_image_write_c_array(data, size, out, symbol)) {
        fprintf(stderr, "cannot write %s\n", out);
        return 1;
    }
    printf("wrote %s (%zu bytes, symbol %s)\n", out, size, symbol);
    return 0;
}

static int usage(void) {
    fprintf(stderr,
        "usage:\n"
        "  melorun run   <file.melo>\n"
        "  melorun build <file.melo> <out.meloc>\n"
        "  melorun exec  <file.meloc>\n"
        "  melorun embed <file.melo> <out.c> <symbol>\n");
    return 2;
}

int main(int argc, char **argv) {
    if (argc >= 3 && strcmp(argv[1], "run") == 0) return cmd_run(argv[2]);
    if (argc >= 4 && strcmp(argv[1], "build") == 0) return cmd_build(argv[2], argv[3]);
    if (argc >= 3 && strcmp(argv[1], "exec") == 0) return cmd_exec(argv[2]);
    if (argc >= 5 && strcmp(argv[1], "embed") == 0) return cmd_embed(argv[2], argv[3], argv[4]);
    return usage();
}
