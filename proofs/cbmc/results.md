# CBMC results: native __int128 arm == emulated int128 arm

Environment: CBMC 6.6.0 with CaDiCaL 2.1.3 as an external DIMACS solver
(`--external-sat-solver cadical`; the bench CBMC build has no CaDiCaL compiled in), on
the Linux benches amd-2 (AMD, 16 cores) and vision (64 cores). Flags, as the Makefile
names them: `--unwinding-assertions --bounds-check --signed-overflow-check --undefined-shift-check --slice-formula --external-sat-solver cadical`, each run under
GNU `timeout 1200` (plants: `timeout 600` and `--stop-on-fail`).

Last full gate: `make -C proofs/cbmc all plant` at ff1c70f (a fresh clone of the pushed
branch; int128_equiv.c sha256 3ac04a37…, Makefile sha256 adfcf811…, the same two files
this commit carries), 2026-10-19T03:57:11Z to 04:43:42Z, split across two benches by target (amd-2: every
non-isqrt target and its plant; vision: the isqrt targets, their plants and the seed
plants): 55 of 55 proof runs printed VERIFICATION SUCCESSFUL, 59 of
59 planted runs were CAUGHT, `make` exit 0 on both, no timeout. The proof
runs' property counts: `** 0 of N failed` with N in 550, 552, 553, 556.

## What "width" means

A 64-bit input under `CBMC_WIDTH=w` is w free low bits extended with constant zeros (sign
bit 0) or constant ones (sign bit 1). The Makefile runs every sign combination a property
lists in `SIGNS_<target>`, so the union is every sign extension of a w-bit value: as
signed, the range [-2^w, 2^w - 1]; read unsigned, the values nearest 0 and nearest 2^64.
A 128-bit input is the same over 128 bits. `CBMC_WIDTH_B` narrows the second input
alone. 64 (for a 64-bit input) or 128 (for a 128-bit input) is the full domain. A
narrowed row is never a full proof.

## Per property

Times are the sum over the target's sign runs in the last gate. Command for each run:
`timeout 1200 cbmc -I include -D CBMC_WIDTH=<w> -D CBMC_WIDTH_B=<wb> -D CBMC_SIGNS=<s> [-D CBMC_SQRT_BASE=…] --function harness_<h> --unwind <u> <flags> native.c emulated.c harness.c`.
The Plant column is `make plant-<target>`: the same runs with `-D CBMC_PLANT --stop-on-fail`, each of which must stop on a violated "arms agree" assertion.

| Target | Harness | Domain proved | Unwind | Runs | Result | Time | Plant |
|---|---|---|---:|---:|---|---:|---|
| addsub | addsub | **full**: both 128-bit inputs free (u128/i128 add, sub, FromI64, ToI64) | 12 | 1 | VERIFICATION SUCCESSFUL | 2 s | CAUGHT 1/1 |
| compares | compares | **full**: both 128-bit inputs free (Eq/Lt/Gt/Le/Ge, signed and unsigned) | 12 | 1 | VERIFICATION SUCCESSFUL | 0 s | CAUGHT 1/1 |
| shifts | shifts | **full**: 128-bit input free, every count 0..127 | 12 | 1 | VERIFICATION SUCCESSFUL | 7 s | CAUGHT 1/1 |
| mul_u64 | mul_u64 | narrowed: a, b each a sign extension of 8 bits (4 sign runs) | 12 | 4 | VERIFICATION SUCCESSFUL | 75 s | CAUGHT 4/4 |
| mul_u64_wide | mul_u64 | narrowed: a full 64 bits, b in [0, 15] | 12 | 1 | VERIFICATION SUCCESSFUL | 2 s | CAUGHT 1/1 |
| mul_i64 | mul_i64 | narrowed: a, b in [-256, 255] | 12 | 4 | VERIFICATION SUCCESSFUL | 43 s | CAUGHT 4/4 |
| mul_i64_wide | mul_i64 | narrowed: a full 64 bits, b in [0, 15] | 12 | 1 | VERIFICATION SUCCESSFUL | 11 s | CAUGHT 1/1 |
| mul_128 | mul_128 | narrowed: a, b each a 128-bit sign extension of 6 bits, i.e. [-64, 63] (u128 and i128 wrapping products) | 12 | 4 | VERIFICATION SUCCESSFUL | 62 s | CAUGHT 4/4 |
| mul_128_wide | mul_128 | narrowed: a full 128 bits, b in [0, 3] | 12 | 1 | VERIFICATION SUCCESSFUL | 4 s | CAUGHT 1/1 |
| udivmod | udivmod | narrowed: a, b each a 128-bit sign extension of 6 bits, b != 0 (u128 `/` and `%` vs fixEmuUInt128Div/Mod) | 130 | 4 | VERIFICATION SUCCESSFUL | 79 s | CAUGHT 4/4 |
| sdiv | sdiv | narrowed: a, b in [-256, 255] as i128, b != 0 (fixInt128Div) | 130 | 4 | VERIFICATION SUCCESSFUL | 33 s | CAUGHT 4/4 |
| sdiv_min | sdiv_min | narrowed: a = INT128_MIN, b in [-256, 255] as i128, b != 0, so INT128_MIN / -1 included (fixInt128Div) | 130 | 2 | VERIFICATION SUCCESSFUL | 71 s | CAUGHT 2/2 |
| divrem64 | divrem64 | narrowed: hi, lo, d each a 64-bit sign extension of 4 bits, hi < d (fixUInt128DivRemBy64's precondition); sign runs 0, 2, 4, 5, 6, 7 (1 and 3 leave no input) | 130 | 6 | VERIFICATION SUCCESSFUL | 45 s | CAUGHT 6/6 |
| fixmul | fixmul | narrowed: raw a, b in [-256, 255] | 12 | 4 | VERIFICATION SUCCESSFUL | 14 s | CAUGHT 4/4 |
| fixmul_wide | fixmul | narrowed: raw a full 64 bits, raw b in [0, 15] | 12 | 1 | VERIFICATION SUCCESSFUL | 16 s | CAUGHT 1/1 |
| fixdiv | fixdiv | narrowed: raw a, b in [-256, 255], b == 0 included (fixDiv's 64-bit fast path) | 130 | 4 | VERIFICATION SUCCESSFUL | 288 s | CAUGHT 4/4 |
| fixdiv_wide | fixdiv | narrowed: raw a full 64 bits, raw b in [0, 3] (b == 0 included) | 130 | 1 | VERIFICATION SUCCESSFUL | 18 s | CAUGHT 1/1 |
| fixdiv_big | fixdiv_big | narrowed: fixDiv's 128-bit path, a in [2^47, 2^47 + 15] or [-2^47 - 16, -2^47 - 1], b in [-16, 15] (b == 0 included) | 130 | 4 | VERIFICATION SUCCESSFUL | 33 s | CAUGHT 4/4 |
| isqrt_low | isqrt_low | narrowed: hi == 0, every n whose integer root s is in [0, 255]; every IEEE-allowed seed (see below) | 6 | 1 | VERIFICATION SUCCESSFUL | 139 s | CAUGHT 1/1 |
| isqrt_mid | isqrt_mid | narrowed: 0 < hi < 2^40, every n whose root s is in [2^32, 2^32 + 255]; every IEEE-allowed seed | 6 | 1 | VERIFICATION SUCCESSFUL | 294 s | CAUGHT 1/1 |
| isqrt_mid_top | isqrt_mid | narrowed: same path, root s in [2^52 - 256, 2^52 - 1] (`CBMC_SQRT_BASE=0xFFFFFFFFFFF00`); every IEEE-allowed seed | 6 | 1 | VERIFICATION SUCCESSFUL | 551 s | CAUGHT 1/1 |
| isqrt_high | isqrt_high | narrowed: hi >= 2^40 path (no seed), hi in [2^40, 2^40 + 255] or [2^64 - 256, 2^64 - 1], lo a sign extension of 8 bits | 66 | 4 | VERIFICATION SUCCESSFUL | 163 s | CAUGHT 4/4 |

## The sqrt seed (isqrt rows)

Nothing about the seed is assumed beyond the IEEE 754 contract of sqrt, in round-to-nearest
mode, with the library's `x` evaluated in double precision (FLT_EVAL_METHOD 0, no
fast-math); fixed's determinism promise already requires both. `fixISqrt128High`
seeds its repair loops with `r = (uint64_t)sqrt(x)`, `x` the double built from (hi, lo).
CBMC has no body for `__builtin_sqrt`, so both arms' call is routed to one stub
(int128_equiv.c, SQRT SEED and SEED MODEL):

1. The harness rebuilds `x` with the library's own expression; CBMC models the integer to
   double conversions, the scale by 2^64 and the rounded add bit-exactly. The stub
   asserts (`seed argument is the library's double expression`) that each arm passes
   exactly that `x`.
2. sqrt is correctly rounded, so `|t - sqrt(x)| <= ulp(t)/2 <= 1/2` (here `x <= 2^104`,
   so `t <= 2^52`). With `r = floor(t)`: `(2r - 1)^2 <= 4x < (2r + 3)^2`, exact integers.
   The seed is ANY r meeting that condition, one value shared by both arms; the
   production seed is always one of them.
3. r is drawn as `s + d`, `|d| <= CBMC_SEED_WINDOW` (4), s the exact root. Two checked
   assertions (`every allowed seed is above the window floor`, `... below the window ceiling`) show `s - 5` and `s + 5` fail the condition; both sides of the condition are
   monotone in r, so every allowed r is inside the window and none is cut off.
4. The seed lemma is checked, not assumed: `seed within CBMC_SEED_BOUND of the exact root`, `|r - s| <= 3`, for every allowed r.
5. The repair loops are bounded by `--unwinding-assertions` (unwind 6) for every allowed
   seed.

Each seed check is shown able to fail: `plant-seed-window-low/mid` (`CBMC_SEED_WINDOW=0`)
violate a window assertion and `plant-seed-bound-low/mid` (`CBMC_SEED_BOUND=0`) violate the
lemma, all four CAUGHT in the last gate (plant-seed-bound-low, plant-seed-bound-mid, plant-seed-window-low, plant-seed-window-mid).

CBMC's own math.h `sqrt` is not used: its library model (dumped with
`--show-goto-functions`) picks a nondet `lower` with `lower*lower <= d < up*up` computed
with ROUNDED multiplies, which is not the IEEE operation.

A receipt beside the proof, not a proof: a C program on amd-2 (gcc -O2, `sqrtsd`)
computed the real `(uint64_t)__builtin_sqrt` seed for 22,164,735 inputs (every root of the
three narrowed isqrt domains, at every n of `[s^2, s^2 + 2s]` when 2s < 4096 and else at
4,097 strided points plus both ends, and 20 million random n with roots below 2^52) and
found every seed meeting the condition in 2 and within 3 of the root (0 bad).

## Vacuity

`make plant` is the vacuity check: a sign run whose assumptions leave no input cannot
reach its assertion and passes even with the plant, which the Makefile reports as
NOT CAUGHT and fails. Every run in the gate was CAUGHT. divrem64 no longer runs sign
combinations 1 and 3 (hi ones-extended, d zero-extended): `hi >= 2^64 - 16 > d` there,
so `hi < d` leaves no input; `make plant-divrem64 SIGNS=1` prints `NOT CAUGHT (vacuous or unreachable)`. The union of runs 0, 2, 4, 5, 6 and 7 is every `hi < d` in the domain.

## Timed out (no proof inferred)

Each exited 124 under `timeout`; a timeout is not a result.

| Property, domain | Solver | Cap |
|---|---|---:|
| fixmul, a and b full 64 bits | CaDiCaL (external, CBMC) | 1200 s (this attempt, amd-1) |
| fixmul, a and b full 64 bits | MiniSat 2 (CBMC), Z3 (CBMC) | 1200 s each (attempt 2) |
| fixmul, a and b full 64 bits | Boolector 3.2.4 (ESBMC 8.5.0) | 600 s (attempt 4) |
| mul_u64, mul_i64, full 64 bits | MiniSat 2 (CBMC) | 150 s (attempt 4) |
| udivmod, sdiv, full 128 bits | MiniSat 2 (CBMC) | 150 s (attempt 4) |

## ESBMC is not used

ESBMC merges same-named `static inline` functions across translation units, so fed the
three TUs it runs one body for both arms (a full-width fixmul "passed" in 1 s in
attempt 4). That result is void, and the gate is CBMC only. CBMC links the arms
correctly: `--show-goto-functions` lists `fixMul` and `fixMul$link1`, the first on the
emulated struct type and the second on `signedbv[128]`.

## Owed widenings

Everything except addsub, compares and shifts is narrowed. Owed, per function, from the
widths above to full width:

- fixMul: 8-bit sign extensions of a and b; and a full 64 bits with b in [0, 15] only.
- fixDiv: fast path 8 bits; a full 64 bits with b in [0, 3] only (negative b owed);
  128-bit path |a| >= 2^47 with a 4-bit free part and b in [-16, 15].
- fixISqrt128High: an 8-bit slice of each of the three branches.
- 64x64 products: 8 bits; a full with b in [0, 15] only (negative b owed).
- 128x128 products: 6 bits; a full with b in [0, 3] only (negative b owed).
- udivmod 6 bits, sdiv 8 bits (and INT128_MIN against 8-bit divisors), divrem64 4 bits.

Also owed, because CBMC does not check it: that the inline `divq` the native x86-64 arm
uses gives the same quotient as the C `/` checked here.

## Coverage of the primitives

fixMul uses fixInt128MulI64, Add, FromI64, Shr, Gt, Lt and ToI64; fixDiv uses
fixInt128Div, ShiftLeft, FromI64, Gt, Lt and ToI64; fixISqrt128High uses fixUInt128MulU64,
Add, Sub, Shl, Shr, Eq, Ge, Gt, Le, Make, Lo and FromU64 (read from fixed.h). Each has its
own harness above (mul_i64, mul_u64, addsub, shifts, compares, sdiv, sdiv_min), except
FromU64, Make and Lo, which are checked only inside the isqrt harnesses and the output
wrappers. INT128_MIN / -1 is defined in both arms since the base landed
fixed-int128-div-min-by-minus-one-defined; sdiv no longer excludes it and sdiv_min
checks it.

## Scope

Agreement of the two arms' C code, for the domains above. The native arm is built with
`__x86_64__` undefined, so the C `/` spelling is checked, not the inline `divq`. This is
not a proof of correctness of either arm against exact arithmetic, and says nothing about
compiler code generation. Bench only; not part of CTest or CI.
