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

int main(void) {
    /* break exits a while loop early */
    {
        const char *src =
            "func main() -> int32 {\n"
            "    var i = 0;\n"
            "    var sum = 0;\n"
            "    while (i < 100) {\n"
            "        if (i == 5) {\n"
            "            break;\n"
            "        }\n"
            "        sum = sum + i;\n"
            "        i = i + 1;\n"
            "    }\n"
            "    return sum;\n"
            "}\n";
        MeloValue r;
        if (compile_and_run(src, &r)) return 1;
        if (r.as.i32 != 10) { printf("FAIL: while break, got %d expected 10\n", r.as.i32); return 1; }
        printf("OK: while+break (sum 0..4 = %d)\n", r.as.i32);
    }

    /* continue skips even numbers in a while loop */
    {
        const char *src =
            "func main() -> int32 {\n"
            "    var i = 0;\n"
            "    var sum = 0;\n"
            "    while (i < 10) {\n"
            "        i = i + 1;\n"
            "        if (i % 2 == 0) {\n"
            "            continue;\n"
            "        }\n"
            "        sum = sum + i;\n"
            "    }\n"
            "    return sum;\n"
            "}\n";
        MeloValue r;
        if (compile_and_run(src, &r)) return 1;
        if (r.as.i32 != 25) { printf("FAIL: while continue, got %d expected 25\n", r.as.i32); return 1; }
        printf("OK: while+continue (sum of odd 1..10 = %d)\n", r.as.i32);
    }

    /* break/continue inside for..in over an array */
    {
        const char *src =
            "func main() -> int32 {\n"
            "    var arr = array<int32>(1, 2, 3, 4, 5, 6, 7, 8);\n"
            "    var sum = 0;\n"
            "    for v in arr {\n"
            "        if (v == 6) {\n"
            "            break;\n"
            "        }\n"
            "        if (v % 2 == 0) {\n"
            "            continue;\n"
            "        }\n"
            "        sum = sum + v;\n"
            "    }\n"
            "    return sum;\n"
            "}\n";
        MeloValue r;
        if (compile_and_run(src, &r)) return 1;
        if (r.as.i32 != 9) { printf("FAIL: for break+continue, got %d expected 9\n", r.as.i32); return 1; }
        printf("OK: for..in break+continue (1+3+5 = %d)\n", r.as.i32);
    }

    /* break from inside an arena block must not corrupt the arena stack */
    {
        const char *src =
            "func main() -> int32 {\n"
            "    var i = 0;\n"
            "    var sum = 0;\n"
            "    while (i < 20) {\n"
            "        arena Frame {\n"
            "            var doubled = i * 2;\n"
            "            if (doubled > 10) {\n"
            "                break;\n"
            "            }\n"
            "            sum = sum + doubled;\n"
            "        }\n"
            "        i = i + 1;\n"
            "    }\n"
            "    return sum;\n"
            "}\n";
        MeloValue r;
        if (compile_and_run(src, &r)) return 1;
        /* i=0..5: doubled=0,2,4,6,8,10 all summed (sum=30), i=6: doubled=12>10 -> break */
        if (r.as.i32 != 30) { printf("FAIL: arena+break, got %d expected 30\n", r.as.i32); return 1; }
        printf("OK: break inside arena block unwinds correctly (sum = %d)\n", r.as.i32);

        /* Run twice more to make sure arena pool wasn't left in a bad state */
        MeloValue r2;
        if (compile_and_run(src, &r2)) return 1;
        if (r2.as.i32 != 30) { printf("FAIL: repeated run after arena+break gave %d\n", r2.as.i32); return 1; }
        printf("OK: arena pool state is clean across repeated runs\n");
    }

    /* nested loops: break only exits the innermost loop */
    {
        const char *src =
            "func main() -> int32 {\n"
            "    var total = 0;\n"
            "    var i = 0;\n"
            "    while (i < 3) {\n"
            "        var j = 0;\n"
            "        while (j < 10) {\n"
            "            if (j == 2) {\n"
            "                break;\n"
            "            }\n"
            "            total = total + 1;\n"
            "            j = j + 1;\n"
            "        }\n"
            "        i = i + 1;\n"
            "    }\n"
            "    return total;\n"
            "}\n";
        MeloValue r;
        if (compile_and_run(src, &r)) return 1;
        if (r.as.i32 != 6) { printf("FAIL: nested break, got %d expected 6\n", r.as.i32); return 1; }
        printf("OK: break only exits innermost loop (total = %d)\n", r.as.i32);
    }

    printf("\nALL BREAK/CONTINUE TESTS PASSED\n");
    return 0;
}
