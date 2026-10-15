# VERDICT: DROP

Bit-exact on every check, and slower on every operation that calls `fixMul`: 0.75x on `fixMul`
batch, 0.50x to 0.65x on `fixMulBatch`, 0.16x to 0.20x on `fixCross`, 0.23x to 0.25x on
`fixRotateVector` (Apple M5 Pro, median of 11 interleaved repeats). The reason is measured, not
guessed: clang compiles the gate to a `CSEL` that computes both arms, so the 64-bit path adds work and
never skips the `SMULH`.

## Hypothesis

`spike-fixmul-product-fits-64-arm` lost on x86-64 (0.43x, one `imul` already gives `rdx:rax`) and left
arm64 unmeasured. On arm64 the 128-bit product is `MUL` + `SMULH` + a carry fix-up + `EXTR`; when
`clz(|a|) + clz(|b|) >= 65` one `MUL`, an add and an `ASR` are bit-identical. New reason to measure:
arm64 is the one target where the widening is two instructions, and this is the first run on Apple
silicon (M5 Pro, the host the card names).

## The change

`include/fixed/fixed.h`: a compile-time arm, `FIX_MUL_FITS64_ARM64`, defined only on aarch64 with the
native `__int128` route and without `FIX_SATURATE` (emulated and saturated builds keep the unchanged
route, and the test pins the macro absent there). `fixMul` first computes `ua = |a|`, `ub = |b|`; when
`fixClz64(ua) + fixClz64(ub) >= 65` it returns `(int64_t)((uint64_t)(a*b) + FIX_HALF) >> 16`, else falls
through to the unchanged 128-bit code. Not a dispatched variant. The experiment also adds
`test/fixmul_fits64_arm64_test.c` (`FixMulFits64Arm64EquivalenceTest`, with emulated and saturated
twins) and its CMake registration. It is one patch: `experiment.patch`, applies cleanly with `git apply`
on BASE (checked).

## Prediction (llvm-mca, `-mcpu=apple-m5`, LLVM 23.1.3, static) against measurement

Inner loop of `fixIsaMulBatchScalar`: BASE 10 instructions, block reciprocal throughput 2.5 cycles;
CANDIDATE 21 instructions, 5.5 cycles. Predicted speedup 2.5 / 5.5 = 0.45x. Measured `fixMulBatch`
scalar 0.497x and batch 0.648x. The batch row is above the prediction because the loop also carries
loads, stores and the checksum, which both sides pay. Prediction and measurement agree on direction and
roughly on size: the mca model is adequate here and said "loss" before a number was taken.

## Disassembly (full excerpts in raw.txt)

BASE batch kernel, 7 instructions per element: `ldr ldr smulh mul adds cinc extr str`.
CANDIDATE, per element: the same `smulh/mul/adds/cinc/extr` PLUS `cmp cneg cmp cneg clz clz add madd asr cmp csel`. Answer to the card's question "branch or csel?": **CSEL**. clang if-converts the gate, so the
`SMULH` is computed on every element whatever the gate says; the fast path can only add. A single
`fixMul` is 7 instructions at BASE and 18 at CANDIDATE. Second effect: that growth flips an inlining
decision. In `op_bench` BASE inlines all six multiplies of `fixCross` into `applyCross` (smulh x6, no
calls); CANDIDATE calls `fixCross` out of line (`bl` x3, 91-instruction body). That is most of the
0.16x on `fixCross` and 0.23x on `fixRotateVector`, which is far more than the instruction count alone
explains.

## Before/after (ns/op, median of 11 interleaved repeats, 2,000,000 iterations, `ab.sh`, FIXED_ISA scalar both sides)

`speedup = a_median / b_median`; `> 1` means the candidate is faster. The A/A column is the same tree
against itself in the same session.

| op | mode | base med | cand med | speedup | A/A speedup | candidate spread |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| fixMul | scalar | 3.933 | 4.273 | 0.920 | 1.000 | 35.1% |
| fixMul | batch | 0.976 | 1.308 | 0.746 | 0.995 | 26.0% |
| fixMulBatch | scalar | 1.401 | 2.819 | 0.497 | 0.994 | 9.3% |
| fixMulBatch | batch | 0.456 | 0.704 | 0.648 | 0.995 | 5.0% |
| fixCross | scalar | 1.851 | 11.255 | 0.164 | 0.997 | 2.0% |
| fixCross | batch | 2.536 | 12.383 | 0.205 | 0.995 | 0.8% |
| fixRotateVector | scalar | 6.969 | 30.071 | 0.232 | 0.956 | 2.7% |
| fixRotateVector | batch | 7.559 | 30.768 | 0.246 | 0.998 | 3.3% |
| fixDot (control, no fixMul) | scalar | 4.618 | 4.550 | 1.015 | 0.993 | 14.4% |
| fixDot (control) | batch | 1.265 | 1.259 | 1.005 | 0.990 | 9.2% |
| fixMulQuat (control) | scalar | 3.787 | 3.752 | 1.009 | 1.006 | 3.8% |
| fixMulQuat (control) | batch | 5.294 | 5.265 | 1.005 | 0.998 | 1.6% |
| fixDotBatch3 (control) | scalar | 2.577 | 2.567 | 1.004 | 0.998 | 6.6% |
| fixDotBatch3 (control) | batch | 0.993 | 0.997 | 0.995 | 0.999 | 2.9% |

Noise floor: the A/A medians agree within 1% on 13 of 14 rows (`fixRotateVector` scalar 0.956); the
fixMul scalar and batch min-max spreads are large (the first repeat is slow) but the medians are
steady, so a median loss of 8% on `fixMul` scalar is the only row near the noise; every other loss is
many times the spread. The controls `fixDot`, `fixMulQuat`, `fixDotBatch3` do not call `fixMul` (they
reduce on one fused 128-bit sum) and sit at 1.00, which shows the harness is not moved by the patch
itself. No gain anywhere.

Not covered, and the reason the table is not an inverse-scale-times-ordinary-values claim: `op_bench`
uses the oracle's input fills (full-width and edge-heavy), not the workload the gate targets. Test
harness counters show the fast path is taken on 25% of the equivalence-test pairs and rarely on the
oracle rows. With a csel, input distribution does not matter for the loss: both arms always execute.

## Bit-exact evidence (judge part a)

All on mini-m5-1 (Apple M5 Pro), clang -O2 -ffp-contract=off:

- RED at BASE: `FixMulFits64Arm64EquivalenceTest FAIL: aarch64 native build but the fits-64 arm absent (FIX_MUL_FITS64_ARM64 undefined)`, exit 1.
- GREEN at CANDIDATE: `PASS: arm PRESENT, 200068863 scalar pairs checked, fast gate taken on 50810167 (25.4%)`; that is the structured grid crossed with itself, the gate-boundary products and every
  split of a product just under 2^63, 100,000,000 seeded full-width pairs, 100,000,000 magnitude-
  stratified pairs, `fixMulBatch` over tails 0..70 at unaligned pointers, and `fixCross`, all against
  the compiler's own `__int128`. Emulated twin (10,000,000): arm absent, PASS. Saturated twin: arm
  absent, PASS.
- The test has teeth: loosening the gate to `>= 64` turns it red (61,047 mismatches).
- `benchmark/ab.sh` oracle against the candidate: `ORACLE: all operations match benchmark/oracle-baseline.txt (base_random=50000000)`; `fixMul` hash `eb1995a6b21bf036` unchanged,
  `fixCross` `5a5555238d655a2c`, `fixRotateVector` `64afce887683c703`. `oracle_check` native and
  emulated pass.
- Mini-ctest (clang lines mirroring CMakeLists.txt, no cmake on the host): 47 of 47 pass, covering
  native, emulated and saturated suites and every WILL_FAIL negative control.

## Bench hygiene

Host mini-m5-1, Apple M5 Pro, 18 logical CPUs, Apple clang 21.0.0, flags `-O3 -g -DNDEBUG -ffp-contract=off`. macOS has no core pinning (no taskset), so the process was not pinned. Load average
2.00 before and 1.92 after; `ps` showed no `op_bench`, `ab.sh`, `oracle_check` or `numerical_bench`
before and after. One foreign `oracle_check` (another job's emulated oracle run, pid 8203) was on the
box earlier; I did not touch it and waited for it to exit before starting the measurement. The
dispatch is `scalar` on both sides (the host has SME2; the SME2 variant is not under test).

## What to try next

- Not a port. The port card `port-fixed-fixmul-fits-64-arm64` should be dropped on this verdict.
- One sentence, as the contract allows: forcing a real branch (a `__builtin_expect` on the gate plus a
  `noinline` cold slow path so clang cannot if-convert) would make the cost data-dependent and could win
  on a stream that is almost always small, but it adds a mispredict hazard on mixed inputs, and the
  inlining collapse seen in `fixCross` would still have to be avoided; it needs a different input
  distribution to be worth a card.
- `fixDot`/`fixMulQuat` use one fused 128-bit sum and are unaffected; a gated 64-bit sum there is a
  separate question, not answered here.

## What not to repeat

Do not gate `fixMul` with a clz test that the compiler can if-convert: on arm64 clang turns it into a
`CSEL` and computes both arms. Do not read a `fixMul` microbenchmark in isolation: the code growth
(7 to 18 instructions per multiply) changes inlining of `fixCross`/`fixRotateVector`, which swamps
the instruction count.

## Not done

Instruments Time Profiler and `sample` profiles of the hot loops (the disassembly plus llvm-mca
answered the card's gating question: csel, no fusion win); `perf stat` style counters (no PMU access on
macOS without sudo); core pinning (not available on macOS); cmake `ctest` itself (no cmake on the host:
the suite was run as written clang lines); five separate A/A passes (one 11-repeat A/A pass run instead);
an inverse-scale workload harness (out of scope for the first measurement).
