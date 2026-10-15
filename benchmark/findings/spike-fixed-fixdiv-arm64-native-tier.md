VERDICT: DROP (native arm64 __divti3 on the M5 Pro is already 4.9x faster than the reciprocal-plus-repair routine; the emulated arm keeps its gain)

## Hypothesis

Native aarch64 has no 128/64 hardware divide, so clang lowers fixDiv's slow tier (numerator >= 2^47) and fixInt128Div to the `__divti3` libcall. Routing the non-zero 64-bit-divisor case through `fixEmuUInt128DivRemBy64Recip` (double seed, three integer Newton steps, exact remainder repair) was expected to beat the libcall.

## The change (one paragraph)

On BASE (sprint/fixed-perf 29ac749) the reciprocal routine from spike-fixdiv-and-int128-division-fast-tiers is not present (that spike delivered a patch only), so the experiment patch is cumulative: that patch, plus a native aarch64 tier in `fixInt128Div` (macro `FIX_INT128_DIV_NATIVE_RECIP_TIER`, off with `-DFIX_NO_ARM64_RECIP_DIV`) that takes magnitudes, divides the high word by hardware `udiv` only when it is >= the divisor, sends the low word through the reciprocal routine, and falls through to `a / b` for zero or wider divisors. `fixEmuCountLeadingZeros64` uses `__builtin_clzll` on aarch64. The equivalence test is `test/fixdiv_arm64_native_tier_test.c`, registered in CMakeLists.txt.

## Host

Apple M5 Pro (ssh mini-m5-1), macOS arm64, FEAT_SME2/SME2p1/SME_I16I64 present, Apple clang 21.0.0, `-O3 -g -DNDEBUG -ffp-contract=off`. No cmake on the host: builds are written clang lines mirroring CMakeLists.txt, so ctest itself was NOT run. macOS has no taskset, so no core pin. Another worker's spike shares this Mac; ab.sh was started only when no op_bench was running, but load averages were 2 to 6.5 (recorded in raw.txt).

## Before / after (ns/op, median of 9 interleaved repeats, 1,000,000 iterations)

| op | mode | base (libcall) | candidate | speedup | A/A (base vs base) |
| --- | --- | ---: | ---: | ---: | ---: |
| fixDiv_above2p47 | batch (the slow tier) | 4.545 | 22.127 | 0.205x | 1.000 |
| fixInt128Div | scalar | 1.864 | 2.325 | 0.802x | 1.000 |
| fixInt128Div | batch | 30.58 | 31.11 | 0.983x | 1.001 |
| fixDiv_below2p47 | batch | 0.957 | 0.989 | 0.968x | (control, fast path) |
| fixMul (control) | batch | 0.974 | 0.977 | 0.997x | |
| fixInvertMatrix | batch | 349.4 | 1761.7 | 0.198x | 0.997 |
| fixSolve3 | batch | 60.5 | 219.2 | 0.276x | 0.981 |
| fixNormalize | batch | 12.01 | 12.86 | 0.934x | 1.001 |

A/A medians agree within about 1 percent; the candidate losses are far outside that. Emulated arm (same machine, 200,000 iterations): fixDiv_above2p47 batch 157.2 -> 24.0 ns (6.5x), fixInvertMatrix 11356 -> 2074 ns (5.5x), fixInt128Div 0.98x (wide divisors, unchanged); that confirms the earlier emulated KEEP on this CPU.

## Why (post-hoc, not a prediction)

llvm-mca and llvm-objdump are not installed on the host, so no mca prediction was run before measuring; this is an analytic reading afterward. The base slow tier is one `bl ___divti3` (about 4.5 ns, roughly 20 cycles: compiler-rt normalizes and uses the 64-bit `udiv`). The candidate inlines about 36 multiply instructions, an `fdiv`, a `clz` and a `udiv` into a serial dependency chain (double seed, three Newton steps of two dependent 64x64->128 products each, final product, repair compare), roughly 90 dependent cycles, which matches 22 ns. M5 integer divide and multiply-high are fast enough that the libcall wins. I could not disassemble compiler-rt's `__udivti3` (it lives in the dyld shared cache; an lldb attempt hung and was killed).

## Bit-exact evidence

- FixDivArm64NativeTierEquivalenceTest: 781x781 structured grid plus 100,026,227 seeded lanes (50,311,671 through the 64-bit-divisor tier), 0 mismatches against the compiler's own `__int128` division and an independent shift-subtract; PASS in native (tier=1), emulated (tier=0) and saturated builds.
- Written-line suite: native 25/25, emulated 25/25, saturated 4/4 (includes int128, int256_division, batch with 100M lanes, negative controls that must fail).
- Oracle (ab.sh, candidate, base_random=50000000): `ORACLE: all operations match benchmark/oracle-baseline.txt` native and emulated.
- Red at BASE: compiling the test against BASE fails with `FIX_INT128_DIV_NATIVE_RECIP_TIER is not defined: the native arm64 division tier is absent from this tree`.

## What to try next

Nothing on the divide itself: the libcall is the right native arm64 answer on this CPU. Only an unmeasured case remains: older Apple cores (M1 to M3) and Linux aarch64 (Graviton) with a different libcall, which this card did not measure. If a port card for the emulated reciprocal lands, it must stay gated to the emulated arm.

## What not to repeat

Do not route native aarch64 64-bit-divisor division through the reciprocal routine on Apple M-series. Do not read the scalar fixDiv_above2p47 row as the slow tier (dependent chain stays on the fast path); use batch.

## Not done

ctest through cmake (no cmake on host); llvm-mca prediction; compiler-rt disassembly; perf counters (no perf on macOS; Instruments and sample not run); non-M5 aarch64 hosts; core pinning.
