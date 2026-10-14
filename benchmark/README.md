# Performance work on fixed: the protocol

Every performance change to this library has to answer two questions with evidence: is it
faster, and does it return exactly the same bits. This directory holds one tool for each, plus
the script that runs both. A spike that cannot show both does not land.

| File | What it is |
| --- | --- |
| `op_bench.c` | ns per operation for every hot public operation, scalar (latency) and batch (throughput) |
| `oracle.c` | the bit-exact output oracle: one 64-bit hash per operation over a defined input set |
| `oracle_check.c` | runs the oracle and diffs it against `oracle-baseline.txt`; exits non-zero on any difference |
| `oracle-baseline.txt` | the frozen hashes, captured from the base this harness was built on (`eb059e6`) |
| `ab.sh` | baseline tree against candidate tree: interleaved timings, speedups, then the oracle |
| `cpu_info.sh` | the machine header every finding pastes |
| `numerical_bench.c` | the earlier harness, kept: its history is in the second half of this file |

The unit tier has a small piece of the oracle too: `test/oracle_unit_test.c`
(`OracleHashStableTest`, ctest `oracle_unit` and `oracle_unit_emulated`) hashes the first
1,000 inputs of every operation against values baked into the test. No clock, no files.

## Running a spike, start to finish

```sh
# 1. a bench machine, never the machine you work on; the header goes in the finding
sh benchmark/cpu_info.sh

# 2. two trees side by side: the base and your candidate
git worktree add --detach ../fixed-base origin/sprint/fixed-perf

# 3. the whole comparison: timings, speedups, oracle (native and emulated 128-bit)
benchmark/ab.sh ../fixed-base . fixMul fixDiv      # or no op names for all of them
```

`ab.sh` compiles **this tree's** `op_bench.c` against both trees with
`cc -O3 -DNDEBUG -ffp-contract=off` (add flags to both with `AB_CFLAGS`, pick the compiler
with `CC`), runs the two binaries interleaved for `AB_REPEATS` rounds (default 7: A B, B A,
A B, ...), and prints per operation and mode the min, median and spread of each side and
the speedup (base median / candidate median). A speedup no larger than the wider of the two
spreads is printed as `within spread`, which means "no measured change". A checksum that
differs between the two sides is printed as `CHECKSUM DIFFERS`: same inputs, different
outputs, so the candidate is not bit-exact and the oracle below will say where.

Then it builds `oracle_check` against the candidate, native and with
`-DFIX_FORCE_EMULATED_INT128`, diffs each against `oracle-baseline.txt`, and passes the base
tree's build as `--against`, so a difference is reported down to the exact input. `ab.sh`
exits non-zero when the oracle fails. `AB_ORACLE=0` skips the oracle (for a quick timing
look only; nothing is evidence of bit-exactness without it).

`ab.sh` refuses to start while another benchmark process is running on the box
(`pgrep -af`); it never stops anything. Two benchmarks on one machine measure each other.

The pass mark for a spike: the oracle passes on both arms, and the speedup is outside the
A/A noise for that operation ("The A/A noise floor" below), measured on the same
machine. Report the `cpu_info.sh` header and the whole `ab.sh` table, not a chosen row.

## op_bench

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DFIX_BUILD_BENCHMARKS=ON
cmake --build build --parallel 2
./build/fixed_op_bench                         # every operation, CSV on stdout
./build/fixed_op_bench --repeats 11 fixSqrt    # every row whose name starts with fixSqrt
./build/fixed_op_bench --list
```

CSV columns: `op,mode,ns_min,ns_median,ns_max,checksum,reps`. Each sample runs at least
`--min-ms` (default 20) after calibration; `--repeats` samples (default 7) give min, median
and max. Inputs are generated from `--seed` (default 42) before anything is timed.

* **scalar** is a dependent chain: each call's input carries the previous call's output
  through `x & zero`, where `zero` is a volatile the optimizer cannot see. That is latency.
  The chain itself costs an and, an xor and folding the output into one word; the
  `chain.overhead` row is exactly that with no operation, so subtract it when the
  difference matters.
* **batch** is independent calls over arrays of 4,096 inputs: throughput, and the baseline
  that a SIMD batch implementation has to beat.
* The **checksum** is computed from one fixed pass, so it is the same in both modes and
  between two builds of different trees on the same seed. It keeps the work from being
  elided; it is not a correctness check (that is the oracle).

The rows, and the input each one times:

| Row | Inputs (raw is the Q48.16 integer) |
| --- | --- |
| `fixMul` | both operands within +-32768 units |
| `fixDiv.lo` / `fixDiv.hi` | numerator below 2^46 raw (the one-hardware-divide tier) / between 2^48 and 2^61 raw (the 128-bit tier); divisor 1/16 to 4096 units |
| `fixSqrt.small` / `fixSqrt.large` | below 2^20 raw / 2^40 to 2^62 raw |
| `fixISqrt128High.le104` / `.gt104` | high word below 2^40 (seeded and repaired) / at or above 2^40 (the restoring loop) |
| `fixComputeCosSin`, `fixSin`, `fixCos` | angles in [-2 pi, 2 pi] |
| `fixAtan2` | both within +-1024 units |
| `fixLog2`, `fixExp2`, `fixPow` | (0, 2^31) units; [-16, 47) units; base (0, 16] units with exponent in [-4, 4] |
| `fixLerp`, `fixNLerp` | alpha in [0, 1] |
| vectors | components within +-256 units |
| quaternions, transforms | normalized quaternions made with integers |
| `fixInvertMatrix`, `fixSolve3` | diagonally dominant matrices |
| `fixQuantize`, `fixDequantize` | scale 65536 |
| `fixInt128Div` | numerator about 2^100, divisor below 2^50 |
| `fixUInt256DivMod` | dividend about 2^248, divisor about 2^124 |

Operations that do **not** exist in the library today, so have no row: an inverse or
reciprocal square root (`fixLength` and `fixNormalize` are the rsqrt-shaped work, and
`fixNormalizeQuat` and `fixISqrt128High` cover the same kernels), `fixTan`, `fixAsin`,
`fixAcos`, a natural `exp` or `log`, and any batch or SIMD entry point. A spike that adds one
adds its row here and its operation to the oracle in the same change.

## The oracle

`oracle.c` gives every operation a fixed input sequence and hashes every output word. The
sequence, in this order:

1. **The edge grid**: 0, +-1, +-2^k and +-(2^k - 1) for every k, `INT64_MAX`, `INT64_MIN` and
   `-INT64_MAX` (`FIX_MIN`), crossed with itself for binary operations. The 128-bit seam uses
   the same grid at 128 bits and the 256-bit divide at 256 bits. Operations with a documented
   domain use the part of the grid inside it (`fixLerp`: |a|, |b| <= 2^46, |alpha| <= 2^20;
   the quantize boundary: |value| <= 2^30). `fixAtan2` leaves out `INT64_MIN`, which is not a
   `fixed_t` value: on x86-64 with native 128-bit arithmetic `fixAtan2( 0, INT64_MIN )`
   raises SIGFPE in the tree this baseline was captured from (the emulated arm returns).
2. **Exhaustive or dense sweeps**: `fixSqrt` for every raw input below 2^32 (4.3 billion);
   `fixComputeCosSin`, `fixSin`, `fixCos` for every raw angle in [-2 pi, 2 pi] (823,551);
   `fixLog2` for every raw input in [1, 2^22] plus 4,096 points in every octave above;
   `fixExp2` for every raw input from -17 to 48 units (4.26 million, underflow through
   saturation).
3. **Random inputs**: 50 million per scalar operation from xoshiro256**, seeded from
   `ORACLE_SEED` in `oracle.c` and the operation's index, log-uniform in magnitude so every
   bit length is drawn equally often.
4. **Random structures** for the composite operations: 5 million each of vectors,
   quaternions (normalized ones made with integers, and arbitrary ones), matrices (general
   and diagonally dominant), transforms and AABBs, inside each operation's documented domain.

Nothing in the input generation calls the library, so a change to the library can never
change an input, only an output.

```sh
./build/fixed_oracle_check                      # all operations against oracle-baseline.txt
./build/fixed_oracle_check_emulated             # the same on the emulated 128-bit arm
./build/fixed_oracle_check --ops fixMul,fixDiv
./build/fixed_oracle_check --against /path/to/base/build/fixed_oracle_check   # exact input
./build/fixed_oracle_check --dump fixMul 0 10   # inputs 0..9 with their outputs, hex
./build/fixed_oracle_check --write benchmark/oracle-baseline.txt   # recapture (see below)
```

A difference prints the operation, the chunk of inputs it is in (from checkpoint hashes in
the baseline file, every 2^16 inputs below 2^20 and every 2^22 above), and the first input of
that chunk. With `--against <oracle_check built from the base tree>` it runs the base build
over that chunk and prints the exact first differing input with both outputs; `ab.sh` always
does this.

**Never recapture the baseline to make a change pass.** A moved hash is the failure. The only
honest recapture is a deliberate semantic change, made in its own pull request that says
which operations move and why, with the frozen suites under `test/` moving with it.

A whole run took about 45 seconds natively and about two and a half minutes emulated on the
bench of the A/A table below (one thread each), so it belongs in the bench tier, never in ctest.

## The A/A noise floor

`benchmark/ab.sh . .` with `AB_REPEATS=5`: the tree against itself, five interleaved rounds,
on hetzner1 (AMD Ryzen 7 PRO 8700GE, 16 threads, avx2 and avx512f/dq/vl/ifma, gcc 13.3.0,
`powersave` governor), 2026-10-14, at tree `eb059e6`. Every row came out `within spread`, which
is what an A/A run has to show.

**Read this table as the floor, and as a warning.** That machine was a working fleet member
with a load average between about 18 and 35 on 16 threads during the run, and it shows: medians
wander by 25% to 100% between rounds, and the A/A median ratio reaches 2x on the matrix rows.
The minimums are far steadier (the A/A min ratio is within 3% of 1 for half the rows and
within 20% for all of them), because the fastest round is the one the neighbours left alone.
So:

* run spikes on a quiet bench (load average below 1) and re-measure A/A there first; the
  numbers below are what a busy machine gives, not what the code costs;
* a spike's speedup must be outside the **wider spread** for its row on the machine it was
  measured on, and the min columns should move the same way as the medians;
* a row whose spread here is near or above 100% (`fixInvertMatrix`, `fixSolve3`,
  `fixUInt256DivMod`, `fixLength` batch) cannot show anything on a busy box at all.

The oracle half of the same run passed on both arms (28 operations bit-identical, native and
emulated), so the run also checked the whole tool chain end to end.

| Operation | Mode | min ns | median ns | A/A median ratio | wider spread | A/A min ratio |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| `chain.overhead` | scalar | 0.41 | 0.57 | 1.286 | 33% | 0.962 |
| `chain.overhead` | batch | 0.16 | 0.29 | 1.249 | 56% | 1.000 |
| `fixMul` | scalar | 1.84 | 2.46 | 1.131 | 30% | 1.001 |
| `fixMul` | batch | 0.74 | 1.33 | 1.218 | 56% | 0.963 |
| `fixDiv.lo` | scalar | 3.00 | 4.17 | 1.123 | 32% | 0.993 |
| `fixDiv.lo` | batch | 1.40 | 2.50 | 1.335 | 61% | 0.997 |
| `fixDiv.hi` | scalar | 5.07 | 6.89 | 1.105 | 30% | 0.995 |
| `fixDiv.hi` | batch | 2.39 | 4.07 | 1.272 | 58% | 0.988 |
| `fixSqrt.small` | scalar | 8.29 | 10.78 | 1.093 | 27% | 1.003 |
| `fixSqrt.small` | batch | 1.83 | 2.92 | 1.159 | 52% | 1.042 |
| `fixSqrt.large` | scalar | 9.73 | 12.26 | 1.106 | 28% | 1.044 |
| `fixSqrt.large` | batch | 2.19 | 3.08 | 0.823 | 83% | 1.037 |
| `fixISqrt128High.le104` | scalar | 8.64 | 10.62 | 0.945 | 30% | 1.007 |
| `fixISqrt128High.le104` | batch | 2.33 | 3.04 | 0.771 | 88% | 1.071 |
| `fixISqrt128High.gt104` | scalar | 194.56 | 247.22 | 0.874 | 47% | 1.069 |
| `fixISqrt128High.gt104` | batch | 192.01 | 253.36 | 0.856 | 50% | 1.039 |
| `fixComputeCosSin` | scalar | 27.47 | 30.54 | 0.928 | 26% | 1.053 |
| `fixComputeCosSin` | batch | 15.18 | 18.47 | 0.854 | 53% | 1.093 |
| `fixSin` | scalar | 26.05 | 31.20 | 0.920 | 29% | 1.048 |
| `fixSin` | batch | 14.72 | 16.53 | 0.691 | 59% | 1.016 |
| `fixCos` | scalar | 24.95 | 28.49 | 0.889 | 27% | 1.010 |
| `fixCos` | batch | 14.79 | 16.82 | 0.686 | 58% | 1.082 |
| `fixAtan2` | scalar | 12.66 | 14.40 | 0.917 | 30% | 1.058 |
| `fixAtan2` | batch | 7.66 | 12.89 | 0.962 | 53% | 1.062 |
| `fixLog2` | scalar | 101.12 | 138.15 | 1.010 | 35% | 1.038 |
| `fixLog2` | batch | 98.68 | 138.98 | 1.029 | 34% | 1.016 |
| `fixExp2` | scalar | 10.33 | 14.11 | 1.055 | 32% | 0.970 |
| `fixExp2` | batch | 7.73 | 12.55 | 1.091 | 45% | 0.973 |
| `fixPow` | scalar | 124.01 | 188.80 | 1.090 | 40% | 0.966 |
| `fixPow` | batch | 124.83 | 186.91 | 1.042 | 39% | 0.972 |
| `fixLerp` | scalar | 2.02 | 2.44 | 0.691 | 63% | 0.974 |
| `fixLerp` | batch | 0.89 | 0.94 | 0.676 | 56% | 0.937 |
| `fixDot` | scalar | 2.18 | 2.36 | 0.804 | 36% | 0.952 |
| `fixDot` | batch | 0.76 | 0.82 | 0.838 | 76% | 0.993 |
| `fixCross` | scalar | 3.85 | 6.18 | 1.093 | 72% | 0.999 |
| `fixCross` | batch | 3.70 | 4.20 | 0.672 | 95% | 1.008 |
| `fixLength` | scalar | 8.38 | 9.67 | 0.896 | 34% | 1.006 |
| `fixLength` | batch | 2.26 | 2.52 | 0.544 | 106% | 1.048 |
| `fixNormalize` | scalar | 16.98 | 20.69 | 0.936 | 29% | 0.981 |
| `fixNormalize` | batch | 6.56 | 11.33 | 0.929 | 53% | 0.990 |
| `fixMulQuat` | scalar | 6.70 | 10.74 | 1.070 | 53% | 1.009 |
| `fixMulQuat` | batch | 6.34 | 9.96 | 0.875 | 50% | 0.978 |
| `fixNormalizeQuat` | scalar | 15.30 | 19.57 | 1.010 | 27% | 0.968 |
| `fixNormalizeQuat` | batch | 5.88 | 10.37 | 1.028 | 49% | 0.948 |
| `fixRotateVector` | scalar | 11.80 | 22.80 | 1.013 | 62% | 1.016 |
| `fixRotateVector` | batch | 11.08 | 20.66 | 0.914 | 58% | 0.916 |
| `fixInvRotateVector` | scalar | 13.87 | 19.79 | 0.919 | 51% | 0.910 |
| `fixInvRotateVector` | batch | 12.51 | 19.96 | 0.921 | 55% | 1.006 |
| `fixNLerp` | scalar | 19.28 | 27.07 | 0.943 | 43% | 0.962 |
| `fixNLerp` | batch | 18.78 | 26.43 | 1.258 | 67% | 1.204 |
| `fixInvertMatrix` | scalar | 42.05 | 75.45 | 1.418 | 108% | 1.075 |
| `fixInvertMatrix` | batch | 43.96 | 96.12 | 2.015 | 102% | 0.954 |
| `fixSolve3` | scalar | 25.57 | 43.60 | 1.808 | 97% | 1.134 |
| `fixSolve3` | batch | 24.68 | 43.02 | 1.956 | 112% | 1.137 |
| `fixAABB_Transform` | scalar | 20.90 | 37.15 | 1.717 | 80% | 1.102 |
| `fixAABB_Transform` | batch | 19.57 | 36.66 | 1.547 | 71% | 1.007 |
| `fixQuantize` | scalar | 4.91 | 6.29 | 1.207 | 28% | 1.020 |
| `fixQuantize` | batch | 0.64 | 0.82 | 1.252 | 84% | 1.016 |
| `fixDequantize` | scalar | 3.02 | 3.68 | 1.248 | 37% | 1.056 |
| `fixDequantize` | batch | 0.56 | 0.68 | 1.249 | 108% | 1.061 |
| `fixInt128Div` | scalar | 4.21 | 4.48 | 1.020 | 30% | 0.999 |
| `fixInt128Div` | batch | 2.47 | 2.63 | 1.046 | 49% | 1.002 |
| `fixUInt256DivMod` | scalar | 24.31 | 25.80 | 0.906 | 102% | 1.004 |
| `fixUInt256DivMod` | batch | 24.68 | 26.48 | 0.830 | 83% | 0.975 |

## numerical_bench.c and the September 2026 history

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

### Profiling and optimization passes

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

### September 7, 2026 initial repair

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

### Follow-up: cheaper exact square-root correction

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

### Measurement limits

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
