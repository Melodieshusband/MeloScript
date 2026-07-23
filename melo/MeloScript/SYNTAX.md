# MeloScript Grammar (v0.1)
Notation: EBNF-like. [] — optional, {} — zero or more repetitions, | — alternative.
```
program        = { top_level_decl } ;

top_level_decl = func_decl
                | struct_decl
                | vukernel_decl
                | var_decl ";"
                ;

annotation     = "@" IDENT [ "(" arg_list ")" ]
                | "#" "[" IDENT "]"
                ;

func_decl      = { annotation } "func" IDENT "(" [ param_list ] ")"
                 [ "->" type ] block ;

param_list     = param { "," param } ;
param          = IDENT ":" type ;

struct_decl    = { annotation } "struct" IDENT "{" { field_decl } "}" ;
field_decl     = IDENT ":" type "," ;

vukernel_decl  = "vukernel" IDENT "(" [ param_list ] ")" "->" type block ;

var_decl       = { annotation } ("var" | "let") IDENT [ ":" type ] "=" expr ;

type           = "int32" | "float" | "vec2" | "vec3" | "vec4"
                | "mat4" | "string" | "bool"
                | IDENT
                | "array" "<" type [ "," INT_LIT ] ">"
                ;

block          = "{" { statement } "}" ;

statement      = var_decl ";"
                | expr_stmt ";"
                | if_stmt
                | while_stmt
                | for_stmt
                | return_stmt ";"
                | arena_block
                | block
                ;

expr_stmt      = expr ;

if_stmt        = "if" "(" expr ")" block [ "else" (if_stmt | block) ] ;
while_stmt     = "while" "(" expr ")" block ;
for_stmt       = "for" IDENT "in" expr block ;
return_stmt    = "return" [ expr ] ;
arena_block    = "arena" [ IDENT ] block ;

expr           = assignment ;
assignment     = logic_or [ "=" assignment ] ;
logic_or       = logic_and { "||" logic_and } ;
logic_and      = equality { "&&" equality } ;
equality       = comparison { ("==" | "!=") comparison } ;
comparison     = term { ("<" | "<=" | ">" | ">=") term } ;
term           = factor { ("+" | "-") factor } ;
factor         = unary { ("*" | "/" | "%") unary } ;
unary          = ("-" | "!") unary | postfix ;
postfix        = primary { call_suffix | index_suffix | field_suffix } ;
call_suffix    = "(" [ arg_list ] ")" ;
index_suffix   = "[" expr "]" ;
field_suffix   = "." IDENT ;
arg_list       = expr { "," expr } ;

primary        = INT_LIT | FLOAT_LIT | STRING_LIT | "true" | "false"
                | IDENT
                | vec_ctor
                | array_lit
                | "(" expr ")"
                ;

vec_ctor       = ("vec2"|"vec3"|"vec4") "(" arg_list ")" ;
array_lit      = "array" "<" type [ "," INT_LIT ] ">" "(" [ arg_list ] ")" ;

```
## Lexemes
 * IDENT — [a-zA-Z_][a-zA-Z0-9_]*
 * INT_LIT — [0-9]+
 * FLOAT_LIT — [0-9]+ "." [0-9]+
 * STRING_LIT — "..." with escaping for \", \n, \t, \r, \\
 * Comments: // ... to the end of the line
## Built-in Functions
 * print(x) — prints a value of any type (int32, float, vec2/3/4, string, array) with a newline.
 * vdot(a, b) — vector dot product.
 * vcross(a, b) — vector cross product (vec3).
 * vnorm(a) — vector normalization.
 * len(a) — array length (int32).
## Arrays
Arrays are fixed-size and are stored as a single object in the VM's object heap (using the same allocator as structs), aligned to a 16-byte boundary. The size of the array is defined at compile-time and does not change at runtime—this aligns with a model free of tracing GC: one allocation, with no rebalancing or fragmentation.
Constructor:
```melo
var xs = array<int32, 3>(10, 20, 30);   // explicit size + initialization
var ys = array<int32, 4>();             // explicit size, all elements = nil/0
var zs = array<int32>(1, 2, 3, 4, 5);   // size inferred from argument count

```
If the size is explicitly specified and a non-empty list of initializers is passed, their count must match—otherwise, a compile-time error array initializer count does not match declared array size is generated.
Indexing reads/writes directly to the array elements:
```melo
xs[0] = 99;
var v = xs[0];

```
An index outside the [0, len) range results in a VM runtime error (array index out of range), rather than undefined behavior: arrays always check boundaries because unchecked memory access on the PS2 (lacking traditional user-level MMU protection) is a guaranteed way to crash the console without a sensible error message.
for x in xs { ... } compiles into a counted loop (len + indexing), without creating a hidden iterator object and without allocations within the loop body—the element is copied by value into the loop variable register on each iteration.
```melo
var total = 0;
for x in xs {
    total = total + x;
}

```
Arrays support any element type, including vec2/vec3/vec4 and declared structs:
```melo
struct Particle { x: int32 }
var ps = array<Particle, 3>(Particle(1), Particle(2), Particle(3));
for p in ps {
    print(p.x);
}

```
## @scratch — Scratchpad RAM Allocation
@scratch is an annotation on a variable declaration indicating that the value should be placed in the emulated 16 KB Scratchpad RAM (SPR) instead of the standard VM object heap. On real PS2 hardware, the SPR is mapped to a fixed address 0x70000000 and provides zero-wait-state access, unlike cached main RDRAM—thus, it is a separate, physically smaller, and faster memory region, rather than simply another flag for a standard allocator.
```melo
@scratch var model_matrices = array<int32, 4>(1, 2, 3, 4);

```
Restrictions checked at compile-time:
 * @scratch is only allowed for array initializers (array<...>(...)) and struct constructors. Scalars (int32, float, bool) and vectors (vec2/vec3/vec4) are not allocated on the heap at all (they live by value in VM registers), so using @scratch on them results in a compile-time error @scratch may only be used on array or struct variable declarations, rather than silently being a no-op.
 * The compiler does not evaluate the exact size in advance in the general case (the size of an array with struct/vec4 elements is only known at runtime), so exceeding the 16 KB limit is a **runtime error** for the VM (scratchpad exhausted (16KB SPR limit exceeded)), which stops the VM just as strictly as a standard object heap overflow.
Under the hood, @scratch modifies the allocation opcode: NEWARRAY → NEWARRAY_SCRATCH, NEWOBJECT → NEWOBJECT_SCRATCH. These operate as separate bump-allocators over an independent 16 KB scratch_mem buffer, completely isolated from the object_heap (standard var/let variables without @scratch never end up in this region, and vice versa). Both allocators align every allocation to a 16-byte boundary—just like the normal object heap.
Unlike arena { ... }, @scratch does not yet feature an automatic reset when leaving the scope—the reset is executed via an explicit call to melo_vm_scratch_reset() at the frame boundary (e.g., from the host C game loop, prior to the next frame's melo_vm_run()). This is a deliberate distinction from Memory Arenas: real SPR is most often used for a single consistent set of "hot" data for the entire frame (transformation matrices, vertex buffers) rather than for nested temporary scopes, so scope-based nesting and auto-resets are redundant here.
## VRAM Textures (4 MB eDRAM GS) — Manual Lifecycle Management
Following the memory model from the specific reference document, PS2LANG_SPEC.md, objects in the Graphics Synthesizer's video memory are **not** subject to automatic garbage collection or arena resets—real-time VRAM defragmentation is impossible on the PS2, so the language requires explicit calls at every stage of a texture's life.
Integration is implemented as a standard VM native function library (melo_stdlib_vram.c) alongside the @vram annotation on a variable declaration (patterned after @scratch), which automatically emits an upload() call immediately after allocation:
```melo
func main() -> int32 {
    var tex = vram_alloc(256, 256);  // reserves address space in the 4 MB VRAM
    upload(tex);                     // marks the texture as resident, counts bytes against the budget
    var is_res = resident(tex);      // 1 if resident
    evict(tex);                      // removes residency, frees bandwidth budget, address remains pinned
    vram_free(tex);                  // completely returns the address range to the pool (for scene/level change)
    return is_res;
}

```
Equivalent, but shorter, using @vram:
```melo
func main() -> int32 {
    @vram var tex = vram_alloc(256, 256);
    var is_res = resident(tex);
    evict(tex);
    vram_free(tex);
    return is_res;
}

```
@vram is only permitted on a variable whose initializer is a literal vram_alloc(...) call; any other initializer (a constant, a call to another function) results in a compile-time error @vram may only be used on a variable initialized with vram_alloc(...). During the codegen phase, an additional OP_NATIVECALL to upload is emitted right after the allocation, using a separate register for the argument and result—the texture variable itself is unaffected. evict()/vram_free() are still called manually, just like before: the annotation only automates the initial VRAM upload phase, not the entire lifecycle.
Memory model:
 * vram_alloc(width, height) -> vram_tex — reserves a contiguous address range in the 4 MB MELO_VRAM_SIZE pool via a free-list allocator (not a bump-allocator: it supports arbitrary vram_free ordering, unlike Scratchpad/arenas). The format is fixed to RGBA32 (4 bytes/pixel)—the most general case; indexed paletted formats for denser packing (6:1 / 3:1 from the specification) are not yet implemented. The allocation is aligned to 64 bytes (MELO_VRAM_ALIGN). Failing due to lack of space causes a runtime error VRAM exhausted (4MB eDRAM limit exceeded).
 * upload(tex) / evict(tex) — toggles the residency flag (MELO_VRAM_RESIDENT / MELO_VRAM_EVICTED) and updates the vm->vram_bytes_resident counter. The address range remains reserved for the texture in both states—this corresponds to the model where "the VRAM address is pinned for the lifetime of the asset, while data residency at that address can be toggled on/off" (e.g., for streaming high-res textures only when the camera is nearby).
 * vram_free(tex) — completely returns the address range to the free-list (implicitly calling evict first if the texture was resident). Used when changing levels/scenes, when the entire asset set changes.
 * resident(tex) -> int32 — returns 1 or 0 based on whether the texture is currently resident.
All five functions must be registered in the VM via melo_stdlib_register_vram(vm) before running—without this call, a program using vram_alloc/upload/evict/resident/vram_free will crash during the codegen phase with a call to undefined function error, much like any other unregistered native function. @vram additionally requires that upload be registered under that exact name—otherwise, codegen will fail with @vram requires the vram stdlib to be registered.
## Vectors: Component Access and vec2/vec3/vec4 Typing
vec2, vec3, and vec4 are distinct types at VM runtime (they are not aliases for vec4). Components are accessed via .x, .y, .z, .w; accessing beyond the vector's rank (e.g., .w on a vec3) is a compile-time error.
```melo
var v = vec3(1.0, 2.0, 3.0);
v.y = 99.0;
var s = v.x + v.y + v.z;

```
Constructors support both constant and arbitrary expression arguments (vec2(a + 1.0, b)). Arithmetic operations (+, -, component-wise *, vscale, vnorm) preserve the rank of the input vector: vec2 + vec2 yields a vec2, not a vec4—this follows a type system where vec2/vec3/vec4 are first-class primitives with different physical sizes, rather than the same 128-bit register with "unused" components.
## Native Host Functions (Native Function Registry)
The host (the C code embedding the VM) can register its own C functions to be called from the script by name—this acts as the foundation for a future standard library (audio, I/O, GS/DMA, etc.) without inflating the codegen for every single function:
```c
int my_native(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) { ... }

melo_codegen_compile_with_natives(prog, &mod, names, name_count);
melo_vm_register_native(&vm, "my_native", my_native);

```
The order of names passed to melo_codegen_compile_with_natives must match the registration order in melo_vm_register_native—the native function index is locked in during bytecode compilation (OP_NATIVECALL) and cannot be redefined at runtime.
## Coroutines (yield) — Cooperative Multitasking
Instead of preemptive threads (which are unavailable on the PS2 due to the absence of hardware LL/SC instructions), the language provides cooperative coroutines: context switching occurs only at explicit yield points, deterministically, and without bus locking.
```melo
func counter() -> int32 {
    yield 1;
    yield 2;
    return 3;
}

```
Each coroutine maintains an independent set of registers and a call stack, completely isolated from the main thread and other coroutines (verified by running instances of the same function in parallel). Management is handled via the host C API:
```c
int coro = melo_coro_create(&vm, func_id);
MeloValue yielded;
melo_coro_resume(&vm, coro, melo_nil(), &yielded);
melo_coro_status(&vm, coro);

```
Calling yield outside a coroutine (i.e., in the main VM thread or a nested standard function call without creating a coroutine) results in a runtime error, not a silent no-op: this safeguards against accidental attempts to suspend execution where there is no context to resume.
Current limitation: resume() does not yet have direct syntax within the language itself (await/resume(coro, val) as an expression)—coroutine management is exclusively available via the host C API (melo_coro_create/melo_coro_resume). Adding syntactic sugar to call it directly from the script is the next planned step.
## Struct-of-Arrays (#[soa])
The #[soa] annotation on a struct forces the compiler to physically store arrays of this struct as parallel columns (one float array per field), rather than as an array of distinct objects—this ensures spatial locality for the L1 cache and compatibility with 128-bit SIMD loads, as detailed in the hardware analysis.
```melo
#[soa]
struct Particle {
    x: float,
    y: float,
}

func main() -> float {
    var particles = array<Particle, 3>(
        Particle(1.0, 2.0),
        Particle(3.0, 4.0),
        Particle(5.0, 6.0)
    );
    particles[0].x = 99.0;
    return particles[1].x + particles[1].y;
}

```
Current limitations (checked at compile-time, not silently ignored):
 * Fields of a #[soa] struct must be float or int32. Vector (vec2/vec3/vec4) and nested struct fields are not yet supported—this is the most frequent extension that should be implemented next (it will require multiple columns for a single compound field).
 * Elements of a #[soa] struct array must be literal struct constructor calls (Particle(1.0, 2.0)), not arbitrary expressions—the column layout is resolved element-by-element during compile time.
 * @scratch is not yet supported for #[soa] arrays (due to different memory ownership models—merging them will be a separate task).
 * Standard (non-#[soa] annotated) struct arrays remain unaffected and continue to function as before—this has been confirmed via tests.
## vukernel — Current Implementation Status

TODO: Rewrite this section... if Melodie ever lets me go.
A vukernel compiles and **executes on the VM as a standard function** (software emulation)—this is not yet authentic VU1 microcode. The compiler additionally checks two limits specific to actual VU1 hardware, rejecting compilation with a clear error rather than allowing a runtime crash:
 * **Instruction memory**: the compiled body must not exceed 16 KB (MELO_VU1_INSTR_LIMIT).
 * **Data memory**: the number of VM registers utilized in the function, multiplied by 16 bytes (the size of a single VU register), must not exceed 16 KB (MELO_VU1_DATA_LIMIT).
```melo
vukernel add_offset(v: vec4) -> vec4 {
    return v + vec4(1.0, 1.0, 1.0, 1.0);
}
```

## Loops (`while`, `for`)

MeloScript supports standard `while` condition loops and `for..in` iteration over arrays.

```melo
// Nooo, Melodie, let me go!
var running = true;
while (running) {
    // trapped in an infinite loop
}
```

Each compiled function is tagged with an is_vukernel flag in MeloFuncInfo—this will allow hooking up a real VU1 assembly backend for these functions in the future without disrupting the rest of the codegen.
## Keywords
func var let struct vukernel arena if else while for in return true false
## Example of a Full Program
```melo
func main() {
    print("Hello World");
}

```
```melo
struct Particle {
    position: vec3,
    velocity: vec3,
}

@hot
func update(p: Particle, dt: float) -> vec3 {
    var v = p.velocity;
    return p.position + v * dt;
}

func main() {
    var a = vec4(1.0, 2.0, 3.0, 0.0);
    var b = vec4(4.0, 5.0, 6.0, 0.0);
    var d = vdot(a, b);
    return;
}

```
