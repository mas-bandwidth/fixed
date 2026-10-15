VERDICT: LEARNED

## Hypothesis

The vector/quaternion kernels (`fixCross`, `fixMulMV`, `fixMulQuat`, `fixRotateVector`,
`fixInvRotateVector`) are written as single-expression reductions, so the compiler may
serialize the independent 64x64->128 products through the additions instead of issuing
them back to back. Hoisting the products into named locals and keeping the additions in
their original order should let the scheduler overlap the multiplies without moving a
single rounding point.

## The change

In `include/fixed/fixed_vec.h`, the six products of `fixCross`, the nine of `fixMulMV`,
and the sixteen of `fixMulQuat` (native arm) are hoisted into named `fixed_t`/`fixInt128`
locals, then reduced in exactly the original order (same operands, same per-product
rounding, same left-to-right addition/subtraction). No rounding point, addition order, or
fused form changed; `fixRotateVector`/`fixInvRotateVector` keep their frozen two-cross
form and inherit the change only through `fixCross`. A unit test
`VecQuatKernelEquivalenceTest` (in `test/numerical_regression_test.c`) compares the
restructured kernels to the pre-restructuring bodies copied into the test, over the
structured edge grid and 10M seeded inputs per type, plus aliasing cases. This attempt
constrains the edge grid to raw magnitudes below 2^31: at 2^31 and above the additions in
`fixMulAdd`/`fixMulSub`/`fixCross` overflow signed 64-bit (verified with
`-fsanitize=signed-integer-overflow`), which is UB and made the candidate diverge from the
copied reference even though both are bit-identical arithmetic (the previous attempt fed
`INT64_MIN`/`INT64_MAX`/`FIX_MIN`/`FIX_MAX` and 2^k for k up to 61, and failed 115 cases
natively).

## Before/after (ns/op)

Measured on the Linux x86-64 bench the card names (AMD Ryzen 7 255 w/ Radeon 780M
Graphics, 16 logical CPUs, avx2 + avx512f; gcc 15.2.0, `-O3 -g -DNDEBUG -ffp-contract=off`),
7 interleaved repeats of 200000 iterations, from `benchmark/ab.sh`. `a` = baseline
(6251928), `b` = candidate. speedup = a_med/b_med (>1 = faster). The arm64 Apple-silicon
bench was not available to this run (no arm64 host; the working Mac is forbidden), so
these numbers cover x86-64 only.

### Native __int128

| op | mode | a_med | b_med | speedup | spread |
| --- | --- | ---: | ---: | ---: | ---: |
| fixCross | scalar | 4.683 | 4.683 | 1.000 | 6.0% |
| fixCross | batch | 7.741 | 7.733 | 1.001 | 55.6% |
| fixMulQuat | scalar | 6.104 | 6.112 | 0.999 | 64.0% |
| fixMulQuat | batch | 9.714 | 9.553 | 1.017 | 60.0% |
| fixRotateVector | scalar | 12.196 | 12.269 | 0.994 | 41.5% |
| fixRotateVector | batch | 12.629 | 12.657 | 0.998 | 27.0% |
| fixInvRotateVector | scalar | 12.203 | 12.261 | 0.995 | 14.7% |
| fixInvRotateVector | batch | 12.692 | 12.622 | 1.006 | 3.1% |

### Emulated 128-bit (FIX_FORCE_EMULATED_INT128)

| op | mode | a_med | b_med | speedup | spread |
| --- | --- | ---: | ---: | ---: | ---: |
| fixCross | scalar | 51.519 | 51.969 | 0.991 | 17.6% |
| fixCross | batch | 50.163 | 49.016 | 1.023 | 10.8% |
| fixMulQuat | scalar | 145.732 | 145.671 | 1.000 | 4.1% |
| fixMulQuat | batch | 147.792 | 147.092 | 1.005 | 15.2% |
| fixRotateVector | scalar | 130.379 | 129.280 | 1.008 | 5.3% |
| fixRotateVector | batch | 129.532 | 126.418 | 1.025 | 1.8% |
| fixInvRotateVector | scalar | 131.586 | 128.372 | 1.025 | 2.9% |
| fixInvRotateVector | batch | 126.929 | 124.835 | 1.017 | 3.2% |

No operation reaches the 1.2x KEEP threshold; every row is inside its own spread (the
largest gain, fixInvRotateVector scalar emulated at +2.5%, is inside a 2.9% spread).
`fixMulMV` and `fixTransformPoint` are not in the harness operation table, so they were
not timed (both are bit-exact via the unit test).

## Bit-exact evidence

- `ctest --test-dir build --output-on-failure`: 36/36 pass, native and emulated 128-bit
  arms; frozen hashes unchanged. `VecQuatKernelEquivalenceTest` reports 0 failures in both
  arms (the previous attempt's 115 native failures at the `fixInvRotateVector` edge case
  are gone once the overflow magnitudes are excluded).
- `benchmark/ab.sh`'s oracle leg and a direct `fixed_oracle_check` run both print
  `ORACLE: all operations match benchmark/oracle-baseline.txt (base_random=50000000)` for
  the BASELINE and the CANDIDATE, with identical per-op hashes for the touched operations:
  `fixCross 5a5555238d655a2c`, `fixMulQuat ff696a9b57ac0860`,
  `fixRotateVector 64afce887683c703`, `fixInvRotateVector a7d02b0685663640`. The committed
  baseline is not stale at the current tip (the prior staleness at c4f4510 was resolved by
  the intervening `fixMul` land on this stream).

## What to try next

- Measure on the arm64 Apple-silicon bench. On x86-64 the compiler (gcc, and clang in the
  previous attempt) already issues the independent `imulq`/`imul` products back to back
  with no spills, so the hoist is a scheduling no-op. arm64 is the interesting case: the
  128-bit product there is a library call, and the register pressure in
  `fixMulQuat`/`fixMulMV` may behave differently.
- If arm64 shows no gain either, close this spike: the single-expression reductions are
  already optimally scheduled by every compiler tried, and the only remaining lever is
  fusing, which the fixed3d round-3 bisections explicitly ruled out.

## What not to repeat

- Do not feed `INT64_MIN`/`INT64_MAX`/`FIX_MIN`/`FIX_MAX` or 2^k (k >= 31) to the
  multiplicative kernels in an equivalence test: the additions overflow signed 64-bit, the
  candidate and the copied reference diverge under the optimizer, and the test fails
  natively while passing emulated. Cap the edge grid below raw 2^31.
- Do not expect the hoist to help on x86-64: it is a bit-identical no-op there, in both
  the native and emulated arms.
- Do not trust `benchmark/oracle-baseline.txt` without checking: at the previous base it
  was stale. Here it is current, and both trees match it.
