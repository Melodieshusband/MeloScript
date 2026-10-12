#include "../melo_parser.h"
#include "../melo_codegen.h"
#include "../melo_vm.h"
#include "melo_stdlib_size.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static int run_source(const char *src, MeloVM *vm, MeloCompiledModule *mod) {
    MeloParser parser;
    melo_parser_init(&parser, src);
    MeloProgram *prog = melo_parse_program(&parser);
    if (parser.had_error) {
        printf("PARSE ERROR at line %d: %s\n", parser.error_line, parser.error_msg);
        return 1;
    }
    int count = 0;
    const char **names = melo_stdlib_size_native_names(&count);
    if (melo_codegen_compile_with_natives(prog, mod, names, count)) {
        printf("CODEGEN ERROR: %s\n", mod->error_msg);
        return 1;
    }
    melo_vm_init(vm, mod->code, mod->code_count);
    memcpy(vm->consts, mod->consts, sizeof(MeloValue) * mod->const_count);
    vm->const_count = mod->const_count;
    memcpy(vm->funcs, mod->funcs, sizeof(MeloFuncInfo) * mod->func_count);
    vm->func_count = mod->func_count;
    vm->code = mod->funcs[mod->main_func_id].entry;
    assert(melo_stdlib_register_size(vm) == 0);
    if (melo_vm_run(vm)) {
        printf("VM ERROR: %s\n", vm->error_msg);
        return 1;
    }
    return 0;
}

static int32_t eval_int(const char *src) {
    MeloVM vm;
    MeloCompiledModule mod;
    int rc = run_source(src, &vm, &mod);
    assert(rc == 0);
    int32_t r = vm.reg_stack[0].as.i32;
    melo_codegen_free(&mod);
    return r;
}

static void test_value_sizes(void) {
    assert(eval_int("func main() -> int32 { var a = 5; return bytes_of(a); }") == 4);
    assert(eval_int("func main() -> int32 { var a = 5; return bits_of(a); }") == 32);
    assert(eval_int("func main() -> int32 { var a = 1.5; return bytes_of(a); }") == 4);
    assert(eval_int("func main() -> int32 { var v = vec3(1.0, 2.0, 3.0); return bytes_of(v); }") == 12);
    assert(eval_int("func main() -> int32 { var v = vec3(1.0, 2.0, 3.0); return bits_of(v); }") == 96);
    assert(eval_int("func main() -> int32 { var v = vec4(1.0, 2.0, 3.0, 4.0); return bytes_of(v); }") == 16);
    assert(eval_int("func main() -> int32 { var s = \"hello\"; return bytes_of(s); }") == 5);
    printf("[value_sizes] OK\n");
}

static void test_script_size_positive(void) {
    int32_t n = eval_int("func main() -> int32 { return script_bytes(); }");
    assert(n > 0 && n % 4 == 0);
    printf("[script_size_positive] OK (%d bytes)\n", n);
}

static int32_t script_hash_of(const char *src) {
    return eval_int(src);
}

static void test_integrity_check(void) {
    const char *hash_probe =
        "func main() -> int32 {\n"
        "    var x = 7;\n"
        "    var y = x * 3;\n"
        "    if (script_hash() == 123456789) { return y; }\n"
        "    return script_hash();\n"
        "}\n";
    int32_t h = script_hash_of(hash_probe);
    assert(h > 0);

    char good[512];
    snprintf(good, sizeof(good),
        "func main() -> int32 {\n"
        "    var x = 7;\n"
        "    var y = x * 3;\n"
        "    if (script_hash() == %d) { return y; }\n"
        "    return script_hash();\n"
        "}\n", h);
    assert(eval_int(good) == 21);

    char tampered[512];
    snprintf(tampered, sizeof(tampered),
        "func main() -> int32 {\n"
        "    var x = 7;\n"
        "    var y = x + 3;\n"
        "    if (script_hash() == %d) { return y; }\n"
        "    return 0 - 1;\n"
        "}\n", h);
    assert(eval_int(tampered) == -1);
    printf("[integrity_check] OK (hash=%d)\n", h);
}

static void test_arg_errors(void) {
    MeloVM vm;
    MeloCompiledModule mod;
    int rc = run_source("func main() -> int32 { return bytes_of(); }", &vm, &mod);
    assert(rc == 1);
    assert(vm.error == 1);
    printf("[arg_errors] OK (%s)\n", vm.error_msg);
    melo_codegen_free(&mod);
}

int main(void) {
    test_value_sizes();
    test_script_size_positive();
    test_integrity_check();
    test_arg_errors();
    printf("ALL SIZE TESTS PASSED\n");
    return 0;
}
