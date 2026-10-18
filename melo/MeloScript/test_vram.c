#include "melo_parser.h"
#include "melo_codegen.h"
#include "melo_vm.h"
#include "melo_stdlib_vram.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static int compile_and_run_vram(const char *src, MeloVM *vm, MeloCompiledModule *mod_out) {
    MeloParser parser;
    melo_parser_init(&parser, src);
    MeloProgram *prog = melo_parse_program(&parser);

    if (parser.had_error) {
        printf("PARSE ERROR at line %d: %s\n", parser.error_line, parser.error_msg);
        return 1;
    }

    int name_count = 0;
    const char **names = melo_stdlib_vram_native_names(&name_count);

    MeloCompiledModule mod;
    if (melo_codegen_compile_with_natives(prog, &mod, names, name_count)) {
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
    vm->code = mod.funcs[mod.main_func_id].entry;

    int rc = melo_stdlib_register_vram(vm);
    assert(rc == 0);

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

static void test_vram_alloc_starts_evicted(void) {
    const char *src =
        "func main() -> int32 {\n"
        "    var tex = vram_alloc(256, 256);\n"
        "    return resident(tex);\n"
        "}\n";
    MeloVM vm;
    MeloCompiledModule mod;
    int rc = compile_and_run_vram(src, &vm, &mod);
    assert(rc == 0);
    assert(vm.reg_stack[0].as.i32 == 0);
    printf("[vram_alloc_starts_evicted] OK\n");
    melo_codegen_free(&mod);
}

static void test_vram_upload_makes_resident(void) {
    const char *src =
        "func main() -> int32 {\n"
        "    var tex = vram_alloc(128, 128);\n"
        "    upload(tex);\n"
        "    return resident(tex);\n"
        "}\n";
    MeloVM vm;
    MeloCompiledModule mod;
    int rc = compile_and_run_vram(src, &vm, &mod);
    assert(rc == 0);
    assert(vm.reg_stack[0].as.i32 == 1);
    printf("[vram_upload_makes_resident] OK\n");
    melo_codegen_free(&mod);
}

static void test_vram_evict_after_upload(void) {
    const char *src =
        "func main() -> int32 {\n"
        "    var tex = vram_alloc(64, 64);\n"
        "    upload(tex);\n"
        "    evict(tex);\n"
        "    return resident(tex);\n"
        "}\n";
    MeloVM vm;
    MeloCompiledModule mod;
    int rc = compile_and_run_vram(src, &vm, &mod);
    assert(rc == 0);
    assert(vm.reg_stack[0].as.i32 == 0);
    printf("[vram_evict_after_upload] OK\n");
    melo_codegen_free(&mod);
}

static void test_vram_tracks_resident_bytes(void) {
    const char *src =
        "func main() -> int32 {\n"
        "    var a = vram_alloc(256, 256);\n"
        "    var b = vram_alloc(256, 256);\n"
        "    upload(a);\n"
        "    upload(b);\n"
        "    evict(a);\n"
        "    return 1;\n"
        "}\n";
    MeloVM vm;
    MeloCompiledModule mod;
    int rc = compile_and_run_vram(src, &vm, &mod);
    assert(rc == 0);
    uint32_t expected = 256u * 256u * 4u;
    assert(vm.vram_bytes_resident == expected);
    printf("[vram_tracks_resident_bytes] OK (resident=%u bytes)\n", vm.vram_bytes_resident);
    melo_codegen_free(&mod);
}

static void test_vram_exhaustion(void) {
    const char *src =
        "func main() -> int32 {\n"
        "    var i = 0;\n"
        "    while (i < 8) {\n"
        "        var t = vram_alloc(1024, 1024);\n"
        "        upload(t);\n"
        "        i = i + 1;\n"
        "    }\n"
        "    return 0;\n"
        "}\n";
    MeloVM vm;
    MeloCompiledModule mod;
    int rc = compile_and_run_vram(src, &vm, &mod);
    assert(rc == 1);
    assert(strstr(vm.error_msg, "VRAM exhausted") != NULL);
    printf("[vram_exhaustion] OK (%s)\n", vm.error_msg);
    melo_codegen_free(&mod);
}

static void test_vram_free_reclaims_space(void) {
    const char *src =
        "func main() -> int32 {\n"
        "    var a = vram_alloc(512, 512);\n"
        "    vram_free(a);\n"
        "    var b = vram_alloc(512, 512);\n"
        "    upload(b);\n"
        "    return resident(b);\n"
        "}\n";
    MeloVM vm;
    MeloCompiledModule mod;
    int rc = compile_and_run_vram(src, &vm, &mod);
    assert(rc == 0);
    assert(vm.reg_stack[0].as.i32 == 1);
    printf("[vram_free_reclaims_space] OK\n");
    melo_codegen_free(&mod);
}

static void test_vram_reject_non_texture_upload(void) {
    const char *src =
        "func main() -> int32 {\n"
        "    return upload(5);\n"
        "}\n";
    MeloVM vm;
    MeloCompiledModule mod;
    int rc = compile_and_run_vram(src, &vm, &mod);
    assert(rc == 1);
    assert(strstr(vm.error_msg, "@vram texture") != NULL);
    printf("[vram_reject_non_texture_upload] OK (%s)\n", vm.error_msg);
    melo_codegen_free(&mod);
}

static void test_vram_annotation_auto_uploads(void) {
    const char *src =
        "func main() -> int32 {\n"
        "    @vram var tex = vram_alloc(256, 256);\n"
        "    return resident(tex);\n"
        "}\n";
    MeloVM vm;
    MeloCompiledModule mod;
    int rc = compile_and_run_vram(src, &vm, &mod);
    assert(rc == 0);
    assert(vm.reg_stack[0].as.i32 == 1);
    printf("[vram_annotation_auto_uploads] OK\n");
    melo_codegen_free(&mod);
}

static void test_vram_annotation_preserves_texture_value(void) {
    const char *src =
        "func main() -> int32 {\n"
        "    @vram var tex = vram_alloc(64, 64);\n"
        "    var resident_after = resident(tex);\n"
        "    evict(tex);\n"
        "    return resident_after + resident(tex);\n"
        "}\n";
    MeloVM vm;
    MeloCompiledModule mod;
    int rc = compile_and_run_vram(src, &vm, &mod);
    assert(rc == 0);
    assert(vm.reg_stack[0].as.i32 == 1);
    printf("[vram_annotation_preserves_texture_value] OK\n");
    melo_codegen_free(&mod);
}

static void test_vram_annotation_rejects_non_vram_alloc_init(void) {
    const char *src =
        "func main() -> int32 {\n"
        "    @vram var x = 5;\n"
        "    return x;\n"
        "}\n";
    MeloVM vm;
    MeloCompiledModule mod;
    int rc = compile_and_run_vram(src, &vm, &mod);
    assert(rc == 1);
    assert(strstr(mod.error_msg, "@vram may only be used") != NULL);
    printf("[vram_annotation_rejects_non_vram_alloc_init] OK (%s)\n", mod.error_msg);
    melo_codegen_free(&mod);
}

static void test_vram_annotation_rejects_other_call_init(void) {
    const char *src =
        "func main() -> int32 {\n"
        "    @vram var tex = resident(5);\n"
        "    return 0;\n"
        "}\n";
    MeloVM vm;
    MeloCompiledModule mod;
    int rc = compile_and_run_vram(src, &vm, &mod);
    assert(rc == 1);
    assert(strstr(mod.error_msg, "@vram may only be used") != NULL);
    printf("[vram_annotation_rejects_other_call_init] OK (%s)\n", mod.error_msg);
    melo_codegen_free(&mod);
}

static void test_vram_paletted_4bit(void) {
    const char *src =
        "func main() -> int32 {\n"
        "    var tex = vram_alloc_paletted(256, 256, 4);\n"
        "    upload(tex);\n"
        "    var b = vram_bpp(tex);\n"
        "    var s = vram_size(tex);\n"
        "    evict(tex);\n"
        "    vram_free(tex);\n"
        "    return b;\n"
        "}\n";
    MeloVM vm;
    MeloCompiledModule mod;
    int rc = compile_and_run_vram(src, &vm, &mod);
    assert(rc == 0);
    assert(vm.reg_stack[0].as.i32 == 4);
    printf("[vram_paletted_4bit] OK\n");
    melo_codegen_free(&mod);
}

static void test_vram_paletted_8bit(void) {
    const char *src =
        "func main() -> int32 {\n"
        "    @vram var tex = vram_alloc_paletted(128, 128, 8);\n"
        "    var r = resident(tex);\n"
        "    var b = vram_bpp(tex);\n"
        "    return r + b;\n"
        "}\n";
    MeloVM vm;
    MeloCompiledModule mod;
    int rc = compile_and_run_vram(src, &vm, &mod);
    assert(rc == 0);
    assert(vm.reg_stack[0].as.i32 == 9);
    printf("[vram_paletted_8bit] OK\n");
    melo_codegen_free(&mod);
}

int main(void) {
    test_vram_alloc_starts_evicted();
    test_vram_upload_makes_resident();
    test_vram_evict_after_upload();
    test_vram_tracks_resident_bytes();
    test_vram_exhaustion();
    test_vram_free_reclaims_space();
    test_vram_reject_non_texture_upload();
    test_vram_annotation_auto_uploads();
    test_vram_annotation_preserves_texture_value();
    test_vram_annotation_rejects_non_vram_alloc_init();
    test_vram_annotation_rejects_other_call_init();
    test_vram_paletted_4bit();
    test_vram_paletted_8bit();
    printf("\nALL VRAM TESTS PASSED\n");
    return 0;
}
