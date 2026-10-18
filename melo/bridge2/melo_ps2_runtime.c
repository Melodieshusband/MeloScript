#if defined(MELO_PS2_TARGET) || defined(__mips__)
#include <tamtypes.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <debug.h>
#include <libpad.h>
#include <kernel.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "melo_value.h"
#include <math.h>

#if defined(MELO_PS2_TARGET) || defined(__mips__)

static char pad_dma_buf[256] __attribute__((aligned(64)));
static struct padButtonStatus pad_status;
static uint16_t current_buttons = 0;
static int pad_ready = 0;

static void load_modules(void) {
    SifLoadFileInit();
    SifLoadModule("rom0:SIO2MAN", 0, NULL);
    SifLoadModule("rom0:PADMAN", 0, NULL);
}

static void poll_gamepad(void) {
    if (!pad_ready) {
        current_buttons = 0;
        return;
    }
    int state = padGetState(0, 0);
    if (state == PAD_STATE_STABLE || state == PAD_STATE_FINDCTP1) {
        if (padRead(0, 0, &pad_status) != 0) {
            current_buttons = (uint16_t)(0xFFFF ^ pad_status.btns);
        } else {
            current_buttons = 0;
        }
    } else {
        current_buttons = 0;
    }
}

static void delay_ms(int ms) {
    for (int i = 0; i < ms; i++) {
        for (volatile int j = 0; j < 35000; j++) {
            __asm__ __volatile__("nop");
        }
    }
}

#else

static uint16_t current_buttons = 0;

static void poll_gamepad(void) {
}

static void delay_ms(int ms) {
    (void)ms;
}

#endif

#define RUNTIME_HEAP_SIZE (2 * 1024 * 1024)
static uint8_t runtime_heap[RUNTIME_HEAP_SIZE] __attribute__((aligned(16)));
static size_t runtime_heap_offset = 0;

static void *runtime_alloc(size_t size) {
    size_t aligned = (size + 15) & ~((size_t)15);
    if (runtime_heap_offset + aligned > RUNTIME_HEAP_SIZE) {
        return malloc(aligned);
    }
    void *ptr = &runtime_heap[runtime_heap_offset];
    runtime_heap_offset += aligned;
    return ptr;
}

MeloArrayObj *melo_runtime_alloc_array(int32_t capacity) {
    if (capacity < 0) capacity = 0;
    size_t need = sizeof(MeloArrayObj) + sizeof(MeloValue) * (size_t)capacity;
    MeloArrayObj *arr = (MeloArrayObj *)runtime_alloc(need);
    if (!arr) return NULL;
    arr->length = capacity;
    arr->capacity = capacity;
    for (int32_t i = 0; i < capacity; i++) {
        arr->items[i] = melo_nil();
    }
    return arr;
}

MeloObject *melo_runtime_alloc_object(int32_t struct_id, int32_t field_count) {
    if (field_count < 0) field_count = 0;
    size_t need = sizeof(MeloObject) + sizeof(MeloValue) * (size_t)field_count;
    MeloObject *obj = (MeloObject *)runtime_alloc(need);
    if (!obj) return NULL;
    obj->struct_id = struct_id;
    obj->field_count = field_count;
    for (int32_t i = 0; i < field_count; i++) {
        obj->fields[i] = melo_nil();
    }
    return obj;
}

void melo_runtime_print(MeloValue *v) {
    if (!v) return;
#if defined(MELO_PS2_TARGET) || defined(__mips__)
    switch (v->type) {
        case MELO_NIL:
            scr_printf("nil\n");
            break;
        case MELO_INT32:
            scr_printf("%d\n", (int)v->as.i32);
            break;
        case MELO_FLOAT:
            scr_printf("%f\n", (double)v->as.f32);
            break;
        case MELO_STRING:
            if (v->as.str) scr_printf("%s\n", v->as.str->data);
            break;
        default:
            scr_printf("<val>\n");
            break;
    }
#else
    switch (v->type) {
        case MELO_NIL:
            printf("nil\n");
            break;
        case MELO_INT32:
            printf("%d\n", (int)v->as.i32);
            break;
        case MELO_FLOAT:
            printf("%f\n", (double)v->as.f32);
            break;
        case MELO_STRING:
            if (v->as.str) printf("%s\n", v->as.str->data);
            break;
        default:
            printf("<val>\n");
            break;
    }
#endif
}

int melo_runtime_native_call(int native_id, MeloValue *args, int arg_count, MeloValue *out_result) {
    *out_result = melo_nil();

    switch (native_id) {
        case 0:
#if defined(MELO_PS2_TARGET) || defined(__mips__)
            scr_clear();
#endif
            break;

        case 1:
            if (arg_count >= 2 && args[0].type == MELO_INT32 && args[1].type == MELO_INT32) {
#if defined(MELO_PS2_TARGET) || defined(__mips__)
                scr_setXY(args[0].as.i32, args[1].as.i32);
#endif
            }
            break;

        case 2:
            if (arg_count >= 1 && args[0].type == MELO_INT32) {
#if defined(MELO_PS2_TARGET) || defined(__mips__)
                scr_setfontcolor((uint32_t)args[0].as.i32);
#endif
            }
            break;

        case 3:
            if (arg_count >= 1 && args[0].type == MELO_STRING && args[0].as.str) {
#if defined(MELO_PS2_TARGET) || defined(__mips__)
                scr_printf("%s", args[0].as.str->data);
#else
                printf("%s", args[0].as.str->data);
#endif
            }
            break;

        case 4:
            if (arg_count >= 1 && args[0].type == MELO_INT32) {
#if defined(MELO_PS2_TARGET) || defined(__mips__)
                scr_printf("%d", (int)args[0].as.i32);
#else
                printf("%d", (int)args[0].as.i32);
#endif
            }
            break;

        case 5:
            poll_gamepad();
            break;

        case 6:
#if defined(MELO_PS2_TARGET) || defined(__mips__)
            *out_result = melo_int((current_buttons & PAD_LEFT) ? 1 : 0);
#else
            *out_result = melo_int(0);
#endif
            break;

        case 7:
#if defined(MELO_PS2_TARGET) || defined(__mips__)
            *out_result = melo_int((current_buttons & PAD_RIGHT) ? 1 : 0);
#else
            *out_result = melo_int(0);
#endif
            break;

        case 8:
#if defined(MELO_PS2_TARGET) || defined(__mips__)
            *out_result = melo_int((current_buttons & PAD_DOWN) ? 1 : 0);
#else
            *out_result = melo_int(0);
#endif
            break;

        case 9:
#if defined(MELO_PS2_TARGET) || defined(__mips__)
            *out_result = melo_int(((current_buttons & PAD_CROSS) || (current_buttons & PAD_UP)) ? 1 : 0);
#else
            *out_result = melo_int(0);
#endif
            break;

        case 10:
#if defined(MELO_PS2_TARGET) || defined(__mips__)
            *out_result = melo_int(((current_buttons & PAD_CIRCLE) || (current_buttons & PAD_SQUARE)) ? 1 : 0);
#else
            *out_result = melo_int(0);
#endif
            break;

        case 11:
#if defined(MELO_PS2_TARGET) || defined(__mips__)
            *out_result = melo_int((current_buttons & PAD_START) ? 1 : 0);
#else
            *out_result = melo_int(0);
#endif
            break;

        case 12: {
            int ms = (arg_count >= 1 && args[0].type == MELO_INT32) ? (int)args[0].as.i32 : 16;
            if (ms < 1) ms = 1;
            delay_ms(ms);
            break;
        }

        case 13: {
            int max = (arg_count >= 1 && args[0].type == MELO_INT32) ? (int)args[0].as.i32 : 7;
            if (max <= 0) max = 1;
            static unsigned int seed = 987654321;
            seed = (seed * 1103515245 + 12345) & 0x7fffffff;
            int r = (int)(seed % (unsigned int)max);
            *out_result = melo_int(r);
            break;
        }

        default:
            break;
    }

    return 0;
}

void melo_runtime_vdot(const MeloValue *b, const MeloValue *c, MeloValue *a) {
    a->type = MELO_FLOAT;
    if (b->type == MELO_VEC4 && c->type == MELO_VEC4) {
        a->as.f32 = b->as.v4.x * c->as.v4.x + b->as.v4.y * c->as.v4.y +
                    b->as.v4.z * c->as.v4.z + b->as.v4.w * c->as.v4.w;
    } else if (b->type == MELO_VEC3 && c->type == MELO_VEC3) {
        a->as.f32 = b->as.v3.x * c->as.v3.x + b->as.v3.y * c->as.v3.y +
                    b->as.v3.z * c->as.v3.z;
    } else {
        a->as.f32 = b->as.v2.x * c->as.v2.x + b->as.v2.y * c->as.v2.y;
    }
}

void melo_runtime_vcross(const MeloValue *b, const MeloValue *c, MeloValue *a) {
    a->type = MELO_VEC3;
    float bx = b->as.v3.x, by = b->as.v3.y, bz = b->as.v3.z;
    float cx = c->as.v3.x, cy = c->as.v3.y, cz = c->as.v3.z;
    a->as.v3.x = by * cz - bz * cy;
    a->as.v3.y = bz * cx - bx * cz;
    a->as.v3.z = bx * cy - by * cx;
}

void melo_runtime_vnorm(const MeloValue *b, MeloValue *a) {
    float x = b->as.v4.x, y = b->as.v4.y;
    float z = (b->type == MELO_VEC3 || b->type == MELO_VEC4) ? b->as.v4.z : 0.0f;
    float w = (b->type == MELO_VEC4) ? b->as.v4.w : 0.0f;
    float sq = x * x + y * y + z * z + w * w;
    float inv = (sq > 1e-12f) ? (1.0f / sqrtf(sq)) : 1.0f;
    a->type = b->type;
    a->as.v4.x = x * inv;
    a->as.v4.y = y * inv;
    a->as.v4.z = z * inv;
    a->as.v4.w = w * inv;
}

void melo_runtime_vscale(const MeloValue *b, const MeloValue *c, MeloValue *a) {
    float s = (c->type == MELO_FLOAT) ? c->as.f32 : (float)c->as.i32;
    a->type = b->type;
    a->as.v4.x = b->as.v4.x * s;
    a->as.v4.y = b->as.v4.y * s;
    a->as.v4.z = b->as.v4.z * s;
    a->as.v4.w = b->as.v4.w * s;
}

#define MELO_RUNTIME_MAX_ARENAS 16
static size_t arena_offsets[MELO_RUNTIME_MAX_ARENAS];
static int arena_depth = 0;

void melo_runtime_arena_enter(void) {
    if (arena_depth < MELO_RUNTIME_MAX_ARENAS) {
        arena_offsets[arena_depth++] = runtime_heap_offset;
    }
}

void melo_runtime_arena_exit(void) {
    if (arena_depth > 0) {
        runtime_heap_offset = arena_offsets[--arena_depth];
    }
}

MeloSoAArray *melo_runtime_alloc_soa(int32_t struct_id, int32_t field_count, int32_t capacity) {
    if (capacity < 0) capacity = 0;
    if (field_count > MELO_SOA_MAX_FIELDS) field_count = MELO_SOA_MAX_FIELDS;
    MeloSoAArray *soa = (MeloSoAArray *)runtime_alloc(sizeof(MeloSoAArray));
    if (!soa) return NULL;
    soa->struct_id = struct_id;
    soa->field_count = field_count;
    soa->length = capacity;
    soa->capacity = capacity;
    for (int i = 0; i < field_count; i++) {
        size_t bytes = sizeof(float) * 4 * (size_t)capacity;
        soa->field_data[i] = (float *)runtime_alloc(bytes);
        if (soa->field_data[i]) memset(soa->field_data[i], 0, bytes);
    }
    return soa;
}

void melo_runtime_soa_get(const MeloSoAArray *soa, int32_t index, int field_idx, int field_type, MeloValue *out) {
    if (!soa || index < 0 || index >= soa->length || field_idx < 0 || field_idx >= soa->field_count) {
        *out = melo_nil();
        return;
    }
    float *data = soa->field_data[field_idx];
    int32_t cap = soa->capacity;
    if (field_type == 1) {
        *out = melo_int((int32_t)data[index]);
    } else if (field_type == 3) {
        *out = melo_vec2(data[index], data[cap + index]);
    } else if (field_type == 4) {
        *out = melo_vec3(data[index], data[cap + index], data[2 * cap + index]);
    } else if (field_type == 5) {
        *out = melo_vec4(data[index], data[cap + index], data[2 * cap + index], data[3 * cap + index]);
    } else {
        *out = melo_float(data[index]);
    }
}

void melo_runtime_soa_set(MeloSoAArray *soa, int32_t index, int field_idx, int field_type, const MeloValue *val) {
    if (!soa || index < 0 || index >= soa->length || field_idx < 0 || field_idx >= soa->field_count) {
        return;
    }
    float *data = soa->field_data[field_idx];
    int32_t cap = soa->capacity;
    if (field_type == 3 && val->type == MELO_VEC2) {
        data[index] = val->as.v2.x;
        data[cap + index] = val->as.v2.y;
    } else if (field_type == 4 && val->type == MELO_VEC3) {
        data[index] = val->as.v3.x;
        data[cap + index] = val->as.v3.y;
        data[2 * cap + index] = val->as.v3.z;
    } else if (field_type == 5 && val->type == MELO_VEC4) {
        data[index] = val->as.v4.x;
        data[cap + index] = val->as.v4.y;
        data[2 * cap + index] = val->as.v4.z;
        data[3 * cap + index] = val->as.v4.w;
    } else if (field_type == 1 && val->type == MELO_INT32) {
        data[index] = (float)val->as.i32;
    } else {
        float v = (val->type == MELO_FLOAT) ? val->as.f32 : (float)val->as.i32;
        data[index] = v;
    }
}

#define MELO_RUNTIME_VRAM_SIZE (4u * 1024u * 1024u)
#define MELO_RUNTIME_MAX_VRAM_TEX 64
static MeloVramTex vram_pool[MELO_RUNTIME_MAX_VRAM_TEX];
static int vram_pool_used = 0;
static uint32_t vram_alloc_offset = 0;

MeloVramTex *melo_runtime_vram_alloc(uint32_t width, uint32_t height) {
    if (vram_pool_used >= MELO_RUNTIME_MAX_VRAM_TEX) return NULL;
    uint32_t raw_size = width * height * 4;
    uint32_t aligned_size = (raw_size + 63) & ~63u;
    if (vram_alloc_offset + aligned_size > MELO_RUNTIME_VRAM_SIZE) return NULL;
    MeloVramTex *tex = &vram_pool[vram_pool_used++];
    tex->width = width;
    tex->height = height;
    tex->byte_size = aligned_size;
    tex->vram_offset = vram_alloc_offset;
    tex->status = MELO_VRAM_EVICTED;
    tex->bpp = 32;
    vram_alloc_offset += aligned_size;
    return tex;
}

void melo_runtime_vram_upload(MeloVramTex *tex) {
    if (tex) tex->status = MELO_VRAM_RESIDENT;
}

void melo_runtime_vram_evict(MeloVramTex *tex) {
    if (tex) tex->status = MELO_VRAM_EVICTED;
}

int melo_runtime_vram_resident(const MeloVramTex *tex) {
    return (tex && tex->status == MELO_VRAM_RESIDENT) ? 1 : 0;
}

int melo_runtime_scratch_dma_in(MeloArrayObj *dest, const MeloArrayObj *src) {
    if (!dest || !src) return -1;
    int32_t count = dest->length < src->length ? dest->length : src->length;
    for (int32_t i = 0; i < count; i++) {
        dest->items[i] = src->items[i];
    }
    return count;
}

int melo_runtime_scratch_dma_out(MeloArrayObj *dest, const MeloArrayObj *src) {
    if (!dest || !src) return -1;
    int32_t count = dest->length < src->length ? dest->length : src->length;
    for (int32_t i = 0; i < count; i++) {
        dest->items[i] = src->items[i];
    }
    return count;
}

#define MELO_RUNTIME_MAX_COROS 16
#define MELO_RUNTIME_CORO_STACK_SIZE (16 * 1024)

typedef struct {
    int status;
    int func_id;
    uint8_t *stack_mem;
    uint32_t sp;
    uint32_t fp;
    uint32_t ra;
    uint32_t s[8];
    MeloValue yield_val;
    MeloValue resume_val;
} MeloRuntimeCoro;

static MeloRuntimeCoro runtime_coros[MELO_RUNTIME_MAX_COROS];
static int runtime_coro_count = 0;
static int active_coro_id = -1;
static uint32_t main_sp = 0;
static uint32_t main_fp = 0;
static uint32_t main_ra = 0;
static uint32_t main_s[8];

int melo_runtime_coro_create(int func_id) {
    if (runtime_coro_count >= MELO_RUNTIME_MAX_COROS) return -1;
    int id = runtime_coro_count++;
    MeloRuntimeCoro *c = &runtime_coros[id];
    memset(c, 0, sizeof(MeloRuntimeCoro));
    c->status = 1;
    c->func_id = func_id;
    c->stack_mem = (uint8_t *)runtime_alloc(MELO_RUNTIME_CORO_STACK_SIZE);
    c->sp = (uint32_t)(uintptr_t)(c->stack_mem + MELO_RUNTIME_CORO_STACK_SIZE - 64);
    c->fp = c->sp;
    c->yield_val = melo_nil();
    c->resume_val = melo_nil();
    return id;
}

int melo_runtime_coro_status(int coro_id) {
    if (coro_id < 0 || coro_id >= runtime_coro_count) return 0;
    return runtime_coros[coro_id].status;
}

typedef void (*CoroFn)(void);
extern CoroFn melo_func_table[];

void melo_runtime_coro_resume(int coro_id, const MeloValue *resume_val, MeloValue *out_yield) {
    if (coro_id < 0 || coro_id >= runtime_coro_count) {
        *out_yield = melo_nil();
        return;
    }
    MeloRuntimeCoro *c = &runtime_coros[coro_id];
    if (c->status == 3) {
        *out_yield = c->yield_val;
        return;
    }
    if (resume_val) c->resume_val = *resume_val;
    int prev_coro = active_coro_id;
    active_coro_id = coro_id;
    c->status = 2;

    CoroFn fn = melo_func_table[c->func_id];

#if defined(MELO_PS2_TARGET) || defined(__mips__)
    __asm__ __volatile__(
        "sw $s0, 0(%0)\n sw $s1, 4(%0)\n sw $s2, 8(%0)\n sw $s3, 12(%0)\n"
        "sw $s4, 16(%0)\n sw $s5, 20(%0)\n sw $s6, 24(%0)\n sw $s7, 28(%0)\n"
        "sw $fp, %1\n sw $sp, %2\n la $t0, 1f\n sw $t0, %3\n"
        "lw $sp, %4\n lw $fp, %5\n lw $t0, %6\n bnez $t0, 2f\n nop\n"
        "jalr %7\n nop\n"
        "2: jr $t0\n nop\n"
        "1:\n"
        :
        : "r"(main_s), "m"(main_fp), "m"(main_sp), "m"(main_ra),
          "m"(c->sp), "m"(c->fp), "m"(c->ra), "r"(fn)
        : "$t0", "memory"
    );
#else
    if (fn) fn();
#endif

    active_coro_id = prev_coro;
    *out_yield = c->yield_val;
}

void melo_runtime_coro_yield(const MeloValue *val, MeloValue *out_resumed) {
    if (active_coro_id < 0 || active_coro_id >= runtime_coro_count) {
        if (out_resumed) *out_resumed = melo_nil();
        return;
    }
    MeloRuntimeCoro *c = &runtime_coros[active_coro_id];
    c->status = 1;
    if (val) c->yield_val = *val;

#if defined(MELO_PS2_TARGET) || defined(__mips__)
    __asm__ __volatile__(
        "sw $s0, 0(%0)\n sw $s1, 4(%0)\n sw $s2, 8(%0)\n sw $s3, 12(%0)\n"
        "sw $s4, 16(%0)\n sw $s5, 20(%0)\n sw $s6, 24(%0)\n sw $s7, 28(%0)\n"
        "sw $fp, %1\n sw $sp, %2\n la $t0, 1f\n sw $t0, %3\n"
        "lw $s0, 0(%4)\n lw $s1, 4(%4)\n lw $s2, 8(%4)\n lw $s3, 12(%4)\n"
        "lw $s4, 16(%4)\n lw $s5, 20(%4)\n lw $s6, 24(%4)\n lw $s7, 28(%4)\n"
        "lw $fp, %5\n lw $sp, %6\n lw $t0, %7\n jr $t0\n nop\n"
        "1:\n"
        :
        : "r"(c->s), "m"(c->fp), "m"(c->sp), "m"(c->ra),
          "r"(main_s), "m"(main_fp), "m"(main_sp), "m"(main_ra)
        : "$t0", "memory"
    );
#endif

    if (out_resumed) *out_resumed = c->resume_val;
}

extern void melo_user_main(void);

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

#if defined(MELO_PS2_TARGET) || defined(__mips__)
    sceSifInitRpc(0);
    init_scr();
    scr_setCursor(0);
    scr_clear();

    load_modules();

    int pinit = padInit(0);
    if (pinit != 0) {
        if (padPortOpen(0, 0, pad_dma_buf) != 0) {
            pad_ready = 1;
        }
    }
#endif

    melo_user_main();

    while (1) {
    }

    return 0;
}
