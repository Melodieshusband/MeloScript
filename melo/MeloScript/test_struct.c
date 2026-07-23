#include "melo_parser.h"
#include "melo_codegen.h"
#include "melo_vm.h"
#include <stdio.h>
#include <string.h>

static int compile_and_run(const char *src, MeloValue *out_result, char *err_out, size_t err_out_size) {
    MeloParser parser;
    melo_parser_init(&parser, src);
    MeloProgram *prog = melo_parse_program(&parser);

    if (parser.had_error) {
        snprintf(err_out, err_out_size, "PARSE ERROR at line %d: %s", parser.error_line, parser.error_msg);
        return 1;
    }

    MeloCompiledModule mod;
    if (melo_codegen_compile(prog, &mod)) {
        snprintf(err_out, err_out_size, "CODEGEN ERROR: %s", mod.error_msg);
        return 1;
    }

    if (mod.main_func_id < 0) {
        snprintf(err_out, err_out_size, "no main() function found");
        return 1;
    }

    MeloVM vm;
    melo_vm_init(&vm, mod.code, mod.code_count);
    memcpy(vm.consts, mod.consts, sizeof(MeloValue) * mod.const_count);
    vm.const_count = mod.const_count;
    memcpy(vm.funcs, mod.funcs, sizeof(MeloFuncInfo) * mod.func_count);
    vm.func_count = mod.func_count;
    memcpy(vm.structs, mod.structs, sizeof(MeloStructInfo) * mod.struct_count);
    vm.struct_count = mod.struct_count;
    vm.code = mod.funcs[mod.main_func_id].entry;

    int err = melo_vm_run(&vm);
    if (err) {
        snprintf(err_out, err_out_size, "VM ERROR: %s", vm.error_msg);
        return 1;
    }

    *out_result = vm.reg_stack[0];
    melo_codegen_free(&mod);
    return 0;
}

static int expect_float(const char *name, const char *src, float expected) {
    MeloValue result;
    char err[512] = {0};
    if (compile_and_run(src, &result, err, sizeof(err))) {
        printf("[%s] FAIL (%s)\n", name, err);
        return 1;
    }
    float got = result.type == MELO_FLOAT ? result.as.f32
               : result.type == MELO_INT32 ? (float)result.as.i32
               : -999999.0f;
    if (result.type != MELO_FLOAT && result.type != MELO_INT32) {
        printf("[%s] FAIL (expected numeric result, got type=%d)\n", name, result.type);
        return 1;
    }
    if (got != expected) {
        printf("[%s] FAIL (expected %g, got %g)\n", name, (double)expected, (double)got);
        return 1;
    }
    printf("[%s] OK (%g)\n", name, (double)got);
    return 0;
}

static int expect_error_containing(const char *name, const char *src, const char *needle) {
    MeloValue result;
    char err[512] = {0};
    int rc = compile_and_run(src, &result, err, sizeof(err));
    if (!rc) {
        printf("[%s] FAIL (expected an error, but compilation/execution succeeded)\n", name);
        return 1;
    }
    if (!strstr(err, needle)) {
        printf("[%s] FAIL (expected error containing '%s', got: %s)\n", name, needle, err);
        return 1;
    }
    printf("[%s] OK (%s)\n", name, err);
    return 0;
}

int main(void) {
    int failures = 0;

    failures += expect_float("struct_field_read",
        "struct Particle {\n"
        "    x: float,\n"
        "    y: float,\n"
        "}\n"
        "func main() -> float {\n"
        "    var p = Particle(1.0, 2.0);\n"
        "    return p.x + p.y;\n"
        "}\n", 3.0f);

    failures += expect_float("struct_field_write",
        "struct Particle {\n"
        "    x: float,\n"
        "    y: float,\n"
        "}\n"
        "func main() -> float {\n"
        "    var p = Particle(1.0, 2.0);\n"
        "    p.x = 10.0;\n"
        "    return p.x + p.y;\n"
        "}\n", 12.0f);

    failures += expect_float("struct_as_param",
        "struct Particle {\n"
        "    x: float,\n"
        "    y: float,\n"
        "}\n"
        "func sum(p: Particle) -> float {\n"
        "    return p.x + p.y;\n"
        "}\n"
        "func main() -> float {\n"
        "    var p = Particle(4.0, 5.0);\n"
        "    return sum(p);\n"
        "}\n", 9.0f);

    failures += expect_float("struct_vec_fields",
        "struct Body {\n"
        "    position: vec3,\n"
        "    velocity: vec3,\n"
        "}\n"
        "func main() -> float {\n"
        "    var b = Body(vec3(1.0, 1.0, 1.0), vec3(2.0, 2.0, 2.0));\n"
        "    var moved = b.position + b.velocity;\n"
        "    return vdot(moved, moved);\n"
        "}\n", 27.0f);

    failures += expect_float("multiple_independent_instances",
        "struct Particle {\n"
        "    x: float,\n"
        "}\n"
        "func main() -> float {\n"
        "    var a = Particle(1.0);\n"
        "    var b = Particle(2.0);\n"
        "    a.x = 100.0;\n"
        "    return a.x + b.x;\n"
        "}\n", 102.0f);

    failures += expect_error_containing("wrong_arg_count",
        "struct Particle {\n"
        "    x: float,\n"
        "    y: float,\n"
        "}\n"
        "func main() -> float {\n"
        "    var p = Particle(1.0);\n"
        "    return p.x;\n"
        "}\n", "argument count");

    failures += expect_error_containing("unknown_field",
        "struct Particle {\n"
        "    x: float,\n"
        "}\n"
        "func main() -> float {\n"
        "    var p = Particle(1.0);\n"
        "    return p.z;\n"
        "}\n", "unknown struct field");

    failures += expect_error_containing("field_access_on_non_struct",
        "func main() -> float {\n"
        "    var x = 5.0;\n"
        "    return x.field;\n"
        "}\n", "non-struct");

    if (failures) {
        printf("\n%d TEST(S) FAILED\n", failures);
        return 1;
    }
    printf("\nALL STRUCT TESTS PASSED\n");
    return 0;
}
