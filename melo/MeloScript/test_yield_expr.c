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

static void test_var_bound_yield_receives_resume_value_via_host_api(void) {
    const char *src =
        "func echo() -> int32 {\n"
        "    var got = yield 0;\n"
        "    return got + 1;\n"
        "}\n"
        "func main() -> int32 { return 0; }\n";

    MeloCompiledModule mod;
    assert(compile_module(src, &mod) == 0);

    int echo_id = -1;
    for (int i = 0; i < mod.func_count; i++) {
        if (i != mod.main_func_id) echo_id = i;
    }
    assert(echo_id >= 0);

    MeloVM vm;
    setup_vm_from_module(&vm, &mod);

    int coro = melo_coro_create(&vm, echo_id);
    MeloValue y1;
    int rc1 = melo_coro_resume(&vm, coro, melo_nil(), &y1);
    assert(rc1 == 0);
    assert(vm.error == 0);
    assert(melo_coro_status(&vm, coro) == MELO_CORO_SUSPENDED);
    assert(y1.as.i32 == 0);

    MeloValue y2;
    int rc2 = melo_coro_resume(&vm, coro, melo_int(41), &y2);
    assert(rc2 == 0);
    assert(vm.error == 0);
    assert(melo_coro_status(&vm, coro) == MELO_CORO_FINISHED);
    assert(y2.as.i32 == 42);
    printf("[var_bound_yield_receives_resume_value_via_host_api] OK (got 41, returned %d)\n", y2.as.i32);
}

static void test_var_bound_yield_from_script_via_resume(void) {
    const char *src =
        "func echo() -> int32 {\n"
        "    var got = yield 0;\n"
        "    return got + 1;\n"
        "}\n"
        "func main() -> int32 {\n"
        "    var c = coro_create(echo);\n"
        "    var first = resume(c, 0);\n"
        "    var second = resume(c, 41);\n"
        "    return second;\n"
        "}\n";

    MeloCompiledModule mod;
    assert(compile_module(src, &mod) == 0);

    MeloVM vm;
    setup_vm_from_module(&vm, &mod);
    vm.code = mod.funcs[mod.main_func_id].entry;

    int rc = melo_vm_run(&vm);
    assert(rc == 0);
    assert(vm.error == 0);
    assert(vm.reg_stack[0].as.i32 == 42);
    printf("[var_bound_yield_from_script_via_resume] OK (result=%d)\n", vm.reg_stack[0].as.i32);
}

static void test_yield_expr_in_arithmetic(void) {
    const char *src =
        "func doubler() -> int32 {\n"
        "    var got = yield 0;\n"
        "    return got * 2;\n"
        "}\n"
        "func main() -> int32 {\n"
        "    var c = coro_create(doubler);\n"
        "    var first = resume(c, 0);\n"
        "    var second = resume(c, 21);\n"
        "    return second;\n"
        "}\n";

    MeloCompiledModule mod;
    assert(compile_module(src, &mod) == 0);

    MeloVM vm;
    setup_vm_from_module(&vm, &mod);
    vm.code = mod.funcs[mod.main_func_id].entry;

    int rc = melo_vm_run(&vm);
    assert(rc == 0);
    assert(vm.error == 0);
    assert(vm.reg_stack[0].as.i32 == 42);
    printf("[yield_expr_in_arithmetic] OK (result=%d)\n", vm.reg_stack[0].as.i32);
}

static void test_multiple_var_bound_yields_in_sequence(void) {
    const char *src =
        "func accumulator() -> int32 {\n"
        "    var a = yield 0;\n"
        "    var b = yield 0;\n"
        "    var c = yield 0;\n"
        "    return a + b + c;\n"
        "}\n"
        "func main() -> int32 {\n"
        "    var co = coro_create(accumulator);\n"
        "    var r0 = resume(co, 0);\n"
        "    var r1 = resume(co, 10);\n"
        "    var r2 = resume(co, 20);\n"
        "    var r3 = resume(co, 30);\n"
        "    return r3;\n"
        "}\n";

    MeloCompiledModule mod;
    assert(compile_module(src, &mod) == 0);

    MeloVM vm;
    setup_vm_from_module(&vm, &mod);
    vm.code = mod.funcs[mod.main_func_id].entry;

    int rc = melo_vm_run(&vm);
    assert(rc == 0);
    assert(vm.error == 0);
    assert(vm.reg_stack[0].as.i32 == 60);
    printf("[multiple_var_bound_yields_in_sequence] OK (result=%d)\n", vm.reg_stack[0].as.i32);
}

static void test_bare_yield_statement_still_works(void) {
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
        if (i != mod.main_func_id) counter_id = i;
    }

    MeloVM vm;
    setup_vm_from_module(&vm, &mod);

    int coro = melo_coro_create(&vm, counter_id);
    MeloValue y1, y2, y3;
    melo_coro_resume(&vm, coro, melo_nil(), &y1);
    melo_coro_resume(&vm, coro, melo_nil(), &y2);
    melo_coro_resume(&vm, coro, melo_nil(), &y3);
    assert(y1.as.i32 == 1);
    assert(y2.as.i32 == 2);
    assert(y3.as.i32 == 3);
    printf("[bare_yield_statement_still_works] OK (1, 2, 3 unaffected by grammar change)\n");
}

int main(void) {
    test_var_bound_yield_receives_resume_value_via_host_api();
    test_var_bound_yield_from_script_via_resume();
    test_yield_expr_in_arithmetic();
    test_multiple_var_bound_yields_in_sequence();
    test_bare_yield_statement_still_works();
    printf("ALL YIELD-AS-EXPRESSION TESTS PASSED\n");
    return 0;
}
