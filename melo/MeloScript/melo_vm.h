#ifndef MELO_VM_H
#define MELO_VM_H

#include "melo_value.h"
#include "melo_opcodes.h"
#include "melo_strpool.h"
#include <stddef.h>

#define MELO_MAX_REGS      64
#define MELO_MAX_CALLDEPTH 32
#define MELO_MAX_CONSTS    256

typedef struct {
    uint8_t *base;
    size_t   size;
    size_t   offset;
} MeloArena;

#define MELO_ARENA_POOL_COUNT 16
#define MELO_ARENA_POOL_SIZE  (64 * 1024)
#define MELO_ARENA_MAX_DEPTH  16

typedef struct {
    int base;
    int ret_reg;
    MeloInstr *return_pc;
    int func_id;
} MeloFrame;

#define MELO_REG_STACK_SIZE 1024

#define MELO_VU1_INSTR_LIMIT (16 * 1024)
#define MELO_VU1_DATA_LIMIT  (16 * 1024)
#define MELO_VU_REG_SIZE     16

#ifndef MELO_AOT_FN_DEFINED
#define MELO_AOT_FN_DEFINED
typedef MeloValue (*MeloAotFn)(const MeloValue *args);
#endif

#define MELO_FUNC_NAME_LEN 48

typedef struct {
    MeloInstr *entry;
    int num_params;
    int num_regs;
    int is_vukernel;
    int is_hot;
    uint32_t hot_call_count;
    MeloAotFn aot_fn;
    char name[MELO_FUNC_NAME_LEN];
} MeloFuncInfo;

#define MELO_MAX_FUNCS 128

#define MELO_MAX_NATIVES 64
#define MELO_MAX_NATIVE_ARGS 8
#define MELO_NATIVE_NAME_LEN 48

typedef struct MeloVM MeloVM;

typedef int (*MeloNativeFn)(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result);

typedef struct {
    char name[MELO_NATIVE_NAME_LEN];
    MeloNativeFn fn;
} MeloNativeEntry;

#define MELO_MAX_STRUCTS      32
#define MELO_MAX_STRUCT_FIELDS 16

typedef struct {
    char field_names[MELO_MAX_STRUCT_FIELDS][32];
    int field_count;
    int is_soa;
} MeloStructInfo;

#define MELO_OBJECT_HEAP_SIZE (128 * 1024)

#define MELO_SCRATCH_SIZE (16 * 1024)

#define MELO_VRAM_SIZE      (4u * 1024u * 1024u)
#define MELO_VRAM_MAX_TEX   256
#define MELO_VRAM_MAX_FREE  256
#define MELO_VRAM_ALIGN     64u

typedef struct {
    uint32_t offset;
    uint32_t size;
} MeloVramFreeBlock;

#define MELO_MAX_COROUTINES 16
#define MELO_CORO_REG_STACK_SIZE 256

typedef enum {
    MELO_CORO_DEAD = 0,
    MELO_CORO_SUSPENDED,
    MELO_CORO_RUNNING,
    MELO_CORO_FINISHED
} MeloCoroStatus;

typedef struct {
    MeloCoroStatus status;

    MeloValue reg_stack[MELO_CORO_REG_STACK_SIZE];
    MeloFrame frames[MELO_MAX_CALLDEPTH];
    int frame_top;

    MeloInstr *pc;
    int entry_func_id;

    MeloValue yield_value;
    MeloValue resume_value;
} MeloCoroutine;

struct MeloVM {
    MeloInstr *code;
    size_t code_len;

    MeloValue consts[MELO_MAX_CONSTS];
    size_t const_count;

    MeloStringPool strpool;

    MeloFuncInfo funcs[MELO_MAX_FUNCS];
    int func_count;

    MeloStructInfo structs[MELO_MAX_STRUCTS];
    int struct_count;

    MeloNativeEntry natives[MELO_MAX_NATIVES];
    int native_count;

    MeloValue reg_stack[MELO_REG_STACK_SIZE];

    MeloFrame frames[MELO_MAX_CALLDEPTH];
    int frame_top;

    __attribute__((aligned(16))) uint8_t arena_pool_mem[MELO_ARENA_POOL_COUNT][MELO_ARENA_POOL_SIZE];
    MeloArena arena_pool[MELO_ARENA_POOL_COUNT];
    int arena_pool_used;

    MeloArena *arena_stack[MELO_ARENA_MAX_DEPTH];
    int arena_top;

    __attribute__((aligned(16))) uint8_t object_heap[MELO_OBJECT_HEAP_SIZE];
    size_t object_heap_offset;

    __attribute__((aligned(16))) uint8_t scratch_mem[MELO_SCRATCH_SIZE];
    size_t scratch_offset;
    size_t scratch_high_water;

    MeloVramTex vram_tex_pool[MELO_VRAM_MAX_TEX];
    int vram_tex_used;
    uint32_t vram_bytes_resident;

    MeloVramFreeBlock vram_free[MELO_VRAM_MAX_FREE];
    int vram_free_count;

    MeloCoroutine coroutines[MELO_MAX_COROUTINES];
    int coroutine_count;
    int active_coroutine;

    int halted;
    int error;
    char error_msg[128];
};

void melo_arena_init(MeloArena *a, uint8_t *mem, size_t size);
void *melo_arena_alloc(MeloArena *a, size_t size);
void melo_arena_reset(MeloArena *a);

void  melo_vm_arena_enter(MeloVM *vm);
void  melo_vm_arena_exit(MeloVM *vm);
void *melo_vm_arena_alloc(MeloVM *vm, size_t size);

MeloObject *melo_vm_alloc_object(MeloVM *vm, int32_t struct_id, int32_t field_count);
MeloArrayObj *melo_vm_alloc_array(MeloVM *vm, int32_t capacity);
MeloSoAArray *melo_vm_alloc_soa_array(MeloVM *vm, int32_t struct_id, int32_t field_count, int32_t capacity);

MeloObject *melo_vm_alloc_object_scratch(MeloVM *vm, int32_t struct_id, int32_t field_count);
MeloArrayObj *melo_vm_alloc_array_scratch(MeloVM *vm, int32_t capacity);
void melo_vm_scratch_reset(MeloVM *vm);

MeloVramTex *melo_vm_vram_alloc(MeloVM *vm, uint32_t width, uint32_t height, uint32_t bytes_per_pixel);
int  melo_vm_vram_upload(MeloVM *vm, MeloVramTex *tex);
void melo_vm_vram_evict(MeloVM *vm, MeloVramTex *tex);
void melo_vm_vram_free(MeloVM *vm, MeloVramTex *tex);
void melo_vm_vram_reset(MeloVM *vm);

void melo_vm_init(MeloVM *vm, MeloInstr *code, size_t code_len);
int  melo_vm_run(MeloVM *vm);
int  melo_vm_run_coro(MeloVM *vm, int coro_id);

int melo_vm_register_native(MeloVM *vm, const char *name, MeloNativeFn fn);
int melo_vm_find_native(MeloVM *vm, const char *name);

int melo_vm_register_aot(MeloVM *vm, const char *func_name, MeloAotFn fn);

int melo_coro_create(MeloVM *vm, int func_id);
int melo_coro_resume(MeloVM *vm, int coro_id, MeloValue resume_arg, MeloValue *out_yield_value);
MeloCoroStatus melo_coro_status(MeloVM *vm, int coro_id);

#endif
