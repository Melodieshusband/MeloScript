#include "melo_parser.h"
#include "melo_codegen.h"
#include "melo_vm.h"
#include <stdio.h>
#include <string.h>

static int compile_and_run(const char *src, MeloValue *out_result) {
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
    melo_codegen_free(&mod);
    return 0;
}

static int expect_int(const char *name, const char *src, int32_t expected) {
    MeloValue result;
    if (compile_and_run(src, &result)) {
        printf("[%s] FAIL (compile/run error)\n", name);
        return 1;
    }
    if (result.type != MELO_INT32 || result.as.i32 != expected) {
        printf("[%s] FAIL (expected int32 %d, got type=%d value=%d)\n",
               name, expected, result.type, result.as.i32);
        return 1;
    }
    printf("[%s] OK (%d)\n", name, result.as.i32);
    return 0;
}

int main(void) {
    int failures = 0;

    failures += expect_int("and_true_true",
        "func main() -> int32 { return (1 == 1) && (2 == 2); }", 1);

    failures += expect_int("and_true_false",
        "func main() -> int32 { return (1 == 1) && (2 == 3); }", 0);

    failures += expect_int("or_false_true",
        "func main() -> int32 { return (1 == 2) || (2 == 2); }", 1);

    failures += expect_int("or_false_false",
        "func main() -> int32 { return (1 == 2) || (3 == 4); }", 0);

    failures += expect_int("not_true",
        "func main() -> int32 { return !(1 == 2); }", 1);

    failures += expect_int("not_false",
        "func main() -> int32 { return !(1 == 1); }", 0);

    /* Short-circuit: rhs must not run if lhs already decides the result.
       We verify this indirectly: division by zero in the rhs would halt
       the VM with an error, so if short-circuit works, execution must
       skip evaluating it entirely and the program must still succeed. */
    failures += expect_int("and_short_circuits_rhs",
        "func main() -> int32 {\n"
        "    var x = 0;\n"
        "    if (x != 0 && (1 / x) > 0) {\n"
        "        return 1;\n"
        "    }\n"
        "    return 0;\n"
        "}\n", 0);

    failures += expect_int("or_short_circuits_rhs",
        "func main() -> int32 {\n"
        "    var x = 1;\n"
        "    if (x == 1 || (1 / (x - 1)) > 0) {\n"
        "        return 1;\n"
        "    }\n"
        "    return 0;\n"
        "}\n", 1);

    failures += expect_int("combined_condition",
        "func main() -> int32 {\n"
        "    var a = 5;\n"
        "    var b = 10;\n"
        "    if (a < b && !(a == b) || b == 0) {\n"
        "        return 42;\n"
        "    }\n"
        "    return 0;\n"
        "}\n", 42);

    if (failures) {
        printf("\n%d TEST(S) FAILED\n", failures);
        return 1;
    }
    printf("\nALL LOGIC TESTS PASSED\n");
    return 0;
}
