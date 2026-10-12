#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../MeloScript/melo_vm.h"
#include "../MeloScript/melo_image.h"
#include "../MeloScript/melo_stdlib_vram.h"

extern const unsigned char melo_demo_image[];
extern const unsigned int melo_demo_image_len;

static MeloVM g_vm;

int main(void) {
    MeloImageInfo info;
    char err[160];

    if (melo_image_load(&g_vm, melo_demo_image, melo_demo_image_len, &info, err, sizeof(err))) {
        printf("image load failed: %s\n", err);
        return 1;
    }
    if (melo_stdlib_register_vram(&g_vm) != 0) {
        printf("native registration failed\n");
        return 1;
    }
    if (melo_image_verify_natives(&g_vm, &info, err, sizeof(err))) {
        printf("native binding failed: %s\n", err);
        return 1;
    }
    if (melo_vm_run(&g_vm)) {
        printf("VM error: %s\n", g_vm.error_msg);
        return 1;
    }
    printf("main returned %d\n", g_vm.reg_stack[0].as.i32);
    melo_image_unload(&info);
    return 0;
}
