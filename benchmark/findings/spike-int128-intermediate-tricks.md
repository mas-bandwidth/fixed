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
7 interleaved repeats x 200000 iterations, AMD Ryzen Threadripper 2990WX, x86-64, avx2
yes, bmi2 yes, adx yes, avx512* no, neon/sme no, load ~1.9-6.3):

| op | mode | a ns/op (med) | b ns/op (med) | a spread | b spread | speedup a/b |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| fixMul | scalar | 5.87 | 5.89 | 6.4% | 14.9% | 0.997 |
| fixMul | batch | 2.17 | 2.20 | 11.2% | 9.8% | 0.987 |
| fixDot | scalar | 6.31 | 6.62 | 7.7% | 8.6% | 0.954 |
| fixDot | batch | 3.18 | 3.24 | 8.4% | 8.1% | 0.983 |

Every speedup is <= 1 and inside the greater of the two spreads, so candidate (a) is
indistinguishable-or-slightly-slower. Emulated arm (`FIX_FORCE_EMULATED_INT128`): the
candidate does not touch that arm (the diff changes only the native branch), so the
emulated A/B is an A/A control and its per-op checksums are identical across both trees.
The emulated numbers are reported below as a raw range over the 14 interleaved samples
(7 A + 7 B) captured in `benchmark/experiments/.../raw.txt`, NOT a formal ab.sh min/median/
spread table, because that table was never generated for the emulated arm.

Not measured — named absences (accepted, not hidden):
- **arm64 is not measured at all.** Every timing number above is x86-64 only (Threadripper
  2990WX, BMI2+ADX). The arm64 `umulh`/`smulh` path was not run, so there
  is no Apple-silicon table and no evidence for or against a shift-free rounding win on
  arm64. The candidate (a) is a *transcription* on x86-64; whether arm64's compiler already
  collapses the 128-bit add+shift the same way is unmeasured.
- **Emulated arm ns/op is prose-only.** `raw.txt` carries the per-repeat emulated A/B lines
  (below) with matching checksums, but no min/median/spread table was computed for it.
- **`fixDotRaw` is not measured.** Only `fixDot` (the rounded composite) appears in the
  table; the raw 128-bit sum the brief's candidate (b) targets was never timed.
- **`fixInt128ShiftLeft`/`fixInt128ShiftRight` are not measured.** The brief names them as
  seam operations to A/B, but this spike stopped at candidate (a) (fixMul rounding) per the
  tracer-bullet contract, so no shift rows exist.
- **The unit test sweeps 300k seeded pairs, not the 100M the brief asks.** `SAMPLES(300000)`
  (with `INT128_SWEEP_SCALE == 1` default) over the structured edge grid crossed with itself;
  100 M would be a much longer non-clocked sweep and was not run.

Emulated A/B, raw range over 7 interleaved repeats each (checksum identical in every row:
`fixMul` scalar `6147586388597479916` / batch `2701586523624746469`, `fixDot` scalar
`1491924509133835468` / batch `8015697193`):

| op | mode | ns/op min | ns/op max |
| --- | --- | ---: | ---: |
| fixMul | scalar | 15.51 | 17.17 |
| fixMul | batch | 8.24 | 9.17 |
| fixDot | scalar | 17.60 | 18.72 |
| fixDot | batch | 16.17 | 18.85 |

Bit-exact evidence:
- `benchmark/ab.sh` compiled and ran the full output oracle against the candidate:
  `ORACLE: all operations match benchmark/oracle-baseline.txt (base_random=50000000)`
  (every per-op hash printed, including `ORACLE OK fixMul count=50063756 hash=eb1995a6b21bf036`).
- `ctest --test-dir build` printed the exact summary line

  `100% tests passed, 0 tests failed out of 36`

  (native AND emulated arms, including `oracle_unit` + `oracle_unit_emulated` and the frozen
  `determinism` hash unchanged; the full per-test ctest log with the 36 test names and the
  `Total Test time` line was not captured, only this summary token).
- The unit-tier `Int128SeamTricksEquivalenceTest` (added to `test/int128_test.c` in the
  patch) compares the candidate against a REF_NOINLINE plain-`__int128` reference over the
  structured edge grid (0, +-1, +-2^k, +-(2^k-1) for every k, INT64_MIN/MAX, FIX_MIN)
  crossed with itself plus 300000 seeded int64 pairs (negative operands included:
  round-half-up is not odd-symmetric), with a negative-control arm; zero mismatches. The
  brief named 100 million seeded pairs; the test runs 300k, so that portion of the sweep is
  thinner than specified.

What to try next: candidate (b) — accumulate `fixDotRaw`'s three widening products with
explicit adc chains (`__builtin_addcll` / `_addcarry_u64`) instead of `__int128` adds,
then round once; and candidate (c) — confirm the emulated arm already uses
`_umul128`/`__umulh` on MSVC and whether the schoolbook fallback is ever the hot path.
Both are still open, along with the four named absences above (arm64, emulated table,
fixDotRaw, the 100M sweep) if anyone re-opens this seam.

What not to repeat: rewriting `fixMul`'s native rounding by hand. The compiler already
emits the shift-free `imulq/addq/adcq/shrdq` sequence for the idiomatic spelling; an
explicit hi/lo form adds register moves and measures no faster. Any future fixMul work
must start from the disassembly, not from a "cheaper" source spelling.