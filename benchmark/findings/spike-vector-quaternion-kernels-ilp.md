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
structured edge grid (0, +-1, +-2^k, +-(2^k-1), INT64_MIN/MAX, saturation) and 10M seeded
inputs per type across magnitudes 2^-16..2^30, plus aliasing cases.

## Before/after (ns/op)

Measured on the only box available to this run: an x86-64 Darwin host (Apple clang 17.0.0,
`-O3 -g -DNDEBUG -ffp-contract=off`), 7 interleaved repeats, 200000 iterations. This is
NOT the target arm64/Linux bench the card names, so the numbers are a signal, not the
gate. `a` = baseline (c4f4510), `b` = candidate. speedup = a_med/b_med (>1 = faster).

| op | mode | a_med | b_med | speedup | spread |
| --- | --- | ---: | ---: | ---: | ---: |
| fixCross | scalar | 3.835 | 3.910 | 0.981 | 7.3% |
| fixCross | batch | 5.426 | 5.431 | 0.999 | 10.5% |
| fixMulQuat | scalar | 10.190 | 10.310 | 0.988 | 18.2% |
| fixMulQuat | batch | 16.577 | 15.670 | 1.058 | 15.9% |
| fixRotateVector | scalar | 15.845 | 13.795 | 1.149 | 16.3% |
| fixRotateVector | batch | 15.685 | 15.062 | 1.041 | 28.1% |
| fixInvRotateVector | scalar | 13.890 | 13.515 | 1.028 | 28.2% |
| fixInvRotateVector | batch | 14.738 | 14.783 | 0.997 | 11.3% |

No operation reaches the 1.2x KEEP threshold; the largest gain (fixRotateVector scalar,
+15%) is inside its own spread. `fixMulMV` and `fixTransformPoint` are not in the harness
operation table, so they were not measured.

## Bit-exact evidence

- `ctest --test-dir build`: 36/36 pass, native and emulated 128-bit arms, frozen hashes
  unchanged.
- `VecQuatKernelEquivalenceTest`: 0 failures (native and emulated).
- Direct oracle hash comparison (baseline vs candidate, per op) is IDENTICAL for
  `fixCross`, `fixMulQuat`, `fixRotateVector`, `fixInvRotateVector`.

The harness oracle (`benchmark/oracle_check` against `benchmark/oracle-baseline.txt`)
reports 6 operations differing, but this is a PRE-EXISTING staleness at BASE c4f4510: the
unchanged baseline tree fails the same 6 operations with the same hashes
(`fixMulQuat 89460e7fdc0afa12`, `fixRotateVector 10e435cf24ccc1e3`, `fixInvRotateVector
6f67b302b04a739b`, plus `fixNormalizeQuat`/`fixNLerp`/`fixAABB_Transform`). The committed
`oracle-baseline.txt` was generated against an older `fixed_vec.h` than the one the
harness landed on. The candidate's hashes equal the baseline's hashes for every operation
this spike touched, so the change itself is bit-exact.

## What to try next

- Re-measure on the target arm64 (Apple silicon) and Linux x86-64 benches; the register
  pressure in `fixMulQuat`/`fixMulMV` (visible as `## 8-byte Spill` in the disassembly)
  may behave differently on arm64, where the 128-bit multiply is a library call rather
  than a single `imul`.
- Regenerate `oracle-baseline.txt` at the current tip so the harness oracle is usable
  again (a separate card, not this one).

## What not to repeat

- Do not expect hoisting the products of `fixCross` to help on x86-64: clang already
  issues the six `imulq` back to back with no spills; the hoist is a no-op there.
- Do not trust `benchmark/oracle_check` against the committed baseline until the stale
  `oracle-baseline.txt` is regenerated; compare baseline-vs-candidate hashes directly
  instead.
