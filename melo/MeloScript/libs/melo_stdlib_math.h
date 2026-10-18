#ifndef MELO_STDLIB_MATH_H
#define MELO_STDLIB_MATH_H

#include "../melo_vm.h"

int melo_stdlib_register_math(MeloVM *vm);
const char **melo_stdlib_math_native_names(int *out_count);

int32_t melo_i64_add_lo(int32_t a_hi, int32_t a_lo, int32_t b_hi, int32_t b_lo);
int32_t melo_i64_add_hi(int32_t a_hi, int32_t a_lo, int32_t b_hi, int32_t b_lo);
int32_t melo_i64_sub_lo(int32_t a_hi, int32_t a_lo, int32_t b_hi, int32_t b_lo);
int32_t melo_i64_sub_hi(int32_t a_hi, int32_t a_lo, int32_t b_hi, int32_t b_lo);
int32_t melo_i64_mul_lo(int32_t a_lo, int32_t b_lo);
int32_t melo_i64_mul_hi(int32_t a_lo, int32_t b_lo);
int32_t melo_i64_cmp(int32_t a_hi, int32_t a_lo, int32_t b_hi, int32_t b_lo);

int32_t melo_fix_from_float(float f);
float   melo_fix_to_float(int32_t fix);
int32_t melo_fix_mul(int32_t a, int32_t b);
int32_t melo_fix_div(int32_t a, int32_t b);

float melo_math_clamp(float val, float min_v, float max_v);
float melo_math_lerp(float a, float b, float t);
float melo_math_min(float a, float b);
float melo_math_max(float a, float b);
float melo_math_abs(float a);

#endif
