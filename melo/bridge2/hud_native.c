// hud_native.c
// Native-функция scr_print(int) для вызова из hud_text.melo.
// Под MELO_PS2_TARGET использует настоящий PS2SDK debug screen (scr_printf),
// иначе — printf на хосте (для тестов без консоли).

#include "melo_vm.h"
#include "melo_value.h"

#ifdef MELO_PS2_TARGET
#include <tamtypes.h>
#include <sifrpc.h>
#include <debug.h>
#else
#include <stdio.h>
#endif

int melo_native_scr_print(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    (void)vm;
    if (arg_count != 1 || args[0].type != MELO_INT32) {
        return -1;
    }

    int32_t value = args[0].as.i32;

#ifdef MELO_PS2_TARGET
    scr_printf("%d\n", value);
#else
    printf("[scr_print] %d\n", value);
#endif

    *out_result = melo_int(0);
    return 0;
}

void hud_init_screen(void) {
#ifdef MELO_PS2_TARGET
    sceSifInitRpc(0);
    init_scr();
#endif
}
