#include "melo_parser.h"
#include "melo_codegen.h"
#include "melo_vm.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static int compile_and_run(const char *src, MeloVM *vm) {
    MeloParser parser;
    melo_parser_init(&parser, src);
    MeloProgram *prog = melo_parse_program(&parser);

    if (parser.had_error) {
        printf("PARSE ERROR at line %d: %s\n", parser.error_line, parser.error_msg);
        return 1;
    }

    MeloCompiledModule mod;
    if (melo_codegen_compile(prog, &mod)) {
        printf("CODEGEN ERROR: %s\n", mod.error_msg);
        return 1;
    }

    if (mod.main_func_id < 0) {
        printf("no main() function found\n");
        return 1;
    }

    melo_vm_init(vm, mod.code, mod.code_count);
    memcpy(vm->consts, mod.consts, sizeof(MeloValue) * mod.const_count);
    vm->const_count = mod.const_count;
    memcpy(vm->funcs, mod.funcs, sizeof(MeloFuncInfo) * mod.func_count);
    vm->func_count = mod.func_count;
    vm->code = mod.funcs[mod.main_func_id].entry;

    int err = melo_vm_run(vm);
    melo_codegen_free(&mod);
    if (err) {
        printf("VM ERROR: %s\n", vm->error_msg);
        return 1;
    }
    return 0;
}

static void test_single_arena(void) {
    const char *src =
        "func main() {\n"
        "    arena {\n"
        "        var x = 1;\n"
        "        var y = 2;\n"
        "    }\n"
        "    return;\n"
        "}\n";

    MeloVM vm;
    int rc = compile_and_run(src, &vm);
    assert(rc == 0);
    assert(vm.error == 0);
    assert(vm.arena_top == -1);
    assert(vm.arena_pool_used == 0);
    printf("[single_arena] OK\n");
}

static void test_nested_arena(void) {
    const char *src =
        "func main() {\n"
        "    arena {\n"
        "        var x = 1;\n"
        "        arena {\n"
        "            var y = 2;\n"
        "        }\n"
        "        var z = 3;\n"
        "    }\n"
        "    return;\n"
        "}\n";

    MeloVM vm;
    int rc = compile_and_run(src, &vm);
    assert(rc == 0);
    assert(vm.error == 0);
    assert(vm.arena_top == -1);
    assert(vm.arena_pool_used == 0);
    printf("[nested_arena] OK\n");
}

static void test_direct_arena_api(void) {
    MeloVM vm;
    melo_vm_init(&vm, NULL, 0);

    melo_vm_arena_enter(&vm);
    assert(vm.error == 0);
    assert(vm.arena_top == 0);

    int *a = melo_vm_arena_alloc(&vm, sizeof(int));
    assert(a != NULL);
    assert(((size_t)a & 15) == 0);
    *a = 42;

    float *b = melo_vm_arena_alloc(&vm, sizeof(float) * 4);
    assert(b != NULL);
    assert(((size_t)b & 15) == 0);

    melo_vm_arena_enter(&vm);
    assert(vm.arena_top == 1);
    int *c = melo_vm_arena_alloc(&vm, sizeof(int));
    assert(c != NULL);
    *c = 99;
    assert(*a == 42);

    melo_vm_arena_exit(&vm);
    assert(vm.arena_top == 0);
    assert(vm.arena_pool_used == 1);
    assert(*a == 42);

    int *a2 = melo_vm_arena_alloc(&vm, sizeof(int));
    assert(a2 == (void *)((uint8_t *)a + 32));

    melo_vm_arena_exit(&vm);
    assert(vm.arena_top == -1);
    assert(vm.arena_pool_used == 0);

    melo_vm_arena_enter(&vm);
    int *d = melo_vm_arena_alloc(&vm, sizeof(int));
    assert(d == (void *)a);
    melo_vm_arena_exit(&vm);

    printf("[direct_api] OK\n");
}

static void test_nesting_limit(void) {
    MeloVM vm;
    melo_vm_init(&vm, NULL, 0);
    for (int i = 0; i < MELO_ARENA_MAX_DEPTH; i++) {
        melo_vm_arena_enter(&vm);
        assert(vm.error == 0);
    }
    melo_vm_arena_enter(&vm);
    assert(vm.error != 0);
    printf("[nesting_limit] OK (correctly rejected at depth %d)\n", MELO_ARENA_MAX_DEPTH);
}

int main(void) {
    test_single_arena();
    test_nested_arena();
    test_direct_arena_api();
    test_nesting_limit();
    printf("ALL ARENA TESTS PASSED\n");
    return 0;
}
