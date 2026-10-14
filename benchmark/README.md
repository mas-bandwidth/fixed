# Numerical performance checks

`numerical_bench.c` measures affected public operations with runtime-generated,
repeatable inputs. Setup is outside the timed loops; each operation warms up,
consumes every output component, and emits a checksum. The multiply row is an
unchanged control. The optional third argument selects one operation for profiling.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DFIX_BUILD_BENCHMARKS=ON
cmake --build build --parallel
./build/fixed_numerical_bench 5000000 42
./build/fixed_numerical_bench 1000000000 42 normalize
```

For a fair before/after comparison, compile the **same benchmark source** against
both revisions, with the same compiler and flags. For example, on Clang/GCC:

```sh
git worktree add --detach ../fixed-baseline d0884f2a0e03b74ecf58a3a0e2d4717d48183230
cc -O3 -g -DNDEBUG -ffp-contract=off -I ../fixed-baseline/include \
  benchmark/numerical_bench.c ../fixed-baseline/src/fixed_math.c \
  ../fixed-baseline/src/fixed_vec.c -lm -o build/bench-before
cc -O3 -g -DNDEBUG -ffp-contract=off -I include \
  benchmark/numerical_bench.c src/fixed_math.c src/fixed_vec.c -lm -o build/bench-after
for trial in 1 2 3 4 5; do
  ./build/bench-before 5000000 42 > "build/before-$trial.csv"
  ./build/bench-after 5000000 42 > "build/after-$trial.csv"
done
```

Add `-DFIX_FORCE_EMULATED_INT128` to **both** compile commands to compare the
software integer backend. Output changes are expected for corrected operations;
checksums prevent unused work but do not replace the regression suite.

## Profiling and optimization passes

The repair was measured in successive optimized builds. On macOS, `sample` took
three-second profiles at 1 ms intervals while the normalize-only workload ran.
The baseline profile put nearly all samples in the normalization kernel; source
inspection identified its restoring square root and precision-lift loop. After
introducing an exactly repaired hardware
square-root seed for inputs up to 104 bits, the profile exposed an outlined
normalization helper. Forcing that helper inline and using the compiler's leading
zero intrinsic removed most of the remaining overhead. Larger square roots still
use the restoring algorithm. Both routes return the exact integer floor.

An initial repair also lifted ordinary quaternions unnecessarily. Keeping the
existing near-unit path retained its rounding and avoided that cost. A single
norm bound now proves that all shifted component numerators fit the hardware
divide, replacing repeated per-component checks. Inputs near both sides of the
fast-path boundaries are covered by regression tests.

AABB transformation retains full rotation coefficients until the final outward
rounding. This fixes containment and removes enough repeated fixed-point
multiplication to outperform the old calculation. Overflow-free half sums and
differences also make center/extents calculation cheaper.

## September 7, 2026 initial repair

Apple M3 Ultra, arm64 macOS 26.6.2, Apple Clang 21.0.0
(`clang-2100.1.1.101`), using the flags above. Baseline is `d0884f2a`; after is
the numerical-correctness repair at `28a8bc0`. Each cell is the
median of five alternating before/after runs with seed 42: five million calls
per operation for native arithmetic, 500,000 for emulated arithmetic. Lower is
better. [All trial measurements and checksums](2026-09-07-m3-ultra.csv) are retained.

| Operation | Native before -> after (ns) | Change | Emulated before -> after (ns) | Change |
| --- | ---: | ---: | ---: | ---: |
| Multiply control | 0.724 -> 0.708 | -2.2% | 1.832 -> 1.876 | +2.4% |
| Normalize vector | 82.674 -> 5.802 | -93.0% | 107.462 -> 13.688 | -87.3% |
| Length and normalize | 3.154 -> 3.201 | +1.5% | 7.454 -> 7.342 | -1.5% |
| Normalize quaternion | 3.995 -> 3.983 | -0.3% | 9.148 -> 9.126 | -0.2% |
| Quaternion and swing angles | 29.415 -> 26.531 | -9.8% | 57.386 -> 59.940 | +4.5% |
| Quaternion validity | 1.992 -> 2.175 | +9.2% | 5.092 -> 4.896 | -3.8% |
| Transform AABB | 19.377 -> 11.675 | -39.7% | 51.140 -> 41.620 | -18.6% |
| AABB center and extents | 3.875 -> 1.582 | -59.2% | 3.454 -> 1.562 | -54.8% |
| Clamped quantization | 0.591 -> 0.766 | +29.6% | 0.586 -> 0.748 | +27.6% |
| Narrow time | 0.564 -> 0.569 | +0.9% | 0.558 -> 0.560 | +0.4% |

The substantial normalization and bounds improvements held across repeated passes.
Combined vector/quaternion normalization stayed close to the original timing.
The extra overflow checks cost about 0.18 ns in native validity and clamped
quantization here. Emulated angle extraction costs about 2.55 ns more for the pair
of calls. Those costs remain visible instead of being hidden in an aggregate
score; correctness requires retaining the full squared components and checking
the conversion range.

## Follow-up: cheaper exact square-root correction

For a 64-bit input, the integer root is at most `UINT32_MAX`. Correcting the
hardware seed therefore needs only 64-bit arithmetic, even on the emulated
backend. After adjusting the seed downward so that `r*r <= n`, the next root
is admissible exactly when `n-r*r > 2*r`. This avoids a 128-bit product and also
avoids overflow from forming `(r+1)*(r+1)` at the largest root. The seed is still
repaired to the exact floor; output bits and frozen hashes do not change.

The benchmark now includes scalar square root, vector length, and the raw
64-bit-input root. These inputs are prepared after the original workload so
existing inputs stay unchanged. Scalar square roots use magnitudes up to eight
units; raw integer roots use the full unsigned 64-bit distribution.

The following medians compare **`28a8bc0` against the follow-up**, using the same
machine, compiler, flags, seed, five paired runs and iteration counts as above.
Compile the current benchmark source against both revisions and substitute
`28a8bc0` for the baseline worktree revision to reproduce this comparison.
[All follow-up trials](2026-09-07-sqrt-m3-ultra.csv) include checksums, which
agree across every before/after run for both backends.

| Operation | Native before -> after (ns) | Change | Emulated before -> after (ns) | Change |
| --- | ---: | ---: | ---: | ---: |
| Multiply control | 0.735 -> 0.738 | +0.4% | 2.010 -> 1.998 | -0.6% |
| Normalize vector | 5.960 -> 5.928 | -0.5% | 14.446 -> 14.558 | +0.8% |
| Length and normalize | 3.325 -> 3.088 | -7.1% | 7.726 -> 5.292 | -31.5% |
| Normalize quaternion | 4.081 -> 3.836 | -6.0% | 9.344 -> 6.942 | -25.7% |
| Quaternion and swing angles | 27.362 -> 26.901 | -1.7% | 62.132 -> 60.236 | -3.1% |
| Quaternion validity | 2.191 -> 2.188 | -0.1% | 5.026 -> 5.008 | -0.4% |
| Transform AABB | 12.080 -> 12.109 | +0.2% | 42.556 -> 42.762 | +0.5% |
| AABB center and extents | 1.633 -> 1.632 | -0.1% | 1.620 -> 1.614 | -0.4% |
| Clamped quantization | 0.789 -> 0.789 | 0.0% | 0.778 -> 0.778 | 0.0% |
| Narrow time | 0.584 -> 0.583 | -0.2% | 0.580 -> 0.580 | 0.0% |
| Scalar square root | 1.768 -> 1.577 | -10.8% | 3.886 -> 1.586 | -59.2% |
| Vector length | 2.341 -> 2.126 | -9.2% | 6.832 -> 3.986 | -41.7% |
| Raw 64-bit-input root | 1.195 -> 1.076 | -10.0% | 2.934 -> 1.064 | -63.7% |

Regression coverage includes 65,536 consecutive inputs near `UINT64_MAX`,
perfect-square neighbors around every root-bit boundary, 16,384 random inputs
compared to an independent integer binary-search oracle, and random square
neighbors. All existing frozen hashes remain unchanged by this follow-up.

## Measurement limits

These are cache-resident microbenchmarks of 1,024 inputs on one machine, using
CPU time. Vectors span approximately +/-8 units; quaternion inputs contain four
ordinary rotations. The combined angle row calls both quaternion and swing angle
extraction. Short and full-range inputs are correctness-tested but are not the
ordinary-input timing distribution here. The software backend runs on the same
ARM CPU; it is not a Windows performance measurement.

Sub-nanosecond differences are sensitive to scheduling and code layout, so small
percentage changes should not be interpreted as precise application-level costs.
There is no claim of a whole-simulation speedup or zero regression on every
compiler, CPU, and input distribution. Measure a consuming application's workload
before choosing a frame-time budget from these numbers. CI compiles the benchmark
on all supported toolchains but does not enforce timing thresholds.

## The operation bench and the output oracle

Two programs extend this file's protocol. Neither changes anything in `include/` or `src/`.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DFIX_BUILD_BENCHMARKS=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure        # includes oracle_unit and its emulated twin
./build/fixed_op_bench 7 200000                    # CSV: op,mode,ns_per_op min/median/max,checksum
./build/fixed_op_bench 7 200000 fixNormalize       # one operation
./build/fixed_oracle_check                         # diff a full oracle pass against the baseline
./build/fixed_oracle_check_emulated                # the same baseline, FIX_FORCE_EMULATED_INT128
benchmark/cpu_info.sh                              # the header every finding pastes
benchmark/ab.sh <baseline-src-dir> <candidate-src-dir> [op ...]
```

`op_bench` measures every public hot operation twice. **SCALAR** is a dependent chain: each
call's first output word becomes the next call's first input word, so nothing overlaps and
the number is latency. **BATCH** runs independent operations over an array of 4096 prepared
inputs; that is throughput, and it is the baseline the SIMD batch spikes have to beat. Every
output word is consumed into a checksum, and the printed checksum is the same number for
both trees in an A/B -- a differing checksum means output bits moved, and the row is void.

Each row dispatches through the operation's apply function and stores its output words, so
every row carries the same small constant overhead (about a nanosecond). Absolute numbers
are therefore an upper bound on the operation's own cost; A/B differences are unaffected,
because both trees pay it identically.

### The oracle

`benchmark/oracle.c` is one engine, no `main()`. `oracle_check` runs it over the full input
set and diffs against `benchmark/oracle-baseline.txt`; `test/oracle_unit_test.c`
(`OracleHashStableTest`) runs it over a 1,000-input prefix of every operation against hashes
baked into the test, with no files and no clock. Per operation the input set is: the
structured edge grid (`0`, `+-1`, `+-2^k`, `+-(2^k-1)` for every `k`, `INT64_MIN`,
`INT64_MAX`, the saturation values), crossed with itself for binary operations; exhaustive
where feasible (`fixSqrt` for every raw input below `2^32`, `fixSin`/`fixCos`/
`fixComputeCosSin` for every raw angle in `[-2 pi, 2 pi]`, `fixLog2`/`fixExp2` over a dense
grid); and seeded pseudo-random draws from the xoshiro256\*\* stream whose seed is a constant
in the source, including seeded random vectors, quaternions, matrices and AABBs for the
composite operations. The base random count is recorded in the baseline file's header
(`base_random:`) and each operation divides it by its own cost weight, so a hash is
reproducible rather than merely frozen; the committed baseline was written with
`base_random = 100000`.

The same baseline file must match BOTH 128-bit arms. `fixed_oracle_check` and
`fixed_oracle_check_emulated` are the same source linked against `fixed` and
`fixed_emulated`, so their agreement is itself a cross-check of the emulation. A moved hash
IS the failure: regenerate nothing to make a change pass.

When a hash moves, `oracle_check` names the operation, narrows the difference to the segment
whose running hash first diverges, prints the input at the head of that range, and gives the
command that names the first differing input exactly:

```sh
./build/fixed_oracle_check --dump fixMul <start> <end>   # index/input/output triples
# run it in both trees and diff: the first differing line is the first differing input
```

### A/B protocol

`benchmark/ab.sh A B [op ...]` compiles the SAME `benchmark/op_bench.c` against each tree
with the flags above, runs the two binaries INTERLEAVED for `AB_REPEATS` (default 7) repeats
of `AB_ITERS` (default 200000) iterations, prints min/median/spread per operation and mode
with `speedup = a_median / b_median`, and runs `oracle_check` on the candidate. It refuses
to start while another benchmark process is on the box (`pgrep -af`) and never kills
anything. `benchmark/ab.sh . .` is the A/A control: both columns are the same tree, so every
speedup must land inside the spread. That spread is the floor any spike must beat.

### Operations that do not exist in this library today

There is no `rsqrt`, no fused `fixMulAdd`-style scalar op beyond the vector ones, no
`fixSin`/`fixCos` pair cheaper than `fixComputeCosSin` (both call it), no wide (Q112.16)
transcendental, and no batch/SIMD entry point of any kind: every operation here is scalar,
which is why BATCH is a measurement shape the harness supplies rather than an API. The
rsqrt-shaped work is measured as `fixLength` and `fixNormalize`; a spike that adds an
`fixRsqrt` must add its oracle row and freeze its hashes before it can claim a number.

### A/A noise

NOT MEASURED IN THE ATTEMPT THAT ADDED THIS SECTION, and that is stated rather than filled
with a placeholder table: it needs a quiet bench and five interleaved runs, and this card's
wall ran out before one was available. Produce it with:

```sh
benchmark/ab.sh . . > build/aa-1.txt   # five times, then read the spread column
```

Until that table exists, no spike may claim a speedup smaller than the spread `ab.sh` prints
for the operation it touches.
