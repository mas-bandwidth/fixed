VERDICT: LEARNED. On Apple M3 Ultra, NEON batch length is 2.03x and cross 1.72x faster (native), and every batch op is 2.9-5.8x faster on the emulated arm. Bit-exact on both arms. Native mul (1.16x) and dot (0.81-0.96x) miss the bar, and gate-fail and small-n calls are slower, so the patch as a whole is not KEEP and nothing is adopted.

# Spike: batched fixed-point operations on NEON, where there is no 64-bit lane multiply

Attempt 10 (gen 3, epoch 21). This is the first attempt that ran on arm64. Attempt 2 landed on
x86-64 and measured nothing.

**Read this first: these numbers come from a loaded, shared machine.** The host is
`studio.local`, an Apple M3 Ultra (24P+8E). It also runs the sprint server, the bus, Redis,
Postgres and swarm members. The one-minute load average was 12 to 21 during the runs, and
every table below gives it. The owner approved this host for this card only. One build or
benchmark process ran at a time. No benchmark process was on the box at either A/B start
(`benchmarks_running: no`). `taskset` does not exist on macOS, so affinity was not set: the
OS could schedule a run on either core type. Only two A/B runs were made, native and emulated,
9 interleaved repeats each. No M5 and no arm64 Linux box was measured.

## Hypothesis

The M3 has no 64-bit vector multiply (FEAT_SME 0, no SVE2). An exactness gate can prove that
every operand of a batch fits int32. When it does, `smull`/`smull2` give exact 64-bit
products two lanes per instruction (four per pair). The fixMul rounding `(p + FIX_HALF) >> 16`
on a product that fits 64 bits is then exactly one `srshr` (`vrshrq_n_s64`). That should beat
the scalar loop for independent batch work, gate scan included.

## The change (one paragraph)

`experiment.patch` (applies cleanly with `git apply` on BASE `ac247f7`) adds six `FIX_API`
array entry points in `src/fixed_vec.c`, behind `FIX_HAVE_BATCH_API`:

- `fixMulBatch`: int32 gate.
- `fixDotBatch3`: SoA inputs, int30 gate.
- `fixDotBatch3AoS`: AoS `fixVec3` input, de-swizzled with `vuzp1q_s32` to x0y0z0x1 | y1z1x2y2 | z2x3y3z3, then three `vqtbl3q_u8`.
- `fixDotBatch3AoSLd3`: the same AoS input, de-swizzled with `vld3q_s64` plus one `vuzp1q_s32` per component.
- `fixLengthBatch`: int32 gate. Three `smull`/`smlal` squares are summed exactly in an unsigned lane (below 3·2^62). A `vsqrtq_f64` seed is repaired to the exact floor with `umull` squares, one step down and one step up.
- `fixCrossBatch`: int32 gate. Each product is rounded on its own and then subtracted, as `fixCross` does.

The gate is one OR-of-`x ^ (x >> 63)` scan per call over every operand array, and it is
inside the timing. If any lane is out of range, the whole call runs the scalar loop. Every
other ISA always runs the scalar loop.

The int30 dot gate is the lesson from attempt 2, kept: three int32 products reach
3·2^62 > 2^63.

`NeonBatchEquivalenceTest` lives in `test/exhaustive_test.c`, which is already registered as
`exhaustive` and `exhaustive_emulated`, so no CMake change was needed. It compares every
output word of all six entry points with the scalar function over:

- seeded batches in five magnitude tiers (inside int30, at int30, at int32, past int32, full range) and in mixed tiers;
- one boundary lane at every position of an 11-lane batch;
- the structured grid crossed with itself, raw and with the out-of-gate values clamped so that the vector path sees every in-range grid pair;
- perfect squares and one-below-squares near 2^62, for the sqrt repair;
- n = 0..9 and 1023 at offsets 0..3.

It uses no clock. On aarch64 without the API it FAILS rather than skipping. Elsewhere it
prints SKIP by name.

The oracle gains 10 rows: the six entry points, plus `_mixed` gate-fail variants of mul,
dot, length and cross. Each row hashes four lanes per input, built by fixed permutations, so
that the vector body and not only the tail runs on oracle inputs. The rows are frozen from
the scalar functions on the unchanged BASE, and native equals emulated. `op_bench.c` times the
batch rows as one API call per 4096-lane pass, with the gate scan inside the timed region. A
tree without the API runs the scalar function over the same arrays.

## Before / after (ns per element in BATCH mode; SCALAR mode is a 4-lane call per chained step)

Settings: `-O3 -g -DNDEBUG -ffp-contract=off`, Apple clang 21.0.0 (clang-2100.3.34.2). The
same `op_bench.c`, from the candidate, was compiled against both trees. 9 interleaved repeats
of 200000 iterations. Cells are min / median / spread, and speedup is median_before /
median_after. Everything was run through `ab-arm64.sh`, which is `ab.sh` with one line changed
(see Bit-exact) so that the table prints.

**Native `__int128`, Apple M3 Ultra.** Load 15.72 at start and 12.42 at end.

| op | mode | before min / med / spread | after min / med / spread | speedup |
|---|---|---|---|---|
| fixMulBatch | batch | 0.583 / 0.593 / 2.5% | 0.503 / 0.513 / 3.9% | **1.155** |
| fixMulBatch_mixed (gate fails) | batch | 0.568 / 0.593 / 7.6% | 0.872 / 0.887 / 6.7% | 0.669 |
| fixDotBatch3 (SoA) | batch | 1.370 / 1.450 / 9.6% | 1.470 / 1.510 / 5.0% | **0.960** |
| fixDotBatch3_mixed | batch | 1.325 / 1.455 / 13.0% | 2.143 / 2.167 / 6.2% | 0.671 |
| fixDotBatch3AoS (uzp1+tbl3) | batch | 1.166 / 1.236 / 7.7% | 1.430 / 1.475 / 3.7% | **0.838** |
| fixDotBatch3AoSLd3 (ld3+uzp1) | batch | 1.181 / 1.226 / 19.1% | 1.440 / 1.520 / 5.6% | **0.807** |
| fixLengthBatch | batch | 2.402 / 2.422 / 2.5% | 1.141 / 1.191 / 11.3% | **2.034** |
| fixLengthBatch_mixed | batch | 48.75 / 50.67 / 13.7% | 50.69 / 51.41 / 2.1% | 0.986 |
| fixCrossBatch | batch | 3.473 / 3.672 / 7.1% | 2.048 / 2.133 / 4.9% | **1.722** |
| fixCrossBatch_mixed | batch | 3.508 / 3.607 / 9.3% | 4.031 / 4.215 / 7.4% | 0.856 |
| fixMulBatch | scalar (4-lane call) | 5.34 / 5.48 / 3.4% | 11.83 / 11.92 / 2.1% | 0.459 |
| fixDotBatch3 | scalar | 9.12 / 9.25 / 2.3% | 14.07 / 14.23 / 2.1% | 0.650 |
| fixLengthBatch | scalar | 19.46 / 20.57 / 6.3% | 22.56 / 23.23 / 3.7% | 0.885 |
| fixCrossBatch | scalar | 6.10 / 6.30 / 7.1% | 13.94 / 14.08 / 2.1% | 0.447 |
| fixMul / fixDot / fixCross / fixLength (controls, untouched code) | batch | | | 1.040 / 1.048 / 1.039 / 1.012, each inside its spread |

**Emulated 128-bit (`-DFIX_FORCE_EMULATED_INT128` on both trees), same machine.** Load
12.11 at start and 14.49 at end.

| op | mode | before med | after med | speedup |
|---|---|---|---|---|
| fixMulBatch | batch | 1.764 | 0.508 | **3.471** |
| fixMulBatch_mixed | batch | 1.754 | 1.903 | 0.921 |
| fixDotBatch3 | batch | 5.167 | 1.505 | **3.434** |
| fixDotBatch3_mixed | batch | 5.167 | 5.740 | 0.900 |
| fixDotBatch3AoS | batch | 4.330 | 1.460 | **2.966** |
| fixDotBatch3AoSLd3 | batch | 4.330 | 1.475 | **2.936** |
| fixLengthBatch | batch | 4.584 | 1.181 | **3.882** |
| fixLengthBatch_mixed | batch | 66.89 | 65.92 | 1.015 |
| fixCrossBatch | batch | 12.451 | 2.133 | **5.839** |
| fixCrossBatch_mixed | batch | 12.646 | 17.284 | 0.732 (spreads 30.8% / 33.8%) |
| fixMulBatch / fixDotBatch3 / fixLengthBatch / fixCrossBatch | scalar (4-lane call) | 8.03 / 20.96 / 21.03 / 36.53 | 11.89 / 13.98 / 22.91 / 14.06 | 0.675 / 1.499 / 0.918 / 2.598 |
| fixMul / fixDot / fixCross / fixLength controls | batch | | | 0.994 / 1.013 / 0.997 / 1.093; the first three are inside their spreads, but the fixLength control at 1.093x (before 4.9775, after 4.5540 ns, spreads 5.3% / 5.4%) falls OUTSIDE its listed spreads on untouched code |

Full min/med/spread for every row is in `raw.txt`.

## Bit-exact evidence

- **Red first, at the unchanged BASE.** `ctest -R ^exhaustive` with the new test file: `FAIL ... NeonBatchEquivalenceTest: no batch API (fixMulBatch, fixDotBatch3, ...) on aarch64`, giving `0% tests passed, 2 tests failed out of 2` (exhaustive, exhaustive_emulated).
- **Candidate ctest.** `100% tests passed out of 36`, including `exhaustive`, `exhaustive_emulated`, `oracle_unit` and `oracle_unit_emulated` (41 operations), and every negative control still failing as designed. Each arm prints `NeonBatchEquivalenceTest: compared=1255818 fails=0 PASS`; that is a run, not a SKIP. The unchanged BASE ctest also gives `100% tests passed out of 36`. No frozen hash moved.
- **Mutations prove the checks see the vector path.** Changing `vrshrq` to `vshrq` gives 205240 test failures and moves 6 oracle rows. Dropping the downward sqrt repair gives 1036 test failures and moves `fixLengthBatch`.
- **Not exercised:** dropping the UPWARD sqrt repair step was caught by neither the test nor the oracle. Below 2^64 the correctly rounded double sqrt appears never to undershoot the floor, so the step looks unreachable but is kept as a guard. This is not proven.
- **`fixed_oracle_check` and `fixed_oracle_check_emulated`, full base_random=50000000 runs.** Neither full run passes: both (native and emulated) exit 1 on the 7 pre-existing rows listed below. Only the 10 new batch rows pass: they give `ORACLE OK` on both arms, with identical hashes, equal to the frozen base-scalar values. Examples: `fixMulBatch hash=148e3555c392445b`, `fixDotBatch3 hash=8e65a5439f48cd56`, `fixLengthBatch hash=030faa9c77bc8216`, `fixCrossBatch hash=823849860a8fdc46`. 24 of the 31 pre-existing rows are `ORACLE OK`.
- **A pre-existing divergence, found by this card and not caused by it.** Seven pre-existing rows FAIL against the committed baseline on this arm64 host, on BOTH 128-bit arms, with identical hashes in the candidate and in the UNCHANGED BASE:
  - `fixMulQuat 89460e7fdc0afa12` against `ff696a9b57ac0860`
  - `fixNormalizeQuat`, `fixRotateVector`, `fixInvRotateVector`, `fixNLerp`, `fixAABB_Transform` and `fixQuantize`

  This holds for the cmake build and for the ab.sh flags (`-ffp-contract=off`). The committed baseline was written on x86-64. Either the oracle's quaternion/quantize fills or those operations are not bit-identical between x86-64 and arm64. The frozen ctest suites do not catch it. That is a library-guarantee question for its own card. Since this was written, those 7 rows have been explained as oracle input-generation bugs, not library divergence (an unspecified argument-evaluation order in the oracleQuat draws, and fixQuantize inputs outside its domain), fixed on card oracle-rows-identical-on-x86-64-and-arm64 (branch sprint/oracle-rows-identical-on-x86-64-and-arm64.w1.g1.e21, head ae5af9d9). This card could not name the first differing input, because that needs an x86-64 dump to diff against. The ranges are in `raw.txt`, for example `fixMulQuat [0, 1566484)`. Because of it, `ab.sh` exits before printing its table on arm64. `ab-arm64.sh` changes only that `exit 1` into a message; the diff is in `raw.txt`.

## Keep rule, applied

Bit-exact on both arms: yes.

Rows that pass the 1.5x vector-path bar natively: `fixLengthBatch` 2.03x and `fixCrossBatch`
1.72x, each beyond its spread.

Rows that fail it:
- `fixMulBatch` 1.155x is below 1.2x and below 1.5x.
- `fixDotBatch3` is SLOWER in all three layouts (0.81 to 0.96x).
- Gate-fail batches are 14 to 33% slower natively. The scan is wasted and the scalar loop still runs.
- 4-lane calls (SCALAR mode) are 1.1 to 2.2x slower natively, from the out-of-line call, the gate and the n=4 shape against the inlined scalar code.

Not KEEP.

## What the experiment taught

1. **Native M3 scalar mul is already cheap.** It costs about 0.59 ns per element: `mul` + `smulh` + rounding with two multiply pipes. The NEON body does not save enough to pay for a second pass over the data (the gate scan). The dot is worse: six arrays are scanned and then read again. AoS against SoA is a small effect next to that. The baseline AoS loop is itself faster than SoA (1.23 against 1.45 ns), and the two de-swizzles cost the same (uzp1+tbl3 1.475 ns, ld3+uzp1 1.520 ns, inside the spread).
2. **NEON pays where the scalar body is heavy.** Length replaces a scalar double sqrt plus repair per element with two lanes of `fsqrt` plus `umull` repair: 2.03x. Cross does six products per element: 1.72x. On the emulated arm, where every scalar 128-bit multiply is software, every row wins 2.9 to 5.8x. That matters for MSVC-shaped targets running on arm64.
3. **A per-call gate is all-or-nothing.** One out-of-range lane in 4096 sends the whole call to the scalar loop, after the scan has already been paid for.

## What to try next

- Adopt only `fixLengthBatch` and `fixCrossBatch` (and perhaps the emulated-arm path), as a separate card with its own measurement.
- Fuse the gate into the compute pass: compute and store four lanes, OR their magnitudes, and run the scalar loop only for blocks that failed. That gives one read pass and costs a gate failure only its own block. This is the likely fix for mul and dot.
- Measure on M5 and an arm64 Linux box (Graviton or Ampere, with different multiply throughput) before concluding anything about mul and dot beyond the M3.
- Open a separate card for the 7 oracle rows that differ between x86-64 and arm64: diff `--dump` output from both ISAs and name the first differing input.

## What not to repeat

- Do not put a separate full scan in front of a cheap kernel (mul, dot) on a machine whose scalar multiply is fast. The scan costs as much as the work.
- Do not use the batch API for n=4-sized calls.
- Do not run `ab.sh` from a shell whose command line contains `op_bench` or `oracle_check`: its `pgrep -af` finds your own wrapper and refuses to run.
- On macOS, do not restore a source file with `cp` from a same-second copy: the build can miss the change. `touch` the file.
