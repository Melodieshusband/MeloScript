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

static int expect_int(const char *name, const char *src, int32_t expected) {
    MeloValue result;
    char err[512] = {0};
    if (compile_and_run(src, &result, err, sizeof(err))) {
        printf("[%s] FAIL (%s)\n", name, err);
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
    if (got != expected) {
        printf("[%s] FAIL (expected %g, got %g, type=%d)\n", name, (double)expected, (double)got, result.type);
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

    failures += expect_int("array_literal_index_read",
        "func main() -> int32 {\n"
        "    var xs = array<int32, 3>(10, 20, 30);\n"
        "    return xs[1];\n"
        "}\n", 20);

    failures += expect_int("array_literal_index_write",
        "func main() -> int32 {\n"
        "    var xs = array<int32, 3>(10, 20, 30);\n"
        "    xs[1] = 99;\n"
        "    return xs[0] + xs[1] + xs[2];\n"
        "}\n", 139);

    failures += expect_int("array_zero_init_no_args",
        "func main() -> int32 {\n"
        "    var xs = array<int32, 4>();\n"
        "    xs[2] = 7;\n"
        "    return xs[0] + xs[1] + xs[2] + xs[3];\n"
        "}\n", 7);

    failures += expect_int("array_size_inferred_from_args",
        "func main() -> int32 {\n"
        "    var xs = array<int32>(1, 2, 3, 4, 5);\n"
        "    return len(xs);\n"
        "}\n", 5);

    failures += expect_int("array_len_builtin",
        "func main() -> int32 {\n"
        "    var xs = array<int32, 10>();\n"
        "    return len(xs);\n"
        "}\n", 10);

    failures += expect_int("for_in_sum",
        "func main() -> int32 {\n"
        "    var xs = array<int32, 5>(1, 2, 3, 4, 5);\n"
        "    var total = 0;\n"
        "    for x in xs {\n"
        "        total = total + x;\n"
        "    }\n"
        "    return total;\n"
        "}\n", 15);

    failures += expect_int("for_in_empty_array",
        "func main() -> int32 {\n"
        "    var xs = array<int32, 0>();\n"
        "    var total = 0;\n"
        "    for x in xs {\n"
        "        total = total + 1;\n"
        "    }\n"
        "    return total;\n"
        "}\n", 0);

    failures += expect_float("array_of_vec4_dot_accumulate",
        "func main() -> float {\n"
        "    var vs = array<vec4, 2>(vec4(1.0, 0.0, 0.0, 0.0), vec4(0.0, 1.0, 0.0, 0.0));\n"
        "    var total = 0.0;\n"
        "    for v in vs {\n"
        "        total = total + vdot(v, vec4(1.0, 1.0, 0.0, 0.0));\n"
        "    }\n"
        "    return total;\n"
        "}\n", 2.0f);

    failures += expect_int("array_of_structs_field_sum",
        "struct Particle {\n"
        "    x: int32,\n"
        "}\n"
        "func main() -> int32 {\n"
        "    var ps = array<Particle, 3>(Particle(1), Particle(2), Particle(3));\n"
        "    var total = 0;\n"
        "    for p in ps {\n"
        "        total = total + p.x;\n"
        "    }\n"
        "    return total;\n"
        "}\n", 6);

    failures += expect_int("nested_for_loops",
        "func main() -> int32 {\n"
        "    var xs = array<int32, 3>(1, 2, 3);\n"
        "    var ys = array<int32, 2>(10, 20);\n"
        "    var total = 0;\n"
        "    for x in xs {\n"
        "        for y in ys {\n"
        "            total = total + x * y;\n"
        "        }\n"
        "    }\n"
        "    return total;\n"
        "}\n", 180);

    failures += expect_int("array_passed_to_function",
        "func sum3(xs: array<int32, 3>) -> int32 {\n"
        "    return xs[0] + xs[1] + xs[2];\n"
        "}\n"
        "func main() -> int32 {\n"
        "    var xs = array<int32, 3>(4, 5, 6);\n"
        "    return sum3(xs);\n"
        "}\n", 15);

    failures += expect_int("array_in_arena_block",
        "func main() -> int32 {\n"
        "    var total = 0;\n"
        "    arena {\n"
        "        var xs = array<int32, 3>(1, 2, 3);\n"
        "        for x in xs {\n"
        "            total = total + x;\n"
        "        }\n"
        "    }\n"
        "    return total;\n"
        "}\n", 6);

    failures += expect_error_containing("index_out_of_range",
        "func main() -> int32 {\n"
        "    var xs = array<int32, 2>(1, 2);\n"
        "    return xs[5];\n"
        "}\n", "out of range");

    failures += expect_error_containing("index_negative",
        "func main() -> int32 {\n"
        "    var xs = array<int32, 2>(1, 2);\n"
        "    var i = 0 - 1;\n"
        "    return xs[i];\n"
        "}\n", "out of range");

    failures += expect_error_containing("index_on_non_array",
        "func main() -> int32 {\n"
        "    var x = 5;\n"
        "    return x[0];\n"
        "}\n", "non-array");

    failures += expect_error_containing("mismatched_initializer_count",
        "func main() -> int32 {\n"
        "    var xs = array<int32, 3>(1, 2);\n"
        "    return xs[0];\n"
        "}\n", "initializer count");

    failures += expect_error_containing("for_in_non_array",
        "func main() -> int32 {\n"
        "    var total = 0;\n"
        "    for x in 5 {\n"
        "        total = total + x;\n"
        "    }\n"
        "    return total;\n"
        "}\n", "for..in");

    failures += expect_error_containing("len_on_non_array",
        "func main() -> int32 {\n"
        "    return len(5);\n"
        "}\n", "len()");

    if (failures) {
        printf("\n%d TEST(S) FAILED\n", failures);
        return 1;
    }
    printf("\nALL ARRAY TESTS PASSED\n");
    return 0;
}
