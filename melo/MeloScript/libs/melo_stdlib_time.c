#define _POSIX_C_SOURCE 199309L
#include "melo_stdlib_time.h"
#include <string.h>
#include <stdio.h>

#ifdef MELO_PS2_TARGET
#include <kernel.h>
#include <graph.h>

static uint64_t last_ticks = 0;
static float last_delta = 1.0f / 60.0f;

void melo_time_backend_wait_vsync(void) {
    graph_wait_vsync();
    uint64_t now = GetTimerSystemTime();
    if (last_ticks != 0) {
        uint64_t diff = now - last_ticks;
        last_delta = (float)diff / (float)kBUSCLK;
    }
    last_ticks = now;
}

float melo_time_backend_frame_delta(void) {
    return last_delta;
}

uint32_t melo_time_backend_ticks_ms(void) {
    return (uint32_t)(GetTimerSystemTime() / (kBUSCLK / 1000));
}
#else
#include <time.h>

static struct timespec last_ts;
static int have_last = 0;
static float last_delta = 1.0f / 60.0f;

void melo_time_backend_wait_vsync(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (have_last) {
        double diff = (now.tv_sec - last_ts.tv_sec) +
                      (now.tv_nsec - last_ts.tv_nsec) / 1e9;
        if (diff < 0.0) diff = 0.0;
        last_delta = (float)diff;
    }
    last_ts = now;
    have_last = 1;
}

float melo_time_backend_frame_delta(void) {
    return last_delta;
}

uint32_t melo_time_backend_ticks_ms(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint32_t)(now.tv_sec * 1000 + now.tv_nsec / 1000000);
}
#endif

static int native_wait_vsync(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    (void)vm; (void)args;
    if (arg_count != 0) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg), "wait_vsync() takes no arguments");
        return -1;
    }
    melo_time_backend_wait_vsync();
    *out_result = melo_nil();
    return 0;
}

static int native_frame_delta(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    (void)vm; (void)args;
    if (arg_count != 0) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg), "frame_delta() takes no arguments");
        return -1;
    }
    *out_result = melo_float(melo_time_backend_frame_delta());
    return 0;
}

static int native_ticks_ms(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    (void)vm; (void)args;
    if (arg_count != 0) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg), "ticks_ms() takes no arguments");
        return -1;
    }
    *out_result = melo_int((int32_t)melo_time_backend_ticks_ms());
    return 0;
}

int melo_stdlib_register_time(MeloVM *vm) {
    static const struct {
        const char *name;
        MeloNativeFn fn;
    } entries[] = {
        { "wait_vsync",   native_wait_vsync },
        { "frame_delta",  native_frame_delta },
        { "ticks_ms",     native_ticks_ms },
    };
    int count = (int)(sizeof(entries) / sizeof(entries[0]));
    for (int i = 0; i < count; i++) {
        if (melo_vm_register_native(vm, entries[i].name, entries[i].fn) < 0) {
            return -1;
        }
    }
    return 0;
}

const char **melo_stdlib_time_native_names(int *out_count) {
    static const char *names[] = { "wait_vsync", "frame_delta", "ticks_ms" };
    *out_count = (int)(sizeof(names) / sizeof(names[0]));
    return names;
}
