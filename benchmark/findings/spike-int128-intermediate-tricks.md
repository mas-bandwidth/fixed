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
indistinguishable-or-slightly-slower. Emulated arm (`FIX_FORCE_EMULATED_INT128`): the
candidate does not touch that arm (the diff changes only the native branch), and the
interleaved emulated A/B prints identical per-op checksums for both trees
(`fixMul` 6147586388597479916, `fixDot` 1491924509133835468 / 8015697193); ns/op is
~15.5-17.1 scalar / ~8.2-9.2 batch for both. No measured operation moved.

Bit-exact evidence:
- `benchmark/ab.sh` compiled and ran the full output oracle against the candidate:
  `ORACLE: all operations match benchmark/oracle-baseline.txt (base_random=50000000)`
  (every per-op hash printed, including `ORACLE OK fixMul count=50063756 hash=eb1995a6b21bf036`).
- `ctest --test-dir build`: 100% passed, 36/36 — native AND emulated arms, including
  `oracle_unit` + `oracle_unit_emulated` and the frozen `determinism` hash unchanged.
- The unit-tier `Int128SeamTricksEquivalenceTest` (added to `test/int128_test.c` in the
  patch) compares the candidate against a REF_NOINLINE plain-`__int128` reference over the
  structured edge grid (0, +-1, +-2^k, +-(2^k-1) for every k, INT64_MIN/MAX, FIX_MIN)
  crossed with itself plus 300k seeded int64 pairs (negative operands included:
  round-half-up is not odd-symmetric), with a negative-control arm; zero mismatches.

What to try next: candidate (b) — accumulate `fixDotRaw`'s three widening products with
explicit adc chains (`__builtin_addcll` / `_addcarry_u64`) instead of `__int128` adds,
then round once; and candidate (c) — confirm the emulated arm already uses
`_umul128`/`__umulh` on MSVC and whether the schoolbook fallback is ever the hot path.
Both are still open.

What not to repeat: rewriting `fixMul`'s native rounding by hand. The compiler already
emits the shift-free `imulq/addq/adcq/shrdq` sequence for the idiomatic spelling; an
explicit hi/lo form adds register moves and measures no faster. Any future fixMul work
must start from the disassembly, not from a "cheaper" source spelling.
