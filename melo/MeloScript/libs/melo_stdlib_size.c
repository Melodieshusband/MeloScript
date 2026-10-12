#include "melo_stdlib_size.h"
#include <stdio.h>

uint32_t melo_size_value_bytes(const MeloValue *v) {
    switch (v->type) {
        case MELO_NIL: return 0;
        case MELO_INT32: return 4;
        case MELO_FLOAT: return 4;
        case MELO_VEC2: return 8;
        case MELO_VEC3: return 12;
        case MELO_VEC4: return 16;
        case MELO_STRING: return v->as.str ? v->as.str->length : 0;
        case MELO_OBJECT: {
            uint32_t total = 0;
            if (!v->as.obj) return 0;
            for (int i = 0; i < v->as.obj->field_count; i++) {
                total += melo_size_value_bytes(&v->as.obj->fields[i]);
            }
            return total;
        }
        case MELO_ARRAY: {
            uint32_t total = 0;
            if (!v->as.arr) return 0;
            for (int i = 0; i < v->as.arr->length; i++) {
                total += melo_size_value_bytes(&v->as.arr->items[i]);
            }
            return total;
        }
        case MELO_SOA_ARRAY:
            if (!v->as.soa) return 0;
            return (uint32_t)v->as.soa->length * (uint32_t)v->as.soa->field_count * 4u;
        case MELO_VRAM_TEX:
            return v->as.vram ? v->as.vram->byte_size : 0;
        default:
            return 0;
    }
}

uint32_t melo_size_script_bytes(const MeloVM *vm) {
    return (uint32_t)(vm->code_len * sizeof(MeloInstr));
}

uint32_t melo_size_script_hash(const MeloVM *vm) {
    uint32_t h = 2166136261u;
    const MeloInstr *code = vm->code_base ? vm->code_base : vm->code;
    for (size_t i = 0; i < vm->code_len; i++) {
        MeloInstr w = code[i];
        for (int b = 0; b < 4; b++) {
            h ^= (w >> (b * 8)) & 0xFFu;
            h *= 16777619u;
        }
    }
    return h & 0x7FFFFFFFu;
}

static int native_bytes_of(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    if (arg_count != 1) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg), "bytes_of(value) requires one argument");
        return -1;
    }
    *out_result = melo_int((int32_t)melo_size_value_bytes(&args[0]));
    return 0;
}

static int native_bits_of(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    if (arg_count != 1) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg), "bits_of(value) requires one argument");
        return -1;
    }
    *out_result = melo_int((int32_t)(melo_size_value_bytes(&args[0]) * 8u));
    return 0;
}

static int native_script_bytes(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    if (arg_count != 0) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg), "script_bytes() takes no arguments");
        return -1;
    }
    *out_result = melo_int((int32_t)melo_size_script_bytes(vm));
    return 0;
}

static int native_script_hash(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    if (arg_count != 0) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg), "script_hash() takes no arguments");
        return -1;
    }
    *out_result = melo_int((int32_t)melo_size_script_hash(vm));
    return 0;
}

int melo_stdlib_register_size(MeloVM *vm) {
    static const struct {
        const char *name;
        MeloNativeFn fn;
    } entries[] = {
        { "bytes_of",     native_bytes_of },
        { "bits_of",      native_bits_of },
        { "script_bytes", native_script_bytes },
        { "script_hash",  native_script_hash },
    };
    int count = (int)(sizeof(entries) / sizeof(entries[0]));
    for (int i = 0; i < count; i++) {
        if (melo_vm_register_native(vm, entries[i].name, entries[i].fn) < 0) {
            return -1;
        }
    }
    return 0;
}

const char **melo_stdlib_size_native_names(int *out_count) {
    static const char *names[] = { "bytes_of", "bits_of", "script_bytes", "script_hash" };
    *out_count = (int)(sizeof(names) / sizeof(names[0]));
    return names;
}
