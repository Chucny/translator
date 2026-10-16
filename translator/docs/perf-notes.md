# Performance notes

Measured on 2026-09-14 on this machine (Snapdragon 8 Elite, Oryon cores). zbrun is a
RelWithDebInfo build; guests are built with NDK r29 `-O2` for armeabi-v7a (softfp, VFP).

## bench_dynamic (guest/tests/bench_dynamic.c)

| workload | native aarch64 | arm32 under zbrun | ratio |
|---|---|---|---|
| integer_mix (200M xorshift iterations) | 0.61 s | 1.20-1.30 s | ~2x |
| sieve (20M) | 0.15 s | 0.44-0.47 s | ~3x |
| floating (100M `1/(i*i)` in double) | 0.14 s | 3.0-3.8 s | ~22-26x |
| memcpy (2 GB in 1 MB blocks) | 0.05 s | 0.18-0.22 s | ~3.5x |
| total | 0.96 s | 4.9-5.1 s | ~5x |

With precise faults (`ZB_PRECISE_FAULTS=1`), integer_mix takes 2.5 s: GetSetElimination is
off in that mode.

## fpmicro_dynamic (guest/tests/fpmicro_dynamic.c), 50M iterations each

| loop body | native | zbrun | ratio |
|---|---|---|---|
| double add | 0.10 s | 0.60 s | 6x |
| double mul | 0.11 s | 1.24 s | 12x |
| double div | 0.21 s | 1.58 s | 7x |
| int32 -> double | 0.06 s | 0.69 s | 11x |
| float add | 0.06 s | 1.43 s | 23x |
| float mul | 0.08 s | 1.41 s | 17x |
| float div | 0.17 s | 1.64 s | 10x |

The arm32 loops are three instructions: `vadd.f64 d16,d16,d17; subs r0,r0,#1; bne`.

## What is known and suspected about floating point

- **Unsafe FP optimizations do nothing here.** `unsafe_optimizations` +
  `Unsafe_InaccurateNaN` + `Unsafe_IgnoreStandardFPCRValue` made no measurable
  difference, so NaN fixups are not the cost.
- **The arithmetic itself is native.** The Dynarmic arm64 backend emits `FADD`/`FMUL`/
  `FDIV` directly.
- **Suspected cost 1: FPSR/FPCR switching.** `EmitThreeOp` calls `ctx.fpsr.Load()`, and
  guest FPSR/FPCR state is swapped into and out of the host system registers around
  translated blocks. A three-instruction loop pays that on every block entry and exit.
- **Suspected cost 2: VFP register synchronization.** Guest VFP registers are loaded from
  and stored to the JIT state per block. Single-precision S registers alias halves of D
  registers, which may explain why float is slower than double.
- **Next steps.** Profile with `perf` (not installed here) or with a counting build of
  Dynarmic. Compare with block linking disabled, and look at `fpsr_manager.cpp` and the
  A32 `GetExtendedRegister`/`SetExtendedRegister` emitters.

2D games do use floats for positions and transforms, so this matters before Phase 5.
