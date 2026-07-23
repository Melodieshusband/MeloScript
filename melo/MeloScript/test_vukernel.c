#include "melo_parser.h"
#include "melo_codegen.h"
#include "melo_vm.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

static int compile_and_run(const char *src, MeloVM *vm, MeloCompiledModule *mod_out) {
    MeloParser parser;
    melo_parser_init(&parser, src);
    MeloProgram *prog = melo_parse_program(&parser);

    if (parser.had_error) {
        printf("PARSE ERROR at line %d: %s\n", parser.error_line, parser.error_msg);
        return 1;
    }

    if (melo_codegen_compile(prog, mod_out)) {
        printf("CODEGEN ERROR: %s\n", mod_out->error_msg);
        return 1;
    }

    if (mod_out->main_func_id < 0) {
        printf("no main() function found\n");
        return 1;
    }

    melo_vm_init(vm, mod_out->code, mod_out->code_count);
    memcpy(vm->consts, mod_out->consts, sizeof(MeloValue) * mod_out->const_count);
    vm->const_count = mod_out->const_count;
    memcpy(vm->funcs, mod_out->funcs, sizeof(MeloFuncInfo) * mod_out->func_count);
    vm->func_count = mod_out->func_count;
    vm->code = mod_out->funcs[mod_out->main_func_id].entry;

    int err = melo_vm_run(vm);
    if (err) {
        printf("VM ERROR: %s\n", vm->error_msg);
        return 1;
    }
    return 0;
}

static void test_vukernel_basic_execution(void) {
    const char *src =
        "vukernel add_offset(v: vec4) -> vec4 {\n"
        "    return v + vec4(1.0, 1.0, 1.0, 1.0);\n"
        "}\n"
        "\n"
        "func main() -> vec4 {\n"
        "    var a = vec4(1.0, 2.0, 3.0, 4.0);\n"
        "    var r = add_offset(a);\n"
        "    return r;\n"
        "}\n";

    MeloVM vm;
    MeloCompiledModule mod;
    int rc = compile_and_run(src, &vm, &mod);
    assert(rc == 0);
    assert(vm.error == 0);

    MeloValue result = vm.reg_stack[0];
    assert(result.type == MELO_VEC4);
    assert(result.as.v4.x == 2.0f);
    assert(result.as.v4.y == 3.0f);
    assert(result.as.v4.z == 4.0f);
    assert(result.as.v4.w == 5.0f);

    int found = 0;
    for (int i = 0; i < mod.func_count; i++) {
        if (mod.funcs[i].is_vukernel) found++;
    }
    assert(found == 1);

    melo_codegen_free(&mod);
    printf("[vukernel_basic_execution] OK (result=(%.1f,%.1f,%.1f,%.1f), 1 vukernel registered)\n",
           result.as.v4.x, result.as.v4.y, result.as.v4.z, result.as.v4.w);
}

static void test_regular_func_not_marked_vukernel(void) {
    const char *src =
        "func regular(x: int32) -> int32 {\n"
        "    return x + 1;\n"
        "}\n"
        "\n"
        "func main() -> int32 {\n"
        "    return regular(41);\n"
        "}\n";

    MeloVM vm;
    MeloCompiledModule mod;
    int rc = compile_and_run(src, &vm, &mod);
    assert(rc == 0);
    assert(vm.reg_stack[0].as.i32 == 42);

    for (int i = 0; i < mod.func_count; i++) {
        assert(mod.funcs[i].is_vukernel == 0);
    }

    melo_codegen_free(&mod);
    printf("[regular_func_not_marked_vukernel] OK\n");
}

static void test_vukernel_instruction_limit_exceeded(void) {
    static char src[262144];
    int pos = 0;
    pos += sprintf(src + pos, "vukernel huge(v: vec4) -> vec4 {\n");
    pos += sprintf(src + pos, "    var acc = v;\n");
    pos += sprintf(src + pos, "    var one = vec4(1.0, 1.0, 1.0, 1.0);\n");
    for (int i = 0; i < 1500; i++) {
        pos += sprintf(src + pos, "    acc = acc + one;\n");
    }
    pos += sprintf(src + pos, "    return acc;\n}\n");
    pos += sprintf(src + pos, "func main() -> vec4 {\n    return huge(vec4(0.0,0.0,0.0,0.0));\n}\n");

    MeloParser parser;
    melo_parser_init(&parser, src);
    MeloProgram *prog = melo_parse_program(&parser);
    assert(!parser.had_error);

    MeloCompiledModule mod;
    int rc = melo_codegen_compile(prog, &mod);
    assert(rc != 0);
    assert(strstr(mod.error_msg, "VU1 instruction memory") != NULL);
    printf("[vukernel_instruction_limit_exceeded] OK (%s)\n", mod.error_msg);
}

static void test_vukernel_requires_return_type(void) {
    const char *src =
        "vukernel bad(v: vec4) {\n"
        "    return v;\n"
        "}\n"
        "func main() {\n"
        "    return;\n"
        "}\n";

    MeloParser parser;
    melo_parser_init(&parser, src);
    melo_parse_program(&parser);
    assert(parser.had_error);
    printf("[vukernel_requires_return_type] OK (parser correctly rejected missing '->')\n");
}

int main(void) {
    test_vukernel_basic_execution();
    test_regular_func_not_marked_vukernel();
    test_vukernel_instruction_limit_exceeded();
    test_vukernel_requires_return_type();
    printf("ALL VUKERNEL TESTS PASSED\n");
    return 0;
}
