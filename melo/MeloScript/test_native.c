#include "melo_parser.h"
#include "melo_codegen.h"
#include "melo_vm.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static int native_add10(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    (void)vm;
    if (arg_count != 1 || args[0].type != MELO_INT32) {
        return -1;
    }
    *out_result = melo_int(args[0].as.i32 + 10);
    return 0;
}

static int native_fail_always(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    (void)args; (void)arg_count; (void)out_result;
    vm->error = 1;
    snprintf(vm->error_msg, sizeof(vm->error_msg), "native_fail_always always fails");
    return -1;
}

static int native_sum2(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    (void)vm;
    if (arg_count != 2) return -1;
    *out_result = melo_int(args[0].as.i32 + args[1].as.i32);
    return 0;
}

static int compile_and_run_natives(const char *src, MeloVM *vm, MeloCompiledModule *mod_out,
                                    const char **names, int name_count, MeloNativeFn *fns) {
    MeloParser parser;
    melo_parser_init(&parser, src);
    MeloProgram *prog = melo_parse_program(&parser);

    if (parser.had_error) {
        printf("PARSE ERROR at line %d: %s\n", parser.error_line, parser.error_msg);
        return 1;
    }

    MeloCompiledModule mod;
    if (melo_codegen_compile_with_natives(prog, &mod, names, name_count)) {
        printf("CODEGEN ERROR: %s\n", mod.error_msg);
        if (mod_out) *mod_out = mod;
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

    for (int i = 0; i < name_count; i++) {
        int id = melo_vm_register_native(vm, names[i], fns[i]);
        assert(id == i);
    }

    int err = melo_vm_run(vm);
    if (mod_out) {
        *mod_out = mod;
    } else {
        melo_codegen_free(&mod);
    }
    if (err) {
        printf("VM ERROR: %s\n", vm->error_msg);
        return 1;
    }
    return 0;
}

static void test_native_basic_call(void) {
    const char *src =
        "func main() -> int32 {\n"
        "    return add10(5);\n"
        "}\n";
    const char *names[] = { "add10" };
    MeloNativeFn fns[] = { native_add10 };

    MeloVM vm;
    MeloCompiledModule mod;
    int rc = compile_and_run_natives(src, &vm, &mod, names, 1, fns);
    assert(rc == 0);
    assert(vm.error == 0);
    assert(vm.reg_stack[0].as.i32 == 15);
    printf("[native_basic_call] OK (add10(5)=%d)\n", vm.reg_stack[0].as.i32);
    melo_codegen_free(&mod);
}

static void test_native_two_args(void) {
    const char *src =
        "func main() -> int32 {\n"
        "    return sum2(3, 4);\n"
        "}\n";
    const char *names[] = { "sum2" };
    MeloNativeFn fns[] = { native_sum2 };

    MeloVM vm;
    MeloCompiledModule mod;
    int rc = compile_and_run_natives(src, &vm, &mod, names, 1, fns);
    assert(rc == 0);
    assert(vm.error == 0);
    assert(vm.reg_stack[0].as.i32 == 7);
    printf("[native_two_args] OK (sum2(3,4)=%d)\n", vm.reg_stack[0].as.i32);
    melo_codegen_free(&mod);
}

static void test_native_multiple_registered(void) {
    const char *src =
        "func main() -> int32 {\n"
        "    var a = add10(1);\n"
        "    var b = sum2(a, 2);\n"
        "    return b;\n"
        "}\n";
    const char *names[] = { "add10", "sum2" };
    MeloNativeFn fns[] = { native_add10, native_sum2 };

    MeloVM vm;
    MeloCompiledModule mod;
    int rc = compile_and_run_natives(src, &vm, &mod, names, 2, fns);
    assert(rc == 0);
    assert(vm.error == 0);
    assert(vm.reg_stack[0].as.i32 == 13);
    printf("[native_multiple_registered] OK (result=%d)\n", vm.reg_stack[0].as.i32);
    melo_codegen_free(&mod);
}

static void test_native_call_failure_halts_vm(void) {
    const char *src =
        "func main() -> int32 {\n"
        "    var x = boom(1);\n"
        "    return x;\n"
        "}\n";
    const char *names[] = { "boom" };
    MeloNativeFn fns[] = { native_fail_always };

    MeloVM vm;
    MeloCompiledModule mod;
    int rc = compile_and_run_natives(src, &vm, &mod, names, 1, fns);
    assert(rc == 1);
    assert(vm.error == 1);
    assert(vm.halted == 1);
    printf("[native_call_failure_halts_vm] OK (%s)\n", vm.error_msg);
    melo_codegen_free(&mod);
}

static void test_native_unknown_name_is_codegen_error(void) {
    const char *src =
        "func main() -> int32 {\n"
        "    return not_registered(1);\n"
        "}\n";

    MeloParser parser;
    melo_parser_init(&parser, src);
    MeloProgram *prog = melo_parse_program(&parser);
    assert(!parser.had_error);

    MeloCompiledModule mod;
    int err = melo_codegen_compile(prog, &mod);
    assert(err != 0);
    printf("[native_unknown_name_is_codegen_error] OK (%s)\n", mod.error_msg);
}

static void test_native_too_many_args_rejected(void) {
    char src[1024];
    int off = 0;
    off += snprintf(src + off, sizeof(src) - off, "func main() -> int32 {\n    return manyargs(");
    for (int i = 0; i < MELO_MAX_NATIVE_ARGS + 1; i++) {
        off += snprintf(src + off, sizeof(src) - off, "%s%d", i == 0 ? "" : ", ", i);
    }
    off += snprintf(src + off, sizeof(src) - off, ");\n}\n");

    const char *names[] = { "manyargs" };
    MeloNativeFn fns[] = { native_sum2 };

    MeloParser parser;
    melo_parser_init(&parser, src);
    MeloProgram *prog = melo_parse_program(&parser);
    assert(!parser.had_error);

    MeloCompiledModule mod;
    int err = melo_codegen_compile_with_natives(prog, &mod, names, 1);
    assert(err != 0);
    printf("[native_too_many_args_rejected] OK (%s)\n", mod.error_msg);
    (void)fns;
}

static void test_native_registration_limits(void) {
    MeloVM vm;
    melo_vm_init(&vm, NULL, 0);

    char namebuf[MELO_MAX_NATIVES][16];
    int registered = 0;
    for (int i = 0; i < MELO_MAX_NATIVES + 4; i++) {
        snprintf(namebuf[i < MELO_MAX_NATIVES ? i : 0], 16, "fn_%d", i);
        int id = melo_vm_register_native(&vm, namebuf[i < MELO_MAX_NATIVES ? i : 0], native_add10);
        if (id >= 0) registered++;
    }
    assert(registered <= MELO_MAX_NATIVES);
    printf("[native_registration_limits] OK (registered=%d, cap=%d)\n", registered, MELO_MAX_NATIVES);
}

static void test_native_duplicate_name_rejected(void) {
    MeloVM vm;
    melo_vm_init(&vm, NULL, 0);

    int id1 = melo_vm_register_native(&vm, "dup", native_add10);
    int id2 = melo_vm_register_native(&vm, "dup", native_sum2);
    assert(id1 >= 0);
    assert(id2 == -1);
    printf("[native_duplicate_name_rejected] OK\n");
}

static void test_native_find(void) {
    MeloVM vm;
    melo_vm_init(&vm, NULL, 0);

    melo_vm_register_native(&vm, "alpha", native_add10);
    melo_vm_register_native(&vm, "beta", native_sum2);

    assert(melo_vm_find_native(&vm, "beta") == 1);
    assert(melo_vm_find_native(&vm, "gamma") == -1);
    printf("[native_find] OK\n");
}

int main(void) {
    test_native_basic_call();
    test_native_two_args();
    test_native_multiple_registered();
    test_native_call_failure_halts_vm();
    test_native_unknown_name_is_codegen_error();
    test_native_too_many_args_rejected();
    test_native_registration_limits();
    test_native_duplicate_name_rejected();
    test_native_find();
    printf("ALL NATIVE TESTS PASSED\n");
    return 0;
}
