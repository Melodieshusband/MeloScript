#include "melo_parser.h"
#include "melo_codegen.h"
#include "melo_vm.h"
#include "melo_image.h"
#include "melo_stdlib_vram.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static MeloVM g_vm_a;
static MeloVM g_vm_b;

static int compile_source(const char *src, MeloCompiledModule *mod,
                          const char **natives, int native_count) {
    MeloParser parser;
    melo_parser_init(&parser, src);
    MeloProgram *prog = melo_parse_program(&parser);
    if (parser.had_error) {
        printf("PARSE ERROR at line %d: %s\n", parser.error_line, parser.error_msg);
        return 1;
    }
    if (melo_codegen_compile_with_natives(prog, mod, natives, native_count)) {
        printf("CODEGEN ERROR: %s\n", mod->error_msg);
        return 1;
    }
    return 0;
}

static void load_module_into_vm(MeloVM *vm, MeloCompiledModule *mod) {
    melo_vm_init(vm, mod->code, mod->code_count);
    memcpy(vm->consts, mod->consts, sizeof(MeloValue) * mod->const_count);
    vm->const_count = mod->const_count;
    memcpy(vm->funcs, mod->funcs, sizeof(MeloFuncInfo) * mod->func_count);
    vm->func_count = mod->func_count;
    vm->code = mod->funcs[mod->main_func_id].entry;
}

static void test_roundtrip_arithmetic(void) {
    const char *src =
        "func square(x: float) -> float {\n"
        "    return x * x;\n"
        "}\n"
        "func main() -> float {\n"
        "    var a = vec4(1.0, 2.0, 3.0, 0.0);\n"
        "    var b = vec4(4.0, 5.0, 6.0, 0.0);\n"
        "    var d = vdot(a, b);\n"
        "    return square(d);\n"
        "}\n";

    MeloCompiledModule mod;
    assert(compile_source(src, &mod, NULL, 0) == 0);

    load_module_into_vm(&g_vm_a, &mod);
    assert(melo_vm_run(&g_vm_a) == 0);
    float expected = g_vm_a.reg_stack[0].as.f32;
    assert(expected == 1024.0f);

    uint8_t *data = NULL;
    size_t size = 0;
    char err[128];
    assert(melo_image_serialize(&mod, NULL, 0, &data, &size, err, sizeof(err)) == 0);
    assert(size > 0);

    MeloImageInfo info;
    assert(melo_image_load(&g_vm_b, data, size, &info, err, sizeof(err)) == 0);
    assert(info.native_count == 0);
    assert(info.main_func_id == mod.main_func_id);
    assert(info.code_len == (size_t)mod.code_count);
    assert(memcmp(info.code, mod.code, sizeof(MeloInstr) * mod.code_count) == 0);
    assert(g_vm_b.func_count == mod.func_count);
    assert(g_vm_b.const_count == (size_t)mod.const_count);

    assert(melo_vm_run(&g_vm_b) == 0);
    assert(g_vm_b.reg_stack[0].type == MELO_FLOAT);
    assert(g_vm_b.reg_stack[0].as.f32 == expected);

    melo_image_unload(&info);
    free(data);
    melo_codegen_free(&mod);
    printf("[roundtrip_arithmetic] OK\n");
}

static void test_roundtrip_string_constant(void) {
    const char *src =
        "func main() -> string {\n"
        "    var s = \"hello image\";\n"
        "    return s;\n"
        "}\n";

    MeloCompiledModule mod;
    assert(compile_source(src, &mod, NULL, 0) == 0);

    uint8_t *data = NULL;
    size_t size = 0;
    char err[128];
    assert(melo_image_serialize(&mod, NULL, 0, &data, &size, err, sizeof(err)) == 0);

    MeloImageInfo info;
    assert(melo_image_load(&g_vm_b, data, size, &info, err, sizeof(err)) == 0);
    assert(melo_vm_run(&g_vm_b) == 0);
    assert(g_vm_b.reg_stack[0].type == MELO_STRING);
    assert(strcmp(g_vm_b.reg_stack[0].as.str->data, "hello image") == 0);

    melo_image_unload(&info);
    free(data);
    melo_codegen_free(&mod);
    printf("[roundtrip_string_constant] OK\n");
}

static void test_hot_flags_preserved(void) {
    const char *src =
        "@hot\n"
        "func add_ints(a: int32, b: int32) -> int32 {\n"
        "    return a + b;\n"
        "}\n"
        "func main() -> int32 {\n"
        "    return add_ints(19, 23);\n"
        "}\n";

    MeloCompiledModule mod;
    assert(compile_source(src, &mod, NULL, 0) == 0);

    uint8_t *data = NULL;
    size_t size = 0;
    char err[128];
    assert(melo_image_serialize(&mod, NULL, 0, &data, &size, err, sizeof(err)) == 0);

    MeloImageInfo info;
    assert(melo_image_load(&g_vm_b, data, size, &info, err, sizeof(err)) == 0);

    int found = 0;
    for (int i = 0; i < g_vm_b.func_count; i++) {
        if (strcmp(g_vm_b.funcs[i].name, "add_ints") == 0) {
            assert(g_vm_b.funcs[i].is_hot == 1);
            assert(g_vm_b.funcs[i].aot_fn == NULL);
            found = 1;
        }
    }
    assert(found);

    assert(melo_vm_run(&g_vm_b) == 0);
    assert(g_vm_b.reg_stack[0].as.i32 == 42);

    melo_image_unload(&info);
    free(data);
    melo_codegen_free(&mod);
    printf("[hot_flags_preserved] OK\n");
}

static void test_natives_verified(void) {
    const char *src =
        "func main() -> int32 {\n"
        "    var tex = vram_alloc(64, 64);\n"
        "    upload(tex);\n"
        "    return resident(tex);\n"
        "}\n";

    int name_count = 0;
    const char **names = melo_stdlib_vram_native_names(&name_count);

    MeloCompiledModule mod;
    assert(compile_source(src, &mod, names, name_count) == 0);

    uint8_t *data = NULL;
    size_t size = 0;
    char err[160];
    assert(melo_image_serialize(&mod, names, name_count, &data, &size, err, sizeof(err)) == 0);

    MeloImageInfo info;
    assert(melo_image_load(&g_vm_b, data, size, &info, err, sizeof(err)) == 0);
    assert(info.native_count == name_count);

    assert(melo_image_verify_natives(&g_vm_b, &info, err, sizeof(err)) != 0);
    assert(strstr(err, "natives") != NULL);

    assert(melo_stdlib_register_vram(&g_vm_b) == 0);
    assert(melo_image_verify_natives(&g_vm_b, &info, err, sizeof(err)) == 0);

    assert(melo_vm_run(&g_vm_b) == 0);
    assert(g_vm_b.reg_stack[0].as.i32 == 1);

    melo_image_unload(&info);
    free(data);
    melo_codegen_free(&mod);
    printf("[natives_verified] OK\n");
}

static void test_corruption_rejected(void) {
    const char *src =
        "func main() -> int32 {\n"
        "    return 5;\n"
        "}\n";

    MeloCompiledModule mod;
    assert(compile_source(src, &mod, NULL, 0) == 0);

    uint8_t *data = NULL;
    size_t size = 0;
    char err[128];
    assert(melo_image_serialize(&mod, NULL, 0, &data, &size, err, sizeof(err)) == 0);

    MeloImageInfo info;

    uint8_t *copy = (uint8_t *)malloc(size);
    memcpy(copy, data, size);
    copy[size / 2] ^= 0x5A;
    assert(melo_image_load(&g_vm_b, copy, size, &info, err, sizeof(err)) != 0);
    assert(strstr(err, "checksum") != NULL);
    free(copy);

    assert(melo_image_load(&g_vm_b, data, size - 1, &info, err, sizeof(err)) != 0);
    assert(melo_image_load(&g_vm_b, data, 8, &info, err, sizeof(err)) != 0);

    copy = (uint8_t *)malloc(size);
    memcpy(copy, data, size);
    copy[0] = 'X';
    assert(melo_image_load(&g_vm_b, copy, size, &info, err, sizeof(err)) != 0);
    assert(strstr(err, "magic") != NULL);
    free(copy);

    assert(melo_image_load(&g_vm_b, data, size, &info, err, sizeof(err)) == 0);
    assert(melo_vm_run(&g_vm_b) == 0);
    assert(g_vm_b.reg_stack[0].as.i32 == 5);

    melo_image_unload(&info);
    free(data);
    melo_codegen_free(&mod);
    printf("[corruption_rejected] OK\n");
}

static void test_file_and_c_array(void) {
    const char *src =
        "func main() -> int32 {\n"
        "    return 77;\n"
        "}\n";

    MeloCompiledModule mod;
    assert(compile_source(src, &mod, NULL, 0) == 0);

    uint8_t *data = NULL;
    size_t size = 0;
    char err[128];
    assert(melo_image_serialize(&mod, NULL, 0, &data, &size, err, sizeof(err)) == 0);

    const char *bin_path = "test_image_tmp.meloc";
    const char *c_path = "test_image_tmp.c";
    assert(melo_image_write_file(data, size, bin_path) == 0);

    uint8_t *back = NULL;
    size_t back_size = 0;
    assert(melo_image_read_file(bin_path, &back, &back_size) == 0);
    assert(back_size == size);
    assert(memcmp(back, data, size) == 0);

    MeloImageInfo info;
    assert(melo_image_load(&g_vm_b, back, back_size, &info, err, sizeof(err)) == 0);
    assert(melo_vm_run(&g_vm_b) == 0);
    assert(g_vm_b.reg_stack[0].as.i32 == 77);
    melo_image_unload(&info);

    assert(melo_image_write_c_array(data, size, c_path, "melo_embedded_image") == 0);
    assert(melo_image_write_c_array(data, size, c_path, "9bad") != 0);
    assert(melo_image_write_c_array(data, size, c_path, "bad-name") != 0);
    assert(melo_image_write_c_array(data, size, c_path, "melo_embedded_image") == 0);

    uint8_t *text = NULL;
    size_t text_size = 0;
    assert(melo_image_read_file(c_path, &text, &text_size) == 0);
    text[text_size] = '\0';
    assert(strstr((char *)text, "melo_embedded_image[]") != NULL);
    char len_line[96];
    snprintf(len_line, sizeof(len_line), "melo_embedded_image_len = %zu;", size);
    assert(strstr((char *)text, len_line) != NULL);
    free(text);

    remove(bin_path);
    remove(c_path);
    free(back);
    free(data);
    melo_codegen_free(&mod);
    printf("[file_and_c_array] OK\n");
}

int main(void) {
    test_roundtrip_arithmetic();
    test_roundtrip_string_constant();
    test_hot_flags_preserved();
    test_natives_verified();
    test_corruption_rejected();
    test_file_and_c_array();
    printf("ALL OK\n");
    return 0;
}
