#include "melo_stdlib_pad.h"
#include <string.h>
#include <stdio.h>

#ifdef MELO_PS2_TARGET
#include <libpad.h>
#include <tamtypes.h>

static char pad_dma_buf[2][256] __attribute__((aligned(64)));
static struct padButtonStatus pad_status;
static int pad_ready[2][4];

int melo_pad_backend_init(int port, int slot) {
    if (port < 0 || port > 1 || slot < 0 || slot > 3) return -1;
    int rc = padPortOpen(port, slot, pad_dma_buf[port]);
    pad_ready[port][slot] = (rc == 1);
    return rc == 1 ? 0 : -1;
}

void melo_pad_backend_poll(int port, int slot) {
    (void)slot;
    padGetState(port, slot);
}

uint16_t melo_pad_backend_buttons(int port, int slot) {
    if (padRead(port, slot, &pad_status) == 0) return 0;
    return (uint16_t)(0xFFFF & ~pad_status.btns);
}

float melo_pad_backend_stick(int port, int slot, int stick_index, int axis_index) {
    if (padRead(port, slot, &pad_status) == 0) return 0.0f;
    uint8_t raw;
    if (stick_index == 0) {
        raw = (axis_index == 0) ? pad_status.ljoy_h : pad_status.ljoy_v;
    } else {
        raw = (axis_index == 0) ? pad_status.rjoy_h : pad_status.rjoy_v;
    }
    return ((float)raw - 127.5f) / 127.5f;
}
#else
static uint16_t stub_buttons[2][4];
static float stub_sticks[2][4][2][2];

int melo_pad_backend_init(int port, int slot) {
    if (port < 0 || port > 1 || slot < 0 || slot > 3) return -1;
    stub_buttons[port][slot] = 0;
    return 0;
}

void melo_pad_backend_poll(int port, int slot) {
    (void)port; (void)slot;
}

uint16_t melo_pad_backend_buttons(int port, int slot) {
    if (port < 0 || port > 1 || slot < 0 || slot > 3) return 0;
    return stub_buttons[port][slot];
}

float melo_pad_backend_stick(int port, int slot, int stick_index, int axis_index) {
    if (port < 0 || port > 1 || slot < 0 || slot > 3) return 0.0f;
    if (stick_index < 0 || stick_index > 1 || axis_index < 0 || axis_index > 1) return 0.0f;
    return stub_sticks[port][slot][stick_index][axis_index];
}
#endif

static int check_port_slot(MeloVM *vm, int32_t port, int32_t slot, const char *fname) {
    if (port < 0 || port > 1 || slot < 0 || slot > 3) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg),
                 "%s: port must be 0-1 and slot must be 0-3", fname);
        return -1;
    }
    return 0;
}

static int native_pad_init(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    if (arg_count != 2 || args[0].type != MELO_INT32 || args[1].type != MELO_INT32) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg),
                 "pad_init(port: int32, slot: int32) requires two int32 arguments");
        return -1;
    }
    if (check_port_slot(vm, args[0].as.i32, args[1].as.i32, "pad_init") < 0) return -1;
    int rc = melo_pad_backend_init(args[0].as.i32, args[1].as.i32);
    *out_result = melo_int(rc == 0 ? 1 : 0);
    return 0;
}

static int native_pad_poll(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    if (arg_count != 2 || args[0].type != MELO_INT32 || args[1].type != MELO_INT32) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg),
                 "pad_poll(port: int32, slot: int32) requires two int32 arguments");
        return -1;
    }
    if (check_port_slot(vm, args[0].as.i32, args[1].as.i32, "pad_poll") < 0) return -1;
    melo_pad_backend_poll(args[0].as.i32, args[1].as.i32);
    *out_result = melo_nil();
    return 0;
}

static int native_pad_buttons(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    if (arg_count != 2 || args[0].type != MELO_INT32 || args[1].type != MELO_INT32) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg),
                 "pad_buttons(port: int32, slot: int32) requires two int32 arguments");
        return -1;
    }
    if (check_port_slot(vm, args[0].as.i32, args[1].as.i32, "pad_buttons") < 0) return -1;
    uint16_t btns = melo_pad_backend_buttons(args[0].as.i32, args[1].as.i32);
    *out_result = melo_int((int32_t)btns);
    return 0;
}

static int native_pad_pressed(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    if (arg_count != 3 || args[0].type != MELO_INT32 || args[1].type != MELO_INT32 ||
        args[2].type != MELO_INT32) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg),
                 "pad_pressed(port: int32, slot: int32, mask: int32) requires three int32 arguments");
        return -1;
    }
    if (check_port_slot(vm, args[0].as.i32, args[1].as.i32, "pad_pressed") < 0) return -1;
    uint16_t btns = melo_pad_backend_buttons(args[0].as.i32, args[1].as.i32);
    int32_t mask = args[2].as.i32;
    *out_result = melo_int((btns & (uint16_t)mask) ? 1 : 0);
    return 0;
}

static int native_pad_stick(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    if (arg_count != 4 || args[0].type != MELO_INT32 || args[1].type != MELO_INT32 ||
        args[2].type != MELO_INT32 || args[3].type != MELO_INT32) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg),
                 "pad_stick(port: int32, slot: int32, stick: int32, axis: int32) requires four int32 arguments");
        return -1;
    }
    if (check_port_slot(vm, args[0].as.i32, args[1].as.i32, "pad_stick") < 0) return -1;
    int32_t stick = args[2].as.i32;
    int32_t axis = args[3].as.i32;
    if (stick < 0 || stick > 1 || axis < 0 || axis > 1) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg),
                 "pad_stick: stick must be 0(left)/1(right), axis must be 0(x)/1(y)");
        return -1;
    }
    float v = melo_pad_backend_stick(args[0].as.i32, args[1].as.i32, stick, axis);
    *out_result = melo_float(v);
    return 0;
}

int melo_stdlib_register_pad(MeloVM *vm) {
    static const struct {
        const char *name;
        MeloNativeFn fn;
    } entries[] = {
        { "pad_init",     native_pad_init },
        { "pad_poll",     native_pad_poll },
        { "pad_buttons",  native_pad_buttons },
        { "pad_pressed",  native_pad_pressed },
        { "pad_stick",    native_pad_stick },
    };
    int count = (int)(sizeof(entries) / sizeof(entries[0]));
    for (int i = 0; i < count; i++) {
        if (melo_vm_register_native(vm, entries[i].name, entries[i].fn) < 0) {
            return -1;
        }
    }
    return 0;
}

const char **melo_stdlib_pad_native_names(int *out_count) {
    static const char *names[] = {
        "pad_init", "pad_poll", "pad_buttons", "pad_pressed", "pad_stick"
    };
    *out_count = (int)(sizeof(names) / sizeof(names[0]));
    return names;
}
