#ifndef MELO_VALUE_H
#define MELO_VALUE_H

#include <stdint.h>

#define MELO_ALIGN16 __attribute__((aligned(16)))

typedef enum {
    MELO_NIL = 0,
    MELO_INT32,
    MELO_FLOAT,
    MELO_VEC2,
    MELO_VEC3,
    MELO_VEC4,
    MELO_STRING,
    MELO_OBJECT,
    MELO_ARRAY,
    MELO_SOA_ARRAY,
    MELO_VRAM_TEX,
    MELO_CORO
} MeloType;

typedef enum {
    MELO_VRAM_EVICTED = 0,
    MELO_VRAM_RESIDENT
} MeloVramStatus;

typedef struct MeloVramTex {
    uint32_t width;
    uint32_t height;
    uint32_t byte_size;
    uint32_t vram_offset;
    MeloVramStatus status;
} MeloVramTex;

typedef struct MELO_ALIGN16 {
    float x, y, z, w;
} MeloVec4;

typedef struct {
    float x, y;
} MeloVec2;

typedef struct {
    float x, y, z;
} MeloVec3;

typedef struct MeloString {
    uint32_t length;
    uint32_t hash;
    uint32_t refcount;
    char data[];
} MeloString;

typedef struct MELO_ALIGN16 {
    MeloType type;
    union {
        int32_t i32;
        float f32;
        MeloVec2 v2;
        MeloVec3 v3;
        MeloVec4 v4;
        MeloString *str;
        struct MeloObject *obj;
        struct MeloArrayObj *arr;
        struct MeloSoAArray *soa;
        struct MeloVramTex *vram;
    } as;
} MeloValue;

typedef struct MeloObject {
    int32_t struct_id;
    int32_t field_count;
    MeloValue fields[];
} MeloObject;

typedef struct MeloArrayObj {
    int32_t length;
    int32_t capacity;
    MeloValue items[];
} MeloArrayObj;

#define MELO_SOA_MAX_FIELDS 16

typedef struct MeloSoAArray {
    int32_t struct_id;
    int32_t field_count;
    int32_t length;
    int32_t capacity;
    float *field_data[MELO_SOA_MAX_FIELDS];
} MeloSoAArray;

static inline MeloValue melo_nil(void) {
    MeloValue v;
    v.type = MELO_NIL;
    v.as.i32 = 0;
    return v;
}

static inline MeloValue melo_int(int32_t x) {
    MeloValue v;
    v.type = MELO_INT32;
    v.as.i32 = x;
    return v;
}

static inline MeloValue melo_float(float x) {
    MeloValue v;
    v.type = MELO_FLOAT;
    v.as.f32 = x;
    return v;
}

static inline MeloValue melo_vec4(float x, float y, float z, float w) {
    MeloValue v;
    v.type = MELO_VEC4;
    v.as.v4.x = x;
    v.as.v4.y = y;
    v.as.v4.z = z;
    v.as.v4.w = w;
    return v;
}

static inline MeloValue melo_string(MeloString *s) {
    MeloValue v;
    v.type = MELO_STRING;
    v.as.str = s;
    return v;
}

static inline MeloValue melo_object(MeloObject *o) {
    MeloValue v;
    v.type = MELO_OBJECT;
    v.as.obj = o;
    return v;
}

static inline MeloValue melo_array(MeloArrayObj *a) {
    MeloValue v;
    v.type = MELO_ARRAY;
    v.as.arr = a;
    return v;
}

static inline MeloValue melo_soa_array(struct MeloSoAArray *a) {
    MeloValue v;
    v.type = MELO_SOA_ARRAY;
    v.as.soa = a;
    return v;
}

static inline MeloValue melo_vram_tex(struct MeloVramTex *t) {
    MeloValue v;
    v.type = MELO_VRAM_TEX;
    v.as.vram = t;
    return v;
}

static inline MeloValue melo_coro(int32_t coro_id) {
    MeloValue v;
    v.type = MELO_CORO;
    v.as.i32 = coro_id;
    return v;
}

#endif
