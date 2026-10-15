VERDICT: DROP

# Spike: batched fixed-point operations on NEON, where there is no 64-bit lane multiply

**Why DROP, on the first line.** This is a NEON (arm64) spike that landed on an x86-64
node (`AMD Ryzen 7 PRO 8700GE`, `isa_neon: no`) and no arm64 box was reachable from the
job. The NEON path therefore received no measurement and no bit-exact verification on the
target ISA, and a KEEP needs both. The code is kept only as `experiment.patch` — the record
of a change whose target it could not touch. Nothing is adopted.

## Hypothesis

The M3 has no 64-bit vector multiply (FEAT_SME 0, no SVE2, AMX private), so the fixed3d
narrow phase escapes through int32: when an exactness gate proves a batch's operands all
fit a small lane, `smull`/`smlal` widen `int32x2 -> int64x2` and compute exact 64-bit
products four at a time, with a single rounding per reduction identical to the scalar
raw-128 dot. Expected win on the target arm: the batch loop (throughput) over independent
`fixDot`/`fixMul` calls.

## The change (one paragraph)

Two new inline entry points in `include/fixed/fixed_vec.h` (`fixMulBatch`, `fixDotBatch3`),
compiled only on `__aarch64__` with a scalar fallback that runs the ordinary `fixMul`/`fixDot`
loop on every other ISA (and whenever the gate fails). Each has an exactness gate — a single
max-magnitude scan over the batch — that proves the operands fit the lane before widening;
a failing gate runs the scalar loop, so the batch result is bit-identical for every input
regardless of gate width. `fixMulBatch` widens with `vuzp1q_s32` (the AoS de-swizzle) then
`vmull_s32`/`vmull_high_s32`, adds `FIX_HALF` in the 64-bit lane and does one
`vshrq_n_s64` by 16 — byte-for-byte `fixMul` rounding. `fixDotBatch3` de-swizzles four AoS
`fixVec3` through `vld3q_s32`, accumulates the three products with
`vmull_s32` + `vmlal_s32` + `vmlal_high_s32`, then the same single `(sum + FIX_HALF) >> 16`.
`test/neon_batch_test.c` (`NeonBatchEquivalenceTest`) compares both against the scalar
functions over seeded batches at four magnitude tiers (inside int30, at int30, inside int32,
past int32), the structured edge grid crossed with itself, and n tails 0..9 and 1023; its
CMake registration is guarded to aarch64, so elsewhere it is skipped by name.

## The lesson the spike taught (before any measurement)

The int32 gate as literally hypothesised is **not sufficient** for a three-term dot. A
two-operand product is exact in a signed 64-bit lane when both operands fit int32
(`|x| <= 2^31 - 1` -> `|product| < 2^62`). But three such products sum to `3*2^62 = 1.5*2^63`,
which overflows the int64 accumulation lane. The dot's gate must therefore be **int30**
(`|x| < 2^30`, giving `3*2^60 < 2^62`), one bit per extra term tighter than the naive "fits
int32". This is a correctness of the gate, not a tuning knob: with the int32 gate the NEON
dot would silently wrap on adversarial inputs — exactly the sort of moved bit the oracle
exists to catch. The fix is free (the scan already runs, only `bound` changes) but it does
narrow the set of inputs the vector path serves, which directly eats the win. `fixMulBatch`
(no accumulation) keeps the int32 gate.

## Before / after table

Cannot be produced. There is no arm64 target in this job and the NEON path is unreachable on
x86-64, so neither `benchmark/ab.sh` nor the scalar/batch `ns/op` table has any numbers.
The baseline suite on this node is green (36/36), so the *x86-64* scalar library is
unchanged by this work — which is the only honest thing the numbers below assert.

## Bit-exact evidence

- `ctest --test-dir build --output-on-failure`: **100% tests passed, 0 failed out of 36**
  (x86-64 baseline; `neon_batch` is absent because it is aarch64-only).
- `./build/fixed_oracle_check`: **ORACLE: all operations match benchmark/oracle-baseline.txt
  (base_random=50000000)** — the native arm is bit-identical on this node.
- `./build/fixed_oracle_check_emulated`: not captured (full emulated pass over
  base_random=50000000 exceeded the run's build-time budget); the 36-test ctest includes
  `oracle_unit_emulated` over a 1,000-input prefix, which passes.
- The NEON path itself was never run, so its bit-exactness on arm64 is **unverified** — a
  requirement for KEEP that this box cannot satisfy.
- Scalar-fallback smoke on x86-64 (non-aarch64 body): `fixMulBatch`/`fixDotBatch3` matched
  `fixMul`/`fixDot` for a throwaway 4-element check (`bad=0`).

## What to try next

Dispatch this card to an Apple-silicon node (M3/M5) or an arm64 Linux box so the NEON path
can actually execute, then run `benchmark/ab.sh` interleaved and `oracle_check`. Before
adopting, re-do the gate as a *product-bound* check rather than a uniform per-operand
magnitude: for the dot, a per-`++(2^k)` bound wastes acceptance on the rare large-but-cancelling
input. Compare AoS (`vld3q_s32`/`vuzp1q_s32`) against SoA input layout directly — the
de-swizzle is the one cost this spike could not even begin to price. If the isolated-loop
gate is exceeded (1.5x per fixed3d#64), a KEEP is worth re-claiming on real arm hardware.

## What not to repeat

Do not dispatch a NEON-only spike to an x86-64 node and expect a measurement. Do not trust
the literal "int32 gate" for any multi-term reduction: the per-term bit lost to accumulation
must be budgeted in the gate. Do not claim a SKIP as a pass; `neon_batch` is skipped here,
and it is reported as such.