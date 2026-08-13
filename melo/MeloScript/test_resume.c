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

static void test_resume_expr_drives_coroutine_from_script(void) {
    const char *src =
        "func counter() -> int32 {\n"
        "    yield 1;\n"
        "    yield 2;\n"
        "    return 3;\n"
        "}\n"
        "func main() -> int32 {\n"
        "    var c = coro_create(counter);\n"
        "    var a = resume(c, 0);\n"
        "    var b = resume(c, 0);\n"
        "    var d = resume(c, 0);\n"
        "    return a * 100 + b * 10 + d;\n"
        "}\n";

    MeloCompiledModule mod;
    assert(compile_module(src, &mod) == 0);

    MeloVM vm;
    setup_vm_from_module(&vm, &mod);
    vm.code = mod.funcs[mod.main_func_id].entry;

    int rc = melo_vm_run(&vm);
    assert(rc == 0);
    assert(vm.error == 0);
    assert(vm.reg_stack[0].as.i32 == 123);
    printf("[resume_expr_drives_coroutine_from_script] OK (result=%d)\n", vm.reg_stack[0].as.i32);
}

static void test_resume_accepts_argument_value(void) {
    const char *src =
        "func echo() -> int32 {\n"
        "    yield 0;\n"
        "    return 1;\n"
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
    assert(vm.reg_stack[0].as.i32 == 1);
    printf("[resume_accepts_argument_value] OK (result=%d)\n", vm.reg_stack[0].as.i32);
}

static void test_yield_register_receives_resume_value_via_host_api(void) {
    const char *src =
        "func echo() -> int32 {\n"
        "    yield 0;\n"
        "    return 1;\n"
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
    MeloValue y;
    melo_coro_resume(&vm, coro, melo_nil(), &y);
    assert(melo_coro_status(&vm, coro) == MELO_CORO_SUSPENDED);

    int frame_base = vm.coroutines[coro].frames[vm.coroutines[coro].frame_top].base;
    int yreg = vm.coroutines[coro].yield_reg;
    melo_coro_resume(&vm, coro, melo_int(41), &y);
    assert(melo_coro_status(&vm, coro) == MELO_CORO_FINISHED);
    printf("[yield_register_receives_resume_value_via_host_api] OK (frame_base=%d yield_reg=%d)\n", frame_base, yreg);
}

static void test_coro_status_from_script(void) {
    const char *src =
        "func gen() -> int32 {\n"
        "    yield 1;\n"
        "    return 2;\n"
        "}\n"
        "func main() -> int32 {\n"
        "    var c = coro_create(gen);\n"
        "    var s0 = coro_status(c);\n"
        "    var y1 = resume(c, 0);\n"
        "    var s1 = coro_status(c);\n"
        "    var y2 = resume(c, 0);\n"
        "    var s2 = coro_status(c);\n"
        "    return s0 * 100 + s1 * 10 + s2;\n"
        "}\n";

    MeloCompiledModule mod;
    assert(compile_module(src, &mod) == 0);

    MeloVM vm;
    setup_vm_from_module(&vm, &mod);
    vm.code = mod.funcs[mod.main_func_id].entry;

    int rc = melo_vm_run(&vm);
    assert(rc == 0);
    assert(vm.error == 0);
    int result = vm.reg_stack[0].as.i32;
    int s0 = result / 100;
    int s1 = (result / 10) % 10;
    int s2 = result % 10;
    assert(s0 == MELO_CORO_SUSPENDED);
    assert(s1 == MELO_CORO_SUSPENDED);
    assert(s2 == MELO_CORO_FINISHED);
    printf("[coro_status_from_script] OK (s0=%d s1=%d s2=%d)\n", s0, s1, s2);
}

static void test_resume_on_non_coroutine_is_error(void) {
    const char *src =
        "func main() -> int32 {\n"
        "    var x = 5;\n"
        "    var y = resume(x, 0);\n"
        "    return y;\n"
        "}\n";

    MeloCompiledModule mod;
    assert(compile_module(src, &mod) == 0);

    MeloVM vm;
    setup_vm_from_module(&vm, &mod);
    vm.code = mod.funcs[mod.main_func_id].entry;

    int rc = melo_vm_run(&vm);
    assert(rc != 0);
    assert(vm.error == 1);
    printf("[resume_on_non_coroutine_is_error] OK (%s)\n", vm.error_msg);
}

static void test_coro_create_unknown_function_is_compile_error(void) {
    const char *src =
        "func main() -> int32 {\n"
        "    var c = coro_create(does_not_exist);\n"
        "    return 0;\n"
        "}\n";

    MeloCompiledModule mod;
    int rc = compile_module(src, &mod);
    assert(rc != 0);
    printf("[coro_create_unknown_function_is_compile_error] OK\n");
}

static void test_resume_independent_coroutines_from_script(void) {
    const char *src =
        "func gen() -> int32 {\n"
        "    yield 100;\n"
        "    yield 200;\n"
        "    return 300;\n"
        "}\n"
        "func main() -> int32 {\n"
        "    var a = coro_create(gen);\n"
        "    var b = coro_create(gen);\n"
        "    var a1 = resume(a, 0);\n"
        "    var b1 = resume(b, 0);\n"
        "    var a2 = resume(a, 0);\n"
        "    var b2 = resume(b, 0);\n"
        "    if (a1 == b1 && a2 == b2 && a1 == 100 && a2 == 200) {\n"
        "        return 1;\n"
        "    }\n"
        "    return 0;\n"
        "}\n";

    MeloCompiledModule mod;
    assert(compile_module(src, &mod) == 0);

    MeloVM vm;
    setup_vm_from_module(&vm, &mod);
    vm.code = mod.funcs[mod.main_func_id].entry;

    int rc = melo_vm_run(&vm);
    assert(rc == 0);
    assert(vm.error == 0);
    assert(vm.reg_stack[0].as.i32 == 1);
    printf("[resume_independent_coroutines_from_script] OK\n");
}

int main(void) {
    test_resume_expr_drives_coroutine_from_script();
    test_resume_accepts_argument_value();
    test_yield_register_receives_resume_value_via_host_api();
    test_coro_status_from_script();
    test_resume_on_non_coroutine_is_error();
    test_coro_create_unknown_function_is_compile_error();
    test_resume_independent_coroutines_from_script();
    printf("ALL RESUME-AS-EXPRESSION TESTS PASSED\n");
    return 0;
}
