#include "melo_stdlib_math.h"
#include <stdio.h>
#include <math.h>

int32_t melo_i64_add_lo(int32_t a_hi, int32_t a_lo, int32_t b_hi, int32_t b_lo) {
    (void)a_hi; (void)b_hi;
    uint32_t sum = (uint32_t)a_lo + (uint32_t)b_lo;
    return (int32_t)sum;
}

int32_t melo_i64_add_hi(int32_t a_hi, int32_t a_lo, int32_t b_hi, int32_t b_lo) {
    uint32_t sum_lo = (uint32_t)a_lo + (uint32_t)b_lo;
    uint32_t carry = sum_lo < (uint32_t)a_lo ? 1u : 0u;
    uint32_t sum_hi = (uint32_t)a_hi + (uint32_t)b_hi + carry;
    return (int32_t)sum_hi;
}

int32_t melo_i64_sub_lo(int32_t a_hi, int32_t a_lo, int32_t b_hi, int32_t b_lo) {
    (void)a_hi; (void)b_hi;
    uint32_t diff = (uint32_t)a_lo - (uint32_t)b_lo;
    return (int32_t)diff;
}

int32_t melo_i64_sub_hi(int32_t a_hi, int32_t a_lo, int32_t b_hi, int32_t b_lo) {
    uint32_t borrow = (uint32_t)a_lo < (uint32_t)b_lo ? 1u : 0u;
    uint32_t diff_hi = (uint32_t)a_hi - (uint32_t)b_hi - borrow;
    return (int32_t)diff_hi;
}

int32_t melo_i64_mul_lo(int32_t a_lo, int32_t b_lo) {
    uint64_t prod = (uint64_t)(uint32_t)a_lo * (uint64_t)(uint32_t)b_lo;
    return (int32_t)(uint32_t)prod;
}

int32_t melo_i64_mul_hi(int32_t a_lo, int32_t b_lo) {
    uint64_t prod = (uint64_t)(uint32_t)a_lo * (uint64_t)(uint32_t)b_lo;
    return (int32_t)(uint32_t)(prod >> 32);
}

int32_t melo_i64_cmp(int32_t a_hi, int32_t a_lo, int32_t b_hi, int32_t b_lo) {
    int64_t a = ((int64_t)a_hi << 32) | (uint32_t)a_lo;
    int64_t b = ((int64_t)b_hi << 32) | (uint32_t)b_lo;
    if (a < b) return -1;
    if (a > b) return 1;
    return 0;
}

int32_t melo_fix_from_float(float f) {
    return (int32_t)(f * 65536.0f);
}

float melo_fix_to_float(int32_t fix) {
    return (float)fix / 65536.0f;
}

int32_t melo_fix_mul(int32_t a, int32_t b) {
    int64_t prod = (int64_t)a * (int64_t)b;
    return (int32_t)(prod >> 16);
}

int32_t melo_fix_div(int32_t a, int32_t b) {
    if (b == 0) return 0;
    int64_t numer = (int64_t)a << 16;
    return (int32_t)(numer / b);
}

float melo_math_clamp(float val, float min_v, float max_v) {
    if (val < min_v) return min_v;
    if (val > max_v) return max_v;
    return val;
}

float melo_math_lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

float melo_math_min(float a, float b) {
    return a < b ? a : b;
}

float melo_math_max(float a, float b) {
    return a > b ? a : b;
}

float melo_math_abs(float a) {
    return a < 0.0f ? -a : a;
}

static int native_i64_add_lo(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    if (arg_count != 4 || args[0].type != MELO_INT32 || args[1].type != MELO_INT32 ||
        args[2].type != MELO_INT32 || args[3].type != MELO_INT32) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg), "i64_add_lo(a_hi, a_lo, b_hi, b_lo) requires four int32");
        return -1;
    }
    *out_result = melo_int(melo_i64_add_lo(args[0].as.i32, args[1].as.i32, args[2].as.i32, args[3].as.i32));
    return 0;
}

static int native_i64_add_hi(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    if (arg_count != 4 || args[0].type != MELO_INT32 || args[1].type != MELO_INT32 ||
        args[2].type != MELO_INT32 || args[3].type != MELO_INT32) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg), "i64_add_hi(a_hi, a_lo, b_hi, b_lo) requires four int32");
        return -1;
    }
    *out_result = melo_int(melo_i64_add_hi(args[0].as.i32, args[1].as.i32, args[2].as.i32, args[3].as.i32));
    return 0;
}

static int native_i64_sub_lo(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    if (arg_count != 4 || args[0].type != MELO_INT32 || args[1].type != MELO_INT32 ||
        args[2].type != MELO_INT32 || args[3].type != MELO_INT32) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg), "i64_sub_lo(a_hi, a_lo, b_hi, b_lo) requires four int32");
        return -1;
    }
    *out_result = melo_int(melo_i64_sub_lo(args[0].as.i32, args[1].as.i32, args[2].as.i32, args[3].as.i32));
    return 0;
}

static int native_i64_sub_hi(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    if (arg_count != 4 || args[0].type != MELO_INT32 || args[1].type != MELO_INT32 ||
        args[2].type != MELO_INT32 || args[3].type != MELO_INT32) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg), "i64_sub_hi(a_hi, a_lo, b_hi, b_lo) requires four int32");
        return -1;
    }
    *out_result = melo_int(melo_i64_sub_hi(args[0].as.i32, args[1].as.i32, args[2].as.i32, args[3].as.i32));
    return 0;
}

static int native_i64_mul_lo(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    if (arg_count != 2 || args[0].type != MELO_INT32 || args[1].type != MELO_INT32) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg), "i64_mul_lo(a_lo, b_lo) requires two int32");
        return -1;
    }
    *out_result = melo_int(melo_i64_mul_lo(args[0].as.i32, args[1].as.i32));
    return 0;
}

static int native_i64_mul_hi(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    if (arg_count != 2 || args[0].type != MELO_INT32 || args[1].type != MELO_INT32) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg), "i64_mul_hi(a_lo, b_lo) requires two int32");
        return -1;
    }
    *out_result = melo_int(melo_i64_mul_hi(args[0].as.i32, args[1].as.i32));
    return 0;
}

static int native_i64_cmp(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    if (arg_count != 4 || args[0].type != MELO_INT32 || args[1].type != MELO_INT32 ||
        args[2].type != MELO_INT32 || args[3].type != MELO_INT32) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg), "i64_cmp(a_hi, a_lo, b_hi, b_lo) requires four int32");
        return -1;
    }
    *out_result = melo_int(melo_i64_cmp(args[0].as.i32, args[1].as.i32, args[2].as.i32, args[3].as.i32));
    return 0;
}

static int native_fix_from_float(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    if (arg_count != 1 || (args[0].type != MELO_FLOAT && args[0].type != MELO_INT32)) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg), "fix_from_float(f) requires a numeric argument");
        return -1;
    }
    float f = args[0].type == MELO_FLOAT ? args[0].as.f32 : (float)args[0].as.i32;
    *out_result = melo_int(melo_fix_from_float(f));
    return 0;
}

static int native_fix_to_float(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    if (arg_count != 1 || args[0].type != MELO_INT32) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg), "fix_to_float(fix) requires an int32 argument");
        return -1;
    }
    *out_result = melo_float(melo_fix_to_float(args[0].as.i32));
    return 0;
}

static int native_fix_mul(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    if (arg_count != 2 || args[0].type != MELO_INT32 || args[1].type != MELO_INT32) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg), "fix_mul(a, b) requires two int32 arguments");
        return -1;
    }
    *out_result = melo_int(melo_fix_mul(args[0].as.i32, args[1].as.i32));
    return 0;
}

static int native_fix_div(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    if (arg_count != 2 || args[0].type != MELO_INT32 || args[1].type != MELO_INT32) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg), "fix_div(a, b) requires two int32 arguments");
        return -1;
    }
    *out_result = melo_int(melo_fix_div(args[0].as.i32, args[1].as.i32));
    return 0;
}

static int native_clamp(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    if (arg_count != 3) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg), "clamp(val, min, max) requires 3 arguments");
        return -1;
    }
    float v = args[0].type == MELO_FLOAT ? args[0].as.f32 : (float)args[0].as.i32;
    float mn = args[1].type == MELO_FLOAT ? args[1].as.f32 : (float)args[1].as.i32;
    float mx = args[2].type == MELO_FLOAT ? args[2].as.f32 : (float)args[2].as.i32;
    *out_result = melo_float(melo_math_clamp(v, mn, mx));
    return 0;
}

static int native_lerp(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    if (arg_count != 3) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg), "lerp(a, b, t) requires 3 arguments");
        return -1;
    }
    float a = args[0].type == MELO_FLOAT ? args[0].as.f32 : (float)args[0].as.i32;
    float b = args[1].type == MELO_FLOAT ? args[1].as.f32 : (float)args[1].as.i32;
    float t = args[2].type == MELO_FLOAT ? args[2].as.f32 : (float)args[2].as.i32;
    *out_result = melo_float(melo_math_lerp(a, b, t));
    return 0;
}

static int native_min(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    if (arg_count != 2) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg), "fmin(a, b) requires 2 arguments");
        return -1;
    }
    float a = args[0].type == MELO_FLOAT ? args[0].as.f32 : (float)args[0].as.i32;
    float b = args[1].type == MELO_FLOAT ? args[1].as.f32 : (float)args[1].as.i32;
    *out_result = melo_float(melo_math_min(a, b));
    return 0;
}

static int native_max(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    if (arg_count != 2) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg), "fmax(a, b) requires 2 arguments");
        return -1;
    }
    float a = args[0].type == MELO_FLOAT ? args[0].as.f32 : (float)args[0].as.i32;
    float b = args[1].type == MELO_FLOAT ? args[1].as.f32 : (float)args[1].as.i32;
    *out_result = melo_float(melo_math_max(a, b));
    return 0;
}

static int native_abs(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    if (arg_count != 1) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg), "fabs_val(a) requires 1 argument");
        return -1;
    }
    float a = args[0].type == MELO_FLOAT ? args[0].as.f32 : (float)args[0].as.i32;
    *out_result = melo_float(melo_math_abs(a));
    return 0;
}

int melo_stdlib_register_math(MeloVM *vm) {
    static const struct {
        const char *name;
        MeloNativeFn fn;
    } entries[] = {
        { "i64_add_lo",     native_i64_add_lo },
        { "i64_add_hi",     native_i64_add_hi },
        { "i64_sub_lo",     native_i64_sub_lo },
        { "i64_sub_hi",     native_i64_sub_hi },
        { "i64_mul_lo",     native_i64_mul_lo },
        { "i64_mul_hi",     native_i64_mul_hi },
        { "i64_cmp",        native_i64_cmp },
        { "fix_from_float", native_fix_from_float },
        { "fix_to_float",   native_fix_to_float },
        { "fix_mul",        native_fix_mul },
        { "fix_div",        native_fix_div },
        { "clamp",          native_clamp },
        { "lerp",           native_lerp },
        { "fmin",           native_min },
        { "fmax",           native_max },
        { "fabs_val",       native_abs },
    };
    int count = (int)(sizeof(entries) / sizeof(entries[0]));
    for (int i = 0; i < count; i++) {
        if (melo_vm_register_native(vm, entries[i].name, entries[i].fn) < 0) {
            return -1;
        }
    }
    return 0;
}

const char **melo_stdlib_math_native_names(int *out_count) {
    static const char *names[] = {
        "i64_add_lo", "i64_add_hi", "i64_sub_lo", "i64_sub_hi",
        "i64_mul_lo", "i64_mul_hi", "i64_cmp",
        "fix_from_float", "fix_to_float", "fix_mul", "fix_div",
        "clamp", "lerp", "fmin", "fmax", "fabs_val"
    };
    *out_count = (int)(sizeof(names) / sizeof(names[0]));
    return names;
}
