# MeloScript

MeloScript is a custom, statically-typed programming language and toolchain targeting homebrew development on the PlayStation 2 (Emotion Engine / MIPS R5900). It exists because general-purpose scripting languages (Lua, Squirrel) carry garbage collection and dynamic typing overhead that doesn't fit a 32MB, in-order, single-precision-FPU machine — see `PS2LANG_SPEC.md` for the full hardware rationale behind every design decision in this language.

MeloScript is not a wrapper around an existing scripting VM. The lexer, parser, bytecode compiler, register-based virtual machine, and AOT-to-C translator are all implemented from scratch in this repository.

## Status

This project is actively developed and tested end-to-end against host test suites and the PCSX2 PlayStation 2 emulator.

- Lexer, parser, and register-based bytecode VM: fully implemented and verified across 22 test suites.
- `@hot` functions: translate to real C99 via the AOT path (`melo_aot.c`) and compile directly into bootable PS2 ELF binaries using the PS2SDK MIPS R5900 GCC toolchain.
- Hardware Execution: verified on PCSX2 with `hello.elf` running on the Emotion Engine CPU, drawing to the screen and executing COP2 macro-mode vector instructions.
- VU0 Macro-Mode Lowering: vector additions, subtractions, and multiplications lower to native COP2 inline assembly instructions (`lqc2`, `sqc2`, `vadd.xyzw`, `vsub.xyzw`, `vmul.xyzw`).
- `vukernel` (VU0 & VU1 kernels):
  - Emits real 64-bit dual-issue VLIW microcode (`upper`: `vadd`, `vsub`, `vmul`, `vmove`; `lower`: `lq`, `sq`, `xtop`, `xgkick`, `eret`) and DMA packet bridges in `melo_vu1.c`.
  - Compile-time instruction and data memory limits are strictly enforced: 4KB instruction / 4KB data for `@vu0 vukernel`, and 16KB instruction / 16KB data for VU1 `vukernel`.
- `@scratch`: mapped to the real hardware Scratchpad RAM address `0x70000000` on PlayStation 2 targets with 16-byte quadword alignment, automatic out-of-bounds protection, and high-speed DMA transfers (`scratch_dma_in`, `scratch_dma_out`) with hardware data cache synchronization.
- `@vram`: explicit lifecycle management (`vram_alloc`, `vram_alloc_paletted`, `upload`, `evict`, `resident`, `vram_free`, `vram_bpp`, `vram_size`) with support for RGBA32 as well as 4-bit and 8-bit paletted textures, saving up to 6:1 VRAM bandwidth.
- `arena { }` blocks: zero-fragmentation bump allocation for per-frame and per-scene memory domains.
- Struct-of-Arrays (`#[soa]`): supports both scalar (`float`, `int32`) and vector (`vec2`, `vec3`, `vec4`) fields, storing data in cache-aligned parallel columns for SIMD streaming.
- In-Script Coroutines: cooperative multitasking with `coro_create`, `coro_resume`, and `coro_status` callable directly within scripts, running on isolated register frames without OS thread locks.
- Strict Type Safety: single-precision `float` is enforced; `double` is rejected at compile time to avoid costly software emulation on the R5900 FPU.
- Native Hardware and Math Libraries: controller input (`melo_stdlib_pad`), timers and frame synchronization (`melo_stdlib_time`), audio channels (`melo_stdlib_audio`), video memory (`melo_stdlib_vram`), and 64-bit / 16.16 fixed-point math (`melo_stdlib_math`).

## Repository Layout

```
melo/MeloScript/
    Core language: lexer, parser, codegen, VM, AOT translator,
    VRAM stdlib, VU0/VU1 microcode generator, and comprehensive test suite.
melo/MeloScript/libs/
    Native stdlib modules (pad, time, audio) with PS2SDK hardware backends
    (MELO_PS2_TARGET) and host stub backends for local testing.
melo/bridge/
    Minimal C sanity check program linked with PS2SDK.
melo/bridge2/
    melo2c compiler, PS2SDK runtime host (melo_main.c), and Makefile
    producing bootable hello.elf executables verified in PCSX2.
```

## Building and Running the Test Suite

No PS2 toolchain is required for local host testing. Everything in `melo/MeloScript/` builds and runs with standard C11 compilers:

```bash
cd melo/MeloScript
make -f Makefile.tests test
```

This compiles and runs all test suites (`test_lexer`, `test_parser`, `test_codegen`, `test_vm`, `test_call`, `test_logic`, `test_break_continue`, `test_print`, `test_native`, `test_hot`, `test_aot`, `test_vu1_aot`, `test_vukernel`, `test_arena`, `test_scratch`, `test_soa`, `test_struct`, `test_vec23`, `test_coro`, `test_array`, `test_vram`, `test_libs`) and reports 22 passed, 0 failed.

## Building a PS2 `.elf` (requires PS2SDK)

`melo/bridge2/` provides the build pipeline from `.melo` source to a bootable PlayStation 2 ELF binary:

```
hello.melo -> melo2c -> hello_gen.c -> ee-gcc (PS2SDK) -> hello.elf
```

Run make inside a PS2SDK-equipped environment:

```bash
cd melo/bridge2
make
```

The resulting `hello.elf` can be launched directly in PCSX2:

```powershell
Start-Process -FilePath "C:\Program Files\PCSX2\pcsx2-qt.exe" -WorkingDirectory "C:\Program Files\PCSX2" -ArgumentList @("-batch", "-nogui", "-elf", "hello.elf")
```

## Language Reference

See `melo/MeloScript/SYNTAX.md` for the formal grammar, built-in types, and feature documentation.

See `PS2LANG_SPEC.md` for the PlayStation 2 hardware architectural specifications and design rationale.

## License

MIT [LICENSE](LICENSE).
