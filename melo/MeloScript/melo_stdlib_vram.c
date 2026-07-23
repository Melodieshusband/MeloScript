#include "melo_stdlib_vram.h"

static int native_vram_alloc(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    if (arg_count != 2 || args[0].type != MELO_INT32 || args[1].type != MELO_INT32) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg),
                 "vram_alloc(width: int32, height: int32) requires two int32 arguments");
        return -1;
    }
    int32_t width = args[0].as.i32;
    int32_t height = args[1].as.i32;
    if (width <= 0 || height <= 0) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg),
                 "vram_alloc: width and height must be positive");
        return -1;
    }
    MeloVramTex *tex = melo_vm_vram_alloc(vm, (uint32_t)width, (uint32_t)height, 4);
    if (!tex) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg),
                 "VRAM exhausted (4MB eDRAM limit exceeded)");
        return -1;
    }
    *out_result = melo_vram_tex(tex);
    return 0;
}

static int native_vram_upload(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    if (arg_count != 1 || args[0].type != MELO_VRAM_TEX || args[0].as.vram == NULL) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg),
                 "upload(tex) requires a @vram texture value");
        return -1;
    }
    melo_vm_vram_upload(vm, args[0].as.vram);
    *out_result = melo_nil();
    return 0;
}

static int native_vram_evict(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    if (arg_count != 1 || args[0].type != MELO_VRAM_TEX || args[0].as.vram == NULL) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg),
                 "evict(tex) requires a @vram texture value");
        return -1;
    }
    melo_vm_vram_evict(vm, args[0].as.vram);
    *out_result = melo_nil();
    return 0;
}

static int native_vram_resident(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    if (arg_count != 1 || args[0].type != MELO_VRAM_TEX || args[0].as.vram == NULL) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg),
                 "resident(tex) requires a @vram texture value");
        return -1;
    }
    *out_result = melo_int(args[0].as.vram->status == MELO_VRAM_RESIDENT ? 1 : 0);
    return 0;
}

static int native_vram_free(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    if (arg_count != 1 || args[0].type != MELO_VRAM_TEX || args[0].as.vram == NULL) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg),
                 "vram_free(tex) requires a @vram texture value");
        return -1;
    }
    melo_vm_vram_free(vm, args[0].as.vram);
    *out_result = melo_nil();
    return 0;
}

int melo_stdlib_register_vram(MeloVM *vm) {
    static const struct {
        const char *name;
        MeloNativeFn fn;
    } entries[] = {
        { "vram_alloc",    native_vram_alloc },
        { "upload",        native_vram_upload },
        { "evict",         native_vram_evict },
        { "resident",      native_vram_resident },
        { "vram_free",     native_vram_free },
    };
    int count = (int)(sizeof(entries) / sizeof(entries[0]));
    for (int i = 0; i < count; i++) {
        if (melo_vm_register_native(vm, entries[i].name, entries[i].fn) < 0) {
            return -1;
        }
    }
    return 0;
}

const char **melo_stdlib_vram_native_names(int *out_count) {
    static const char *names[] = {
        "vram_alloc", "upload", "evict", "resident", "vram_free"
    };
    *out_count = (int)(sizeof(names) / sizeof(names[0]));
    return names;
}
