#include "melo_parser.h"
#include "melo_codegen.h"
#include "melo_vm.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static int compile_module(const char *src, MeloCompiledModule *mod) {
    MeloParser parser;
    melo_parser_init(&parser, src);
    MeloProgram *prog = melo_parse_program(&parser);
    if (parser.had_error) {
        printf("PARSE ERROR: %s\n", parser.error_msg);
        return 1;
    }
    if (melo_codegen_compile(prog, mod)) {
        printf("CODEGEN ERROR: %s\n", mod->error_msg);
        return 1;
    }
    return 0;
}

static void setup_vm_from_module(MeloVM *vm, MeloCompiledModule *mod) {
    melo_vm_init(vm, mod->code, mod->code_count);
    memcpy(vm->consts, mod->consts, sizeof(MeloValue) * mod->const_count);
    vm->const_count = mod->const_count;
    memcpy(vm->funcs, mod->funcs, sizeof(MeloFuncInfo) * mod->func_count);
    vm->func_count = mod->func_count;
    memcpy(vm->structs, mod->structs, sizeof(MeloStructInfo) * mod->struct_count);
    vm->struct_count = mod->struct_count;
}

static int find_func_id(MeloCompiledModule *mod, const char *name) {
    (void)mod; (void)name;
    return -1;
}

static void test_coro_single_yield(void) {
    const char *src =
        "func counter() -> int32 {\n"
        "    yield 1;\n"
        "    yield 2;\n"
        "    return 3;\n"
        "}\n"
        "func main() -> int32 { return 0; }\n";

    MeloCompiledModule mod;
    assert(compile_module(src, &mod) == 0);

    int counter_id = -1;
    for (int i = 0; i < mod.func_count; i++) {
        if (mod.funcs[i].entry != mod.funcs[mod.main_func_id].entry && i != mod.main_func_id) {
            counter_id = i;
        }
    }
    assert(counter_id >= 0);

    MeloVM vm;
    setup_vm_from_module(&vm, &mod);

    int coro = melo_coro_create(&vm, counter_id);
    assert(coro >= 0);
    assert(melo_coro_status(&vm, coro) == MELO_CORO_SUSPENDED);

    MeloValue y1;
    int rc1 = melo_coro_resume(&vm, coro, melo_nil(), &y1);
    assert(rc1 == 0);
    assert(vm.error == 0);
    assert(melo_coro_status(&vm, coro) == MELO_CORO_SUSPENDED);
    assert(y1.as.i32 == 1);

    MeloValue y2;
    int rc2 = melo_coro_resume(&vm, coro, melo_nil(), &y2);
    assert(rc2 == 0);
    assert(melo_coro_status(&vm, coro) == MELO_CORO_SUSPENDED);
    assert(y2.as.i32 == 2);

    MeloValue y3;
    int rc3 = melo_coro_resume(&vm, coro, melo_nil(), &y3);
    assert(rc3 == 0);
    assert(melo_coro_status(&vm, coro) == MELO_CORO_FINISHED);
    assert(y3.as.i32 == 3);

    printf("[coro_single_yield] OK (1, 2, 3 in sequence, then finished)\n");
}

static void test_coro_no_yield_runs_to_completion(void) {
    const char *src =
        "func plain() -> int32 {\n"
        "    return 42;\n"
        "}\n"
        "func main() -> int32 { return 0; }\n";

    MeloCompiledModule mod;
    assert(compile_module(src, &mod) == 0);

    int plain_id = -1;
    for (int i = 0; i < mod.func_count; i++) {
        if (i != mod.main_func_id) plain_id = i;
    }
    assert(plain_id >= 0);

    MeloVM vm;
    setup_vm_from_module(&vm, &mod);

    int coro = melo_coro_create(&vm, plain_id);
    MeloValue result;
    int rc = melo_coro_resume(&vm, coro, melo_nil(), &result);
    assert(rc == 0);
    assert(melo_coro_status(&vm, coro) == MELO_CORO_FINISHED);
    assert(result.as.i32 == 42);
    printf("[coro_no_yield_runs_to_completion] OK (result=%d)\n", result.as.i32);
}

static void test_coro_resume_after_finish_fails(void) {
    const char *src =
        "func plain() -> int32 {\n"
        "    return 7;\n"
        "}\n"
        "func main() -> int32 { return 0; }\n";

    MeloCompiledModule mod;
    assert(compile_module(src, &mod) == 0);

    int plain_id = -1;
    for (int i = 0; i < mod.func_count; i++) {
        if (i != mod.main_func_id) plain_id = i;
    }

    MeloVM vm;
    setup_vm_from_module(&vm, &mod);

    int coro = melo_coro_create(&vm, plain_id);
    MeloValue result;
    melo_coro_resume(&vm, coro, melo_nil(), &result);
    assert(melo_coro_status(&vm, coro) == MELO_CORO_FINISHED);

    vm.error = 0;
    vm.halted = 0;
    int rc = melo_coro_resume(&vm, coro, melo_nil(), &result);
    assert(rc == -1);
    printf("[coro_resume_after_finish_fails] OK (%s)\n", vm.error_msg);
}

static void test_coro_yield_outside_coroutine_is_error(void) {
    const char *src =
        "func leaker() -> int32 {\n"
        "    yield 5;\n"
        "    return 0;\n"
        "}\n"
        "func main() -> int32 {\n"
        "    return leaker();\n"
        "}\n";

    MeloCompiledModule mod;
    assert(compile_module(src, &mod) == 0);

    MeloVM vm;
    setup_vm_from_module(&vm, &mod);
    vm.code = mod.funcs[mod.main_func_id].entry;

    int rc = melo_vm_run(&vm);
    assert(rc != 0);
    assert(vm.error == 1);
    printf("[coro_yield_outside_coroutine_is_error] OK (%s)\n", vm.error_msg);
}

static void test_coro_multiple_independent_coroutines(void) {
    const char *src =
        "func gen() -> int32 {\n"
        "    yield 100;\n"
        "    yield 200;\n"
        "    return 300;\n"
        "}\n"
        "func main() -> int32 { return 0; }\n";

    MeloCompiledModule mod;
    assert(compile_module(src, &mod) == 0);

    int gen_id = -1;
    for (int i = 0; i < mod.func_count; i++) {
        if (i != mod.main_func_id) gen_id = i;
    }

    MeloVM vm;
    setup_vm_from_module(&vm, &mod);

    int coro_a = melo_coro_create(&vm, gen_id);
    int coro_b = melo_coro_create(&vm, gen_id);
    assert(coro_a != coro_b);

    MeloValue ya1, yb1, ya2, yb2;
    melo_coro_resume(&vm, coro_a, melo_nil(), &ya1);
    melo_coro_resume(&vm, coro_b, melo_nil(), &yb1);
    melo_coro_resume(&vm, coro_a, melo_nil(), &ya2);
    melo_coro_resume(&vm, coro_b, melo_nil(), &yb2);

    assert(ya1.as.i32 == 100);
    assert(yb1.as.i32 == 100);
    assert(ya2.as.i32 == 200);
    assert(yb2.as.i32 == 200);

    printf("[coro_multiple_independent_coroutines] OK (independent state confirmed)\n");
}

static void test_coro_status_transitions(void) {
    const char *src =
        "func gen() -> int32 {\n"
        "    yield 1;\n"
        "    return 2;\n"
        "}\n"
        "func main() -> int32 { return 0; }\n";

    MeloCompiledModule mod;
    assert(compile_module(src, &mod) == 0);

    int gen_id = -1;
    for (int i = 0; i < mod.func_count; i++) {
        if (i != mod.main_func_id) gen_id = i;
    }

    MeloVM vm;
    setup_vm_from_module(&vm, &mod);

    int coro = melo_coro_create(&vm, gen_id);
    assert(melo_coro_status(&vm, coro) == MELO_CORO_SUSPENDED);

    MeloValue y;
    melo_coro_resume(&vm, coro, melo_nil(), &y);
    assert(melo_coro_status(&vm, coro) == MELO_CORO_SUSPENDED);

    melo_coro_resume(&vm, coro, melo_nil(), &y);
    assert(melo_coro_status(&vm, coro) == MELO_CORO_FINISHED);

    printf("[coro_status_transitions] OK\n");
}

int main(void) {
    (void)find_func_id;
    test_coro_single_yield();
    test_coro_no_yield_runs_to_completion();
    test_coro_resume_after_finish_fails();
    test_coro_yield_outside_coroutine_is_error();
    test_coro_multiple_independent_coroutines();
    test_coro_status_transitions();
    printf("ALL COROUTINE TESTS PASSED\n");
    return 0;
}
