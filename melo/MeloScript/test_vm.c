#include "melo_vm.h"
#include <stdio.h>

int main(void) {
    MeloVM vm;

    MeloInstr code[] = {
        MAKE_ABX(OP_LOADK, 0, 0),
        MAKE_ABX(OP_LOADK, 1, 1),
        MAKE_ABC(OP_VADD, 2, 0, 1),
        MAKE_ABC(OP_VDOT, 3, 0, 1),
        MAKE_ABC(OP_HALT, 0, 0, 0),
    };

    melo_vm_init(&vm, code, sizeof(code) / sizeof(code[0]));

    vm.consts[0] = melo_vec4(1.0f, 2.0f, 3.0f, 0.0f);
    vm.consts[1] = melo_vec4(4.0f, 5.0f, 6.0f, 0.0f);
    vm.const_count = 2;

    int err = melo_vm_run(&vm);

    if (err) {
        printf("VM error: %s\n", vm.error_msg);
        return 1;
    }

    MeloValue *R = vm.reg_stack + vm.frames[vm.frame_top].base;
    MeloValue sum = R[2];
    MeloValue dot = R[3];

    printf("vec4 sum = (%.2f, %.2f, %.2f, %.2f)\n",
           sum.as.v4.x, sum.as.v4.y, sum.as.v4.z, sum.as.v4.w);
    printf("dot product = %.2f\n", dot.as.f32);
    printf("alignof(MeloVec4) = %zu\n", _Alignof(MeloVec4));

    if (sum.as.v4.x != 5.0f || sum.as.v4.y != 7.0f || sum.as.v4.z != 9.0f) {
        printf("FAIL: unexpected sum\n");
        return 1;
    }
    if (dot.as.f32 != 32.0f) {
        printf("FAIL: unexpected dot product\n");
        return 1;
    }

    printf("OK\n");
    return 0;
}
