# VERDICT: LEARNED

The hypothesis is falsified on the measured compiler/ISA: the saturating arithmetic arms are
already branchless, and the one arm that still branches gets *slower* in the throughput shape
when it is made branchless.

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

## Disassembly (Apple clang 17, -O3 -DNDEBUG -ffp-contract=off, x86-64)

| arm | jcc? | note |
|---|---|---|
| fixMul (FIX_SATURATE) | no | 2x cmovgeq |
| fixDiv (FIX_SATURATE) return | no | cmovnsq; the jcc present are the div dispatch / b==0 guard |
| fixClamp / fixMin / fixMax | no | cmovlq/cmovgeq/cmovgq |
| fixFloor / fixCeil | no | pure mask |
| fixQuantizeClamped | yes | 3 jcc (2x double range, 1x raw<minRaw) — the only one |
| fix30FromFix / fix30FromDouble | yes | 2 jcc (not in the named set) |

## Before/after (ns/op), fair inline-vs-inline, x86-64 Apple clang only

| shape | before (branchy) | after (branchless) | speedup |
|---|---:|---:|---:|
| scalar latency, in-range | 7.80 | 4.86 | 1.60x |
| scalar latency, 50% sat | 7.67 | 4.86 | 1.58x |
| batch throughput, in-range | 2.28 | 2.81 | 0.81x (slower) |
| batch throughput, 50% sat | 3.04 | 6.22 | 0.49x (much slower) |

The latency win (~1.6x) is real but on a dependent chain; the throughput regression is the
determinant, because that is the shape the SIMD batch spikes and the KEEP rule target. The
branchless form runs the double->int64 conversion (cvttsd2si) unconditionally, so a
perfectly-predicted in-range stream pays it every call where the branchy form early-exits.
This is the exact trap fixed3d CLAUDE.md recorded: the OOO core hides the branch in steady
state, so replacing it with predicated work is neutral-to-worse.

## Bit-exact evidence

- `ctest --test-dir build --output-on-failure`: 100% of 38 pass (all 36 pre-existing suites
  in native and emulated 128-bit arms, plus the two new `saturation_branchless[ _saturated]`
  targets). The `quantize` suite independently holds `fixQuantizeClamped` against delta's
  verbatim reference over a structured sweep + seeded samples, native and (via the frozen
  suite) unchanged.
- Standalone differential fuzz of the new body vs the old body: 20,000,000 random doubles +
  the structured edge grid crossed with scales and bounds -> 0 mismatches.
- `fixed_oracle_check`: all scalar rows relevant to this change report ORACLE OK at HEAD
  (fixMul, fixDiv_below/above2p47, fixQuantize hash=f27ed579335213a8, fixDequantize).
  The one changed operation, fixQuantizeClamped, is not a row in the oracle/op_bench table.
- Six pre-existing `ORACLE FAIL` rows (fixMulQuat, fixNormalizeQuat, fixRotateVector,
  fixInvRotateVector, fixNLerp, fixAABB_Transform) are environment-only — the baseline was
  frozen on AMD EPYC/gcc, not Apple clang/Xeon — and reproduce identically at BASE with the
  change stashed. None is touched by this change.

## VERDICT

LEARNED, and not KEEP: the only arm that is not already branchless (fixQuantizeClamped)
regresses in the throughput shape (>2x worse at 50% saturation, 19% worse in-range) and is
not even a row in the measurement harness, so the KEEP-rule gate (>=1.2x faster, no >2%
regression) is not met. The experiment taught that clang already cmov-lowers every scalar
saturating/sentinel arm, so there is no branch to remove for the hot arithmetic; the
residual branch is a double-domain boundary helper whose branchless form costs, not saves.

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
Do not expect `benchmark/ab.sh` to print a table while the committed 50M-draw oracle fails
on the six environment-only vector rows on a non-baseline compiler; it exits before the
timing table.