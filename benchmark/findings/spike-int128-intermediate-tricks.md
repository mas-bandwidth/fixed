VERDICT: LEARNED

Hypothesis: `fixMul`'s Q48.16 rounding `(a*b + 2^15) >> 16` at 128 bits can be made
cheaper by widening once and extracting the rounded result from the two 64-bit halves
as `((hi << 48) | (lo >> 16))` with an explicit carry out of the low lane, instead of a
128-bit add of the rounding half followed by a 128-bit arithmetic shift. The other two
candidates (adc-chain dot accumulation, emulated `_umul128`/`__umulh`) were not reached:
the tracer-bullet contract stops after the first full measurement, and candidate (a) is
a no-op, so (b) and (c) are recorded as "what to try next" rather than tried.

Change: the native (`FIX_INT128_EMULATED == 0`) arm of `fixMul` in `include/fixed/fixed.h`
was rewritten from the idiomatic 128-bit spelling to an explicit hi/lo form — widen once
(`fixUInt128 product = (fixUInt128)((fixInt128)a * b)`), add `FIX_HALF` to the low lane
with `rhi = hi + (rlo < lo)`, then assemble `(rhi << 48) | (rlo >> 16)`. The emulated arm
and the `FIX_SATURATE` tail are kept bit-identical (the saturated tail rebuilds the full
128-bit value from `rhi`/`rlo` and shifts arithmetically). The disassembly is the decisive
measurement here: the ORIGINAL spelling already compiles, under `cc -O3`, to the exact
shift-free sequence the candidate names — `imulq; addq $32768; adcq $0; shrdq $16` (6
instructions). The explicit hi/lo form compiles to the same critical path PLUS two extra
register moves (`movq %rdx,%rdi; movq %rax,%rdx; ...; movq %rdx,%rax` — 9 instructions),
so the hand-rewrite is a transcription, not a speedup.

Measured (baseline `a` vs candidate `b`, `benchmark/ab.sh`, same `op_bench.c` against both
trees, `cc (Ubuntu 15.2.0-16ubuntu1) 15.2.0`, `-O3 -g -DNDEBUG -ffp-contract=off`,
7 interleaved repeats x 200000 iterations, AMD Ryzen Threadripper 2990WX, avx2/bmi2/adx
yes, avx512f no, load ~1.9-6.3):

| op | mode | a ns/op (med) | b ns/op (med) | a spread | b spread | speedup a/b |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| fixMul | scalar | 5.87 | 5.89 | 6.4% | 14.9% | 0.997 |
| fixMul | batch | 2.17 | 2.20 | 11.2% | 9.8% | 0.987 |
| fixDot | scalar | 6.31 | 6.62 | 7.7% | 8.6% | 0.954 |
| fixDot | batch | 3.18 | 3.24 | 8.4% | 8.1% | 0.983 |

Every speedup is <= 1 and inside the greater of the two spreads, so candidate (a) is
indistinguishable-or-slightly-slower.

Measurement scope — what this table does NOT cover, named as absences rather than left
silent:
- arm64: NOT measured. The bench is x86-64 only (AMD Ryzen Threadripper 2990WX); no
  Apple silicon bench was available for this spike, so the umulh/smulh arm of the brief
  was not run. The candidate's native-arm change is x86-64-specific in its disassembly
  claim; an arm64 measurement would be required before any KEEP.
- fixDotRaw and fixInt128ShiftLeft/Right: NOT measured. Candidate (a) touched only
  fixMul's rounding; the tracer-bullet contract stops after the first full measurement,
  and (a) was a no-op, so candidates (b) (adc-chain fixDotRaw) and (c) (emulated
  _umul128/__umulh) were never reached and their operations were never timed.
- Emulated arm (`FIX_FORCE_EMULATED_INT128`): measured for bit-exactness only, reported
  as prose here rather than an ns/op table. The candidate does not touch that arm (the
  diff changes only the native branch), and the interleaved emulated A/B prints identical
  per-op checksums for both trees (`fixMul` 6147586388597479916, `fixDot`
  1491924509133835468 / 8015697193); ns/op is ~15.5-17.1 scalar / ~8.2-9.2 batch for
  both. No measured operation moved.

Bit-exact evidence:
- `benchmark/ab.sh` compiled and ran the full output oracle against the candidate:
  `ORACLE: all operations match benchmark/oracle-baseline.txt (base_random=50000000)`
  (every per-op hash printed, including `ORACLE OK fixMul count=50063756 hash=eb1995a6b21bf036`).
- `ctest --test-dir build --output-on-failure` (candidate tree, raw output):
  `100% tests passed, 0 tests failed out of 36` — `Total Test time (real) = 1.99 sec`.
  The 36 are the native arm (determinism, transcendental, wide_time, exhaustive, aabb,
  override_api, port_equality, int128, geometry, quantize, int256_division,
  inverse_envelope, usage, numerical_regression, numerical_saturated, oracle_unit, and
  the four negative controls) plus the emulated arm (determinism_emulated,
  transcendental_emulated, wide_time_emulated, exhaustive_emulated, aabb_emulated,
  port_equality_emulated, int128_emulated, geometry_emulated, inverse_envelope_emulated,
  int256_division_emulated, numerical_regression_emulated, oracle_unit_emulated,
  numerical_saturated_emulated, emulated_negative_control). The frozen `determinism`
  hash is unchanged. Full per-test output is in
  `benchmark/experiments/spike-int128-intermediate-tricks/raw.txt`.
- The unit-tier `Int128SeamTricksEquivalenceTest` (added to `test/int128_test.c` in the
  patch) compares the candidate against a REF_NOINLINE plain-`__int128` reference over the
  structured edge grid (0, +-1, +-2^k, +-(2^k-1) for every k, INT64_MIN/MAX, FIX_MIN)
  crossed with itself plus 300k seeded int64 pairs (negative operands included:
  round-half-up is not odd-symmetric), with a negative-control arm; zero mismatches.
  NOTE: the brief names 100 million seeded pairs; this test uses 300k. That is a
  deviation from the brief, named here rather than hidden — the exhaustive edge grid
  crossed with itself plus the oracle's 50M-input full-set hash carry the bulk of the
  coverage, but the 100M-pair sweep was not run.

What to try next: candidate (b) — accumulate `fixDotRaw`'s three widening products with
explicit adc chains (`__builtin_addcll` / `_addcarry_u64`) instead of `__int128` adds,
then round once; and candidate (c) — confirm the emulated arm already uses
`_umul128`/`__umulh` on MSVC and whether the schoolbook fallback is ever the hot path.
Both are still open.

What not to repeat: rewriting `fixMul`'s native rounding by hand. The compiler already
emits the shift-free `imulq/addq/adcq/shrdq` sequence for the idiomatic spelling; an
explicit hi/lo form adds register moves and measures no faster. Any future fixMul work
must start from the disassembly, not from a "cheaper" source spelling.
