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
./build/fixed_op_bench 7 200000 fixMul fixSqrt_small   # a set; an unknown name fails
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
Names after the iteration count are a set, intersected with the operation table. Every
named operation is measured; an unknown name exits non-zero instead of being dropped.
Passing no names runs every operation.

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
(`base_random:`) and each operation divides it by its own cost weight (minimum one
draw), so a hash is reproducible rather than merely frozen. The committed baseline
is `base_random = 50000000`: fifty million seeded xoshiro256** inputs per operation
before the weight divisor. The weights live in the operation table in `oracle.c`.
`fixUInt256DivMod`'s weight of 100 is 500,000 draws, not 50,000,000. A flat count on
every row would make the oracle's runtime a property of its slowest row. Changing
the base invalidates every full-set hash in the baseline.

`fixAtan2` is hashed after mapping each operand `INT64_MIN` to `INT64_MIN + 1`.
`fixAbs(INT64_MIN)` is undefined: on x86-64 it wraps, the following min/max treats
the still-negative value as a zero divisor, and the native 128-bit divide raises
SIGFPE, while the emulated arm returns. The neighbor is the edge both arms can hash.
`fixUInt256DivMod` hashes all eight limbs of the quotient and the remainder.

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
with `speedup = a_median / b_median`, and compiles `benchmark/oracle_check.c` together
with the candidate's `benchmark/oracle.c`, `include/` and `src/`, then runs that binary.
It does not consult a prebuilt `build/fixed_oracle_check`. Failing to compile the
oracle is a failure. It refuses to start while another benchmark process is on the
box (`pgrep -af`) and never kills anything. `benchmark/ab.sh . .` is the A/A control:
both columns are the same tree.
A row is inside the noise when `|speedup - 1|` is no larger than the greater of
that row's two spreads. That greater spread is the floor any spike must beat.

### Operations that do not exist in this library today

There is no `rsqrt`, no fused `fixMulAdd`-style scalar op beyond the vector ones, no
`fixSin`/`fixCos` pair cheaper than `fixComputeCosSin` (both call it), no wide (Q112.16)
transcendental, and no batch/SIMD entry point of any kind: every operation here is scalar,
which is why BATCH is a measurement shape the harness supplies rather than an API. The
rsqrt-shaped work is measured as `fixLength` and `fixNormalize`; a spike that adds an
`fixRsqrt` must add its oracle row and freeze its hashes before it can claim a number.

### A/A noise

Five runs of `benchmark/ab.sh . .` on `space`, 2026-10-14 07:57:21Z through 08:09:00Z.
Each exited 0. Each printed `ORACLE: all operations match benchmark/oracle-baseline.txt (base_random=100000)`.
That line is the previous budget. The committed base is now `50000000`.
Compiler `cc (Ubuntu 15.2.0-16ubuntu1) 15.2.0`, flags `-O3 -g -DNDEBUG -ffp-contract=off`,
7 repeats, 200000 iterations. CPU from `benchmark/cpu_info.sh` on the first run:
AMD EPYC 9124 16-Core Processor, 32 logical CPUs, affinity 0-31, avx2 yes, avx512f yes,
avx512dq yes, avx512vl yes, avx512ifma yes, bmi2 yes, adx yes, neon no, sme no.
One-minute load at each run's header: 3.60, 3.61, 1.81, 3.42, 2.37.

Speedup is the range of the five `a_median / b_median` values. Worst spread is the
largest of the five greater-of-the-two-spreads. Median ns/op is the range of the ten
medians (five runs, two copies of the same tree), rounded to 0.01 ns. On every row of
every run, `|speedup - 1|` was no larger than the greater spread.

| op | mode | speedup over 5 runs | worst spread | median ns/op range |
|---|---|---|---|---|
| fixMul | scalar | 0.945 .. 1.022 | 149.9% | 2.01 .. 2.40 |
| fixMul | batch | 0.950 .. 1.029 | 155.9% | 1.64 .. 2.52 |
| fixDiv_below2p47 | scalar | 0.997 .. 1.012 | 152.6% | 3.01 .. 3.14 |
| fixDiv_below2p47 | batch | 1.000 .. 1.021 | 162.2% | 1.90 .. 2.92 |
| fixDiv_above2p47 | scalar | 0.990 .. 1.007 | 151.2% | 2.99 .. 3.13 |
| fixDiv_above2p47 | batch | 0.938 .. 1.024 | 153.1% | 3.83 .. 6.72 |
| fixSqrt_small | scalar | 0.999 .. 1.002 | 147.6% | 9.77 .. 9.97 |
| fixSqrt_small | batch | 0.975 .. 1.072 | 159.0% | 3.19 .. 4.86 |
| fixSqrt_raw_below2p32 | scalar | 0.998 .. 1.015 | 147.3% | 9.78 .. 9.98 |
| fixSqrt_raw_below2p32 | batch | 0.992 .. 1.177 | 170.2% | 3.05 .. 3.67 |
| fixISqrt128High_u64 | scalar | 0.978 .. 1.003 | 71.0% | 106.69 .. 124.10 |
| fixISqrt128High_u64 | batch | 0.674 .. 1.189 | 89.2% | 3.63 .. 6.48 |
| fixISqrt128High_above104bits | scalar | 0.867 .. 1.003 | 50.3% | 106.66 .. 149.31 |
| fixISqrt128High_above104bits | batch | 0.838 .. 1.005 | 46.5% | 78.06 .. 101.12 |
| fixComputeCosSin | scalar | 0.999 .. 1.001 | 5.2% | 41.22 .. 42.26 |
| fixComputeCosSin | batch | 0.994 .. 1.023 | 45.5% | 23.95 .. 26.83 |
| fixSin | scalar | 0.999 .. 1.016 | 5.1% | 33.63 .. 34.65 |
| fixSin | batch | 0.995 .. 1.160 | 32.7% | 22.37 .. 30.03 |
| fixCos | scalar | 0.999 .. 1.014 | 5.4% | 31.18 .. 32.34 |
| fixCos | batch | 0.996 .. 1.054 | 38.7% | 21.98 .. 31.01 |
| fixAtan2 | scalar | 1.000 .. 1.001 | 6.9% | 16.58 .. 17.11 |
| fixAtan2 | batch | 0.998 .. 1.020 | 49.1% | 11.16 .. 17.03 |
| fixLog2 | scalar | 0.995 .. 1.002 | 39.7% | 3.25 .. 3.27 |
| fixLog2 | batch | 0.979 .. 1.012 | 21.5% | 64.21 .. 73.00 |
| fixExp2 | scalar | 0.754 .. 1.017 | 43.1% | 2.46 .. 3.28 |
| fixExp2 | batch | 0.995 .. 1.041 | 26.3% | 4.13 .. 4.33 |
| fixPow | scalar | 0.996 .. 1.099 | 39.9% | 3.52 .. 3.90 |
| fixPow | batch | 0.995 .. 1.049 | 20.8% | 65.55 .. 72.64 |
| fixLerp | scalar | 0.909 .. 1.002 | 22.0% | 3.82 .. 4.44 |
| fixLerp | batch | 0.991 .. 1.027 | 44.8% | 2.71 .. 3.91 |
| fixInt128Div | scalar | 0.992 .. 1.044 | 71.7% | 4.61 .. 5.38 |
| fixInt128Div | batch | 0.970 .. 1.002 | 17.1% | 10.82 .. 11.61 |
| fixUInt256DivMod | scalar | 0.973 .. 1.062 | 72.3% | 4.89 .. 5.56 |
| fixUInt256DivMod | batch | 0.917 .. 1.026 | 53.0% | 23.40 .. 39.51 |
| fixDot | scalar | 0.950 .. 1.008 | 42.2% | 2.70 .. 2.99 |
| fixDot | batch | 0.940 .. 1.279 | 70.8% | 2.22 .. 2.99 |
| fixCross | scalar | 0.999 .. 1.040 | 58.9% | 6.23 .. 6.91 |
| fixCross | batch | 0.994 .. 1.004 | 14.5% | 10.08 .. 11.25 |
| fixLength | scalar | 0.999 .. 1.002 | 40.4% | 11.89 .. 11.94 |
| fixLength | batch | 0.994 .. 1.053 | 52.5% | 3.85 .. 7.35 |
| fixNormalize | scalar | 0.750 .. 1.001 | 47.9% | 19.74 .. 28.57 |
| fixNormalize | batch | 0.729 .. 1.009 | 75.3% | 17.66 .. 25.09 |
| fixMulQuat | scalar | 0.831 .. 1.001 | 64.2% | 8.14 .. 10.03 |
| fixMulQuat | batch | 0.976 .. 1.001 | 25.9% | 12.63 .. 13.28 |
| fixNormalizeQuat | scalar | 0.906 .. 1.003 | 34.9% | 115.80 .. 136.54 |
| fixNormalizeQuat | batch | 0.998 .. 1.004 | 25.9% | 122.18 .. 139.03 |
| fixRotateVector | scalar | 0.998 .. 1.138 | 56.2% | 16.27 .. 24.40 |
| fixRotateVector | batch | 0.990 .. 1.013 | 56.4% | 16.85 .. 26.06 |
| fixInvRotateVector | scalar | 0.997 .. 1.042 | 49.5% | 16.27 .. 25.36 |
| fixInvRotateVector | batch | 0.938 .. 1.003 | 44.8% | 16.85 .. 26.57 |
| fixNLerp | scalar | 0.996 .. 1.020 | 13.1% | 105.46 .. 118.62 |
| fixNLerp | batch | 0.985 .. 1.000 | 19.4% | 113.18 .. 124.85 |
| fixInvertMatrix | scalar | 0.925 .. 1.011 | 53.4% | 434.74 .. 628.23 |
| fixInvertMatrix | batch | 0.902 .. 1.002 | 50.7% | 479.53 .. 613.17 |
| fixSolve3 | scalar | 0.757 .. 1.007 | 68.5% | 62.89 .. 87.88 |
| fixSolve3 | batch | 0.845 .. 1.004 | 79.1% | 64.61 .. 79.04 |
| fixAABB_Transform | scalar | 0.787 .. 1.003 | 63.9% | 25.89 .. 33.53 |
| fixAABB_Transform | batch | 0.783 .. 1.006 | 66.1% | 26.39 .. 34.95 |
| fixQuantize | scalar | 0.998 .. 1.003 | 4.1% | 7.53 .. 7.58 |
| fixQuantize | batch | 0.777 .. 1.002 | 45.5% | 2.72 .. 3.54 |
| fixDequantize | scalar | 0.991 .. 1.004 | 4.2% | 8.63 .. 8.81 |
| fixDequantize | batch | 1.000 .. 1.012 | 41.7% | 2.71 .. 2.77 |

Seven rows sat outside the tighter of the two spreads and inside the greater one.
The greater one is the floor:

- run 2 `fixMul` scalar speedup 1.022, spreads 149.9% and 1.4%
- run 2 `fixSolve3` scalar speedup 0.990, spreads 0.9% and 5.3%
- run 3 `fixUInt256DivMod` scalar speedup 0.973, spreads 2.3% and 5.0%
- run 3 `fixDequantize` scalar speedup 0.994, spreads 1.0% and 0.5%
- run 4 `fixISqrt128High_u64` batch speedup 0.838, spreads 3.5% and 16.5%
- run 5 `fixPow` batch speedup 1.016, spreads 9.0% and 1.3%
- run 5 `fixUInt256DivMod` batch speedup 1.026, spreads 2.8% and 2.3%

A fast row (a few nanoseconds) reaches a spread above 100% when one of the seven
repeats stalls: 200000 iterations is a short timed region. The median moves much
less than that spread. A spike still has to beat the spread `ab.sh` prints for the
operation it touches.
