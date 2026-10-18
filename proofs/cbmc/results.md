# CBMC results: native __int128 arm == emulated int128 arm

Run: `make -k -j2 --output-sync=target -C proofs/cbmc all` on amd-1 (Linux x86_64,
16 cores, shared and loaded, so times are indicative), CBMC 6.6.0, 2026-10-18 22:21 to
22:40 UTC. Every run is
`cbmc --unwinding-assertions --bounds-check --signed-overflow-check
--undefined-shift-check --slice-formula --sat-solver cadical`, plus the per-property
`-D CBMC_WIDTH=<w> -D CBMC_WIDTH_B=<wb> -D CBMC_SIGNS=<s>`, `--function` and `--unwind`
in the table, under `timeout 1200`. CaDiCaL was tried first, as the card asks, but it is not
compiled into the bench CBMC 6.6.0 build ("The specified solver, 'cadical', is not
available. The default solver will be used instead."), and installing a CaDiCaL build is
outside this card. So every result below is MiniSat 2, CBMC's default; Z3 was tried as
well (table below). Each run also checks every bounds, overflow and shift property
instrumented in both arms (`0 of 390+ failed`), not only the equivalence assertions.

**No property is proved at full width except addsub, compares and shifts.** Everything
else is proved on the narrowed domain its row states, and is not a full proof.

## How to read a domain

`ext(w)` is a value whose low w bits are free and whose higher bits are all 0 or all 1;
the target runs both cases (the `CBMC_SIGNS` bits). Read as two's complement that is
every value in [-2^w, 2^w); read unsigned it is [0, 2^w) together with the top 2^w values
of the type. `zext(w)` is [0, 2^w) only. "full" is every value of the type.

## Results

| Target | Function(s) checked | Domain | Unwind | Result | Time (worst sign case) |
|---|---|---|---:|---|---:|
| `addsub` | fixUInt128Add/Sub, fixInt128Add/Sub, fixInt128FromI64, fixInt128ToI64 | full: every 128-bit a, b | 12 | VERIFICATION SUCCESSFUL (1 of 1 sign cases) | 3 s |
| `compares` | fixUInt128Eq/Lt/Gt/Le/Ge, fixInt128Eq/Lt/Gt/Le/Ge | full: every 128-bit a, b | 12 | VERIFICATION SUCCESSFUL (1 of 1 sign cases) | 1 s |
| `shifts` | fixUInt128Shl/Shr, fixInt128Shr, fixInt128ShiftLeft | full: every 128-bit a, every n in [0, 127] | 12 | VERIFICATION SUCCESSFUL (1 of 1 sign cases) | 14 s |
| `mul_u64` | fixUInt128MulU64 | a, b in ext(8) | 12 | VERIFICATION SUCCESSFUL (4 of 4 sign cases) | 152 s |
| `mul_u64_wide` | fixUInt128MulU64 | a full, b in zext(4) | 12 | VERIFICATION SUCCESSFUL (1 of 1 sign cases) | 4 s |
| `mul_i64` | fixInt128MulI64 | a, b in ext(8) | 12 | VERIFICATION SUCCESSFUL (4 of 4 sign cases) | 34 s |
| `mul_i64_wide` | fixInt128MulI64 | a full, b in zext(4) | 12 | VERIFICATION SUCCESSFUL (1 of 1 sign cases) | 22 s |
| `mul_128` | fixUInt128Mul, fixInt128Mul | 128-bit a, b in ext(6) | 12 | VERIFICATION SUCCESSFUL (4 of 4 sign cases) | 28 s |
| `mul_128_wide` | fixUInt128Mul, fixInt128Mul | a full (128-bit), b in zext(2) | 12 | VERIFICATION SUCCESSFUL (1 of 1 sign cases) | 3 s |
| `udivmod` | native / and % vs fixEmuUInt128Div/Mod | 128-bit a, b in ext(6), b != 0 | 130 | VERIFICATION SUCCESSFUL (4 of 4 sign cases) | 54 s |
| `sdiv` | fixInt128Div | 128-bit a, b in ext(8), b != 0 | 130 | VERIFICATION SUCCESSFUL (4 of 4 sign cases) | 8 s |
| `divrem64` | fixUInt128DivRemBy64 | hi, lo, d each in ext(4), hi < d | 130 | VERIFICATION SUCCESSFUL (8 of 8 sign cases) | 12 s |
| `fixmul` | fixMul | a, b in ext(8) | 12 | VERIFICATION SUCCESSFUL (4 of 4 sign cases) | 47 s |
| `fixmul_wide` | fixMul | a full, b in zext(4) | 12 | VERIFICATION SUCCESSFUL (1 of 1 sign cases) | 19 s |
| `fixdiv` | fixDiv (fast path only) | a, b in ext(8), b == 0 included | 130 | VERIFICATION SUCCESSFUL (4 of 4 sign cases) | 149 s |
| `fixdiv_wide` | fixDiv | a full, b in [0, 4), b == 0 included | 130 | VERIFICATION SUCCESSFUL (1 of 1 sign cases) | 17 s |
| `fixdiv_big` | fixDiv (128-bit path) | a = 2^47 | x or ~(2^47 | x), x in zext(4); b in ext(4), b == 0 included | 130 | VERIFICATION SUCCESSFUL (4 of 4 sign cases) | 5 s |
| `isqrt_low` | fixISqrt128High, hi == 0 | roots s < 2^8 (lo < 2^16); seed within 3 | 6 | VERIFICATION SUCCESSFUL (1 of 1 sign cases) | 253 s |
| `isqrt_mid` | fixISqrt128High, 0 < hi < 2^40 | roots in [2^32, 2^32 + 2^8); seed within 3 | 6 | VERIFICATION SUCCESSFUL (1 of 1 sign cases) | 176 s |
| `isqrt_mid_top` | fixISqrt128High, 0 < hi < 2^40 | roots in [2^52 - 2^8, 2^52); seed within 3 | 6 | VERIFICATION SUCCESSFUL (1 of 1 sign cases) | 434 s |
| `isqrt_high` | fixISqrt128High, hi >= 2^40 | hi in 2^40 + zext(8) or the top 2^8 values; lo in ext(8) | 66 | VERIFICATION SUCCESSFUL (4 of 4 sign cases) | 8 s |

Every row printed `VERIFICATION SUCCESSFUL` for every sign case (55 of 55 runs); `MAKE_EXIT=0`. The run used int128_equiv.c sha256 559aa300...df532d6 and Makefile sha256 ac6cd8d9...5416af8, the committed files.
No counterexample was found, so there was no finding to confirm with a C program.

## What the narrowings cover, and what they do not

- **fixDiv's 128-bit path** is taken only when |a| >= 2^47; below that fixDiv runs a
  64-bit divide that is the same code in both arms. The symmetric `fixdiv` domain
  (-256 <= a < 256) never leaves the fast path. `fixdiv_big` is the row aimed at the
  128-bit path: a = 2^47 | x or ~(2^47 | x) with x in zext(4), and b in ext(4), negative
  divisors and b == 0 included. `fixdiv_wide` adds every a with b in [0, 4).
- **Ones-extended inputs are the expensive case.** Constant high zero bits collapse the
  multiplier and divider circuits; constant high one bits do not. The `_wide` rows (one
  input full width) only zero-extend the narrow input.
- **fixISqrt128High** is split by its own three branches, which are disjoint and
  together cover every input, but each branch is checked only on a narrow slice:
  `isqrt_low` (hi == 0) only for roots below 2^8, that is lo < 2^16; `isqrt_mid` and
  `isqrt_mid_top` (0 < hi < 2^40) only for roots in [2^32, 2^32 + 2^8) and
  [2^52 - 2^8, 2^52); `isqrt_high` (hi >= 2^40) only for hi in 2^40 + [0, 2^8) or the top
  2^8 values, with lo in ext(8). Everything between is unchecked.
- **The sqrt seed is modelled, not computed, and its bound is an assumption.** CBMC
  has no body for `__builtin_sqrt` and would hand each arm a different free value. The
  harness gives both arms one shared seed, free within +-3 of the exact root. Sharing is
  sound because the seed's argument is the same double expression of (hi, lo) in both
  arms, with no 128-bit operation in it. That the real seed falls in the window is
  argued, not checked by CBMC: the double argument is within one rounding of n, and a
  correctly rounded sqrt of it truncates to within 2 of the root for n < 2^104. The claim
  for the two seeded branches is: the arms agree for every seed in that window.
- **The native arm is the C spelling.** It is built with `__x86_64__` undefined, so its
  128/64 divides are `/`, not the inline `divq` the x86-64 build runs.
- These are proofs about C semantics as CBMC models them, not about any compiler's code.

## Full-width and wider attempts that did not finish

None of these is a result; each is a timeout and nothing is inferred from it.

| Property | Domain tried | Solver | Timeout |
|---|---|---|---:|
| fixmul | full | MiniSat, Z3 | 1200 s (attempt 2), 600 s (Z3) |
| mul_u64, mul_i64, mul_128, udivmod, sdiv, divrem64, fixdiv | full | MiniSat | 150 s |
| mul_u64, mul_i64, mul_128, udivmod, sdiv, divrem64, fixdiv | full | Z3 (`SOLVER=--z3`) | 600 s |
| isqrt_high | full | MiniSat | 1200 s |
| isqrt_low | hi == 0, every root (zext 32) | Z3 | 600 s |
| isqrt_mid | every root in [2^32, 2^52) | Z3 | 600 s |
| mul_u64 | ext(12) zero case; full x zext(8) | MiniSat | 90 s, 60 s |
| mul_u64 | ext(16) ones case | Z3 | 120 s |
| mul_128 | ext(8) both ones; full x zext(4) | MiniSat | 300 s |
| udivmod | ext(8) dividend ones; full / zext(2) | MiniSat | 300 s |
| divrem64 | full / zext(2), d ones case | MiniSat | 300 s |
| fixmul | ext(12) one negative; full x zext(8) | MiniSat | 90 s, 60 s |
| fixmul, fixdiv, mul_i64, mul_u64 (wide) | full x zext(16) | Z3 | 120 s |
| fixdiv, udivmod | ext(16) | MiniSat | 60 s |
| divrem64 | hi, lo, d each ext(8), all zero-extended | MiniSat | 300 s |

The narrowings in the results table are the widest that finished in those probes. With
short probe timeouts they are **not** shown to be the widest that would finish in 20
minutes, and several rows finish in seconds; widening them toward the 20-minute bound is
owed (see "Not done").
Z3 is far faster than MiniSat on zero-extended inputs (mul_u64 on zext(32) x zext(32) in
under a second) but no faster on ones-extended ones, so it does not widen the symmetric
rows.

## Not done

- The card's step 3 asks for the widest narrowing that finishes within 20 minutes. The
  widths here come from probes with 60 to 300 s timeouts. Pushing each row toward 20
  minutes is not done.
- No CaDiCaL-enabled CBMC was available to try.
- No full-width proof of any multiply, divide or square-root property.

## Harness history

Attempt 2's harness declared reserved `__CPROVER_nondet_*` names, which CBMC reported as
`no-body` properties; it was corrected to the `nondet_` convention. This attempt reworked
the harness into one property per primitive, added the narrowing knobs and the shared
seed, and ran every property.
