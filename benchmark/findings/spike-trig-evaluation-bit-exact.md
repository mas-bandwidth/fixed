VERDICT: LEARNED — the branchless octant selection and interleaved cos/sin scheduling are bit-identical but not faster (0.945–1.000 speedup, i.e. the candidate is equal or slightly slower), and the 64-bit-multiply idea is inapplicable because the Q32.32 products need 65–66 bits.

## Hypothesis

`fixComputeCosSin`, `fixAtan2` and the `fixLog2`/`fixExp2` ladder spend their time in Q32.32 arithmetic that routes every multiply through a full 128-bit `fixInt128MulI64` and every divide through a 128-bit `fixInt128Div`. The hypothesis was that (a) interleaving the independent cos and sin polynomial evaluations would expose instruction-level parallelism the compiler cannot find across 128-bit multiplies, (b) a 64-bit multiply could replace the 128-bit one where the operand magnitudes provably fit, and (c) the atan2 octant selection could be made branchless. All three were to keep every rounding point, so output bits stay frozen.

## The change

In `src/fixed_math.c` only: (1) `fixAtan2`'s three octant-mapping `if` statements were rewritten as ternary selects (`r = (ay>ax) ? HALF_PI-r : r;` etc.), identical arithmetic, branchless; (2) `fixComputeCosSin` was restructured so the cosine reduction (`yc`, `y2`) and the sine reduction (`ys`, `u`) are computed together, then the two `fixQ32Div` calls are issued back-to-back, with the sign flips (`c = -c`, `s = -s`) moved after the divisions. Every multiply, divide, shift and rounding point is byte-for-byte the same expression; only the scheduling and control flow changed. The 64-bit-multiply idea (b) was analysed and NOT applied (see "what not to repeat").

## Before / after (native x86-64, AMD Ryzen 7 255, gcc 15.2.0, -O3 -DNDEBUG -ffp-contract=off, 7 interleaved repeats, 200000 iters)

`a` = baseline `c4f4510`, `b` = candidate. speedup = a_median / b_median (>1 means candidate faster). Only the native arm was measured; this box is the x86-64 bench and there is no arm64/Apple silicon here, and the emulated (`FIX_FORCE_EMULATED_INT128`) arm was not run within the wall.

| op | mode | a_med (ns) | b_med (ns) | speedup | a_spread | b_spread |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| fixComputeCosSin | scalar | 30.91 | 31.74 | 0.974 | 10.2% | 2.6% |
| fixComputeCosSin | batch | 17.98 | 18.32 | 0.982 | 1.4% | 2.2% |
| fixSin | scalar | 25.22 | 25.86 | 0.975 | 0.7% | 10.5% |
| fixSin | batch | 16.76 | 17.73 | 0.945 | 2.0% | 1.9% |
| fixCos | scalar | 23.38 | 23.78 | 0.983 | 0.5% | 0.4% |
| fixCos | batch | 16.68 | 17.60 | 0.948 | 21.6% | 1.6% |
| fixAtan2 | scalar | 12.42 | 12.42 | 0.999 | 0.3% | 0.2% |
| fixAtan2 | batch | 8.56 | 8.56 | 1.000 | 1.4% | 2.0% |
| fixLog2 | scalar | 2.03 | 2.84 | 0.715 | 40.9% | 22.1% |
| fixLog2 | batch | 48.40 | 48.67 | 0.994 | 1.1% | 1.0% |
| fixExp2 | scalar | 2.04 | 2.25 | 0.907 | 39.7% | 38.1% |
| fixExp2 | batch | 3.29 | 3.40 | 0.966 | 5.6% | 9.1% |
| fixPow | scalar | 2.83 | 2.83 | 1.000 | 28.7% | 21.4% |
| fixPow | batch | 49.32 | 51.14 | 0.964 | 2.0% | 1.3% |

`fixLog2`/`fixExp2`/`fixPow` were not touched; their rows are A/A noise (the sub-3 ns scalar rows carry 20–40% spread). The rows that matter — `fixComputeCosSin`, `fixSin`, `fixCos`, `fixAtan2` — are all at or below 1.000, i.e. the candidate is equal or a few percent slower. The interleaving did not help: the compiler already schedules the two independent divisions, and the branchless octant select is what the compiler already emitted for the `if` chain.

## Bit-exact evidence

The candidate is bit-identical to the baseline. `benchmark/ab.sh` compiles `oracle_check` against the candidate and it reports every operation matching the frozen baseline:

```
ORACLE OK   fixComputeCosSin  count=50823803  hash=75d77c6260933da6
ORACLE OK   fixSin            count=50823803  hash=ea6b89849492e203
ORACLE OK   fixCos            count=50823803  hash=92884abbd0d0dfc4
ORACLE OK   fixAtan2          count=6313756   hash=e783be1aa18a9e6c
ORACLE OK   fixLog2           count=9395980   hash=7d1ccc520e783eca
ORACLE OK   fixExp2           count=9395980   hash=a1d52ff7b1ceef98
ORACLE OK   fixPow            count=2563756   hash=55410e05742068c7
ORACLE: all operations match benchmark/oracle-baseline.txt (base_random=50000000)
```

`ctest --test-dir build --output-on-failure` passes 36/36 (native and emulated arms, frozen hashes unchanged). The new `TrigEvaluationExhaustiveTest` (test/trig_evaluation_exhaustive_test.c) compares the candidate against the baseline bodies copied into the test as reference over every raw angle in [-4 pi, 4 pi] (~1.6M values) for cos/sin and a 4097x4097 atan2 grid plus the axes and equal-magnitude edges, and passes.

## What to try next

The cost is not in the multiply scheduling — it is in the two 128-bit `fixQ32Div` calls and the `fixQ32Sqrt` normalization in `fixComputeCosSin`. A real speedup would have to reduce those: e.g. a single reciprocal of the denominator shared by cos and sin (they divide by `pi2 + y2` and `fivePi2 - 4*u` respectively, so no common denominator today), or a cheaper exact normalization. Those change the arithmetic and are out of scope for a bit-exact scheduling spike.

## What not to repeat

The 64-bit-multiply idea (b) does not apply here. In `fixComputeCosSin` the reduced angle `y` is in [-halfPi, halfPi] with `halfPi = 6746518852 ≈ 2^32.65`, so `y*y` reaches ~2^65.3 — beyond both int64 and uint64. In `fixAtan2`, `a = mn/mx` reaches exactly `2^32` when `|y| == |x|` (e.g. `atan2(1,1)`), so `a*a` reaches `2^64`, overflowing uint64. Only the four coefficient products in `fixAtan2` (`106688212*q`, `-404147609*q`, `r*s`, `r*c`) provably fit in 64 bits, and replacing just those four of seven multiplies is not worth the added code. Do not repeat the branchless/interleave scheduling change: it is bit-exact but measurably not faster.
