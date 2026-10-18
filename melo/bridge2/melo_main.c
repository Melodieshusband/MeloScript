#include <tamtypes.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <debug.h>
#include <libpad.h>
#include <kernel.h>
#include <string.h>
#include <stdio.h>
#include "melo_vm.h"
#include "melo_value.h"

extern int melo_load_game_module(MeloVM *vm);

static char pad_dma_buf[256] __attribute__((aligned(64)));
static struct padButtonStatus pad_status;
static uint16_t current_buttons = 0;
static int pad_ready = 0;

static void load_modules(void) {
    SifLoadFileInit();
    SifLoadModule("rom0:SIO2MAN", 0, NULL);
    SifLoadModule("rom0:PADMAN", 0, NULL);
}

static void poll_gamepad(void) {
    if (!pad_ready) {
        current_buttons = 0;
        return;
    }
    int state = padGetState(0, 0);
    if (state == PAD_STATE_STABLE || state == PAD_STATE_FINDCTP1) {
        if (padRead(0, 0, &pad_status) != 0) {
            current_buttons = (uint16_t)(0xFFFF ^ pad_status.btns);
        } else {
            current_buttons = 0;
        }
    } else {
        current_buttons = 0;
    }
}

static void delay_ms(int ms) {
    for (int i = 0; i < ms; i++) {
        for (volatile int j = 0; j < 35000; j++) {
            __asm__ __volatile__("nop");
        }
    }
}

static int native_scr_clear(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    (void)vm; (void)args; (void)arg_count;
    scr_clear();
    *out_result = melo_nil();
    return 0;
}

static int native_scr_set_xy(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    (void)vm;
    if (arg_count >= 2 && args[0].type == MELO_INT32 && args[1].type == MELO_INT32) {
        scr_setXY(args[0].as.i32, args[1].as.i32);
    }
    *out_result = melo_nil();
    return 0;
}

static int native_scr_set_color(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    (void)vm;
    if (arg_count >= 1 && args[0].type == MELO_INT32) {
        scr_setfontcolor((uint32_t)args[0].as.i32);
    }
    *out_result = melo_nil();
    return 0;
}

static int native_scr_print(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    (void)vm;
    if (arg_count >= 1 && args[0].type == MELO_STRING && args[0].as.str) {
        scr_printf("%s", args[0].as.str->data);
    }
    *out_result = melo_nil();
    return 0;
}

static int native_scr_print_num(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    (void)vm;
    if (arg_count >= 1 && args[0].type == MELO_INT32) {
        scr_printf("%d", (int)args[0].as.i32);
    }
    *out_result = melo_nil();
    return 0;
}

static int native_pad_poll(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    (void)vm; (void)args; (void)arg_count;
    poll_gamepad();
    *out_result = melo_nil();
    return 0;
}

static int native_pad_btn_left(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    (void)vm; (void)args; (void)arg_count;
    *out_result = melo_int((current_buttons & PAD_LEFT) ? 1 : 0);
    return 0;
}

static int native_pad_btn_right(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    (void)vm; (void)args; (void)arg_count;
    *out_result = melo_int((current_buttons & PAD_RIGHT) ? 1 : 0);
    return 0;
}

static int native_pad_btn_down(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    (void)vm; (void)args; (void)arg_count;
    *out_result = melo_int((current_buttons & PAD_DOWN) ? 1 : 0);
    return 0;
}

static int native_pad_btn_rotate(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    (void)vm; (void)args; (void)arg_count;
    int p = ((current_buttons & PAD_CROSS) || (current_buttons & PAD_UP)) ? 1 : 0;
    *out_result = melo_int(p);
    return 0;
}

static int native_pad_btn_drop(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    (void)vm; (void)args; (void)arg_count;
    int p = ((current_buttons & PAD_CIRCLE) || (current_buttons & PAD_SQUARE)) ? 1 : 0;
    *out_result = melo_int(p);
    return 0;
}

static int native_pad_btn_start(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    (void)vm; (void)args; (void)arg_count;
    *out_result = melo_int((current_buttons & PAD_START) ? 1 : 0);
    return 0;
}

static int native_sys_sleep_ms(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    (void)vm;
    int ms = (arg_count >= 1 && args[0].type == MELO_INT32) ? (int)args[0].as.i32 : 16;
    if (ms < 1) ms = 1;
    delay_ms(ms);
    *out_result = melo_nil();
    return 0;
}

static int native_sys_rand(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    (void)vm;
    int max = (arg_count >= 1 && args[0].type == MELO_INT32) ? (int)args[0].as.i32 : 7;
    if (max <= 0) max = 1;
    static unsigned int seed = 987654321;
    seed = (seed * 1103515245 + 12345) & 0x7fffffff;
    int r = (int)(seed % (unsigned int)max);
    *out_result = melo_int(r);
    return 0;
}

static MeloVM game_vm;

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    sceSifInitRpc(0);
    init_scr();
    scr_setCursor(0);
    scr_clear();

    scr_printf("Initializing MeloScript Tetris...\n");

    load_modules();

    int pinit = padInit(0);
    if (pinit != 0) {
        if (padPortOpen(0, 0, pad_dma_buf) != 0) {
            pad_ready = 1;
        }
    }

    melo_load_game_module(&game_vm);

    melo_vm_register_native(&game_vm, "scr_clear", native_scr_clear);
    melo_vm_register_native(&game_vm, "scr_set_xy", native_scr_set_xy);
    melo_vm_register_native(&game_vm, "scr_set_color", native_scr_set_color);
    melo_vm_register_native(&game_vm, "scr_print", native_scr_print);
    melo_vm_register_native(&game_vm, "scr_print_num", native_scr_print_num);
    melo_vm_register_native(&game_vm, "pad_poll", native_pad_poll);
    melo_vm_register_native(&game_vm, "pad_btn_left", native_pad_btn_left);
    melo_vm_register_native(&game_vm, "pad_btn_right", native_pad_btn_right);
    melo_vm_register_native(&game_vm, "pad_btn_down", native_pad_btn_down);
    melo_vm_register_native(&game_vm, "pad_btn_rotate", native_pad_btn_rotate);
    melo_vm_register_native(&game_vm, "pad_btn_drop", native_pad_btn_drop);
    melo_vm_register_native(&game_vm, "pad_btn_start", native_pad_btn_start);
    melo_vm_register_native(&game_vm, "sys_sleep_ms", native_sys_sleep_ms);
    melo_vm_register_native(&game_vm, "sys_rand", native_sys_rand);

    int rc = melo_vm_run(&game_vm);
    if (rc != 0 || game_vm.error) {
        scr_clear();
        scr_setXY(0, 0);
        scr_setfontcolor(0x000000FF);
        scr_printf("MeloScript VM Execution Error (%d):\n", rc);
        scr_printf("%s\n", game_vm.error_msg);
    }

    while (1) {
    }

    return 0;
}
