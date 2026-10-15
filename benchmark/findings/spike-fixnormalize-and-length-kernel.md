# VERDICT: DROP

## Hypothesis

`fixNormalize` (and the `FIX_HALF..2^47` fast paths of `fixGetLengthAndNormalize` and
`fixNormalizeQuat`) divides each component by the length. On x86-64 those three/four
64-bit hardware divides (`idiv`) are the remaining cost after the hardware-seeded
square root. Hypothesis: replace the per-component divides with ONE reciprocal of the
length, `R = floor(2^64 / d)`, then form each quotient by a 64x64->128 multiply
(`(n * R) >> 64`) and repair the at-most-one underestimate with an exact remainder
test. One divide instead of three should be faster.

## The change

Added `fixReciprocal64` (one `fixUInt128DivRemBy64`, i.e. a single `divq`),
`fixNormalizeComponentByRecip`, `fixNormalizeThreeByRecip` and `fixNormalizeQuatByRecip`
in `include/fixed/fixed_vec.h`. `fixNormalizeWithLength` uses the reciprocal path when
`length < 2^47` (the lifted components are below `2^46`, inside the `2^48` magnitude
precondition); `fixGetLengthAndNormalize` and `fixNormalizeQuat` use it on their existing
`FIX_HALF..2^47` fast paths. `fixLength` and the full-range 128-bit arm are untouched.
Outputs are bit-identical to baseline: the quotient is repaired to the exact truncated
integer, not approximated.

## Bit-exact evidence

- `ctest --test-dir build --output-on-failure`: 36/36 pass in native AND emulated arms
  (including the new `fixNormalizeKernelEquivalence` check over the structured edge grid,
  2^16..2^47 random vectors, near-unit quaternions, and raw-squared-length-1 tiny vectors).
- `./build/fixed_oracle_check` (candidate): `ORACLE: all operations match
  benchmark/oracle-baseline.txt (base_random=50000000)`.
- The `ab.sh` oracle pass over the candidate printed the same line.

## Before / after (ns/op), x86-64 only

AMD Ryzen 7 255 (Zen 5), gcc 15.2.0, `-O3 -g -DNDEBUG -ffp-contract=off`, 7 interleaved
repeats x 200000 iters, ops `fixLength fixNormalize fixNormalizeQuat fixNLerp`.
`speedup = a_median / b_median` (>1 = candidate faster).

| op | mode | before med | after med | speedup |
| --- | --- | ---: | ---: | ---: |
| fixLength | scalar | 8.94 | 8.95 | 0.998 |
| fixLength | batch | 2.87 | 2.89 | 0.992 |
| fixNormalize | scalar | 14.77 | 18.38 | 0.804 |
| fixNormalize | batch | 13.41 | 14.55 | 0.922 |
| fixNormalizeQuat | scalar | 86.74 | 186.55 | 0.465 |
| fixNormalizeQuat | batch | 91.29 | 191.27 | 0.477 |
| fixNLerp | scalar | 79.20 | 53.44 | 1.482 |
| fixNLerp | batch | 84.80 | 53.99 | 1.570 |

## What it taught

The reciprocal-multiply kernel is bit-exact (the remainder repair is correct and the
frozen hashes/oracle all hold), but it is NOT faster on this CPU: `fixNormalize` regresses
24% (scalar) / 8% (batch), and `fixNormalizeQuat` regresses 2.1x. The one divide it
saves is not free: `fixReciprocal64` runs a serializing `divq` (`volatile` inline asm)
that sits on the dependent chain, and each component then pays two 64x64 multiplies plus
a subtract/compare/add-back. On Zen 5 the hardware `idiv` is already fast enough that
one `divq` + 6-8 multiplies costs more than three independent `idiv`s. (`fixNLerp`
speeds up 1.48x for a reason not yet understood — it normalizes near-unit quaternions
where the reciprocal's `R` is a power-of-two shift — but the headline normalize kernels
regress, so the change is dropped.)

## What to try next

- A Newton-refined reciprocal from a table/double seed (no `divq` at all) may remove the
  serializing divide that dominates here; keep the exact remainder repair.
- Restrict the reciprocal path to `fixNormalize`/`fixGetLengthAndNormalize` only and
  leave `fixNormalizeQuat` (near-unit, `R == 2^48`) on the direct divide, since that is
  where the 2.1x regression lives.
- Measure on Apple silicon (arm64): the branch is regressing on a CPU whose division is
  cheap, but the conclusion could invert where `idiv` is costly.

## What not to repeat

- Do not assume "one divide + multiplies" beats "three divides" on AMD: the `volatile`
  `divq` is serializing and the multiply+correct overhead is larger than the saved
  divides on Zen 5.
- Do not ship the reciprocal path behind the quaternion fast path; it is a 2.1x
  regression there even though it is bit-exact.
