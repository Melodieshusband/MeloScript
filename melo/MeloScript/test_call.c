#include "melo_vm.h"
#include <stdio.h>

int main(void) {
    MeloVM vm;

    static MeloInstr square_code[] = {
        MAKE_ABC(OP_MUL, 1, 0, 0),
        MAKE_ABC(OP_RET, 1, 0, 0),
    };

    static MeloInstr main_code[] = {
        MAKE_ABX(OP_LOADK, 0, 0),
        MAKE_ABC(OP_CALL, 1, 0, 0),
        MAKE_ABC(OP_HALT, 0, 0, 0),
    };

    melo_vm_init(&vm, main_code, sizeof(main_code) / sizeof(main_code[0]));

    vm.funcs[0].entry = square_code;
    vm.funcs[0].num_params = 1;
    vm.funcs[0].num_regs = 2;
    vm.func_count = 1;

    vm.consts[0] = melo_int(7);
    vm.const_count = 1;

    int err = melo_vm_run(&vm);

    if (err) {
        printf("VM error: %s\n", vm.error_msg);
        return 1;
    }

    MeloValue result = vm.reg_stack[1];
    printf("square(7) = %d\n", result.as.i32);

    if (result.as.i32 != 49) {
        printf("FAIL\n");
        return 1;
    }

    printf("OK\n");
    return 0;
}
