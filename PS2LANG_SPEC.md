# Programming Language Specification for PlayStation 2
A technical reference for designing a programming language (tentative name in the document: **PS2Lang**), tailored for creating games on the PlayStation 2. The document is intended to be read during compiler/interpreter development—each section explains a hardware limitation and derives a direct language requirement from it.
## 1. Target Hardware — Summary
| Component | Characteristic | Implication for the Language |
|---|---|---|
| CPU (EE) | MIPS R5900 @ 294 MHz, 2-way superscalar, in-order | No speculative/out-of-order execution—instruction order in the codegen matters |
| Main RAM | 32 MB RDRAM, dual-channel, 3.2 GB/s | Strict memory budget, any data structure must be compact |
| Scratchpad | 16 KB fast SRAM, tied to EE | Manual management of "hot" data, DMA back and forth |
| VU0 | 4 KB instr + 4 KB data, coprocessor (COP2), macro+micro modes | Accessible inline from main code—candidate for automatic compiler vectorization |
| VU1 | 16 KB instr + 16 KB data, micro-mode only, tied to GS | For the language—exported "kernels" loaded separately as microprograms |
| GS (Video Memory) | 4 MB eDRAM, 48 GB/s locally, but narrow EE↔GS channel | VRAM is the most scarce resource, requiring explicit control by the language |
| FPU | Single precision (float), double is emulated in software and is very slow | double as a type should be excluded or highly discouraged |
| Missing MIPS III Instructions | No LL/SC (atomic operations), no DMULT(U)/DDIV(U) (64-bit multiply/divide) | 64-bit integer arithmetic is expensive; concurrency cannot be built on hardware atomics |
| Pipeline Errata | Known bug with short loops and delay slots | Relevant for the machine code generator, not for the VM interpreter |
The source of the hardware characteristics is confirmed by several independent materials (ps2tek, psdevwiki, Emotion Engine wiki, LLVM patch notes for r5900, Copetti PS2 architecture writeup).
## 2. Strategic Decision: Interpreter or Native Compiler
Two architectural paths—determine this before writing the grammar.
### Option A — Bytecode Interpreter (recommended for starters)
 * Written once in C/C++ using the existing PS2SDK/GCC toolchain.
 * The VM does not have to fight with MIPS specifics directly—all its complexities are encapsulated at the time of writing the VM itself.
 * Easier to add features, safer, compiles quickly.
 * The performance ceiling is lower than native code, but with reasonable design (see section 4)—quite sufficient for game logic.
### Option B — Compiler to Native MIPS Code
 * Maximum speed, direct access to EE and VU0/VU1 SIMD instructions.
 * You will have to either reuse an existing backend (GCC for R5900, or the experimental LLVM patch for r5900 recently added to LLVM with soft-float support), or write your own code generator and manually bypass pipeline errata.
 * An order of magnitude more work; it only makes sense if the goal is a language that competes with C in speed.
**Recommendation:** start with Option A. Reserve the possibility of future AOT compilation of critical functions in the language design (see section 8, "hot path" annotations)—this leaves the path to Option B open, but does not block the release.
## 3. Type System
Design types based on hardware, not programmer convenience.
### 3.1 Numbers
 * **float** — 32-bit, the only floating-point type in the language. **Never introduce double** as a regular type: software emulation of double on the EE is an order of magnitude slower than hardware float.
   * If double is absolutely necessary for a rare case (e.g., error accumulation in long simulations)—move it to an explicit library function f64_accumulate(...), and not into the language syntax, so the programmer sees they are paying a price.
 * **int32** — basic integer type, completely native on the EE.
 * **int64 — avoid as a general-purpose type.** R5900 does not have DMULT/DDIV (64-bit multiplication/division), so such operations are emulated using multiple 32-bit instructions. If the type is needed (time counters, hashes)—implement it as a library structure (i64 as a pair of int32), not as a language primitive with overloaded default operators.
 * **fixed** (optional) — consider adding a fixed-point type as an alternative to float where the programmer wants guaranteed determinism (important for network code-sync, replay systems). On older hardware, fixed-point is sometimes preferable to float in terms of speed and predictability.
### 3.2 Vectors — First-Class Type, Not a Library
This is a key architectural decision that distinguishes PS2Lang from Lua/JS ports.
 * Introduce vec2, vec3, vec4 as **built-in language primitives**, not user-level structures.
 * The internal representation of vec4 is 4×float32 = 128 bits—**exactly the size of a VU register** (vf00–vf31, each 128-bit, {x,y,z,w}).
 * The compiler/VM must be able to map operations on vec4 (+, -, *, dot, cross, normalize) to:
   * in the interpreter (Option A) — to custom 128-bit EE SIMD instructions, which add/multiply groups of four 32-bit numbers with a single instruction;
   * in the future compiler (Option B) — directly to VU0 macro-mode instructions.
 * **Alignment Requirement:** all vec2/vec3/vec4 must be aligned to a quadword boundary (16 bytes). Unaligned access on the PS2 is emulated in software and is significantly slower—this is a confirmed practice in existing PS2 tutorials (the ALIGN_QW macro in standard VU0 examples). The VM/compiler must guarantee this alignment automatically during allocation; the programmer shouldn't have to think about it manually.
### 3.3 Strings
 * Minimize the use of strings in hot paths—they do not map well to an architecture geared towards numbers/vectors.
 * Strings should be immutable and interned for constants—this reduces the load on the GC and saves memory.
 * Do not make Unicode/UTF-8 the heavy default—ASCII/latin1 as the primary path, UTF-8 optional.
### 3.4 Avoid Excessive Dynamic Typing in Hot Paths
 * Fully dynamic typing (like in Lua/Python) adds overhead to every operation (runtime type checking).
 * Recommendation: **optional static typing**—the programmer can annotate types in functions marked as "hot" (see section 8), and the VM uses a specialized fast execution path for typed functions, similar to how LuaJIT accelerates typed numeric code, but without needing a JIT itself.
## 4. Memory Model
32 MB of total RAM is less than a modern smartphone spends on a single open browser tab. The language design must treat memory as a first-class limited resource, not an abstraction to be ignored.
### 4.1 Memory Management — Do Not Rely on Tracing GC
 * A classic mark-and-sweep or generational GC on 32 MB with an active game load will almost certainly cause noticeable pauses (stutter), unacceptable in a game aiming for a stable 50/60 FPS.
 * Recommended alternatives:
   1. **Reference counting** with manual cycle breaking (the programmer explicitly marks "weak" references)—more predictable in time than trace-GC, pauses are smeared over time instead of peaking.
   2. **Arenas/object pools** as the main allocation mechanism in the game loop—the programmer allocates an arena per frame/level, and all objects in it are freed at once. This is standard practice in game dev of that era and should be a first-class language construct (arena { ... } block), not a pattern on top of malloc.
   3. If GC is still needed for "cold" code (level loading, menus)—use it only outside the game loop and explicitly show the programmer where it might trigger.
### 4.2 Data Layout
 * Support **struct-of-arrays (SoA)** in the language as an alternative to array-of-structs (AoS) at the syntax level, not just as a pattern the programmer implements by hand. Example: an array of 1000 particles with x,y,z fields—SoA provides cache-efficient sequential access and naturally maps to batch SIMD operations on vec4.
 * Consider a compiler directive like #[soa] above a data structure declaration so the translator automatically generates the SoA layout from familiar AoS syntax.
### 4.3 Scratchpad as a Language-Level Concept
 * The 16 KB scratchpad is fast memory, separate from main RAM, critical for intensive computations (physics, skeleton skinning, particles).
 * Introduce an explicit variable/buffer qualifier, e.g., @scratch, which tells the compiler/VM to place and keep the data in the scratchpad for the duration of a hot function's execution, with automatic DMA copying back and forth.
 * This is a unique feature not found in any existing scripting language for the PS2 (Lua ports, JS ports don't think about this at all)—this is exactly where the language can yield real gains that regular interpreted Lua won't.
### 4.4 Working with VRAM (4 MB eDRAM) — A Separate Memory Model
GS video memory is the most bottlenecked resource in the whole system, and it should be handled differently than regular RAM.
 * **Budget:** a typical 640×480×32-bit framebuffer takes up about 1.2 MB, and often two buffers are needed (front/back)—meaning after framebuffers, little more than 1 MB might remain for textures. Ignoring this budget in language design is impossible.
 * The language must **explicitly** type VRAM objects separately from regular heap objects—for instance, a Texture type cannot be "just an object" that lives anywhere; it must have an explicit lifecycle (load/upload/evict) visible to the programmer in the language API, not hidden behind automatic GC.
 * Build support for typical PS2 VRAM saving techniques, known from homebrew development practice, into the standard library:
   * paletted textures (4-bit/8-bit indexed) instead of full-color—savings ratio up to 6:1 for 4-bit and 3:1 for 8-bit;
   * lowering the framebuffer bit depth (16-bit instead of 32-bit) as a configuration flag, not something to be re-encoded manually;
   * texture streaming as a built-in API pattern, not something every developer reinvents.
 * Since the EE↔GS channel is significantly narrower than the internal bandwidth of the GS itself, the language should encourage **batch** uploading of textures/data, rather than frequent small transfers—for example, the texture upload API should batch requests per frame by default.
## 5. Working with VU0/VU1 — A Unique Language Opportunity
This is the section that truly distinguishes a specialized PS2 language from a portable general-purpose scripting language.
### 5.1 Hardware Basics Briefly
 * **VU0** — EE coprocessor (COP2), available in two modes:
   * **macro-mode**: VU0 instructions are inserted inline into the EE instruction stream, VU0 acts as an extension of the main CPU (similar to "SIMD instructions" like SSE on x86).
   * **micro-mode**: a separate microprogram is loaded into VU0, which executes in parallel with the main EE.
 * **VU1** — works only in micro-mode, physically closer to the GS, and is typically used as a hardware vertex shader (vertex transformation, lighting, clipping) before sending primitives to the rasterizer.
 * Both VUs have 32 vector registers of 128 bits (vf00–vf31, represented as 4×float32: x,y,z,w) and 16 integer 16-bit registers (vi00–vi15), running at 150 MHz.
 * Practical observation from PS2DEV documentation: most commercial games do **not** squeeze the maximum out of VU0 because using VU0 effectively requires baking it into the game's architecture from the very beginning, rather than adding it as an afterthought.
### 5.2 What This Means for Language Design
Here you have a real chance to make a language better than existing alternatives:
 1. **Automatic vectorization in VU0 macro-mode.** Any operation on vec4/vec3 in the main code stream should default to the compiler/VM attempting to execute it as a VU0 macro-instruction, not as a sequence of scalar float operations on the main EE. The programmer shouldn't have to think about it—just writes a + b for vectors and gets hardware acceleration for free.
 2. **Explicit "kernel" blocks for VU1 micro-mode.** Since VU1 only works in micro-mode and is physically separated, introduce a top-level syntactic construct into the language, for example:
   ```
   vukernel transform_vertices(vertices: vec4[], matrix: mat4) -> vec4[] {
       // body compiles into a VU1 microprogram separately
   }
   
   ```
   Such a block compiles into a separate artifact (VU1 microprogram), and the calling code on the main EE simply prepares the data, initiates a DMA transfer to VU1 data memory, and starts execution—meaning the language should encapsulate this entire protocol (instruction upload, data upload, start, result read) behind a single keyword.
 3. **Account for VU memory size limits during compilation.** VU0 only has 4 KB instruction + 4 KB data memory, VU1 — 16 KB + 16 KB. The compiler for vukernel blocks must check at compile time that the compiled code and working data fit within these limits, and throw a clear compilation error (not a runtime crash) if exceeded.
 4. **DMA as part of the model, not an implementation detail.** The DMA controller is capable of automatically feeding VU1 with data from the main DRAM without CPU intervention, including double buffering (while VU1 processes one buffer, DMA fills another). If the language provides a high-level construct like stream for streaming data to a vukernel, the VM must generate exactly this double-buffered DMA pattern automatically—this is a standard technique from PS2 graphics pipeline documentation, and it's worth "hiding" from the programmer as an implementation detail of the language construct.
## 6. Execution Model and Performance
### 6.1 Bytecode VM — Specific Design Requirements
 * **Register-based VM, not stack-based.** Experience from LuaJIT and other high-performance interpreters shows that register-based bytecode (like in Lua 5.x) requires fewer instructions per operation than stack-based (like in classic Python/JVM bytecode before JIT)—this is important on a slow CPU without JIT compensation.
 * **Dispatch via computed goto** (where the GCC compiler for EE supports it), rather than via switch/case—this reduces interpreter dispatcher overhead on every instruction.
 * **Inline caching for field/method access**, if the language supports structured objects—saves repeated hash table lookups on every access.
 * Strictly budget the VM's own memory: bytecode instructions must be compact (e.g., fixed 32-bit instructions, like in Lua 5.x), not variable-length with parsing overhead.
### 6.2 No JIT
Already discussed earlier in the conversation—R5900 has no existing JIT backend (LuaJIT does not support this architecture, and its custom extensions make developing a new JIT unprofitable for a niche platform). Performance design must rely entirely on:
 * the efficiency of the bytecode interpreter itself (section 6.1),
 * offloading heavy numerical operations to VU0/VU1 (section 5),
 * optional AOT compilation for truly hot functions (section 8) as a JIT alternative.
### 6.3 Avoid Hidden Allocations in the Hot Path
 * No implicit boxing/unboxing of numbers on every operation (a classic problem with dynamic languages—even a simple integer gets wrapped in an object with metadata).
 * Numbers (int32, float, vec2/3/4) must always be value-types at the VM level—stored in VM registers or inline within structures, not as pointers to heap objects.
 * String concatenation, closure creation, creating new tables/arrays inside a loop running 60 times a second—these are things for which the language should explicitly encourage the "allocate once upfront, reuse" pattern, rather than creating the illusion that it's cheap.
## 7. Multitasking and Parallelism
### 7.1 No Atomic Instructions at the Hardware Level
R5900 lacks LL/SC (load-linked/store-conditional)—meaning classic lock-free data structures tied to compare-and-swap are unavailable in their usual form.
 * Do not design the language around shared-memory multithreading with locks in the spirit of POSIX threads.
 * A more suitable model for this hardware is **cooperative coroutines/fibers** within a single EE thread (deterministic context switching without data races), plus separate explicit "channels" for passing data to/from VU1 or the IOP (I/O Processor, the second CPU in the system responsible for I/O and audio).
 * If synchronization with the IOP is needed (which performs separate tasks—audio, memory cards, USB)—use existing SIF (Subsystem Interface) mechanisms at the language runtime level, don't try to implement this as language multithreading.
## 8. Performance Annotations as Part of the Syntax
Since the language is specifically aimed at games on weak hardware, it makes sense to make performance awareness an explicit part of the language, not just a code styling convention.
Recommended annotations/qualifiers:
 * @hot — the function is marked as performance-critical; the VM applies a specialized fast execution path (see 3.4), and in the future, it's a candidate for AOT compilation into native MIPS code.
 * @scratch — the variable/buffer must be located in scratchpad memory (section 4.3).
 * @vram — the object lives in GS video memory with explicit lifecycle management (section 4.4).
 * vukernel — a block of code compiles into a VU1 microprogram (section 5.2).
 * arena { ... } — a block with arena allocation, all memory is freed at once upon exiting the block (section 4.1).
This approach—explicit, code-visible annotations instead of compiler magic—reduces the risk of a programmer accidentally writing slow code and not understanding why: bottlenecks on the PS2 should be made visible at the syntax level, rather than hidden behind "under-the-hood" optimizations that might fail in unexpected cases.
## 9. Final Checklist Before Starting Implementation
 * Numeric types: int32, float — present; double as a primitive — excluded; int64 — library-based, not primitive.
 * vec2/vec3/vec4 — built-in types with guaranteed quadword alignment.
 * Memory management — arenas/refcounting instead of tracing GC in the game loop.
 * SoA data layout is available as a compiler directive.
 * @scratch qualifier for scratchpad memory is implemented in the VM.
 * VRAM objects (Texture and similar) have an explicit lifecycle API, not hidden behind automatic GC.
 * Vector operations map to VU0 macro-mode instructions by default.
 * There is a vukernel construct for VU1 micro-mode with compile-time memory limit checking (4 KB/16 KB).
 * VM — register-based bytecode, fixed instruction length, no JIT dependency.
 * No assumptions about lock-free multithreading based on hardware atomics — coroutine-based model.
 * @hot annotation is reserved in the syntax for future AOT compilation of critical functions.
## 10. Sources Used in Preparing the Document
 * ps2tek — technical documentation on PS2 internals (EE, VU, GS, memory).
 * psdevwiki — Graphics Synthesizer, VRAM/eDRAM specifications.
 * Emotion Engine (Wikipedia) — R5900 architecture, SIMD instructions.
 * Copetti.org — practical breakdown of PS2 architecture.
 * LLVM/Clang mailing list and PR #176666 — practical difficulties of compiling for r5900 (lack of LL/SC, DMULT/DDIV, soft-float).
 * lukasz.dk "An Introduction to PS2DEV" — macro/micro modes of VU0/VU1 in practice.
 * glampert.com PS2 homebrew blog — practical experience in load distribution among EE/VU0/VU1.
 * ps2-home.com forum — practical VRAM budget calculations for real scenes.
 * forums.ps2dev.org — the practical problem of Lua_Number (double vs float) on this hardware.
