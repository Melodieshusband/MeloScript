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

static void test_vec2_const_type_tag(void) {
    const char *src =
        "func main() -> float {\n"
        "    var v = vec2(3.0, 4.0);\n"
        "    return v.x + v.y;\n"
        "}\n";
    MeloVM vm;
    int rc = compile_and_run(src, &vm);
    assert(rc == 0);
    assert(vm.error == 0);
    assert(vm.reg_stack[0].as.f32 == 7.0f);
    printf("[vec2_const_type_tag] OK (x+y=%g)\n", vm.reg_stack[0].as.f32);
}

static void test_vec3_const_component_access(void) {
    const char *src =
        "func main() -> float {\n"
        "    var v = vec3(1.0, 2.0, 3.0);\n"
        "    return v.x + v.y + v.z;\n"
        "}\n";
    MeloVM vm;
    int rc = compile_and_run(src, &vm);
    assert(rc == 0);
    assert(vm.error == 0);
    assert(vm.reg_stack[0].as.f32 == 6.0f);
    printf("[vec3_const_component_access] OK (sum=%g)\n", vm.reg_stack[0].as.f32);
}

static void test_vec3_rejects_w_component(void) {
    const char *src =
        "func main() -> float {\n"
        "    var v = vec3(1.0, 2.0, 3.0);\n"
        "    return v.w;\n"
        "}\n";
    MeloParser parser;
    melo_parser_init(&parser, src);
    MeloProgram *prog = melo_parse_program(&parser);
    assert(!parser.had_error);
    MeloCompiledModule mod;
    int err = melo_codegen_compile(prog, &mod);
    assert(err != 0);
    printf("[vec3_rejects_w_component] OK (%s)\n", mod.error_msg);
}

static void test_vec2_add_preserves_rank(void) {
    const char *src =
        "func main() -> float {\n"
        "    var a = vec2(1.0, 2.0);\n"
        "    var b = vec2(10.0, 20.0);\n"
        "    var c = a + b;\n"
        "    return c.x + c.y;\n"
        "}\n";
    MeloVM vm;
    int rc = compile_and_run(src, &vm);
    assert(rc == 0);
    assert(vm.error == 0);
    assert(vm.reg_stack[0].as.f32 == 33.0f);
    printf("[vec2_add_preserves_rank] OK (sum=%g)\n", vm.reg_stack[0].as.f32);
}

static void test_vec3_normalize_preserves_rank(void) {
    const char *src =
        "func main() -> float {\n"
        "    var v = vec3(3.0, 0.0, 4.0);\n"
        "    var n = vnorm(v);\n"
        "    return n.x * 5.0;\n"
        "}\n";
    MeloVM vm;
    int rc = compile_and_run(src, &vm);
    assert(rc == 0);
    assert(vm.error == 0);
    float expected = 3.0f;
    assert(vm.reg_stack[0].as.f32 > expected - 0.001f && vm.reg_stack[0].as.f32 < expected + 0.001f);
    printf("[vec3_normalize_preserves_rank] OK (n.x*5=%g)\n", vm.reg_stack[0].as.f32);
}

static void test_vec3_scale_preserves_rank(void) {
    const char *src =
        "func main() -> float {\n"
        "    var v = vec3(1.0, 2.0, 3.0);\n"
        "    var s = v * 2.0;\n"
        "    return s.x + s.y + s.z;\n"
        "}\n";
    MeloVM vm;
    int rc = compile_and_run(src, &vm);
    assert(rc == 0);
    assert(vm.error == 0);
    assert(vm.reg_stack[0].as.f32 == 12.0f);
    printf("[vec3_scale_preserves_rank] OK (sum=%g)\n", vm.reg_stack[0].as.f32);
}

static void test_vec2_non_const_ctor(void) {
    const char *src =
        "func main() -> float {\n"
        "    var a = 1.0;\n"
        "    var b = 2.0;\n"
        "    var v = vec2(a + 1.0, b + 1.0);\n"
        "    return v.x + v.y;\n"
        "}\n";
    MeloVM vm;
    int rc = compile_and_run(src, &vm);
    assert(rc == 0);
    assert(vm.error == 0);
    assert(vm.reg_stack[0].as.f32 == 5.0f);
    printf("[vec2_non_const_ctor] OK (sum=%g)\n", vm.reg_stack[0].as.f32);
}

static void test_vec4_non_const_ctor_all_components(void) {
    const char *src =
        "func main() -> float {\n"
        "    var a = 1.0;\n"
        "    var v = vec4(a, a + 1.0, a + 2.0, a + 3.0);\n"
        "    return v.x + v.y + v.z + v.w;\n"
        "}\n";
    MeloVM vm;
    int rc = compile_and_run(src, &vm);
    assert(rc == 0);
    assert(vm.error == 0);
    assert(vm.reg_stack[0].as.f32 == 10.0f);
    printf("[vec4_non_const_ctor_all_components] OK (sum=%g)\n", vm.reg_stack[0].as.f32);
}

static void test_vec_component_assignment(void) {
    const char *src =
        "func main() -> float {\n"
        "    var v = vec3(1.0, 2.0, 3.0);\n"
        "    v.y = 99.0;\n"
        "    return v.x + v.y + v.z;\n"
        "}\n";
    MeloVM vm;
    int rc = compile_and_run(src, &vm);
    assert(rc == 0);
    assert(vm.error == 0);
    assert(vm.reg_stack[0].as.f32 == 103.0f);
    printf("[vec_component_assignment] OK (sum=%g)\n", vm.reg_stack[0].as.f32);
}

static void test_vec_field_access_on_non_vector_rejected(void) {
    const char *src =
        "func main() -> float {\n"
        "    var v = 5.0;\n"
        "    return v.x;\n"
        "}\n";
    MeloParser parser;
    melo_parser_init(&parser, src);
    MeloProgram *prog = melo_parse_program(&parser);
    assert(!parser.had_error);
    MeloCompiledModule mod;
    int err = melo_codegen_compile(prog, &mod);
    assert(err != 0);
    printf("[vec_field_access_on_non_vector_rejected] OK (%s)\n", mod.error_msg);
}

static void test_vec2_dot_product(void) {
    const char *src =
        "func main() -> float {\n"
        "    var a = vec2(1.0, 0.0);\n"
        "    var b = vec2(0.0, 1.0);\n"
        "    return vdot(a, b);\n"
        "}\n";
    MeloVM vm;
    int rc = compile_and_run(src, &vm);
    assert(rc == 0);
    assert(vm.error == 0);
    assert(vm.reg_stack[0].as.f32 == 0.0f);
    printf("[vec2_dot_product] OK (dot=%g)\n", vm.reg_stack[0].as.f32);
}

int main(void) {
    test_vec2_const_type_tag();
    test_vec3_const_component_access();
    test_vec3_rejects_w_component();
    test_vec2_add_preserves_rank();
    test_vec3_normalize_preserves_rank();
    test_vec3_scale_preserves_rank();
    test_vec2_non_const_ctor();
    test_vec4_non_const_ctor_all_components();
    test_vec_component_assignment();
    test_vec_field_access_on_non_vector_rejected();
    test_vec2_dot_product();
    printf("ALL VEC2/VEC3 TESTS PASSED\n");
    return 0;
}
