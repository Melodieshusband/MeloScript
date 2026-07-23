# MeloScript

MeloScript is a custom, statically-typed programming language and toolchain
targeting homebrew development on the PlayStation 2 (Emotion Engine / MIPS
R5900). It exists because general-purpose scripting languages (Lua, Squirrel)
carry garbage collection and dynamic typing overhead that doesn't fit a
32MB, in-order, single-precision-FPU machine — see `PS2LANG_SPEC.md` for the
full hardware rationale behind every design decision in this language.

MeloScript is not a wrapper around an existing scripting VM. The lexer,
parser, bytecode compiler, register-based virtual machine, and AOT-to-C
translator are all implemented from scratch in this repository.

## Status

This project is under active development. Read this section before opening
an issue about missing functionality — most gaps below are already known
and tracked in `melo/MeloScript/SYNTAX.md`.

- Lexer, parser, and register-based bytecode VM: implemented and tested.
- `@hot` functions: translate to real C99 via the AOT path (`melo_aot.c`).
  This C output is then meant to be compiled by a real MIPS R5900 toolchain
  (see "PS2SDK dependency" below) — it has not yet been verified against
  real PS2 hardware or a cycle-accurate emulator from within this
  repository's own test suite.
- `vukernel` (VU1 kernels): currently compiles and executes as an ordinary
  function on the VM (software emulation). It is not yet real VU1 VLIW
  microcode. Compile-time instruction/data memory limit checks (4KB/16KB)
  are enforced regardless.
- `@scratch`: emulated as an isolated bump-allocated buffer inside the host
  VM process. Not yet mapped to the real Scratchpad RAM address window on
  actual hardware.
- `@vram`: manual VRAM lifecycle API (`vram_alloc`/`upload`/`evict`/
  `vram_free`) plus a `@vram` annotation that auto-emits `upload()` right
  after allocation. RGBA32 only — paletted 4-bit/8-bit texture formats are
  not implemented yet.
- `arena { }` blocks, Struct-of-Arrays (`#[soa]` — scalar fields only, not
  yet vector fields), and cooperative coroutines (`yield`, host-driven
  `resume`) are implemented and tested.
- Additional native stdlib modules for controller input (`melo_stdlib_pad`),
  timing (`melo_stdlib_time`), and audio channels (`melo_stdlib_audio`) are
  implemented in `melo/MeloScript/libs/` with their own test coverage. Each
  one compiles against real PS2SDK calls (`padPortOpen`, `padRead`, etc.)
  when built with `-DMELO_PS2_TARGET`, and against a host-side stub backend
  otherwise — the test suite always exercises the stub path, since it runs
  on a regular host compiler.

In short: the language frontend and VM are real and tested. The path from
generated C to a signed, bootable PS2 `.elf` running on real hardware still
depends on an external MIPS R5900 toolchain and has not been exercised
end-to-end inside this repo's CI.

## Repository layout

```
melo/MeloScript/   Core language: lexer, parser, codegen, VM, AOT translator,
                    VRAM stdlib, VU1 kernel checks, and the full test suite.
melo/MeloScript/libs/
                    Additional native stdlib modules (pad, time, audio),
                    each with a real PS2SDK-backed path (MELO_PS2_TARGET)
                    and a host stub path used by the test suite.
melo/bridge/       Minimal example wiring a plain C program into a PS2SDK
                    build (no MeloScript involved — a sanity-check example).
melo/bridge2/      melo2c: compiles a .melo file into a hot-path C bundle,
                    plus a minimal PS2SDK-based runtime host (melo_main.c)
                    and Makefile wiring to link a .elf.
```

## Building and running the test suite

No PS2 toolchain is required for this. Everything in `melo/MeloScript/`
builds and runs against a regular host C compiler.

```
cd melo/MeloScript
make -f Makefile.tests test
```

This builds and runs all 22 test binaries (lexer, parser, codegen, VM,
AOT, VU1 limits, arenas, scratch, SoA, structs, vectors, coroutines,
arrays, VRAM, and the pad/time/audio stdlib) and reports a pass/fail
summary.

## Building a PS2 `.elf` (requires PS2SDK)

`melo/bridge2/` contains the pipeline from a `.melo` source file to a
linked PS2 executable:

```
hello.melo -> melo2c -> hello_gen.c -> ee-gcc (PS2SDK) -> hello.elf
```

This step requires a working `mips64r5900el-ps2-elf-gcc` cross-compiler
and the PS2SDK runtime/link files, neither of which ship with this
repository. See `melo/bridge2/check_toolchain.sh` for a quick toolchain
sanity check, and `melo/bridge2/Makefile` for the build rule.

## Language reference

See `melo/MeloScript/SYNTAX.md` for the full grammar, built-in types, and
a section-by-section account of what each language feature actually does
today versus what the specification calls for.

See `PS2LANG_SPEC.md` for the hardware-driven design rationale (why there
is no `double`, why vectors are first-class primitives, why there is no
tracing garbage collector, etc).

## License

MIT [LICENSE](LICENSE).
