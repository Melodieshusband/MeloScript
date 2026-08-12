#include "../melo_parser.h"
#include "../melo_codegen.h"
#include "../melo_vm.h"
#include "melo_stdlib_pad.h"
#include "melo_stdlib_time.h"
#include "melo_stdlib_audio.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

typedef int (*RegisterFn)(MeloVM *vm);
typedef const char **(*NamesFn)(int *out_count);

static int compile_and_run(const char *src, MeloVM *vm, MeloCompiledModule *mod_out,
                            RegisterFn *regfns, NamesFn *namefns, int lib_count) {
    MeloParser parser;
    melo_parser_init(&parser, src);
    MeloProgram *prog = melo_parse_program(&parser);

    if (parser.had_error) {
        printf("PARSE ERROR at line %d: %s\n", parser.error_line, parser.error_msg);
        return 1;
    }

    const char *all_names[64];
    int total_names = 0;
    for (int i = 0; i < lib_count; i++) {
        int count = 0;
        const char **names = namefns[i](&count);
        for (int j = 0; j < count; j++) {
            all_names[total_names++] = names[j];
        }
    }

    MeloCompiledModule mod;
    if (melo_codegen_compile_with_natives(prog, &mod, all_names, total_names)) {
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

    for (int i = 0; i < lib_count; i++) {
        assert(regfns[i](vm) == 0);
    }

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

static void test_pad_init_and_poll(void) {
    const char *src =
        "func main() -> int32 {\n"
        "    var ok = pad_init(0, 0);\n"
        "    pad_poll(0, 0);\n"
        "    var btns = pad_buttons(0, 0);\n"
        "    return ok;\n"
        "}\n";
    RegisterFn regs[] = { melo_stdlib_register_pad };
    NamesFn names[] = { melo_stdlib_pad_native_names };

    MeloVM vm;
    MeloCompiledModule mod;
    int rc = compile_and_run(src, &vm, &mod, regs, names, 1);
    assert(rc == 0);
    assert(vm.error == 0);
    assert(vm.reg_stack[0].as.i32 == 1);
    printf("[pad_init_and_poll] OK\n");
    melo_codegen_free(&mod);
}

static void test_pad_invalid_port_is_runtime_error(void) {
    const char *src =
        "func main() -> int32 {\n"
        "    return pad_buttons(9, 0);\n"
        "}\n";
    RegisterFn regs[] = { melo_stdlib_register_pad };
    NamesFn names[] = { melo_stdlib_pad_native_names };

    MeloVM vm;
    MeloCompiledModule mod;
    int rc = compile_and_run(src, &vm, &mod, regs, names, 1);
    assert(rc == 1);
    assert(vm.error == 1);
    printf("[pad_invalid_port_is_runtime_error] OK (%s)\n", vm.error_msg);
    melo_codegen_free(&mod);
}

static void test_pad_stick_range(void) {
    const char *src =
        "func main() -> float {\n"
        "    pad_init(0, 0);\n"
        "    pad_poll(0, 0);\n"
        "    return pad_stick(0, 0, 0, 0);\n"
        "}\n";
    RegisterFn regs[] = { melo_stdlib_register_pad };
    NamesFn names[] = { melo_stdlib_pad_native_names };

    MeloVM vm;
    MeloCompiledModule mod;
    int rc = compile_and_run(src, &vm, &mod, regs, names, 1);
    assert(rc == 0);
    assert(vm.error == 0);
    float v = vm.reg_stack[0].as.f32;
    assert(v >= -1.0f && v <= 1.0f);
    printf("[pad_stick_range] OK (stick=%f)\n", v);
    melo_codegen_free(&mod);
}

static void test_time_vsync_and_delta(void) {
    const char *src =
        "func main() -> float {\n"
        "    wait_vsync();\n"
        "    wait_vsync();\n"
        "    return frame_delta();\n"
        "}\n";
    RegisterFn regs[] = { melo_stdlib_register_time };
    NamesFn names[] = { melo_stdlib_time_native_names };

    MeloVM vm;
    MeloCompiledModule mod;
    int rc = compile_and_run(src, &vm, &mod, regs, names, 1);
    assert(rc == 0);
    assert(vm.error == 0);
    assert(vm.reg_stack[0].as.f32 >= 0.0f);
    printf("[time_vsync_and_delta] OK (delta=%f)\n", vm.reg_stack[0].as.f32);
    melo_codegen_free(&mod);
}

static void test_time_ticks_monotonic(void) {
    const char *src =
        "func main() -> int32 {\n"
        "    var a = ticks_ms();\n"
        "    var b = ticks_ms();\n"
        "    if (b < a) {\n"
        "        return -1;\n"
        "    }\n"
        "    return 1;\n"
        "}\n";
    RegisterFn regs[] = { melo_stdlib_register_time };
    NamesFn names[] = { melo_stdlib_time_native_names };

    MeloVM vm;
    MeloCompiledModule mod;
    int rc = compile_and_run(src, &vm, &mod, regs, names, 1);
    assert(rc == 0);
    assert(vm.error == 0);
    assert(vm.reg_stack[0].as.i32 == 1);
    printf("[time_ticks_monotonic] OK\n");
    melo_codegen_free(&mod);
}

static void test_audio_play_and_stop(void) {
    const char *src =
        "func main() -> int32 {\n"
        "    audio_init();\n"
        "    audio_play(0, 0, 1024, 100, 0);\n"
        "    var was_playing = audio_playing(0);\n"
        "    audio_stop(0);\n"
        "    var stopped = audio_playing(0);\n"
        "    return was_playing - stopped;\n"
        "}\n";
    RegisterFn regs[] = { melo_stdlib_register_audio };
    NamesFn names[] = { melo_stdlib_audio_native_names };

    MeloVM vm;
    MeloCompiledModule mod;
    int rc = compile_and_run(src, &vm, &mod, regs, names, 1);
    assert(rc == 0);
    assert(vm.error == 0);
    assert(vm.reg_stack[0].as.i32 == 1);
    printf("[audio_play_and_stop] OK\n");
    melo_codegen_free(&mod);
}

static void test_audio_invalid_channel_is_runtime_error(void) {
    const char *src =
        "func main() -> int32 {\n"
        "    audio_init();\n"
        "    return audio_play(999, 0, 1024, 100, 0);\n"
        "}\n";
    RegisterFn regs[] = { melo_stdlib_register_audio };
    NamesFn names[] = { melo_stdlib_audio_native_names };

    MeloVM vm;
    MeloCompiledModule mod;
    int rc = compile_and_run(src, &vm, &mod, regs, names, 1);
    assert(rc == 1);
    assert(vm.error == 1);
    printf("[audio_invalid_channel_is_runtime_error] OK (%s)\n", vm.error_msg);
    melo_codegen_free(&mod);
}

static void test_audio_loop_and_pause(void) {
    const char *src =
        "func main() -> int32 {\n"
        "    audio_init();\n"
        "    audio_play_loop(1, 0, 2048, 100, 0);\n"
        "    var playing = audio_playing(1);\n"
        "    audio_pause(1);\n"
        "    var paused = audio_paused(1);\n"
        "    var still_playing = audio_playing(1);\n"
        "    audio_resume(1);\n"
        "    var resumed = audio_playing(1);\n"
        "    audio_stop(1);\n"
        "    return playing - still_playing + paused + resumed;\n"
        "}\n";
    RegisterFn regs[] = { melo_stdlib_register_audio };
    NamesFn names[] = { melo_stdlib_audio_native_names };

    MeloVM vm;
    MeloCompiledModule mod;
    int rc = compile_and_run(src, &vm, &mod, regs, names, 1);
    assert(rc == 0);
    assert(vm.error == 0);
    assert(vm.reg_stack[0].as.i32 == 3);
    printf("[audio_loop_and_pause] OK\n");
    melo_codegen_free(&mod);
}

static void test_audio_volume_pan_master(void) {
    const char *src =
        "func main() -> int32 {\n"
        "    audio_init();\n"
        "    audio_play(2, 0, 512, 100, 0);\n"
        "    audio_set_volume(2, 8000);\n"
        "    audio_set_pan(2, -50);\n"
        "    audio_master_volume(16383);\n"
        "    var ok = audio_playing(2);\n"
        "    audio_stop(2);\n"
        "    return ok;\n"
        "}\n";
    RegisterFn regs[] = { melo_stdlib_register_audio };
    NamesFn names[] = { melo_stdlib_audio_native_names };

    MeloVM vm;
    MeloCompiledModule mod;
    int rc = compile_and_run(src, &vm, &mod, regs, names, 1);
    assert(rc == 0);
    assert(vm.error == 0);
    assert(vm.reg_stack[0].as.i32 == 1);
    printf("[audio_volume_pan_master] OK\n");
    melo_codegen_free(&mod);
}

static void test_all_three_libs_together(void) {
    const char *src =
        "func main() -> int32 {\n"
        "    pad_init(0, 0);\n"
        "    audio_init();\n"
        "    pad_poll(0, 0);\n"
        "    wait_vsync();\n"
        "    if (pad_pressed(0, 0, 4096)) {\n"
        "        audio_play(0, 0, 512, 100, 0);\n"
        "    }\n"
        "    return 1;\n"
        "}\n";
    RegisterFn regs[] = { melo_stdlib_register_pad, melo_stdlib_register_time, melo_stdlib_register_audio };
    NamesFn names[] = { melo_stdlib_pad_native_names, melo_stdlib_time_native_names, melo_stdlib_audio_native_names };

    MeloVM vm;
    MeloCompiledModule mod;
    int rc = compile_and_run(src, &vm, &mod, regs, names, 3);
    assert(rc == 0);
    assert(vm.error == 0);
    assert(vm.reg_stack[0].as.i32 == 1);
    printf("[all_three_libs_together] OK\n");
    melo_codegen_free(&mod);
}

int main(void) {
    test_pad_init_and_poll();
    test_pad_invalid_port_is_runtime_error();
    test_pad_stick_range();
    test_time_vsync_and_delta();
    test_time_ticks_monotonic();
    test_audio_play_and_stop();
    test_audio_invalid_channel_is_runtime_error();
    test_audio_loop_and_pause();
    test_audio_volume_pan_master();
    test_all_three_libs_together();
    printf("ALL LIBS TESTS PASSED\n");
    return 0;
}
