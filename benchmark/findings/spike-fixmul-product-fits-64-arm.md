# VERDICT: LEARNED

## Hypothesis

`fixMul` always widens to 128 bits. On arm64 that is `MUL` + `SMULH`; when
`|a|*|b| + 2^15 < 2^63` a single 64-bit multiply, add and shift by 16 is
bit-identical, and `clz(|a|) + clz(|b|) >= 65` is a cheap sufficient gate.
Inverse-scale times ordinary values (`~2^40 * 2^16..2^24`) hits this gate.

## The change

A leading-zero gate in `fixMul` (include/fixed/fixed.h): compute
`clz(|a|) + clz(|b|)`; when it is `>= 65` return
`(fixed_t)((int64_t)((uint64_t)(a*b) + FIX_HALF) >> 16)`, otherwise fall through
to the unchanged 128-bit widening multiply. Rounding (round-half-up), wrap
behaviour and `FIX_SATURATE` are untouched (the fast path's result is always
inside `[-2^47, 2^47]`, so saturation never fires there). `fixClz64` uses
`__builtin_clzll` on gcc/clang and `_BitScanReverse64` on plain MSVC.

## Before/after (ns/op, median of 7 interleaved repeats, 200000 iters)

Only x86-64 was available (AMD Ryzen 7 255, avx2/avx512f, cc 15.2.0,
`-O3 -g -DNDEBUG -ffp-contract=off`). **arm64 was not measured: no Apple-silicon
bench is reachable from this box.** `speedup = a_median / b_median`; `> 1` means
the candidate is faster.

| op | mode | native a->b | speedup | emulated a->b | speedup |
| --- | --- | ---: | ---: | ---: | ---: |
| fixMul | scalar | 1.665 -> 3.919 | 0.425 | 3.532 -> 5.984 | 0.590 |
| fixMul | batch | 1.238 -> 2.680 | 0.462 | 2.873 -> 3.718 | 0.773 |
| fixDot | scalar | 2.087 -> 2.066 | 1.010 | 6.837 -> 6.721 | 1.017 |
| fixDot | batch | 2.228 -> 2.221 | 1.003 | 6.937 -> 6.965 | 0.996 |
| fixCross | scalar | 4.673 -> 4.271 | 1.094 | 52.095 -> 18.763 | 2.776 |
| fixCross | batch | 7.702 -> 9.164 | 0.840 | 50.782 -> 20.070 | 2.530 |
| fixRotateVector | scalar | 12.205 -> 21.903 | 0.557 | 130.154 -> 112.353 | 1.158 |
| fixRotateVector | batch | 12.735 -> 14.945 | 0.852 | 129.768 -> 84.795 | 1.530 |

## Bit-exact evidence

- `ctest --test-dir build --output-on-failure`: **36/36 pass**, native and
  emulated arms, frozen hashes unchanged (exhaustive sweep hash
  `0x5a4aac80ad1b6c5d`).
- `FixMulFits64ExhaustiveTest` (new section I in test/exhaustive_test.c): full
  structured grid, 100M seeded pairs, and the gate-boundary products, all equal
  to the compiler's own `__int128` reference. Passes.
- `benchmark/ab.sh` oracle (candidate): `ORACLE: all operations match benchmark/oracle-baseline.txt (base_random=50000000)`. `fixMul` hash
  `eb1995a6b21bf036` unchanged.

## What the experiment taught

The gate is correct and bit-exact, but the fast path is **not** a win on x86-64:
`imul r64` already writes `rdx:rax`, so the 128-bit widening is one instruction,
and the added `clz`+`clz`+branch makes `fixMul` 2.35x slower (native) and 1.7x
slower (emulated) on the harness's full-range random inputs, where the branch is
unpredictable. The benefit is input-distribution-dependent: `fixCross` (small
`~±2^19` components, branch always taken) is 2.78x faster emulated and 1.09x
faster native. The arm64 claim (MUL+SMULH -> MUL) remains unproven here.

## What to try next

Gate the fast path behind `#if defined(__aarch64__)` (fixed3d#62 item 4 permits
architecture-specific integer paths) and measure on an Apple-silicon bench, where
the 128-bit widening is genuinely two instructions and the branch is predictable
for the inverse-scale workload. The emulated `fixCross` 2.78x shows the single
64-bit multiply is worth having when the branch is taken.

## What not to repeat

Do not measure `fixMul` with the harness's full-range random `fillScalar2` as the
only evidence: it makes the gate branch unpredictable and is not the
inverse-scale-times-ordinary-values distribution the spike targets.
