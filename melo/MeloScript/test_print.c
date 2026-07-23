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

static void test_hello_world(void) {
    const char *src =
        "func main() {\n"
        "    print(\"Hello World\");\n"
        "    return;\n"
        "}\n";

    printf("--- expected output: Hello World ---\n");
    MeloVM vm;
    int rc = compile_and_run(src, &vm);
    assert(rc == 0);
    assert(vm.error == 0);
    printf("--- end output ---\n");
    printf("[hello_world] OK\n");
}

static void test_print_numbers(void) {
    const char *src =
        "func main() {\n"
        "    print(42);\n"
        "    print(3.5);\n"
        "    print(vec4(1.0, 2.0, 3.0, 4.0));\n"
        "    return;\n"
        "}\n";

    printf("--- expected output: 42 / 3.5 / (1, 2, 3, 4) ---\n");
    MeloVM vm;
    int rc = compile_and_run(src, &vm);
    assert(rc == 0);
    assert(vm.error == 0);
    printf("--- end output ---\n");
    printf("[print_numbers] OK\n");
}

static void test_escape_sequences(void) {
    const char *src =
        "func main() {\n"
        "    print(\"line1\\nline2\\ttabbed\\\"quoted\\\"\");\n"
        "    return;\n"
        "}\n";

    printf("--- expected output: line1 / line2\\ttabbed\"quoted\" ---\n");
    MeloVM vm;
    int rc = compile_and_run(src, &vm);
    assert(rc == 0);
    assert(vm.error == 0);
    printf("--- end output ---\n");
    printf("[escape_sequences] OK\n");
}

static void test_string_dedup(void) {
    const char *src =
        "func main() {\n"
        "    print(\"same\");\n"
        "    print(\"same\");\n"
        "    return;\n"
        "}\n";

    MeloParser parser;
    melo_parser_init(&parser, src);
    MeloProgram *prog = melo_parse_program(&parser);
    assert(!parser.had_error);

    MeloCompiledModule mod;
    int rc = melo_codegen_compile(prog, &mod);
    assert(rc == 0);

    int string_const_count = 0;
    for (int i = 0; i < mod.const_count; i++) {
        if (mod.consts[i].type == MELO_STRING) string_const_count++;
    }
    assert(string_const_count == 1);
    melo_codegen_free(&mod);
    printf("[string_dedup] OK (1 constant for 2 identical literals)\n");
}

int main(void) {
    test_hello_world();
    test_print_numbers();
    test_escape_sequences();
    test_string_dedup();
    printf("ALL STRING/PRINT TESTS PASSED\n");
    return 0;
}
