# MeloScript Grammar and Specification

MeloScript is a statically-typed language designed for PlayStation 2 homebrew game development.

## Grammar

```ebnf
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

vukernel_decl  = { annotation } "vukernel" IDENT "(" [ param_list ] ")" "->" type block ;

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
                | yield_stmt ";"
                | arena_block
                | block
                ;

expr_stmt      = expr ;

if_stmt        = "if" "(" expr ")" block [ "else" (if_stmt | block) ] ;
while_stmt     = "while" "(" expr ")" block ;
for_stmt       = "for" IDENT "in" expr block ;
return_stmt    = "return" [ expr ] ;
yield_stmt     = "yield" [ expr ] ;
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

- IDENT: `[a-zA-Z_][a-zA-Z0-9_]*`
- INT_LIT: `[0-9]+`
- FLOAT_LIT: `[0-9]+.[0-9]+`
- STRING_LIT: `"..."` with escaping for `\"`, `\n`, `\t`, `\r`, `\\`

## Numeric Types and Float Guarantee

- `int32`: 32-bit signed integer native to the Emotion Engine CPU.
- `float`: 32-bit single-precision IEEE 754 floating point.
- `double`: prohibited at compile time. The Emotion Engine R5900 hardware only supports single-precision floating point. Using `double` causes a compile-time error to prevent unintentional software emulation slowdowns.

## Vectors

Vectors are first-class language primitives aligned to 16-byte quadword boundaries:

- `vec2`: 2-component float vector (.x, .y).
- `vec3`: 3-component float vector (.x, .y, .z).
- `vec4`: 4-component float vector (.x, .y, .z, .w) matching the 128-bit VU register width.

Component access outside of the declared vector rank produces a compile-time error.
Arithmetic operations on vectors preserve rank. On PlayStation 2 targets, vector addition, subtraction, and multiplication lower to native COP2 instructions: `lqc2`, `sqc2`, `vadd.xyzw`, `vsub.xyzw`, `vmul.xyzw`.

Built-in vector functions:
- `vdot(a, b)`: vector dot product.
- `vcross(a, b)`: vector cross product (`vec3`).
- `vnorm(a)`: vector normalization.

```melo
var v = vec3(1.0, 2.0, 3.0);
v.y = 99.0;
var len = vnorm(v);
```

## Arrays

Arrays are fixed-size and stored as a single contiguous object aligned to 16 bytes:

```melo
var xs = array<int32, 3>(10, 20, 30);
var ys = array<int32, 4>();
var zs = array<int32>(1, 2, 3, 4, 5);
```

Boundary checking is enforced at runtime. Iteration uses counted loops without hidden iterator allocations:

```melo
var total = 0;
for x in xs {
    total = total + x;
}
```

## In-Script Coroutines

Cooperative multitasking without thread locking or kernel overhead:

- `coro_create(fn)`: creates a coroutine context with its own call stack and register frame.
- `coro_resume(c, [arg])`: resumes execution up to the next `yield` or `return`.
- `coro_status(c)`: returns current state (0 = finished, 1 = suspended, 2 = running).

```melo
func worker() -> int32 {
    yield 10;
    yield 20;
    return 30;
}

func main() -> int32 {
    var c = coro_create(worker);
    var v1 = coro_resume(c);
    var v2 = coro_resume(c);
    var v3 = coro_resume(c);
    return v1 + v2 + v3;
}
```

## Scratchpad RAM (@scratch)

Places data in the 16 KB fast Scratchpad RAM (SPR):
- On PlayStation 2 hardware targets, addresses map to `0x70000000`.
- All allocations are quadword-aligned (16 bytes).
- Exceeding the 16 KB limit triggers an immediate runtime error.
- `scratch_dma_in(dest_scratch, src_ram)`: transfers elements from main RAM into Scratchpad RAM with hardware cache flush.
- `scratch_dma_out(dest_ram, src_scratch)`: transfers elements from Scratchpad RAM back to main RAM with cache synchronization.

```melo
var ram_data = array<int32, 4>(10, 20, 30, 40);
@scratch var spr = array<int32, 4>();
scratch_dma_in(spr, ram_data);
spr[0] = 99;
scratch_dma_out(ram_data, spr);
```

## 64-bit Integer and Fixed-Point Math

Provides deterministic arithmetic and 64-bit operations without slow CPU division or multiplication emulation:

- `i64_add_lo(a_hi, a_lo, b_hi, b_lo)` / `i64_add_hi(...)`: 64-bit integer addition.
- `i64_sub_lo(...)` / `i64_sub_hi(...)`: 64-bit integer subtraction.
- `i64_mul_lo(a_lo, b_lo)` / `i64_mul_hi(a_lo, b_lo)`: 32x32 -> 64-bit multiplication.
- `i64_cmp(a_hi, a_lo, b_hi, b_lo)`: 64-bit integer comparison.
- `fix_from_float(f)` / `fix_to_float(x)`: converts between float and 16.16 fixed-point format.
- `fix_mul(a, b)` / `fix_div(a, b)`: deterministic 16.16 fixed-point operations.
- `clamp(val, min, max)`, `lerp(a, b, t)`, `fmin(a, b)`, `fmax(a, b)`, `fabs_val(a)`: common game math routines.

## Video Memory Management (@vram)

Explicit lifecycle for textures in 4 MB GS eDRAM:
- `vram_alloc(width, height, [bpp])`: reserves contiguous VRAM space.
- `vram_alloc_paletted(width, height, bpp)`: allocates 4-bit (16 colors) or 8-bit (256 colors) paletted textures, saving up to 6:1 VRAM bandwidth.
- `upload(tex)`: marks texture as resident and accounts for bandwidth.
- `resident(tex)`: queries residency status (1 or 0).
- `evict(tex)`: frees resident bandwidth while keeping the VRAM address reserved.
- `vram_free(tex)`: releases the VRAM address back to the free-list pool.
- `vram_bpp(tex)`: returns texture bits per pixel (4, 8, 16, or 32).
- `vram_size(tex)`: returns texture byte size.

```melo
@vram var tex = vram_alloc_paletted(128, 128, 8);
var is_res = resident(tex);
evict(tex);
vram_free(tex);
```

## Struct-of-Arrays (#[soa])

Stores arrays of structs in parallel column buffers to optimize L1 data cache and enable 128-bit SIMD loads:

```melo
#[soa]
struct Particle {
    pos: vec3,
    vel: vec3,
    color: int32,
}

func main() -> float {
    var particles = array<Particle, 2>(
        Particle(vec3(1.0, 2.0, 3.0), vec3(0.1, 0.2, 0.3), 255),
        Particle(vec3(4.0, 5.0, 6.0), vec3(0.4, 0.5, 0.6), 128)
    );
    particles[0].pos.x = 99.0;
    return particles[0].pos.x + particles[1].pos.y;
}
```

## Vector Unit Microprograms (vukernel)

`vukernel` defines microprograms for VU0 or VU1:
- `@vu0 vukernel`: enforced compile-time limit of 4 KB instruction memory and 4 KB data memory.
- `vukernel` (or `@vu1 vukernel`): enforced compile-time limit of 16 KB instruction memory and 16 KB data memory.
- Compiles to 64-bit VLIW microcode instructions dual-issuing upper and lower operations with automatic E-flag and DMA packet generation.

```melo
@vu0
vukernel vu0_add(v: vec4) -> vec4 {
    return v + vec4(1.0, 1.0, 1.0, 1.0);
}
```

## Performance Annotations

- `@hot`: marks performance-critical functions for AOT compilation into native C99 for the MIPS R5900 cross-compiler.
- `@scratch`: forces variable allocation to Scratchpad RAM (`0x70000000`).
- `@vram`: automatically emits `upload()` on texture allocation.
- `@vu0`: targets the VU0 coprocessor and checks 4 KB memory limits.
- `arena`: scopes bump memory allocations that are cleared simultaneously on block exit.
