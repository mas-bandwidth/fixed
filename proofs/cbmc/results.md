# CBMC results: native __int128 arm == emulated int128 arm

Environment: CBMC 6.6.0 on amd-1 (Linux x86_64, AMD, 16 cores), `make -C proofs/cbmc all`
with the flags the Makefile names (`--unwinding-assertions --bounds-check
--signed-overflow-check --undefined-shift-check --slice-formula --sat-solver cadical`),
each run under GNU `timeout 1200`. The bench CBMC 6.6.0 has no CaDiCaL compiled in: every
log says "The specified solver, 'cadical', is not available. The default solver will be
used instead." and the runs use MiniSat 2. No CaDiCaL-enabled CBMC exists on amd-1 or
amd-2 (no `cadical` binary on PATH either).

Last full gate: `make -C proofs/cbmc all` at c351eab (int128_equiv.c sha256 559aa300…,
Makefile sha256 ac6cd8d9…, the same two files this commit carries), 2026-10-18
22:42–23:12 UTC: 55 of 55 runs printed VERIFICATION SUCCESSFUL, `make` exit 0, no
counterexample and no timeout. Every run reports `** 0 of 417 failed` (419 to 423 for the
isqrt harnesses, which carry extra assumption-guarded properties).

## What "width" means

A 64-bit input under `CBMC_WIDTH=w` is w free low bits extended with constant zeros (sign
bit 0) or constant ones (sign bit 1). The Makefile runs every sign combination a property
lists in `SIGNS_<target>`, so the union is every sign extension of a w-bit value: as
signed, the range [-2^w, 2^w - 1]; read unsigned, the values
nearest 0 and nearest 2^64. A 128-bit input is the same over 128 bits. `CBMC_WIDTH_B`
narrows the second input alone. 64 (for a 64-bit input) or 128 (for a 128-bit input) is
the full domain. A narrowed row is never a full proof.

## Per property

Times are the sum over the target's sign runs in the last gate. Command for each run:
`timeout 1200 cbmc -I include -D CBMC_WIDTH=<w> -D CBMC_WIDTH_B=<wb> -D CBMC_SIGNS=<s>
[-D CBMC_SQRT_BASE=…] --function harness_<h> --unwind <u> <flags> native.c emulated.c harness.c`.

| Target | Harness | Domain proved | Unwind | Runs | Result | Time |
|---|---|---|---:|---:|---|---:|
| addsub | addsub | **full**: both 128-bit inputs free (u128/i128 add, sub, FromI64, ToI64) | 12 | 1 | VERIFICATION SUCCESSFUL | 2 s |
| compares | compares | **full**: both 128-bit inputs free (Eq/Lt/Gt/Le/Ge, signed and unsigned) | 12 | 1 | VERIFICATION SUCCESSFUL | 1 s |
| shifts | shifts | **full**: 128-bit input free, every count 0..127 | 12 | 1 | VERIFICATION SUCCESSFUL | 9 s |
| mul_u64 | mul_u64 | narrowed: a, b each a sign extension of 8 bits (4 sign runs) | 12 | 4 | VERIFICATION SUCCESSFUL | 57 s |
| mul_u64_wide | mul_u64 | narrowed: a full 64 bits, b in [0, 15] | 12 | 1 | VERIFICATION SUCCESSFUL | 2 s |
| mul_i64 | mul_i64 | narrowed: a, b in [-256, 255] | 12 | 4 | VERIFICATION SUCCESSFUL | 36 s |
| mul_i64_wide | mul_i64 | narrowed: a full 64 bits, b in [0, 15] | 12 | 1 | VERIFICATION SUCCESSFUL | 9 s |
| mul_128 | mul_128 | narrowed: a, b each a 128-bit sign extension of 6 bits, i.e. [-64, 63] (u128 and i128 wrapping products) | 12 | 4 | VERIFICATION SUCCESSFUL | 7 s |
| mul_128_wide | mul_128 | narrowed: a full 128 bits, b in [0, 3] | 12 | 1 | VERIFICATION SUCCESSFUL | 1 s |
| udivmod | udivmod | narrowed: a, b each a 128-bit sign extension of 6 bits, b != 0 (u128 `/` and `%` vs fixEmuUInt128Div/Mod) | 130 | 4 | VERIFICATION SUCCESSFUL | 27 s |
| sdiv | sdiv | narrowed: a, b in [-256, 255] as i128, b != 0, INT128_MIN / -1 excluded (fixInt128Div) | 130 | 4 | VERIFICATION SUCCESSFUL | 15 s |
| divrem64 | divrem64 | narrowed: hi, lo, d each a 64-bit sign extension of 4 bits (8 sign runs), hi < d (fixUInt128DivRemBy64's precondition) | 130 | 8 | VERIFICATION SUCCESSFUL | 24 s |
| fixmul | fixmul | narrowed: raw a, b in [-256, 255] | 12 | 4 | VERIFICATION SUCCESSFUL | 31 s |
| fixmul_wide | fixmul | narrowed: raw a full 64 bits, raw b in [0, 15] | 12 | 1 | VERIFICATION SUCCESSFUL | 16 s |
| fixdiv | fixdiv | narrowed: raw a, b in [-256, 255], b == 0 included (fixDiv's 64-bit fast path) | 130 | 4 | VERIFICATION SUCCESSFUL | 338 s |
| fixdiv_wide | fixdiv | narrowed: raw a full 64 bits, raw b in [0, 3] (b == 0 included) | 130 | 1 | VERIFICATION SUCCESSFUL | 15 s |
| fixdiv_big | fixdiv_big | narrowed: fixDiv's 128-bit path, a in [2^47, 2^47 + 15] or [-2^47 - 16, -2^47 - 1], b in [-16, 15] (b == 0 included) | 130 | 4 | VERIFICATION SUCCESSFUL | 17 s |
| isqrt_low | isqrt_low | narrowed: hi == 0, every n whose integer root s is in [0, 255] | 6 | 1 | VERIFICATION SUCCESSFUL | 183 s |
| isqrt_mid | isqrt_mid | narrowed: 0 < hi < 2^40, every n whose root s is in [2^32, 2^32 + 255] | 6 | 1 | VERIFICATION SUCCESSFUL | 376 s |
| isqrt_mid_top | isqrt_mid | narrowed: same path, root s in [2^52 - 256, 2^52 - 1] (`CBMC_SQRT_BASE=0xFFFFFFFFFFF00`) | 6 | 1 | VERIFICATION SUCCESSFUL | 549 s |
| isqrt_high | isqrt_high | narrowed: hi >= 2^40 path, hi in [2^40, 2^40 + 255] or [2^64 - 256, 2^64 - 1], lo a sign extension of 8 bits | 66 | 4 | VERIFICATION SUCCESSFUL | 103 s |

The isqrt rows rest on one stated ASSUMPTION (int128_equiv.c, SQRT SEED): both arms get
one shared seed within 3 of the exact root, standing in for `__builtin_sqrt`, which CBMC
cannot model. The claim is agreement for every seed in that window.

## Timed out (no proof inferred)

Each of these exited 124 under `timeout`; a timeout is not a result.

| Property, domain | Solver | Cap |
|---|---|---:|
| fixmul, a and b full 64 bits | MiniSat 2 (CBMC), Z3 (CBMC) | 1200 s each (attempt 2) |
| fixmul, a and b full 64 bits | Boolector 3.2.4 (ESBMC 8.5.0) | 600 s |
| mul_u64, mul_i64, full 64 bits | MiniSat 2 (CBMC) | 150 s |
| udivmod, sdiv, full 128 bits | MiniSat 2 (CBMC) | 150 s |
| mul_u64, 12- and 16-bit sign extensions | MiniSat 2 (CBMC) | 90 s |
| mul_u64, mul_i64, mul_128, udivmod, sdiv, fixmul, a full / b 4 bits with sign bit set | MiniSat 2 (CBMC) | 90 s |
| mul_u64, 12-bit sign extension (sign runs 3) | Boolector (ESBMC) | 300 s |
| isqrt_low, root 32 bits (the whole hi == 0 path) | MiniSat 2 (CBMC) | 600 s |
| isqrt_low, root 8 bits, under Z3 | Z3 (CBMC) | 120 s |

## ESBMC as a second engine

ESBMC 8.5.0 (`/home/nova/sdk/bin/esbmc`, Boolector, Z3, Bitwuzla) was tried as the
coordinator asked. Fed the three translation units directly it is UNSOUND for this
harness: it merges same-named `static inline` functions across TUs, so both arms ran one
body (a full-width fixmul "passed" in 1 s, and mul_u64 aborted with "argument type
mismatch: got unsignedbv, expected struct" on `fixUInt128Hi`). That result is void.
With each arm preprocessed (`gcc -std=gnu11 -E -P`) and its `fix*`/`out_*` names
prefixed per arm, ESBMC + Boolector proved fixmul with sign runs 3 at 8 bits in 15 s
(CBMC: 47 s) and at 12 bits in 85 s, but full-width fixmul still timed out at 600 s.
That pipeline is not in the Makefile; the gate is CBMC only.

CBMC links the arms correctly: `--show-goto-functions` lists `fixMul` and
`fixMul$link1`, `fixUInt128Hi` and `fixUInt128Hi$link1`, `out_u` and `out_u$link1`, the
first on the emulated struct type and the second on `signedbv[128]`.

## Owed widenings

Everything except addsub, compares and shifts is narrowed. Owed, per function, from the
widths above to full width:

- fixMul: 8-bit sign extensions of a and b; and a full 64 bits with b in [0, 15] only.
  Full a with NEGATIVE b is not proved (it timed out at 90 s with b's sign bit set).
- fixDiv: fast path 8 bits; a full 64 bits with b in [0, 3] only (negative b owed);
  128-bit path |a| >= 2^47 with a 4-bit free part and b in [-16, 15].
- fixISqrt128High: an 8-bit slice of each of the three branches.
- 64x64 products: 8 bits; a full with b in [0, 15] only (negative b owed).
- 128x128 products: 6 bits; a full with b in [0, 3] only (negative b owed).
- udivmod 6 bits, sdiv 8 bits, divrem64 4 bits.

Also owed, because CBMC does not check them: the SQRT SEED assumption (both arms pass the
same double to the seed, and the real seed is within 3 of the exact root), and that the
inline `divq` the native x86-64 arm uses gives the same quotient as the C `/` checked here.

No CaDiCaL-enabled CBMC was available to try; the Makefile's `--sat-solver cadical` falls
back to MiniSat 2 on these benches. The likely route to the widenings: the per-arm renamed
ESBMC pipeline above (about 3x faster than CBMC + MiniSat here), or a CBMC build with
CaDiCaL, at 20 minutes per harness.

## Coverage of the primitives

fixMul uses fixInt128MulI64, Add, FromI64, Shr, Gt, Lt and ToI64; fixDiv uses
fixInt128Div, ShiftLeft, FromI64, Gt, Lt and ToI64; fixISqrt128High uses fixUInt128MulU64,
Add, Sub, Shl, Shr, Eq, Ge, Gt, Le, Make, Lo and FromU64 (read from fixed.h). Each has its
own harness above (mul_i64, mul_u64, addsub, shifts, compares, sdiv), except FromU64,
Make and Lo, which are checked only inside the isqrt harnesses and the output wrappers.

Vacuity: no harness checks that its assumptions leave a path through every assertion.
Some divrem64 sign runs make `hi < d` impossible (hi ones-extended, d zero-extended) and
pass vacuously. The union over the 8 runs still covers every hi < d in the domain.

## Scope

Agreement of the two arms' C code, for the domains above. The native arm is built with
`__x86_64__` undefined, so the C `/` spelling is checked, not the inline `divq`. This is
not a proof of correctness of either arm against exact arithmetic, and says nothing about
compiler code generation. Bench only; not part of CTest or CI.
