# VERDICT: LEARNED

The hypothesis is falsified on the measured compiler/ISA: the saturating arithmetic arms are
already branchless, and the one arm that still branches gets *slower* in the throughput shape
when it is made branchless. Measured on the Linux bench (AMD Ryzen 9 7950X3D, gcc 13.3.0 and
clang 18.1.3), not the working Mac.

## Hypothesis

"The default fixMul wraps (overflow checks are opt-in under FIX_SATURATE); the saturating
arms and the sentinel clamps (fixClamp, fixMin, fixMax, fixDiv's +-FIX_MAX return,
fixQuantizeClamped) use branches that mispredict on mixed data. clang already lowers simple
ternaries to csel/cmov, so the question is which arms still branch."

## The change (one paragraph)

`include/fixed/fixed_quantize.h`: `fixQuantizeClamped` rewritten so its storage-range and
integer-clamp selects are branchless — `rounded >= 2^63` / `rounded <= -2^63` become two
compare flags (setcc), the double->int64 cast is masked to zero by those flags, and the
final min/max selects fold into a cmov chain (raw < minRaw, raw > maxRaw, above -> maxRaw,
below -> minRaw). No other arm is touched: the disassembly shows fixMul/fixDiv under
FIX_SATURATE, fixClamp, fixMin, fixMax, fixFloor/fixCeil already compile to cmov with zero
conditional jumps on clang -O3, so there is nothing to rewrite there.

## Disassembly (Linux bench, -O3 -DNDEBUG -ffp-contract=off)

| arm | clang 18 jcc? | gcc 13.3 jcc? | note |
|---|---|---|---|
| fixMul (FIX_SATURATE) | no | no | cmov saturation selects |
| fixDiv (FIX_SATURATE) return | no | no | cmov; the jcc present are the div dispatch / b==0 guard |
| fixClamp / fixMin / fixMax | no | no | cmov |
| fixFloor / fixCeil | no | no | pure mask |
| fixQuantizeClamped (branchy) | 3 jcc (2 jae, 1 jl) | 2 jcc (jae, jb) | the only branchy arm |
| fixQuantizeClamped (branchless) | 0 jcc | 2 jcc (jb) | clang fully cmov-lowers; gcc keeps 2 jb |

The branchless rewrite is only truly branchless on clang; gcc 13.3 still emits two `jb`
branches for the rewritten body, which is why the gcc numbers below show no latency win.

## Before/after (ns/op), fixQuantizeClamped, Linux AMD Ryzen 9 7950X3D

Custom always_inline A/B (both bodies inline, same flags, xoshiro seed, 4096-input batch,
20,000,000 iterations). mode 0 = all in range, 1 = 1% saturate, 2 = 50% saturate.

| compiler | mode | latency branchy | latency branchless | speedup | throughput branchy | throughput branchless | speedup |
|---|---|---:|---:|---:|---:|---:|---:|
| gcc 13.3.0 | 0 | 4.943 | 5.113 | 0.967x | 0.851 | 1.048 | 0.811x |
| gcc 13.3.0 | 1 | 4.552 | 4.759 | 0.956x | 0.854 | 1.070 | 0.798x |
| gcc 13.3.0 | 2 | 0.996 | 1.000 | 0.996x | 0.769 | 0.751 | 1.023x |
| clang 18.1.3 | 0 | 2.251 | 1.668 | 1.349x | 1.029 | 1.505 | 0.683x |
| clang 18.1.3 | 1 | 2.230 | 1.709 | 1.305x | 1.034 | 1.522 | 0.680x |
| clang 18.1.3 | 2 | 1.277 | 1.675 | 0.762x | 0.882 | 1.479 | 0.596x |

The clang latency win (~1.3x in-range) is real but on a dependent chain; the throughput
regression is the determinant, because that is the shape the SIMD batch spikes and the KEEP
rule target. The branchless form runs the double->int64 conversion (cvttsd2si)
unconditionally, so a perfectly-predicted in-range stream pays it every call where the
branchy form early-exits. This is the exact trap fixed3d CLAUDE.md recorded: the OOO core
hides the branch in steady state, so replacing it with predicated work is neutral-to-worse.
On gcc the branchless form is not even branchless (2 jb remain), so it is neutral-to-worse
in both shapes.

## Harness (ab.sh) before/after, Linux bench

`benchmark/ab.sh baseline candidate` (gcc 13.3.0, 7 interleaved repeats, 200000 iterations).
The changed operation `fixQuantizeClamped` is NOT a row in the op_bench/oracle table, so the
harness cannot see it; every measured row is inside its own spread (speedup ~1.0), and the
oracle passes bit-exact:

```
ORACLE: all operations match benchmark/oracle-baseline.txt (base_random=50000000)
```

Relevant rows (all inside noise): fixMul scalar 1.004 / batch 0.989, fixDiv_below2p47
scalar 1.006 / batch 1.013, fixDiv_above2p47 scalar 1.009 / batch 1.041, fixQuantize
scalar 1.005 / batch 0.859, fixDequantize scalar 1.003 / batch 0.997.

## Bit-exact evidence

- `ctest --test-dir build --output-on-failure`: 100% of 38 pass (36 pre-existing suites in
  native and emulated 128-bit arms, plus the two new `saturation_branchless[ _saturated]`
  targets). The reader's stack out-of-bounds finding is fixed: the grid is now sized
  `G = 63*4 + 8` (260) to match the 8 + 63*4 entries the fill writes.
- `benchmark/ab.sh` oracle (compiled against the candidate): all 36 operations report
  `ORACLE OK` and the summary line is `all operations match benchmark/oracle-baseline.txt (base_random=50000000)`. No environment-only failures on this box (the baseline was frozen
  on AMD EPYC/gcc, and this box is AMD Ryzen/gcc, so the six vector rows that failed on
  Apple clang/Xeon reproduce identically here).
- The two new equivalence tests hold `fixQuantizeClamped` against the branching reference
  over the structured edge grid crossed with scales and bounds, plus 2,000,000 seeded pairs
  crossing the saturation boundaries, and (under FIX_SATURATE) `fixMul` against an
  independent 128-bit reference over the grid crossed with itself.

## VERDICT

LEARNED, and not KEEP: the only arm that is not already branchless (fixQuantizeClamped)
regresses in the throughput shape on both compilers (0.60-0.81x, i.e. 20-40% slower) and is
not even a row in the measurement harness, so the KEEP-rule gate (>=1.2x faster, no >2%
regression) is not met. The experiment taught that clang already cmov-lowers every scalar
saturating/sentinel arm, so there is no branch to remove for the hot arithmetic; the
residual branch is a double-domain boundary helper whose branchless form costs, not saves,
and gcc does not even cmov-lower the rewritten body.

## What to try next

If branchless saturation is wanted at all, target the SIMD/batch shape with a real vector
path behind a gate (fixed3d#64 requires >=1.5x in the isolated loop) rather than a scalar
cmov rewrite. A fused `min(max(x,lo),hi)`-style clamp for `fixClamp` is already what clang
emits; the opportunity, if any, is in `fix30FromFix`/`fix30FromDouble` (also branchy) or in
hoisting the double->int64 storage check out of `fixQuantizeClamped`'s loop when the caller
can prove the range — not in removing the branch itself.

## What not to repeat

Do not re-run the "are fixMul/fixDiv under FIX_SATURATE branchy?" question on clang -O3: the
answer is no, and it took a disassembly to confirm. Do not measure fixQuantizeClamped against
a NOINLINE reference (call overhead swamps the signal); both bodies must be always_inline.
Do not expect `benchmark/ab.sh` to show a fixQuantizeClamped row: it is not in the
op_bench/oracle table, so the harness A/B is a bit-exactness check only, not a timing signal
for this operation.
