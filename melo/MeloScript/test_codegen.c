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

    printf("Compiled %d instructions, %d functions, %d constants\n",
           mod.code_count, mod.func_count, mod.const_count);

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

int main(void) {
    const char *src =
        "func square(x: float) -> float {\n"
        "    return x * x;\n"
        "}\n"
        "\n"
        "func main() -> float {\n"
        "    var a = 7.0;\n"
        "    var b = square(a);\n"
        "    return b;\n"
        "}\n";

    MeloValue result;
    if (compile_and_run(src, &result)) return 1;

    printf("main() returned float %.2f\n", result.as.f32);
    if (result.as.f32 != 49.0f) { printf("FAIL\n"); return 1; }
    printf("OK: function calls + float arithmetic\n\n");

    const char *src2 =
        "func main() -> float {\n"
        "    var a = vec4(1.0, 2.0, 3.0, 0.0);\n"
        "    var b = vec4(4.0, 5.0, 6.0, 0.0);\n"
        "    var d = vdot(a, b);\n"
        "    return d;\n"
        "}\n";

    if (compile_and_run(src2, &result)) return 1;
    printf("main() returned float %.2f\n", result.as.f32);
    if (result.as.f32 != 32.0f) { printf("FAIL\n"); return 1; }
    printf("OK: vector construction + vdot\n\n");

    const char *src3 =
        "func main() -> int32 {\n"
        "    var i = 0;\n"
        "    var sum = 0;\n"
        "    while (i < 10) {\n"
        "        sum = sum + i;\n"
        "        i = i + 1;\n"
        "    }\n"
        "    return sum;\n"
        "}\n";

    if (compile_and_run(src3, &result)) return 1;
    printf("main() returned int32 %d\n", result.as.i32);
    if (result.as.i32 != 45) { printf("FAIL\n"); return 1; }
    printf("OK: while loop\n\n");

    const char *src4 =
        "func main() -> int32 {\n"
        "    var x = 5;\n"
        "    if (x > 3) {\n"
        "        return 1;\n"
        "    } else {\n"
        "        return 0;\n"
        "    }\n"
        "}\n";

    if (compile_and_run(src4, &result)) return 1;
    printf("main() returned int32 %d\n", result.as.i32);
    if (result.as.i32 != 1) { printf("FAIL\n"); return 1; }
    printf("OK: if/else branching\n");

    printf("\nALL TESTS PASSED\n");
    return 0;
}
