#include "melo_parser.h"
#include "melo_codegen.h"
#include "melo_vm.h"
#include "melo_aot.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dlfcn.h>

static int compile_program(const char *src, MeloParser *parser, MeloProgram **prog_out, MeloCompiledModule *mod) {
    melo_parser_init(parser, src);
    MeloProgram *prog = melo_parse_program(parser);
    if (parser->had_error) {
        printf("PARSE ERROR at line %d: %s\n", parser->error_line, parser->error_msg);
        return 1;
    }
    if (melo_codegen_compile(prog, mod)) {
        printf("CODEGEN ERROR: %s\n", mod->error_msg);
        return 1;
    }
    *prog_out = prog;
    return 0;
}

static int run_vm(MeloCompiledModule *mod, MeloValue *out_result, void (*setup)(MeloVM *)) {
    MeloVM vm;
    melo_vm_init(&vm, mod->code, mod->code_count);
    memcpy(vm.consts, mod->consts, sizeof(MeloValue) * mod->const_count);
    vm.const_count = mod->const_count;
    memcpy(vm.funcs, mod->funcs, sizeof(MeloFuncInfo) * mod->func_count);
    vm.func_count = mod->func_count;
    vm.code = mod->funcs[mod->main_func_id].entry;

    if (setup) setup(&vm);

    int err = melo_vm_run(&vm);
    if (err) {
        printf("VM ERROR: %s\n", vm.error_msg);
        return 1;
    }
    *out_result = vm.reg_stack[0];
    return 0;
}

static MeloVM *g_vm_for_setup = NULL;
static void *g_dl_handle = NULL;

static int build_and_load_aot(MeloAotResult *aot, const char *bundle_path, const char *so_path) {
    if (melo_aot_write_bundle_file(aot, bundle_path, NULL)) {
        printf("failed to write AOT bundle\n");
        return 1;
    }

    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "gcc -shared -fPIC -O2 -o %s %s -I%s 2>/tmp/melo_aot_build_err.txt",
             so_path, bundle_path, MELO_SRC_DIR);
    int rc = system(cmd);
    if (rc != 0) {
        printf("AOT bundle failed to compile:\n");
        FILE *f = fopen("/tmp/melo_aot_build_err.txt", "r");
        if (f) {
            char buf[4096];
            size_t n = fread(buf, 1, sizeof(buf) - 1, f);
            buf[n] = '\0';
            printf("%s\n", buf);
            fclose(f);
        }
        return 1;
    }

    g_dl_handle = dlopen(so_path, RTLD_NOW);
    if (!g_dl_handle) {
        printf("dlopen failed: %s\n", dlerror());
        return 1;
    }
    return 0;
}

static void register_aot_symbol(MeloVM *vm, const char *melo_func_name) {
    char symbol[128];
    snprintf(symbol, sizeof(symbol), "melo_hot_%s", melo_func_name);
    void *fnptr = dlsym(g_dl_handle, symbol);
    if (!fnptr) {
        printf("FAIL: dlsym could not find %s: %s\n", symbol, dlerror());
        exit(1);
    }
    int id = melo_vm_register_aot(vm, melo_func_name, (MeloAotFn)fnptr);
    if (id < 0) {
        printf("FAIL: melo_vm_register_aot could not find hot function '%s'\n", melo_func_name);
        exit(1);
    }
}

static const char *g_reg_name;
static void setup_register_single(MeloVM *vm) {
    register_aot_symbol(vm, g_reg_name);
}

int main(void) {
    const char *src =
        "@hot\n"
        "func add_ints(a: int32, b: int32) -> int32 {\n"
        "    return a + b;\n"
        "}\n"
        "\n"
        "func main() -> int32 {\n"
        "    return add_ints(19, 23);\n"
        "}\n";

    MeloParser parser;
    MeloProgram *prog;
    MeloCompiledModule mod;
    if (compile_program(src, &parser, &prog, &mod)) return 1;

    MeloAotResult aot;
    if (melo_aot_translate_program(prog, &aot)) {
        printf("AOT TRANSLATE ERROR: %s\n", aot.error_msg);
        return 1;
    }
    if (aot.unit_count != 1) {
        printf("FAIL: expected 1 AOT unit, got %d\n", aot.unit_count);
        return 1;
    }
    printf("--- generated C for add_ints ---\n%s\n", aot.units[0].c_source);

    if (build_and_load_aot(&aot, "/tmp/melo_aot_bundle1.c", "/tmp/melo_aot_bundle1.so")) return 1;

    g_reg_name = "add_ints";
    MeloValue result;
    if (run_vm(&mod, &result, setup_register_single)) return 1;
    printf("AOT-backed add_ints(19,23) via VM call = %d\n", result.as.i32);
    if (result.as.i32 != 42) { printf("FAIL\n"); return 1; }
    printf("OK: int32 @hot function executed via AOT path\n\n");

    melo_aot_free(&aot);
    melo_codegen_free(&mod);
    dlclose(g_dl_handle);
    g_dl_handle = NULL;

    const char *src_vec =
        "@hot\n"
        "func add_vecs(a: vec3, b: vec3) -> vec3 {\n"
        "    return a + b;\n"
        "}\n"
        "\n"
        "func main() -> float {\n"
        "    var r: vec3 = add_vecs(vec3(1.0, 2.0, 3.0), vec3(10.0, 20.0, 30.0));\n"
        "    return r.x + r.y + r.z;\n"
        "}\n";

    MeloParser parser2;
    MeloProgram *prog2;
    MeloCompiledModule mod2;
    if (compile_program(src_vec, &parser2, &prog2, &mod2)) return 1;

    MeloAotResult aot2;
    if (melo_aot_translate_program(prog2, &aot2)) {
        printf("AOT TRANSLATE ERROR: %s\n", aot2.error_msg);
        return 1;
    }
    printf("--- generated C for add_vecs ---\n%s\n", aot2.units[0].c_source);

    if (build_and_load_aot(&aot2, "/tmp/melo_aot_bundle2.c", "/tmp/melo_aot_bundle2.so")) return 1;

    g_reg_name = "add_vecs";
    MeloValue result2;
    if (run_vm(&mod2, &result2, setup_register_single)) return 1;
    printf("AOT-backed add_vecs sum-of-components = %.2f\n", result2.as.f32);
    if (result2.as.f32 != 66.0f) { printf("FAIL\n"); return 1; }
    printf("OK: vec3 @hot function executed via AOT path\n\n");

    melo_aot_free(&aot2);
    melo_codegen_free(&mod2);
    dlclose(g_dl_handle);
    g_dl_handle = NULL;

    const char *src_reject =
        "func not_hot() -> int32 { return 1; }\n"
        "@hot\n"
        "func bad(a: int32) -> int32 {\n"
        "    return not_hot() + a;\n"
        "}\n"
        "func main() -> int32 { return bad(1); }\n";

    MeloParser parser3;
    MeloProgram *prog3;
    MeloCompiledModule mod3;
    if (compile_program(src_reject, &parser3, &prog3, &mod3)) return 1;

    MeloAotResult aot3;
    int rc = melo_aot_translate_program(prog3, &aot3);
    if (!rc) {
        printf("FAIL: expected AOT translation to reject call to non-@hot function\n");
        return 1;
    }
    printf("OK: AOT translator rejected call to non-@hot function (%s)\n\n", aot3.error_msg);
    melo_codegen_free(&mod3);

    printf("ALL AOT TESTS PASSED\n");
    return 0;
}
