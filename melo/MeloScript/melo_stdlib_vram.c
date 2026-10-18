#include "melo_stdlib_vram.h"

static int native_vram_alloc(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    if ((arg_count != 2 && arg_count != 3) || args[0].type != MELO_INT32 || args[1].type != MELO_INT32) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg),
                 "vram_alloc(width: int32, height: int32, [bpp: int32]) requires int32 arguments");
        return -1;
    }
    int32_t width = args[0].as.i32;
    int32_t height = args[1].as.i32;
    uint32_t bpp = 32;
    if (arg_count == 3) {
        if (args[2].type != MELO_INT32) {
            vm->error = 1;
            snprintf(vm->error_msg, sizeof(vm->error_msg),
                     "vram_alloc: bpp argument must be int32 (4, 8, 16, or 32)");
            return -1;
        }
        bpp = (uint32_t)args[2].as.i32;
    }
    if (width <= 0 || height <= 0) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg),
                 "vram_alloc: width and height must be positive");
        return -1;
    }
    MeloVramTex *tex = melo_vm_vram_alloc_format(vm, (uint32_t)width, (uint32_t)height, bpp);
    if (!tex) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg),
                 "VRAM exhausted (4MB eDRAM limit exceeded)");
        return -1;
    }
    *out_result = melo_vram_tex(tex);
    return 0;
}

static int native_vram_alloc_paletted(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    if (arg_count != 3 || args[0].type != MELO_INT32 || args[1].type != MELO_INT32 || args[2].type != MELO_INT32) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg),
                 "vram_alloc_paletted(width: int32, height: int32, bpp: int32) requires three int32 arguments");
        return -1;
    }
    int32_t width = args[0].as.i32;
    int32_t height = args[1].as.i32;
    int32_t bpp = args[2].as.i32;
    if (bpp != 4 && bpp != 8) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg),
                 "vram_alloc_paletted: bpp must be 4 or 8");
        return -1;
    }
    if (width <= 0 || height <= 0) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg),
                 "vram_alloc_paletted: width and height must be positive");
        return -1;
    }
    MeloVramTex *tex = melo_vm_vram_alloc_format(vm, (uint32_t)width, (uint32_t)height, (uint32_t)bpp);
    if (!tex) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg),
                 "VRAM exhausted (4MB eDRAM limit exceeded)");
        return -1;
    }
    *out_result = melo_vram_tex(tex);
    return 0;
}

static int native_vram_bpp(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    if (arg_count != 1 || args[0].type != MELO_VRAM_TEX || args[0].as.vram == NULL) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg),
                 "vram_bpp(tex) requires a @vram texture value");
        return -1;
    }
    *out_result = melo_int((int32_t)args[0].as.vram->bpp);
    return 0;
}

static int native_vram_size(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    if (arg_count != 1 || args[0].type != MELO_VRAM_TEX || args[0].as.vram == NULL) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg),
                 "vram_size(tex) requires a @vram texture value");
        return -1;
    }
    *out_result = melo_int((int32_t)args[0].as.vram->byte_size);
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
        { "vram_alloc",          native_vram_alloc },
        { "vram_alloc_paletted", native_vram_alloc_paletted },
        { "vram_bpp",            native_vram_bpp },
        { "vram_size",           native_vram_size },
        { "upload",              native_vram_upload },
        { "evict",               native_vram_evict },
        { "resident",            native_vram_resident },
        { "vram_free",           native_vram_free },
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
        "vram_alloc", "vram_alloc_paletted", "vram_bpp", "vram_size", "upload", "evict", "resident", "vram_free"
    };
    *out_count = (int)(sizeof(names) / sizeof(names[0]));
    return names;
}
