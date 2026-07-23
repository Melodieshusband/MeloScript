#include "melo_parser.h"
#include "melo_codegen.h"
#include "melo_vm.h"
#include "melo_opcodes.h"
#include <stdio.h>
#include <string.h>

static int compile_only(const char *src, MeloCompiledModule *mod) {
    MeloParser parser;
    melo_parser_init(&parser, src);
    MeloProgram *prog = melo_parse_program(&parser);

    if (parser.had_error) {
        printf("PARSE ERROR at line %d: %s\n", parser.error_line, parser.error_msg);
        return 1;
    }

    if (melo_codegen_compile(prog, mod)) {
        return 2;
    }
    return 0;
}

static int compile_and_run(const char *src, MeloValue *out_result, MeloCompiledModule *mod_out) {
    MeloCompiledModule mod;
    int rc = compile_only(src, &mod);
    if (rc == 1) return 1;
    if (rc == 2) {
        printf("CODEGEN ERROR: %s\n", mod.error_msg);
        return 1;
    }

    if (mod.main_func_id < 0) {
        printf("no main() function found\n");
        return 1;
    }

    MeloVM vm;
    melo_vm_init(&vm, mod.code, mod.code_count);
    memcpy(vm.consts, mod.consts, sizeof(MeloValue) * mod.const_count);
    vm.const_count = mod.const_count;
    memcpy(vm.funcs, mod.funcs, sizeof(MeloFuncInfo) * mod.func_count);
    vm.func_count = mod.func_count;
    vm.code = mod.funcs[mod.main_func_id].entry;

    int err = melo_vm_run(&vm);
    if (err) {
        printf("VM ERROR: %s\n", vm.error_msg);
        return 1;
    }

    *out_result = vm.reg_stack[0];
    if (mod_out) {
        *mod_out = mod;
    } else {
        melo_codegen_free(&mod);
    }
    return 0;
}

static int count_opcode(MeloCompiledModule *mod, MeloOpcode op) {
    int n = 0;
    for (int i = 0; i < mod->code_count; i++) {
        if (INSTR_OP(mod->code[i]) == op) n++;
    }
    return n;
}

int main(void) {
    const char *src_int =
        "@hot\n"
        "func add_ints(a: int32, b: int32) -> int32 {\n"
        "    return a + b;\n"
        "}\n"
        "\n"
        "func main() -> int32 {\n"
        "    return add_ints(19, 23);\n"
        "}\n";

    MeloValue result;
    MeloCompiledModule mod;
    if (compile_and_run(src_int, &result, &mod)) return 1;
    printf("main() returned int32 %d\n", result.as.i32);
    if (result.as.i32 != 42) { printf("FAIL\n"); return 1; }
    if (count_opcode(&mod, OP_ADDI) < 1) {
        printf("FAIL: expected OP_ADDI to be emitted for @hot int32 add\n");
        return 1;
    }
    if (count_opcode(&mod, OP_ADD) != 0) {
        printf("FAIL: generic OP_ADD should not appear inside @hot function\n");
        return 1;
    }
    printf("OK: @hot int32 function uses OP_ADDI\n\n");
    melo_codegen_free(&mod);

    const char *src_float =
        "@hot\n"
        "func mix(a: float, b: float, t: float) -> float {\n"
        "    return a + (b - a) * t;\n"
        "}\n"
        "\n"
        "func main() -> float {\n"
        "    return mix(0.0, 10.0, 0.5);\n"
        "}\n";

    if (compile_and_run(src_float, &result, &mod)) return 1;
    printf("main() returned float %.2f\n", result.as.f32);
    if (result.as.f32 != 5.0f) { printf("FAIL\n"); return 1; }
    if (count_opcode(&mod, OP_ADDF) < 1 || count_opcode(&mod, OP_SUBF) < 1 || count_opcode(&mod, OP_MULF) < 1) {
        printf("FAIL: expected typed float opcodes in @hot function\n");
        return 1;
    }
    printf("OK: @hot float function uses OP_ADDF/OP_SUBF/OP_MULF\n\n");
    melo_codegen_free(&mod);

    const char *src_noret =
        "@hot\n"
        "func broken2(a: int32, b: int32) {\n"
        "    var c = a + b;\n"
        "}\n"
        "func main() -> int32 { return broken2(1, 2); }\n";

    MeloCompiledModule mod3;
    int rc = compile_only(src_noret, &mod3);
    if (rc != 2) {
        printf("FAIL: expected codegen error for @hot function missing return type\n");
        return 1;
    }
    printf("OK: @hot function without return type rejected (%s)\n\n", mod3.error_msg);

    const char *src_call_count =
        "@hot\n"
        "func sq(x: int32) -> int32 {\n"
        "    return x * x;\n"
        "}\n"
        "func main() -> int32 {\n"
        "    var s = 0;\n"
        "    var i = 0;\n"
        "    while (i < 5) {\n"
        "        s = s + sq(i);\n"
        "        i = i + 1;\n"
        "    }\n"
        "    return s;\n"
        "}\n";

    MeloCompiledModule mod4;
    rc = compile_only(src_call_count, &mod4);
    if (rc != 0) {
        printf("FAIL: unexpected codegen error: %s\n", mod4.error_msg);
        return 1;
    }
    MeloVM vm;
    melo_vm_init(&vm, mod4.code, mod4.code_count);
    memcpy(vm.consts, mod4.consts, sizeof(MeloValue) * mod4.const_count);
    vm.const_count = mod4.const_count;
    memcpy(vm.funcs, mod4.funcs, sizeof(MeloFuncInfo) * mod4.func_count);
    vm.func_count = mod4.func_count;
    vm.code = mod4.funcs[mod4.main_func_id].entry;
    int err = melo_vm_run(&vm);
    if (err) { printf("VM ERROR: %s\n", vm.error_msg); return 1; }

    int hot_func_id = -1;
    for (int i = 0; i < vm.func_count; i++) {
        if (vm.funcs[i].is_hot) hot_func_id = i;
    }
    if (hot_func_id < 0) {
        printf("FAIL: no function marked is_hot in compiled module\n");
        return 1;
    }
    if (vm.funcs[hot_func_id].hot_call_count != 5) {
        printf("FAIL: expected hot_call_count == 5, got %u\n", vm.funcs[hot_func_id].hot_call_count);
        return 1;
    }
    printf("OK: @hot call count tracked (%u calls)\n\n", vm.funcs[hot_func_id].hot_call_count);
    melo_codegen_free(&mod4);

    printf("ALL HOT TESTS PASSED\n");
    return 0;
}
