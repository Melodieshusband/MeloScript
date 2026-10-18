#include "melo_parser.h"
#include "melo_codegen.h"
#include "melo_vm.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static int compile_and_run(const char *src, MeloVM *vm, MeloCompiledModule *mod_out) {
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
    memcpy(vm->structs, mod.structs, sizeof(MeloStructInfo) * mod.struct_count);
    vm->struct_count = mod.struct_count;
    vm->code = mod.funcs[mod.main_func_id].entry;

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

static void test_soa_basic_read(void) {
    const char *src =
        "#[soa]\n"
        "struct Particle {\n"
        "    x: float,\n"
        "    y: float,\n"
        "}\n"
        "func main() -> float {\n"
        "    var particles = array<Particle, 3>(\n"
        "        Particle(1.0, 2.0),\n"
        "        Particle(3.0, 4.0),\n"
        "        Particle(5.0, 6.0)\n"
        "    );\n"
        "    return particles[1].x + particles[1].y;\n"
        "}\n";

    MeloVM vm;
    MeloCompiledModule mod;
    int rc = compile_and_run(src, &vm, &mod);
    assert(rc == 0);
    assert(vm.error == 0);
    assert(vm.reg_stack[0].as.f32 == 7.0f);
    printf("[soa_basic_read] OK (particles[1].x+y=%g)\n", vm.reg_stack[0].as.f32);
    melo_codegen_free(&mod);
}

static void test_soa_write(void) {
    const char *src =
        "#[soa]\n"
        "struct Particle {\n"
        "    x: float,\n"
        "    y: float,\n"
        "}\n"
        "func main() -> float {\n"
        "    var particles = array<Particle, 2>(\n"
        "        Particle(1.0, 1.0),\n"
        "        Particle(1.0, 1.0)\n"
        "    );\n"
        "    particles[0].x = 99.0;\n"
        "    return particles[0].x + particles[1].x;\n"
        "}\n";

    MeloVM vm;
    MeloCompiledModule mod;
    int rc = compile_and_run(src, &vm, &mod);
    assert(rc == 0);
    assert(vm.error == 0);
    assert(vm.reg_stack[0].as.f32 == 100.0f);
    printf("[soa_write] OK (result=%g)\n", vm.reg_stack[0].as.f32);
    melo_codegen_free(&mod);
}

static void test_soa_len(void) {
    const char *src =
        "#[soa]\n"
        "struct Particle {\n"
        "    x: float,\n"
        "}\n"
        "func main() -> int32 {\n"
        "    var particles = array<Particle, 5>(\n"
        "        Particle(1.0), Particle(1.0), Particle(1.0), Particle(1.0), Particle(1.0)\n"
        "    );\n"
        "    return len(particles);\n"
        "}\n";

    MeloVM vm;
    MeloCompiledModule mod;
    int rc = compile_and_run(src, &vm, &mod);
    assert(rc == 0);
    assert(vm.error == 0);
    assert(vm.reg_stack[0].as.i32 == 5);
    printf("[soa_len] OK (len=%d)\n", vm.reg_stack[0].as.i32);
    melo_codegen_free(&mod);
}

static void test_soa_vector_fields(void) {
    const char *src =
        "#[soa]\n"
        "struct Particle {\n"
        "    pos: vec3,\n"
        "    vel: vec3,\n"
        "    color: int32,\n"
        "}\n"
        "func main() -> float {\n"
        "    var particles = array<Particle, 2>(\n"
        "        Particle(vec3(1.0, 2.0, 3.0), vec3(0.1, 0.2, 0.3), 255),\n"
        "        Particle(vec3(4.0, 5.0, 6.0), vec3(0.4, 0.5, 0.6), 128)\n"
        "    );\n"
        "    particles[0].pos.x = 99.0;\n"
        "    var p0 = particles[0].pos;\n"
        "    return p0.x + particles[1].pos.y;\n"
        "}\n";

    MeloVM vm;
    MeloCompiledModule mod;
    int rc = compile_and_run(src, &vm, &mod);
    assert(rc == 0);
    assert(vm.error == 0);
    assert(vm.reg_stack[0].as.f32 == 104.0f);
    printf("[soa_vector_fields] OK (result=%g)\n", vm.reg_stack[0].as.f32);
    melo_codegen_free(&mod);
}

static void test_soa_rejects_nested_struct_fields(void) {
    const char *src =
        "struct Inner { x: float }\n"
        "#[soa]\n"
        "struct Particle {\n"
        "    sub: Inner,\n"
        "}\n"
        "func main() -> int32 { return 0; }\n";

    MeloParser parser;
    melo_parser_init(&parser, src);
    MeloProgram *prog = melo_parse_program(&parser);
    assert(!parser.had_error);
    MeloCompiledModule mod;
    int err = melo_codegen_compile(prog, &mod);
    assert(err != 0);
    printf("[soa_rejects_nested_struct_fields] OK (%s)\n", mod.error_msg);
}

static void test_soa_rejects_non_constructor_elements(void) {
    const char *src =
        "#[soa]\n"
        "struct Particle {\n"
        "    x: float,\n"
        "}\n"
        "func main() -> int32 {\n"
        "    var a = 1.0;\n"
        "    var particles = array<Particle, 1>(a);\n"
        "    return 0;\n"
        "}\n";

    MeloParser parser;
    melo_parser_init(&parser, src);
    MeloProgram *prog = melo_parse_program(&parser);
    assert(!parser.had_error);
    MeloCompiledModule mod;
    int err = melo_codegen_compile(prog, &mod);
    assert(err != 0);
    printf("[soa_rejects_non_constructor_elements] OK (%s)\n", mod.error_msg);
}

static void test_non_soa_struct_array_still_works(void) {
    const char *src =
        "struct Plain {\n"
        "    x: float,\n"
        "    y: float,\n"
        "}\n"
        "func main() -> float {\n"
        "    var items = array<Plain, 2>(Plain(1.0, 2.0), Plain(3.0, 4.0));\n"
        "    return items[0].x + items[1].y;\n"
        "}\n";

    MeloVM vm;
    MeloCompiledModule mod;
    int rc = compile_and_run(src, &vm, &mod);
    assert(rc == 0);
    assert(vm.error == 0);
    assert(vm.reg_stack[0].as.f32 == 5.0f);
    printf("[non_soa_struct_array_still_works] OK (regular struct arrays unaffected, result=%g)\n",
           vm.reg_stack[0].as.f32);
    melo_codegen_free(&mod);
}

static void test_soa_index_out_of_bounds(void) {
    const char *src =
        "#[soa]\n"
        "struct Particle {\n"
        "    x: float,\n"
        "}\n"
        "func main() -> float {\n"
        "    var particles = array<Particle, 2>(Particle(1.0), Particle(2.0));\n"
        "    var i = 5;\n"
        "    return particles[i].x;\n"
        "}\n";

    MeloVM vm;
    MeloCompiledModule mod;
    int rc = compile_and_run(src, &vm, &mod);
    assert(rc == 1);
    assert(vm.error == 1);
    printf("[soa_index_out_of_bounds] OK (%s)\n", vm.error_msg);
    melo_codegen_free(&mod);
}

static void test_soa_opcode_used(void) {
    const char *src =
        "#[soa]\n"
        "struct Particle {\n"
        "    x: float,\n"
        "}\n"
        "func main() -> float {\n"
        "    var particles = array<Particle, 1>(Particle(1.0));\n"
        "    return particles[0].x;\n"
        "}\n";

    MeloParser parser;
    melo_parser_init(&parser, src);
    MeloProgram *prog = melo_parse_program(&parser);
    assert(!parser.had_error);

    MeloCompiledModule mod;
    int err = melo_codegen_compile(prog, &mod);
    assert(!err);

    int found_new = 0, found_get = 0;
    for (int i = 0; i < mod.code_count; i++) {
        if (INSTR_OP(mod.code[i]) == OP_NEWARRAY_SOA) found_new = 1;
        if (INSTR_OP(mod.code[i]) == OP_SOA_GETFIELD) found_get = 1;
    }
    assert(found_new);
    assert(found_get);
    printf("[soa_opcode_used] OK (NEWARRAY_SOA and SOA_GETFIELD emitted)\n");
    melo_codegen_free(&mod);
}

int main(void) {
    test_soa_basic_read();
    test_soa_write();
    test_soa_len();
    test_soa_vector_fields();
    test_soa_rejects_nested_struct_fields();
    test_soa_rejects_non_constructor_elements();
    test_non_soa_struct_array_still_works();
    test_soa_index_out_of_bounds();
    test_soa_opcode_used();
    printf("ALL SOA TESTS PASSED\n");
    return 0;
}
