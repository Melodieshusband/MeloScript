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

static void test_scratch_array_basic(void) {
    const char *src =
        "func main() -> int32 {\n"
        "    @scratch var buf = array<int32, 4>(10, 20, 30, 40);\n"
        "    return buf[0] + buf[3];\n"
        "}\n";

    MeloVM vm;
    MeloCompiledModule mod;
    int rc = compile_and_run(src, &vm, &mod);
    assert(rc == 0);
    assert(vm.error == 0);
    assert(vm.reg_stack[0].as.i32 == 50);
    assert(vm.scratch_offset > 0);
    assert(vm.scratch_offset <= MELO_SCRATCH_SIZE);
    printf("[scratch_array_basic] OK (sum=%d, scratch_used=%zu)\n", vm.reg_stack[0].as.i32, vm.scratch_offset);
    melo_codegen_free(&mod);
}

static void test_scratch_alignment(void) {
    MeloVM vm;
    melo_vm_init(&vm, NULL, 0);

    MeloArrayObj *a = melo_vm_alloc_array_scratch(&vm, 3);
    assert(a != NULL);
    assert(((size_t)a & 15) == 0);

    MeloArrayObj *b = melo_vm_alloc_array_scratch(&vm, 1);
    assert(b != NULL);
    assert(((size_t)b & 15) == 0);

    printf("[scratch_alignment] OK\n");
}

static void test_scratch_struct(void) {
    const char *src =
        "struct Vec3Pack {\n"
        "    x: float,\n"
        "    y: float,\n"
        "    z: float,\n"
        "}\n"
        "func main() -> float {\n"
        "    @scratch var p = Vec3Pack(1.0, 2.0, 3.0);\n"
        "    return p.x + p.y + p.z;\n"
        "}\n";

    MeloVM vm;
    MeloCompiledModule mod;
    int rc = compile_and_run(src, &vm, &mod);
    assert(rc == 0);
    assert(vm.error == 0);
    assert(vm.reg_stack[0].as.f32 == 6.0f);
    assert(vm.scratch_offset > 0);
    printf("[scratch_struct] OK (sum=%g)\n", vm.reg_stack[0].as.f32);
    melo_codegen_free(&mod);
}

static void test_scratch_isolated_from_object_heap(void) {
    const char *src =
        "func main() -> int32 {\n"
        "    @scratch var a = array<int32, 2>(1, 2);\n"
        "    var b = array<int32, 2>(3, 4);\n"
        "    return a[0] + b[0];\n"
        "}\n";

    MeloVM vm;
    MeloCompiledModule mod;
    int rc = compile_and_run(src, &vm, &mod);
    assert(rc == 0);
    assert(vm.error == 0);
    assert(vm.scratch_offset > 0);
    assert(vm.object_heap_offset > 0);
    printf("[scratch_isolated_from_object_heap] OK (scratch=%zu, heap=%zu)\n",
           vm.scratch_offset, vm.object_heap_offset);
    melo_codegen_free(&mod);
}

static void test_scratch_overflow(void) {
    MeloVM vm;
    melo_vm_init(&vm, NULL, 0);

    int total = 0;
    MeloArrayObj *last = NULL;
    for (;;) {
        MeloArrayObj *arr = melo_vm_alloc_array_scratch(&vm, 64);
        if (!arr) break;
        last = arr;
        total++;
    }
    assert(last != NULL);
    assert(total > 1);
    assert(vm.scratch_offset <= MELO_SCRATCH_SIZE);
    MeloArrayObj *overflow = melo_vm_alloc_array_scratch(&vm, 64);
    assert(overflow == NULL);
    printf("[scratch_overflow] OK (allocated %d chunks before exhaustion, used=%zu/%d)\n",
           total, vm.scratch_offset, MELO_SCRATCH_SIZE);
}

static void test_scratch_overflow_at_source_level(void) {
    const char *src =
        "func main() -> int32 {\n"
        "    @scratch var huge = array<int32, 8192>();\n"
        "    return len(huge);\n"
        "}\n";

    MeloVM vm;
    MeloCompiledModule mod;
    int rc = compile_and_run(src, &vm, &mod);
    assert(rc == 1);
    assert(vm.error == 1);
    printf("[scratch_overflow_at_source_level] OK (VM ERROR: %s)\n", vm.error_msg);
    melo_codegen_free(&mod);
}

static void test_scratch_reset(void) {
    MeloVM vm;
    melo_vm_init(&vm, NULL, 0);

    MeloArrayObj *a = melo_vm_alloc_array_scratch(&vm, 4);
    assert(a != NULL);
    size_t used_before = vm.scratch_offset;
    assert(used_before > 0);

    melo_vm_scratch_reset(&vm);
    assert(vm.scratch_offset == 0);

    MeloArrayObj *b = melo_vm_alloc_array_scratch(&vm, 4);
    assert(b == a);

    printf("[scratch_reset] OK (reused same address after reset)\n");
}

static void test_scratch_rejects_scalar(void) {
    const char *src =
        "func main() -> int32 {\n"
        "    @scratch var x = 5;\n"
        "    return x;\n"
        "}\n";

    MeloVM vm;
    MeloCompiledModule mod;
    int rc = compile_and_run(src, &vm, &mod);
    assert(rc == 1);
    printf("[scratch_rejects_scalar] OK (%s)\n", mod.error_msg);
    melo_codegen_free(&mod);
}

static void test_scratch_rejects_vec4(void) {
    const char *src =
        "func main() -> int32 {\n"
        "    @scratch var v = vec4(1.0, 2.0, 3.0, 4.0);\n"
        "    return 0;\n"
        "}\n";

    MeloVM vm;
    MeloCompiledModule mod;
    int rc = compile_and_run(src, &vm, &mod);
    assert(rc == 1);
    printf("[scratch_rejects_vec4] OK (%s)\n", mod.error_msg);
    melo_codegen_free(&mod);
}

static void test_scratch_opcode_used(void) {
    const char *src =
        "func main() -> int32 {\n"
        "    @scratch var buf = array<int32, 2>(1, 2);\n"
        "    return buf[0];\n"
        "}\n";

    MeloParser parser;
    melo_parser_init(&parser, src);
    MeloProgram *prog = melo_parse_program(&parser);
    assert(!parser.had_error);

    MeloCompiledModule mod;
    int err = melo_codegen_compile(prog, &mod);
    assert(!err);

    int found = 0;
    for (int i = 0; i < mod.code_count; i++) {
        if (INSTR_OP(mod.code[i]) == OP_NEWARRAY_SCRATCH) found = 1;
    }
    assert(found);
    printf("[scratch_opcode_used] OK (OP_NEWARRAY_SCRATCH emitted)\n");
    melo_codegen_free(&mod);
}

static void test_scratch_base_mapping(void) {
    MeloVM vm;
    melo_vm_init(&vm, NULL, 0);
    uint8_t *base = melo_vm_scratch_base(&vm);
#if defined(MELO_PS2_TARGET) || defined(__mips__)
    assert((uintptr_t)base == 0x70000000);
#else
    assert(base == vm.scratch_mem);
#endif
    printf("[scratch_base_mapping] OK\n");
}

static void test_scratch_dma_in_and_out(void) {
    const char *src =
        "func main() -> int32 {\n"
        "    var src = array<int32, 4>(10, 20, 30, 40);\n"
        "    @scratch var spr = array<int32, 4>();\n"
        "    scratch_dma_in(spr, src);\n"
        "    spr[2] = 999;\n"
        "    var dst = array<int32, 4>();\n"
        "    scratch_dma_out(dst, spr);\n"
        "    return dst[0] + dst[2];\n"
        "}\n";

    MeloVM vm;
    MeloCompiledModule mod;
    int rc = compile_and_run(src, &vm, &mod);
    assert(rc == 0);
    assert(vm.error == 0);
    assert(vm.reg_stack[0].as.i32 == 1009);
    printf("[scratch_dma_in_and_out] OK (result=%d)\n", vm.reg_stack[0].as.i32);
    melo_codegen_free(&mod);
}

int main(void) {
    test_scratch_array_basic();
    test_scratch_alignment();
    test_scratch_struct();
    test_scratch_isolated_from_object_heap();
    test_scratch_overflow();
    test_scratch_overflow_at_source_level();
    test_scratch_reset();
    test_scratch_rejects_scalar();
    test_scratch_rejects_vec4();
    test_scratch_opcode_used();
    test_scratch_base_mapping();
    test_scratch_dma_in_and_out();
    printf("ALL SCRATCH TESTS PASSED\n");
    return 0;
}
