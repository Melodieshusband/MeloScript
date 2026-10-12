#include "melo_vm.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

static void melo_vm_error(MeloVM *vm, const char *msg);

#if defined(__GNUC__)
#define MELO_USE_COMPUTED_GOTO 1
#else
#define MELO_USE_COMPUTED_GOTO 0
#endif

void melo_arena_init(MeloArena *a, uint8_t *mem, size_t size) {
    a->base = mem;
    a->size = size;
    a->offset = 0;
}

void *melo_arena_alloc(MeloArena *a, size_t size) {
    size_t aligned = (size + 15) & ~((size_t)15);
    if (a->offset + aligned > a->size) {
        return NULL;
    }
    void *ptr = a->base + a->offset;
    a->offset += aligned;
    return ptr;
}

void melo_arena_reset(MeloArena *a) {
    a->offset = 0;
}

void melo_vm_arena_enter(MeloVM *vm) {
    if (vm->arena_top + 1 >= MELO_ARENA_MAX_DEPTH) {
        vm->halted = 1;
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg), "arena nesting exceeds MELO_ARENA_MAX_DEPTH");
        return;
    }
    if (vm->arena_pool_used >= MELO_ARENA_POOL_COUNT) {
        vm->halted = 1;
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg), "arena pool exhausted");
        return;
    }

    int slot = vm->arena_pool_used++;
    MeloArena *a = &vm->arena_pool[slot];
    melo_arena_init(a, vm->arena_pool_mem[slot], MELO_ARENA_POOL_SIZE);

    vm->arena_top++;
    vm->arena_stack[vm->arena_top] = a;
}

void melo_vm_arena_exit(MeloVM *vm) {
    if (vm->arena_top < 0) {
        vm->halted = 1;
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg), "arena_exit without matching arena_enter");
        return;
    }

    MeloArena *a = vm->arena_stack[vm->arena_top];
    melo_arena_reset(a);
    vm->arena_top--;
    vm->arena_pool_used--;
}

void *melo_vm_arena_alloc(MeloVM *vm, size_t size) {
    if (vm->arena_top < 0) {
        return NULL;
    }
    return melo_arena_alloc(vm->arena_stack[vm->arena_top], size);
}

MeloObject *melo_vm_alloc_object(MeloVM *vm, int32_t struct_id, int32_t field_count) {
    size_t need = sizeof(MeloObject) + sizeof(MeloValue) * (size_t)field_count;
    size_t aligned = (need + 15) & ~((size_t)15);

    if (vm->object_heap_offset + aligned > MELO_OBJECT_HEAP_SIZE) {
        return NULL;
    }

    MeloObject *obj = (MeloObject *)(vm->object_heap + vm->object_heap_offset);
    vm->object_heap_offset += aligned;

    obj->struct_id = struct_id;
    obj->field_count = field_count;
    for (int32_t i = 0; i < field_count; i++) {
        obj->fields[i] = melo_nil();
    }

    return obj;
}

MeloArrayObj *melo_vm_alloc_array(MeloVM *vm, int32_t capacity) {
    if (capacity < 0) capacity = 0;
    size_t need = sizeof(MeloArrayObj) + sizeof(MeloValue) * (size_t)capacity;
    size_t aligned = (need + 15) & ~((size_t)15);

    if (vm->object_heap_offset + aligned > MELO_OBJECT_HEAP_SIZE) {
        return NULL;
    }

    MeloArrayObj *arr = (MeloArrayObj *)(vm->object_heap + vm->object_heap_offset);
    vm->object_heap_offset += aligned;

    arr->length = capacity;
    arr->capacity = capacity;
    for (int32_t i = 0; i < capacity; i++) {
        arr->items[i] = melo_nil();
    }

    return arr;
}

MeloSoAArray *melo_vm_alloc_soa_array(MeloVM *vm, int32_t struct_id, int32_t field_count, int32_t capacity) {
    if (capacity < 0) capacity = 0;
    if (field_count < 0 || field_count > MELO_SOA_MAX_FIELDS) return NULL;

    size_t header_need = sizeof(MeloSoAArray);
    size_t header_aligned = (header_need + 15) & ~((size_t)15);

    if (vm->object_heap_offset + header_aligned > MELO_OBJECT_HEAP_SIZE) {
        return NULL;
    }
    MeloSoAArray *soa = (MeloSoAArray *)(vm->object_heap + vm->object_heap_offset);
    vm->object_heap_offset += header_aligned;

    soa->struct_id = struct_id;
    soa->field_count = field_count;
    soa->length = capacity;
    soa->capacity = capacity;

    size_t column_need = sizeof(float) * (size_t)capacity;
    size_t column_aligned = (column_need + 15) & ~((size_t)15);

    for (int32_t f = 0; f < field_count; f++) {
        if (vm->object_heap_offset + column_aligned > MELO_OBJECT_HEAP_SIZE) {
            return NULL;
        }
        float *col = (float *)(vm->object_heap + vm->object_heap_offset);
        vm->object_heap_offset += column_aligned;
        for (int32_t i = 0; i < capacity; i++) col[i] = 0.0f;
        soa->field_data[f] = col;
    }

    return soa;
}

MeloObject *melo_vm_alloc_object_scratch(MeloVM *vm, int32_t struct_id, int32_t field_count) {
    size_t need = sizeof(MeloObject) + sizeof(MeloValue) * (size_t)field_count;
    size_t aligned = (need + 15) & ~((size_t)15);

    if (vm->scratch_offset + aligned > MELO_SCRATCH_SIZE) {
        return NULL;
    }

    MeloObject *obj = (MeloObject *)(vm->scratch_mem + vm->scratch_offset);
    vm->scratch_offset += aligned;
    if (vm->scratch_offset > vm->scratch_high_water) {
        vm->scratch_high_water = vm->scratch_offset;
    }

    obj->struct_id = struct_id;
    obj->field_count = field_count;
    for (int32_t i = 0; i < field_count; i++) {
        obj->fields[i] = melo_nil();
    }

    return obj;
}

MeloArrayObj *melo_vm_alloc_array_scratch(MeloVM *vm, int32_t capacity) {
    if (capacity < 0) capacity = 0;
    size_t need = sizeof(MeloArrayObj) + sizeof(MeloValue) * (size_t)capacity;
    size_t aligned = (need + 15) & ~((size_t)15);

    if (vm->scratch_offset + aligned > MELO_SCRATCH_SIZE) {
        return NULL;
    }

    MeloArrayObj *arr = (MeloArrayObj *)(vm->scratch_mem + vm->scratch_offset);
    vm->scratch_offset += aligned;
    if (vm->scratch_offset > vm->scratch_high_water) {
        vm->scratch_high_water = vm->scratch_offset;
    }

    arr->length = capacity;
    arr->capacity = capacity;
    for (int32_t i = 0; i < capacity; i++) {
        arr->items[i] = melo_nil();
    }

    return arr;
}

void melo_vm_scratch_reset(MeloVM *vm) {
    vm->scratch_offset = 0;
}

static uint32_t vram_align_up(uint32_t v) {
    return (v + (MELO_VRAM_ALIGN - 1)) & ~(MELO_VRAM_ALIGN - 1);
}

static int vram_free_list_take(MeloVM *vm, uint32_t size, uint32_t *out_offset) {
    for (int i = 0; i < vm->vram_free_count; i++) {
        MeloVramFreeBlock *b = &vm->vram_free[i];
        if (b->size < size) continue;

        *out_offset = b->offset;
        if (b->size == size) {
            vm->vram_free[i] = vm->vram_free[vm->vram_free_count - 1];
            vm->vram_free_count--;
        } else {
            b->offset += size;
            b->size -= size;
        }
        return 1;
    }
    return 0;
}

static void vram_free_list_release(MeloVM *vm, uint32_t offset, uint32_t size) {
    if (vm->vram_free_count >= MELO_VRAM_MAX_FREE) {
        return;
    }
    vm->vram_free[vm->vram_free_count].offset = offset;
    vm->vram_free[vm->vram_free_count].size = size;
    vm->vram_free_count++;
}

MeloVramTex *melo_vm_vram_alloc(MeloVM *vm, uint32_t width, uint32_t height, uint32_t bytes_per_pixel) {
    if (vm->vram_tex_used >= MELO_VRAM_MAX_TEX) {
        return NULL;
    }
    if (width == 0 || height == 0 || bytes_per_pixel == 0) {
        return NULL;
    }

    uint32_t raw_size = width * height * bytes_per_pixel;
    uint32_t aligned_size = vram_align_up(raw_size);

    uint32_t offset;
    if (!vram_free_list_take(vm, aligned_size, &offset)) {
        return NULL;
    }

    MeloVramTex *tex = &vm->vram_tex_pool[vm->vram_tex_used++];
    tex->width = width;
    tex->height = height;
    tex->byte_size = aligned_size;
    tex->vram_offset = offset;
    tex->status = MELO_VRAM_EVICTED;
    return tex;
}

int melo_vm_vram_upload(MeloVM *vm, MeloVramTex *tex) {
    if (!tex) return -1;
    if (tex->status == MELO_VRAM_RESIDENT) return 0;
    tex->status = MELO_VRAM_RESIDENT;
    vm->vram_bytes_resident += tex->byte_size;
    return 0;
}

void melo_vm_vram_evict(MeloVM *vm, MeloVramTex *tex) {
    if (!tex) return;
    if (tex->status == MELO_VRAM_EVICTED) return;
    tex->status = MELO_VRAM_EVICTED;
    vm->vram_bytes_resident -= tex->byte_size;
}

void melo_vm_vram_free(MeloVM *vm, MeloVramTex *tex) {
    if (!tex) return;
    if (tex->status == MELO_VRAM_RESIDENT) {
        melo_vm_vram_evict(vm, tex);
    }
    vram_free_list_release(vm, tex->vram_offset, tex->byte_size);
    tex->byte_size = 0;
    tex->vram_offset = 0;
}

void melo_vm_vram_reset(MeloVM *vm) {
    vm->vram_tex_used = 0;
    vm->vram_bytes_resident = 0;
    vm->vram_free_count = 1;
    vm->vram_free[0].offset = 0;
    vm->vram_free[0].size = MELO_VRAM_SIZE;
}

int melo_vm_register_native(MeloVM *vm, const char *name, MeloNativeFn fn) {
    if (vm->native_count >= MELO_MAX_NATIVES) {
        return -1;
    }
    for (int i = 0; i < vm->native_count; i++) {
        if (strcmp(vm->natives[i].name, name) == 0) {
            return -1;
        }
    }
    int id = vm->native_count++;
    snprintf(vm->natives[id].name, MELO_NATIVE_NAME_LEN, "%s", name);
    vm->natives[id].fn = fn;
    return id;
}

int melo_vm_find_native(MeloVM *vm, const char *name) {
    for (int i = 0; i < vm->native_count; i++) {
        if (strcmp(vm->natives[i].name, name) == 0) return i;
    }
    return -1;
}

int melo_vm_register_aot(MeloVM *vm, const char *func_name, MeloAotFn fn) {
    for (int i = 0; i < vm->func_count; i++) {
        if (!vm->funcs[i].is_hot) continue;
        if (strcmp(vm->funcs[i].name, func_name) != 0) continue;
        vm->funcs[i].aot_fn = fn;
        return i;
    }
    return -1;
}

int melo_coro_create(MeloVM *vm, int func_id) {
    if (func_id < 0 || func_id >= vm->func_count) {
        return -1;
    }
    if (vm->coroutine_count >= MELO_MAX_COROUTINES) {
        return -1;
    }

    int id = vm->coroutine_count++;
    MeloCoroutine *coro = &vm->coroutines[id];
    memset(coro, 0, sizeof(MeloCoroutine));
    coro->status = MELO_CORO_SUSPENDED;
    coro->entry_func_id = func_id;
    coro->frame_top = 0;
    coro->frames[0].base = 0;
    coro->pc = vm->funcs[func_id].entry;
    return id;
}

MeloCoroStatus melo_coro_status(MeloVM *vm, int coro_id) {
    if (coro_id < 0 || coro_id >= vm->coroutine_count) {
        return MELO_CORO_DEAD;
    }
    return vm->coroutines[coro_id].status;
}

int melo_coro_resume(MeloVM *vm, int coro_id, MeloValue resume_arg, MeloValue *out_yield_value) {
    if (coro_id < 0 || coro_id >= vm->coroutine_count) {
        melo_vm_error(vm, "resume of unknown coroutine");
        return -1;
    }

    MeloCoroutine *coro = &vm->coroutines[coro_id];

    if (coro->status == MELO_CORO_FINISHED || coro->status == MELO_CORO_DEAD) {
        melo_vm_error(vm, "cannot resume a finished or dead coroutine");
        return -1;
    }
    if (coro->status == MELO_CORO_RUNNING) {
        melo_vm_error(vm, "coroutine is already running");
        return -1;
    }

    int first_resume = (coro->frame_top == 0 && coro->pc == vm->funcs[coro->entry_func_id].entry);
    if (first_resume) {
        coro->reg_stack[0] = resume_arg;
    } else {
        coro->resume_value = resume_arg;
        int frame_base = coro->frames[coro->frame_top].base;
        coro->reg_stack[frame_base + coro->yield_reg] = resume_arg;
    }

    coro->status = MELO_CORO_RUNNING;
    int rc = melo_vm_run_coro(vm, coro_id);

    if (out_yield_value) {
        *out_yield_value = coro->yield_value;
    }

    if (rc == 2) {
        return 0;
    }
    if (rc != 0) {
        return -1;
    }
    return 0;
}

void melo_vm_init(MeloVM *vm, MeloInstr *code, size_t code_len) {
    memset(vm, 0, sizeof(MeloVM));
    vm->code = code;
    vm->code_base = code;
    vm->code_len = code_len;
    vm->frame_top = 0;
    vm->frames[0].base = 0;
    vm->arena_top = -1;
    vm->arena_pool_used = 0;
    vm->active_coroutine = -1;
    melo_strpool_init(&vm->strpool);

    vm->vram_tex_used = 0;
    vm->vram_bytes_resident = 0;
    vm->vram_free_count = 1;
    vm->vram_free[0].offset = 0;
    vm->vram_free[0].size = MELO_VRAM_SIZE;
}

static inline MeloVec4 vec4_add(MeloVec4 a, MeloVec4 b) {
    MeloVec4 r = { a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w };
    return r;
}

static inline MeloVec4 vec4_sub(MeloVec4 a, MeloVec4 b) {
    MeloVec4 r = { a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w };
    return r;
}

static inline MeloVec4 vec4_mul(MeloVec4 a, MeloVec4 b) {
    MeloVec4 r = { a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w };
    return r;
}

static inline float vec4_dot(MeloVec4 a, MeloVec4 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

static inline MeloVec4 vec4_scale(MeloVec4 a, float s) {
    MeloVec4 r = { a.x * s, a.y * s, a.z * s, a.w * s };
    return r;
}

static inline MeloVec4 vec4_normalize(MeloVec4 a) {
    float len = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
    if (len < 1e-8f) len = 1e-8f;
    float inv = 1.0f / len;
    MeloVec4 r = { a.x * inv, a.y * inv, a.z * inv, a.w };
    return r;
}

static inline MeloVec4 vec3_cross(MeloVec4 a, MeloVec4 b) {
    MeloVec4 r;
    r.x = a.y * b.z - a.z * b.y;
    r.y = a.z * b.x - a.x * b.z;
    r.z = a.x * b.y - a.y * b.x;
    r.w = 0.0f;
    return r;
}

static inline int melo_value_truthy(MeloValue v) {
    switch (v.type) {
        case MELO_NIL:    return 0;
        case MELO_INT32:  return v.as.i32 != 0;
        case MELO_FLOAT:  return v.as.f32 != 0.0f;
        case MELO_VEC2:   return v.as.v2.x != 0.0f || v.as.v2.y != 0.0f;
        case MELO_VEC3:   return v.as.v3.x != 0.0f || v.as.v3.y != 0.0f || v.as.v3.z != 0.0f;
        case MELO_VEC4:   return v.as.v4.x != 0.0f || v.as.v4.y != 0.0f || v.as.v4.z != 0.0f || v.as.v4.w != 0.0f;
        case MELO_STRING: return v.as.str != NULL;
        case MELO_OBJECT: return v.as.obj != NULL;
        case MELO_ARRAY:  return v.as.arr != NULL;
        case MELO_SOA_ARRAY: return v.as.soa != NULL;
        case MELO_VRAM_TEX: return v.as.vram != NULL;
    }
    return 0;
}

static inline int vec_rank(MeloType t) {
    switch (t) {
        case MELO_VEC2: return 2;
        case MELO_VEC3: return 3;
        case MELO_VEC4: return 4;
        default: return 0;
    }
}

static inline MeloType vec_type_from_rank(int rank) {
    if (rank >= 4) return MELO_VEC4;
    if (rank == 3) return MELO_VEC3;
    return MELO_VEC2;
}

static inline MeloValue vec_result(MeloType tag, MeloVec4 v4) {
    MeloValue r;
    r.type = tag;
    if (tag == MELO_VEC2) {
        r.as.v2.x = v4.x;
        r.as.v2.y = v4.y;
    } else if (tag == MELO_VEC3) {
        r.as.v3.x = v4.x;
        r.as.v3.y = v4.y;
        r.as.v3.z = v4.z;
    } else {
        r.as.v4 = v4;
    }
    return r;
}

static inline MeloVec4 vec_widen(MeloValue v) {
    MeloVec4 r = {0.0f, 0.0f, 0.0f, 0.0f};
    switch (v.type) {
        case MELO_VEC2: r.x = v.as.v2.x; r.y = v.as.v2.y; break;
        case MELO_VEC3: r.x = v.as.v3.x; r.y = v.as.v3.y; r.z = v.as.v3.z; break;
        case MELO_VEC4: r = v.as.v4; break;
        default: break;
    }
    return r;
}

static void melo_vm_error(MeloVM *vm, const char *msg) {
    vm->error = 1;
    snprintf(vm->error_msg, sizeof(vm->error_msg), "%s", msg);
    vm->halted = 1;
}

int melo_vm_run(MeloVM *vm) {
    return melo_vm_run_coro(vm, -1);
}

int melo_vm_run_coro(MeloVM *vm, int coro_id) {
    MeloValue *reg_stack_base;
    MeloFrame *frames_base;
    int *frame_top_ptr;
    MeloInstr *pc;
    MeloCoroutine *coro = NULL;
    int prev_active_coroutine = vm->active_coroutine;

    if (coro_id >= 0) {
        coro = &vm->coroutines[coro_id];
        reg_stack_base = coro->reg_stack;
        frames_base = coro->frames;
        frame_top_ptr = &coro->frame_top;
        pc = coro->pc;
        vm->active_coroutine = coro_id;
    } else {
        reg_stack_base = vm->reg_stack;
        frames_base = vm->frames;
        frame_top_ptr = &vm->frame_top;
        pc = vm->code;
        vm->active_coroutine = -1;
    }

    MeloValue *R = reg_stack_base + frames_base[*frame_top_ptr].base;
    MeloInstr *code_end = vm->code + vm->code_len;

#if MELO_USE_COMPUTED_GOTO
    static void *dispatch_table[OP_COUNT] = {
        [OP_NOP] = &&L_NOP,
        [OP_LOADK] = &&L_LOADK,
        [OP_LOADNIL] = &&L_LOADNIL,
        [OP_MOVE] = &&L_MOVE,
        [OP_ADD] = &&L_ADD,
        [OP_SUB] = &&L_SUB,
        [OP_MUL] = &&L_MUL,
        [OP_DIV] = &&L_DIV,
        [OP_MOD] = &&L_MOD,
        [OP_NEG] = &&L_NEG,
        [OP_NOT] = &&L_NOT,
        [OP_ADDI] = &&L_ADDI,
        [OP_SUBI] = &&L_SUBI,
        [OP_MULI] = &&L_MULI,
        [OP_ADDF] = &&L_ADDF,
        [OP_SUBF] = &&L_SUBF,
        [OP_MULF] = &&L_MULF,
        [OP_DIVF] = &&L_DIVF,
        [OP_VADD] = &&L_VADD,
        [OP_VSUB] = &&L_VSUB,
        [OP_VMUL] = &&L_VMUL,
        [OP_VDOT] = &&L_VDOT,
        [OP_VCROSS] = &&L_VCROSS,
        [OP_VNORM] = &&L_VNORM,
        [OP_VSCALE] = &&L_VSCALE,
        [OP_VGET] = &&L_VGET,
        [OP_VSET] = &&L_VSET,
        [OP_VZERO] = &&L_VZERO,
        [OP_EQ] = &&L_EQ,
        [OP_LT] = &&L_LT,
        [OP_LE] = &&L_LE,
        [OP_JMP] = &&L_JMP,
        [OP_JMPIF] = &&L_JMPIF,
        [OP_JMPIFNOT] = &&L_JMPIFNOT,
        [OP_CALL] = &&L_CALL,
        [OP_NATIVECALL] = &&L_NATIVECALL,
        [OP_YIELD] = &&L_YIELD,
        [OP_RET] = &&L_RET,
        [OP_RETNIL] = &&L_RETNIL,
        [OP_NEWOBJECT] = &&L_NEWOBJECT,
        [OP_GETFIELD] = &&L_GETFIELD,
        [OP_SETFIELD] = &&L_SETFIELD,
        [OP_NEWARRAY] = &&L_NEWARRAY,
        [OP_GETINDEX] = &&L_GETINDEX,
        [OP_SETINDEX] = &&L_SETINDEX,
        [OP_LEN] = &&L_LEN,
        [OP_NEWOBJECT_SCRATCH] = &&L_NEWOBJECT_SCRATCH,
        [OP_NEWARRAY_SCRATCH] = &&L_NEWARRAY_SCRATCH,
        [OP_VRAM_ALLOC] = &&L_VRAM_ALLOC,
        [OP_VRAM_UPLOAD] = &&L_VRAM_UPLOAD,
        [OP_VRAM_EVICT] = &&L_VRAM_EVICT,
        [OP_VRAM_RESIDENT] = &&L_VRAM_RESIDENT,
        [OP_NEWARRAY_SOA] = &&L_NEWARRAY_SOA,
        [OP_SOA_GETFIELD] = &&L_SOA_GETFIELD,
        [OP_SOA_SETFIELD] = &&L_SOA_SETFIELD,
        [OP_SOA_LEN] = &&L_SOA_LEN,
        [OP_ARENA_ENTER] = &&L_ARENA_ENTER,
        [OP_ARENA_EXIT] = &&L_ARENA_EXIT,
        [OP_COROCREATE] = &&L_COROCREATE,
        [OP_RESUME] = &&L_RESUME,
        [OP_CORO_STATUS] = &&L_CORO_STATUS,
        [OP_PRINT] = &&L_PRINT,
        [OP_HALT] = &&L_HALT,
    };

#define DISPATCH() do { \
        instr = *pc++; \
        goto *dispatch_table[INSTR_OP(instr)]; \
    } while (0)
    (void)code_end;

    MeloInstr instr;
    DISPATCH();

L_NOP:
    DISPATCH();

L_LOADK:
    {
        int a = INSTR_A(instr);
        int bx = INSTR_BX(instr);
        R[a] = vm->consts[bx];
    }
    DISPATCH();

L_LOADNIL:
    R[INSTR_A(instr)] = melo_nil();
    DISPATCH();

L_MOVE:
    R[INSTR_A(instr)] = R[INSTR_B(instr)];
    DISPATCH();

L_ADD:
    {
        int a = INSTR_A(instr), b = INSTR_B(instr), c = INSTR_C(instr);
        if (R[b].type == MELO_FLOAT || R[c].type == MELO_FLOAT) {
            float bv = R[b].type == MELO_FLOAT ? R[b].as.f32 : (float)R[b].as.i32;
            float cv = R[c].type == MELO_FLOAT ? R[c].as.f32 : (float)R[c].as.i32;
            R[a] = melo_float(bv + cv);
        } else {
            R[a] = melo_int(R[b].as.i32 + R[c].as.i32);
        }
    }
    DISPATCH();

L_SUB:
    {
        int a = INSTR_A(instr), b = INSTR_B(instr), c = INSTR_C(instr);
        if (R[b].type == MELO_FLOAT || R[c].type == MELO_FLOAT) {
            float bv = R[b].type == MELO_FLOAT ? R[b].as.f32 : (float)R[b].as.i32;
            float cv = R[c].type == MELO_FLOAT ? R[c].as.f32 : (float)R[c].as.i32;
            R[a] = melo_float(bv - cv);
        } else {
            R[a] = melo_int(R[b].as.i32 - R[c].as.i32);
        }
    }
    DISPATCH();

L_MUL:
    {
        int a = INSTR_A(instr), b = INSTR_B(instr), c = INSTR_C(instr);
        if (R[b].type == MELO_FLOAT || R[c].type == MELO_FLOAT) {
            float bv = R[b].type == MELO_FLOAT ? R[b].as.f32 : (float)R[b].as.i32;
            float cv = R[c].type == MELO_FLOAT ? R[c].as.f32 : (float)R[c].as.i32;
            R[a] = melo_float(bv * cv);
        } else {
            R[a] = melo_int(R[b].as.i32 * R[c].as.i32);
        }
    }
    DISPATCH();

L_DIV:
    {
        int a = INSTR_A(instr), b = INSTR_B(instr), c = INSTR_C(instr);
        float bv = R[b].type == MELO_FLOAT ? R[b].as.f32 : (float)R[b].as.i32;
        float cv = R[c].type == MELO_FLOAT ? R[c].as.f32 : (float)R[c].as.i32;
        if (cv == 0.0f) {
            melo_vm_error(vm, "division by zero");
            goto L_HALT;
        }
        R[a] = melo_float(bv / cv);
    }
    DISPATCH();

L_MOD:
    {
        int a = INSTR_A(instr), b = INSTR_B(instr), c = INSTR_C(instr);
        if (R[c].as.i32 == 0) {
            melo_vm_error(vm, "modulo by zero");
            goto L_HALT;
        }
        R[a] = melo_int(R[b].as.i32 % R[c].as.i32);
    }
    DISPATCH();

L_ADDI:
    {
        int a = INSTR_A(instr), b = INSTR_B(instr), c = INSTR_C(instr);
        R[a] = melo_int(R[b].as.i32 + R[c].as.i32);
    }
    DISPATCH();

L_SUBI:
    {
        int a = INSTR_A(instr), b = INSTR_B(instr), c = INSTR_C(instr);
        R[a] = melo_int(R[b].as.i32 - R[c].as.i32);
    }
    DISPATCH();

L_MULI:
    {
        int a = INSTR_A(instr), b = INSTR_B(instr), c = INSTR_C(instr);
        R[a] = melo_int(R[b].as.i32 * R[c].as.i32);
    }
    DISPATCH();

L_ADDF:
    {
        int a = INSTR_A(instr), b = INSTR_B(instr), c = INSTR_C(instr);
        R[a] = melo_float(R[b].as.f32 + R[c].as.f32);
    }
    DISPATCH();

L_SUBF:
    {
        int a = INSTR_A(instr), b = INSTR_B(instr), c = INSTR_C(instr);
        R[a] = melo_float(R[b].as.f32 - R[c].as.f32);
    }
    DISPATCH();

L_MULF:
    {
        int a = INSTR_A(instr), b = INSTR_B(instr), c = INSTR_C(instr);
        R[a] = melo_float(R[b].as.f32 * R[c].as.f32);
    }
    DISPATCH();

L_DIVF:
    {
        int a = INSTR_A(instr), b = INSTR_B(instr), c = INSTR_C(instr);
        if (R[c].as.f32 == 0.0f) {
            melo_vm_error(vm, "division by zero");
            goto L_HALT;
        }
        R[a] = melo_float(R[b].as.f32 / R[c].as.f32);
    }
    DISPATCH();

L_NEG:
    {
        int a = INSTR_A(instr), b = INSTR_B(instr);
        if (R[b].type == MELO_FLOAT) {
            R[a] = melo_float(-R[b].as.f32);
        } else {
            R[a] = melo_int(-R[b].as.i32);
        }
    }
    DISPATCH();

L_NOT:
    {
        int a = INSTR_A(instr), b = INSTR_B(instr);
        R[a] = melo_int(!melo_value_truthy(R[b]));
    }
    DISPATCH();

L_VADD:
    {
        int a = INSTR_A(instr), b = INSTR_B(instr), c = INSTR_C(instr);
        int rank = vec_rank(R[b].type) > vec_rank(R[c].type) ? vec_rank(R[b].type) : vec_rank(R[c].type);
        if (rank == 0) rank = 4;
        MeloVec4 sum = vec4_add(vec_widen(R[b]), vec_widen(R[c]));
        R[a] = vec_result(vec_type_from_rank(rank), sum);
    }
    DISPATCH();

L_VSUB:
    {
        int a = INSTR_A(instr), b = INSTR_B(instr), c = INSTR_C(instr);
        int rank = vec_rank(R[b].type) > vec_rank(R[c].type) ? vec_rank(R[b].type) : vec_rank(R[c].type);
        if (rank == 0) rank = 4;
        MeloVec4 diff = vec4_sub(vec_widen(R[b]), vec_widen(R[c]));
        R[a] = vec_result(vec_type_from_rank(rank), diff);
    }
    DISPATCH();

L_VMUL:
    {
        int a = INSTR_A(instr), b = INSTR_B(instr), c = INSTR_C(instr);
        int rank = vec_rank(R[b].type) > vec_rank(R[c].type) ? vec_rank(R[b].type) : vec_rank(R[c].type);
        if (rank == 0) rank = 4;
        MeloVec4 prod = vec4_mul(vec_widen(R[b]), vec_widen(R[c]));
        R[a] = vec_result(vec_type_from_rank(rank), prod);
    }
    DISPATCH();

L_VDOT:
    {
        int a = INSTR_A(instr), b = INSTR_B(instr), c = INSTR_C(instr);
        R[a] = melo_float(vec4_dot(vec_widen(R[b]), vec_widen(R[c])));
    }
    DISPATCH();

L_VCROSS:
    {
        int a = INSTR_A(instr), b = INSTR_B(instr), c = INSTR_C(instr);
        MeloValue v; v.type = MELO_VEC3;
        MeloVec4 cr = vec3_cross(vec_widen(R[b]), vec_widen(R[c]));
        v.as.v3.x = cr.x; v.as.v3.y = cr.y; v.as.v3.z = cr.z;
        R[a] = v;
    }
    DISPATCH();

L_VNORM:
    {
        int a = INSTR_A(instr), b = INSTR_B(instr);
        int rank = vec_rank(R[b].type);
        if (rank == 0) rank = 4;
        MeloVec4 norm = vec4_normalize(vec_widen(R[b]));
        R[a] = vec_result(vec_type_from_rank(rank), norm);
    }
    DISPATCH();

L_VSCALE:
    {
        int a = INSTR_A(instr), b = INSTR_B(instr), c = INSTR_C(instr);
        float s = R[c].type == MELO_FLOAT ? R[c].as.f32 : (float)R[c].as.i32;
        int rank = vec_rank(R[b].type);
        if (rank == 0) rank = 4;
        MeloVec4 scaled = vec4_scale(vec_widen(R[b]), s);
        R[a] = vec_result(vec_type_from_rank(rank), scaled);
    }
    DISPATCH();

L_VGET:
    {
        int a = INSTR_A(instr), b = INSTR_B(instr), c = INSTR_C(instr);
        int rank = vec_rank(R[b].type);
        if (rank == 0) {
            melo_vm_error(vm, "component access on a non-vector value");
            goto L_HALT;
        }
        if (c < 0 || c >= rank) {
            melo_vm_error(vm, "vector component index out of range for this vector type");
            goto L_HALT;
        }
        MeloVec4 wv = vec_widen(R[b]);
        float comp = (c == 0) ? wv.x : (c == 1) ? wv.y : (c == 2) ? wv.z : wv.w;
        R[a] = melo_float(comp);
    }
    DISPATCH();

L_VSET:
    {
        int a = INSTR_A(instr), b = INSTR_B(instr), c = INSTR_C(instr);
        int rank = vec_rank(R[a].type);
        if (rank == 0) {
            melo_vm_error(vm, "component assignment on a non-vector value");
            goto L_HALT;
        }
        if (b < 0 || b >= rank) {
            melo_vm_error(vm, "vector component index out of range for this vector type");
            goto L_HALT;
        }
        float val = R[c].type == MELO_FLOAT ? R[c].as.f32 : (float)R[c].as.i32;
        switch (rank) {
            case 2:
                if (b == 0) R[a].as.v2.x = val; else R[a].as.v2.y = val;
                break;
            case 3:
                if (b == 0) R[a].as.v3.x = val;
                else if (b == 1) R[a].as.v3.y = val;
                else R[a].as.v3.z = val;
                break;
            default:
                if (b == 0) R[a].as.v4.x = val;
                else if (b == 1) R[a].as.v4.y = val;
                else if (b == 2) R[a].as.v4.z = val;
                else R[a].as.v4.w = val;
                break;
        }
    }
    DISPATCH();

L_VZERO:
    {
        int a = INSTR_A(instr);
        int rank = INSTR_B(instr);
        MeloValue v;
        v.type = vec_type_from_rank(rank);
        v.as.v4.x = 0.0f; v.as.v4.y = 0.0f; v.as.v4.z = 0.0f; v.as.v4.w = 0.0f;
        R[a] = v;
    }
    DISPATCH();

L_EQ:
    {
        int a = INSTR_A(instr), b = INSTR_B(instr), c = INSTR_C(instr);
        int eq;
        if (R[b].type == MELO_FLOAT || R[c].type == MELO_FLOAT) {
            float bv = R[b].type == MELO_FLOAT ? R[b].as.f32 : (float)R[b].as.i32;
            float cv = R[c].type == MELO_FLOAT ? R[c].as.f32 : (float)R[c].as.i32;
            eq = (bv == cv);
        } else {
            eq = (R[b].as.i32 == R[c].as.i32);
        }
        R[a] = melo_int(eq);
    }
    DISPATCH();

L_LT:
    {
        int a = INSTR_A(instr), b = INSTR_B(instr), c = INSTR_C(instr);
        float bv = R[b].type == MELO_FLOAT ? R[b].as.f32 : (float)R[b].as.i32;
        float cv = R[c].type == MELO_FLOAT ? R[c].as.f32 : (float)R[c].as.i32;
        R[a] = melo_int(bv < cv);
    }
    DISPATCH();

L_LE:
    {
        int a = INSTR_A(instr), b = INSTR_B(instr), c = INSTR_C(instr);
        float bv = R[b].type == MELO_FLOAT ? R[b].as.f32 : (float)R[b].as.i32;
        float cv = R[c].type == MELO_FLOAT ? R[c].as.f32 : (float)R[c].as.i32;
        R[a] = melo_int(bv <= cv);
    }
    DISPATCH();

L_JMP:
    {
        int16_t offset = (int16_t)INSTR_BX(instr);
        pc = pc - 1 + offset;
    }
    DISPATCH();

L_JMPIF:
    {
        int a = INSTR_A(instr);
        int16_t offset = (int16_t)INSTR_BX(instr);
        if (melo_value_truthy(R[a])) pc = pc - 1 + offset;
    }
    DISPATCH();

L_JMPIFNOT:
    {
        int a = INSTR_A(instr);
        int16_t offset = (int16_t)INSTR_BX(instr);
        if (!melo_value_truthy(R[a])) pc = pc - 1 + offset;
    }
    DISPATCH();

L_CALL:
    {
        int result_reg = INSTR_A(instr);
        int func_id = INSTR_B(instr);
        int arg_base = INSTR_C(instr);

        if (func_id < 0 || func_id >= vm->func_count) {
            melo_vm_error(vm, "call to unknown function");
            goto L_HALT;
        }
        if (*frame_top_ptr + 1 >= MELO_MAX_CALLDEPTH) {
            melo_vm_error(vm, "call stack overflow");
            goto L_HALT;
        }

        MeloFuncInfo *fn = &vm->funcs[func_id];
        if (fn->is_hot) fn->hot_call_count++;
        int caller_base = frames_base[*frame_top_ptr].base;
        int callee_base = caller_base + arg_base;

        if (fn->aot_fn) {
            MeloValue *caller_r = reg_stack_base + caller_base;
            MeloValue *arg_r = reg_stack_base + callee_base;
            MeloValue result = fn->aot_fn(arg_r);
            caller_r[result_reg] = result;
            DISPATCH();
        }

        int reg_stack_limit = coro ? MELO_CORO_REG_STACK_SIZE : MELO_REG_STACK_SIZE;
        if (callee_base + fn->num_regs > reg_stack_limit) {
            melo_vm_error(vm, "register stack overflow");
            goto L_HALT;
        }

        (*frame_top_ptr)++;
        MeloFrame *frame = &frames_base[*frame_top_ptr];
        frame->base = callee_base;
        frame->ret_reg = caller_base + result_reg;
        frame->return_pc = pc;
        frame->func_id = func_id;

        R = reg_stack_base + callee_base;
        pc = fn->entry;
    }
    DISPATCH();

L_YIELD:
    {
        int a = INSTR_A(instr);
        if (!coro) {
            melo_vm_error(vm, "yield used outside of a coroutine");
            goto L_HALT;
        }
        coro->yield_value = R[a];
        coro->pc = pc;
        coro->yield_reg = a;
        coro->status = MELO_CORO_SUSPENDED;
        vm->active_coroutine = prev_active_coroutine;
        return 2;
    }
    DISPATCH();

L_NATIVECALL:
    {
        int result_reg = INSTR_A(instr);
        int native_id = INSTR_B(instr);
        int arg_base = INSTR_C(instr);
        uint32_t arg_count = *pc++;

        if (native_id < 0 || native_id >= vm->native_count) {
            melo_vm_error(vm, "call to unknown native function");
            goto L_HALT;
        }
        if (arg_count > MELO_MAX_NATIVE_ARGS) {
            melo_vm_error(vm, "too many arguments in native call");
            goto L_HALT;
        }

        MeloValue result = melo_nil();
        int rc = vm->natives[native_id].fn(vm, &R[arg_base], (int)arg_count, &result);
        if (rc != 0) {
            if (!vm->error) {
                melo_vm_error(vm, "native function call failed");
            }
            goto L_HALT;
        }
        R[result_reg] = result;
    }
    DISPATCH();

L_RET:
    {
        int a = INSTR_A(instr);
        MeloValue result = R[a];

        if (*frame_top_ptr == 0) {
            if (coro) {
                coro->status = MELO_CORO_FINISHED;
                coro->yield_value = result;
                vm->active_coroutine = prev_active_coroutine;
                return 0;
            }
            reg_stack_base[0] = result;
            goto L_HALT;
        }

        MeloFrame *frame = &frames_base[*frame_top_ptr];
        MeloInstr *ret_pc = frame->return_pc;
        int ret_reg = frame->ret_reg;

        (*frame_top_ptr)--;
        reg_stack_base[ret_reg] = result;
        R = reg_stack_base + frames_base[*frame_top_ptr].base;
        pc = ret_pc;
    }
    DISPATCH();

L_RETNIL:
    {
        if (*frame_top_ptr == 0) {
            if (coro) {
                coro->status = MELO_CORO_FINISHED;
                coro->yield_value = melo_nil();
                vm->active_coroutine = prev_active_coroutine;
                return 0;
            }
            reg_stack_base[0] = melo_nil();
            goto L_HALT;
        }

        MeloFrame *frame = &frames_base[*frame_top_ptr];
        MeloInstr *ret_pc = frame->return_pc;
        int ret_reg = frame->ret_reg;

        (*frame_top_ptr)--;
        reg_stack_base[ret_reg] = melo_nil();
        R = reg_stack_base + frames_base[*frame_top_ptr].base;
        pc = ret_pc;
    }
    DISPATCH();

L_NEWOBJECT:
    {
        int a = INSTR_A(instr);
        int struct_id = INSTR_B(instr);

        if (struct_id < 0 || struct_id >= vm->struct_count) {
            melo_vm_error(vm, "unknown struct id in NEWOBJECT");
            goto L_HALT;
        }

        int field_count = vm->structs[struct_id].field_count;
        MeloObject *obj = melo_vm_alloc_object(vm, struct_id, field_count);
        if (!obj) {
            melo_vm_error(vm, "object heap exhausted");
            goto L_HALT;
        }
        R[a] = melo_object(obj);
    }
    DISPATCH();

L_GETFIELD:
    {
        int a = INSTR_A(instr), b = INSTR_B(instr), c = INSTR_C(instr);
        if (R[b].type != MELO_OBJECT || R[b].as.obj == NULL) {
            melo_vm_error(vm, "field access on non-object value");
            goto L_HALT;
        }
        MeloObject *obj = R[b].as.obj;
        if (c < 0 || c >= obj->field_count) {
            melo_vm_error(vm, "field index out of range");
            goto L_HALT;
        }
        R[a] = obj->fields[c];
    }
    DISPATCH();

L_SETFIELD:
    {
        int a = INSTR_A(instr), b = INSTR_B(instr), c = INSTR_C(instr);
        if (R[a].type != MELO_OBJECT || R[a].as.obj == NULL) {
            melo_vm_error(vm, "field assignment on non-object value");
            goto L_HALT;
        }
        MeloObject *obj = R[a].as.obj;
        if (b < 0 || b >= obj->field_count) {
            melo_vm_error(vm, "field index out of range");
            goto L_HALT;
        }
        obj->fields[b] = R[c];
    }
    DISPATCH();

L_NEWARRAY:
    {
        int a = INSTR_A(instr);
        int32_t capacity = (int32_t)INSTR_BX(instr);

        if (capacity < 0) {
            melo_vm_error(vm, "negative array capacity");
            goto L_HALT;
        }

        MeloArrayObj *arr = melo_vm_alloc_array(vm, capacity);
        if (!arr) {
            melo_vm_error(vm, "object heap exhausted (array allocation)");
            goto L_HALT;
        }
        R[a] = melo_array(arr);
    }
    DISPATCH();

L_GETINDEX:
    {
        int a = INSTR_A(instr), b = INSTR_B(instr), c = INSTR_C(instr);
        if (R[b].type != MELO_ARRAY || R[b].as.arr == NULL) {
            melo_vm_error(vm, "index access on non-array value");
            goto L_HALT;
        }
        if (R[c].type != MELO_INT32) {
            melo_vm_error(vm, "array index must be int32");
            goto L_HALT;
        }
        MeloArrayObj *arr = R[b].as.arr;
        int32_t idx = R[c].as.i32;
        if (idx < 0 || idx >= arr->length) {
            melo_vm_error(vm, "array index out of range");
            goto L_HALT;
        }
        R[a] = arr->items[idx];
    }
    DISPATCH();

L_SETINDEX:
    {
        int a = INSTR_A(instr), b = INSTR_B(instr), c = INSTR_C(instr);
        if (R[a].type != MELO_ARRAY || R[a].as.arr == NULL) {
            melo_vm_error(vm, "index assignment on non-array value");
            goto L_HALT;
        }
        if (R[b].type != MELO_INT32) {
            melo_vm_error(vm, "array index must be int32");
            goto L_HALT;
        }
        MeloArrayObj *arr = R[a].as.arr;
        int32_t idx = R[b].as.i32;
        if (idx < 0 || idx >= arr->length) {
            melo_vm_error(vm, "array index out of range");
            goto L_HALT;
        }
        arr->items[idx] = R[c];
    }
    DISPATCH();

L_LEN:
    {
        int a = INSTR_A(instr), b = INSTR_B(instr);
        if (R[b].type == MELO_ARRAY && R[b].as.arr != NULL) {
            R[a] = melo_int(R[b].as.arr->length);
        } else if (R[b].type == MELO_SOA_ARRAY && R[b].as.soa != NULL) {
            R[a] = melo_int(R[b].as.soa->length);
        } else {
            melo_vm_error(vm, "len() on non-array value");
            goto L_HALT;
        }
    }
    DISPATCH();

L_NEWOBJECT_SCRATCH:
    {
        int a = INSTR_A(instr);
        int struct_id = INSTR_B(instr);

        if (struct_id < 0 || struct_id >= vm->struct_count) {
            melo_vm_error(vm, "unknown struct id in NEWOBJECT_SCRATCH");
            goto L_HALT;
        }

        int field_count = vm->structs[struct_id].field_count;
        MeloObject *obj = melo_vm_alloc_object_scratch(vm, struct_id, field_count);
        if (!obj) {
            melo_vm_error(vm, "scratchpad exhausted (16KB SPR limit exceeded)");
            goto L_HALT;
        }
        R[a] = melo_object(obj);
    }
    DISPATCH();

L_NEWARRAY_SCRATCH:
    {
        int a = INSTR_A(instr);
        int32_t capacity = (int32_t)INSTR_BX(instr);

        if (capacity < 0) {
            melo_vm_error(vm, "negative array capacity");
            goto L_HALT;
        }

        MeloArrayObj *arr = melo_vm_alloc_array_scratch(vm, capacity);
        if (!arr) {
            melo_vm_error(vm, "scratchpad exhausted (16KB SPR limit exceeded)");
            goto L_HALT;
        }
        R[a] = melo_array(arr);
    }
    DISPATCH();

L_VRAM_ALLOC:
    {
        int a = INSTR_A(instr), b = INSTR_B(instr), c = INSTR_C(instr);
        if (R[b].type != MELO_INT32 || R[c].type != MELO_INT32) {
            melo_vm_error(vm, "@vram texture width/height must be int32");
            goto L_HALT;
        }
        int32_t width = R[b].as.i32;
        int32_t height = R[c].as.i32;
        if (width <= 0 || height <= 0) {
            melo_vm_error(vm, "@vram texture dimensions must be positive");
            goto L_HALT;
        }
        MeloVramTex *tex = melo_vm_vram_alloc(vm, (uint32_t)width, (uint32_t)height, 4);
        if (!tex) {
            melo_vm_error(vm, "VRAM exhausted (4MB eDRAM limit exceeded)");
            goto L_HALT;
        }
        R[a] = melo_vram_tex(tex);
    }
    DISPATCH();

L_VRAM_UPLOAD:
    {
        int a = INSTR_A(instr);
        if (R[a].type != MELO_VRAM_TEX || R[a].as.vram == NULL) {
            melo_vm_error(vm, "upload() requires a @vram texture value");
            goto L_HALT;
        }
        melo_vm_vram_upload(vm, R[a].as.vram);
    }
    DISPATCH();

L_VRAM_EVICT:
    {
        int a = INSTR_A(instr);
        if (R[a].type != MELO_VRAM_TEX || R[a].as.vram == NULL) {
            melo_vm_error(vm, "evict() requires a @vram texture value");
            goto L_HALT;
        }
        melo_vm_vram_evict(vm, R[a].as.vram);
    }
    DISPATCH();

L_VRAM_RESIDENT:
    {
        int a = INSTR_A(instr), b = INSTR_B(instr);
        if (R[b].type != MELO_VRAM_TEX || R[b].as.vram == NULL) {
            melo_vm_error(vm, "resident() requires a @vram texture value");
            goto L_HALT;
        }
        R[a] = melo_int(R[b].as.vram->status == MELO_VRAM_RESIDENT ? 1 : 0);
    }
    DISPATCH();

L_NEWARRAY_SOA:
    {
        int a = INSTR_A(instr);
        int struct_id = INSTR_B(instr);
        int32_t capacity = (int32_t)(*pc++);

        if (struct_id < 0 || struct_id >= vm->struct_count) {
            melo_vm_error(vm, "unknown struct id in NEWARRAY_SOA");
            goto L_HALT;
        }
        if (capacity < 0) {
            melo_vm_error(vm, "negative array capacity");
            goto L_HALT;
        }

        int field_count = vm->structs[struct_id].field_count;
        MeloSoAArray *soa = melo_vm_alloc_soa_array(vm, struct_id, field_count, capacity);
        if (!soa) {
            melo_vm_error(vm, "object heap exhausted while allocating SoA array");
            goto L_HALT;
        }
        R[a] = melo_soa_array(soa);
    }
    DISPATCH();

L_SOA_GETFIELD:
    {
        int a = INSTR_A(instr), b = INSTR_B(instr), c = INSTR_C(instr);
        int field_idx = (int)(*pc++);
        int32_t index = R[c].as.i32;

        if (R[b].type != MELO_SOA_ARRAY || R[b].as.soa == NULL) {
            melo_vm_error(vm, "field access on a non-SoA-array value");
            goto L_HALT;
        }
        MeloSoAArray *soa = R[b].as.soa;
        if (index < 0 || index >= soa->length) {
            melo_vm_error(vm, "SoA array index out of bounds");
            goto L_HALT;
        }
        if (field_idx < 0 || field_idx >= soa->field_count) {
            melo_vm_error(vm, "unknown SoA field index");
            goto L_HALT;
        }
        R[a] = melo_float(soa->field_data[field_idx][index]);
    }
    DISPATCH();

L_SOA_SETFIELD:
    {
        int a = INSTR_A(instr), b = INSTR_B(instr), c = INSTR_C(instr);
        int field_idx = (int)(*pc++);
        int32_t index = R[b].as.i32;

        if (R[a].type != MELO_SOA_ARRAY || R[a].as.soa == NULL) {
            melo_vm_error(vm, "field assignment on a non-SoA-array value");
            goto L_HALT;
        }
        MeloSoAArray *soa = R[a].as.soa;
        if (index < 0 || index >= soa->length) {
            melo_vm_error(vm, "SoA array index out of bounds");
            goto L_HALT;
        }
        if (field_idx < 0 || field_idx >= soa->field_count) {
            melo_vm_error(vm, "unknown SoA field index");
            goto L_HALT;
        }
        float val = R[c].type == MELO_FLOAT ? R[c].as.f32 : (float)R[c].as.i32;
        soa->field_data[field_idx][index] = val;
    }
    DISPATCH();

L_SOA_LEN:
    {
        int a = INSTR_A(instr), b = INSTR_B(instr);
        if (R[b].type != MELO_SOA_ARRAY || R[b].as.soa == NULL) {
            melo_vm_error(vm, "len() on non-SoA-array value");
            goto L_HALT;
        }
        R[a] = melo_int(R[b].as.soa->length);
    }
    DISPATCH();

L_UNIMPL:
    melo_vm_error(vm, "opcode not yet implemented");
    goto L_HALT;

L_PRINT:
    {
        int a = INSTR_A(instr);
        MeloValue v = R[a];
        switch (v.type) {
            case MELO_NIL:
                printf("nil\n");
                break;
            case MELO_INT32:
                printf("%d\n", v.as.i32);
                break;
            case MELO_FLOAT:
                printf("%g\n", v.as.f32);
                break;
            case MELO_VEC2:
                printf("(%g, %g)\n", v.as.v2.x, v.as.v2.y);
                break;
            case MELO_VEC3:
                printf("(%g, %g, %g)\n", v.as.v3.x, v.as.v3.y, v.as.v3.z);
                break;
            case MELO_VEC4:
                printf("(%g, %g, %g, %g)\n", v.as.v4.x, v.as.v4.y, v.as.v4.z, v.as.v4.w);
                break;
            case MELO_STRING:
                printf("%s\n", v.as.str->data);
                break;
            case MELO_OBJECT:
                printf("<object>\n");
                break;
            case MELO_ARRAY:
                printf("<array len=%d>\n", v.as.arr ? v.as.arr->length : 0);
                break;
            case MELO_SOA_ARRAY:
                printf("<soa_array len=%d fields=%d>\n",
                       v.as.soa ? v.as.soa->length : 0, v.as.soa ? v.as.soa->field_count : 0);
                break;
            case MELO_VRAM_TEX:
                printf("<vram_tex %ux%u %s>\n",
                       v.as.vram ? v.as.vram->width : 0,
                       v.as.vram ? v.as.vram->height : 0,
                       (v.as.vram && v.as.vram->status == MELO_VRAM_RESIDENT) ? "resident" : "evicted");
                break;
        }
    }
    DISPATCH();

L_ARENA_ENTER:
    melo_vm_arena_enter(vm);
    if (vm->halted) return vm->error;
    DISPATCH();

L_ARENA_EXIT:
    melo_vm_arena_exit(vm);
    if (vm->halted) return vm->error;
    DISPATCH();

L_COROCREATE:
    {
        int a = INSTR_A(instr);
        int func_id = INSTR_B(instr);
        int new_coro = melo_coro_create(vm, func_id);
        if (new_coro < 0) {
            melo_vm_error(vm, "failed to create coroutine");
            goto L_HALT;
        }
        R[a] = melo_coro(new_coro);
    }
    DISPATCH();

L_RESUME:
    {
        int a = INSTR_A(instr);
        int coro_reg = INSTR_B(instr);
        int val_reg = INSTR_C(instr);

        if (R[coro_reg].type != MELO_CORO) {
            melo_vm_error(vm, "resume() requires a coroutine value");
            goto L_HALT;
        }
        int target_coro_id = R[coro_reg].as.i32;
        MeloValue resume_arg = R[val_reg];

        vm->active_coroutine = prev_active_coroutine;
        int rc = melo_coro_resume(vm, target_coro_id, resume_arg, &R[a]);
        vm->active_coroutine = coro_id >= 0 ? coro_id : -1;
        if (rc != 0) {
            goto L_HALT;
        }
    }
    DISPATCH();

L_CORO_STATUS:
    {
        int a = INSTR_A(instr);
        int coro_reg = INSTR_B(instr);
        if (R[coro_reg].type != MELO_CORO) {
            melo_vm_error(vm, "coro_status() requires a coroutine value");
            goto L_HALT;
        }
        MeloCoroStatus st = melo_coro_status(vm, R[coro_reg].as.i32);
        R[a] = melo_int((int32_t)st);
    }
    DISPATCH();

L_HALT:
    vm->halted = 1;
    return vm->error;

#else
    while (!vm->halted && pc < code_end) {
        MeloInstr instr = *pc++;
        MeloOpcode op = (MeloOpcode)INSTR_OP(instr);
        switch (op) {
            case OP_HALT:
                vm->halted = 1;
                break;
            default:
                melo_vm_error(vm, "opcode requires computed goto build");
                break;
        }
    }
    return vm->error;
#endif
}
