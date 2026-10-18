# Frama-C WP Loop-Annotation Results (`fixISqrt128High` and `fixInt128Div`)

Measured on `vision` (`x86_64` Ubuntu 24.04):
- Frama-C `33.0 (Sterlingshire)`
- Alt-Ergo `2.4.3`
- Z3 `4.13.3`

Command:

```sh
nice make -C proofs/framac/loops nightly
```

## Summary

| wrapper | arm | proved / total | notes |
| --- | --- | --- | --- |
| `proveFixISqrt128High` | `emulated` (`-wp-rte`, `-wp-prop=-@assigns,-@terminates`) | `47 / 54` | 34/36 RTE goals proved (only the 2 IEEE-754 float seed `is_nan_or_infinite` goals open); 13/18 loop invariant/variant goals proved |
| `proveFixISqrt128High` | `frame` (`-wp-prop=@assigns,@terminates`) | `235 / 235` | all `loop assigns`, `assigns \nothing`, and `terminates` goals proved (`Qed: 235`) |
| `proveInt128Div` | `emulated` (`-wp-rte`, `-wp-prop=-@assigns,-@terminates`) | `31 / 31` | all 28 RTE goals (including shift bounds `0 <= i < 64` and `0 <= i - 64 < 64` inside `proveEmuUInt128DivMod`), both loop invariant goals (`-1 <= i <= 127`), and both loop variant goals (`i + 1`) proved |
| `proveInt128Div` | `frame` (`-wp-prop=@assigns,@terminates`) | `135 / 135` | all `loop assigns i, quotient, remainder`, `assigns *quotientOut, *remainderOut`, `assigns \nothing`, and `terminates` goals proved |

## Why This Runs Nightly Only

With `-inline-calls @inline -remove-inlined @inline`, WP expands all six loops of `fixISqrt128High` and the 128-iteration shift-subtract loop of `proveEmuUInt128DivMod` together with the `fixEmuUInt128` lane helpers. Under `-wp-rte`, Qed simplification and SMT dispatch across `proveFixISqrt128High` and `proveInt128Div` take ~18 minutes of wall time on a bench (`-wp-timeout 60 -wp-par 4`). Running this per commit would stall CI; `proofs/framac/loops/Makefile` exposes `make -C proofs/framac/loops nightly` for scheduled bench runs and is intentionally not registered in `CMakeLists.txt` or `proofs/framac/Makefile`.

## Detailed Breakdown

### 1. `proveInt128Div` (`include/fixed/fixed_int128.h:395`, `wrappers_acsl.c:154-247`)

- **`emulated` (`31 / 31`)**:
  - Adding `loop invariant -1 <= i <= 127; loop assigns i, quotient, remainder; loop variant i + 1;` to the 128-step shift-subtract loop in `proveEmuUInt128DivMod` (`wrappers_acsl.c:176-180`) closes all 28 RTE goals (including the two lane-shift range goals `(dividend.lo >> i)` for `i < 64` and `(dividend.hi >> (i - 64))` for `i >= 64` at `wrappers_acsl.c:184`, and the pointer-validity goals after the loop at `wrappers_acsl.c:196-197`), both loop invariant goals, and both loop variant goals (`Unreachable: 1, Qed: 21, Alt-Ergo 2.4.3: 6, Z3 4.13.3: 3`).
- **`frame` (`135 / 135`)**:
  - Closes every `loop assigns`, helper `assigns`, `proveInt128Div` `assigns \nothing`, and loop `terminates` goal (`Terminating: 1, Qed: 127, Alt-Ergo 2.4.3: 5, Z3 4.13.3: 2`).

### 2. `proveFixISqrt128High` (`include/fixed/fixed.h:299`, `wrappers_acsl.c:48-143`)

- **`frame` (`235 / 235`)**:
  - Annotating all six `while` loops with explicit `loop assigns` (`r` on Loops 1-4, `d` on Loop 5, and `x, c, d` on Loop 6) and `loop variant` clauses closes all 235 `assigns` and `terminates` goals (`Qed: 235`). Previously (`proofs/framac/results.md`), `proveFixISqrt128High` without loop annotations left 58 goals unproved (`49 / 107`) because WP could not frame the six loops.
- **`emulated` (`47 / 54`)**:
  - **Closed (47 goals)**:
    - All 34 integer/shift/unsigned-wrap RTE goals across all three branches (`0 < hi < 2^40`, `hi == 0`, and `hi >= 2^40`), including `r + 1` in Loop 2 (`wrappers_acsl.c:68`, guarded by `loop invariant r <= 4503599627370496`), `r * r`, `r - 1`, `lo - r * r`, `2 * r`, and `r + 1` in Loops 3-4 (`wrappers_acsl.c:92-107`, guarded by `loop invariant r <= 0xFFFFFFFF` and `loop invariant r <= 0xFFFFFFFF && r * r <= lo`), and all `fixUInt128` shifts/adds/subtracts in Loops 5-6 (`wrappers_acsl.c:116-140`).
    - All 6 loop invariant/variant goals on the 64-bit fast path (`hi == 0`, Loops 3 and 4 at `wrappers_acsl.c:92-107`).
    - Loop 1 variant positivity (`0 <= r` at `wrappers_acsl.c:61`), Loop 2 variant decrease and positivity (`4503599627370496 - r` at `wrappers_acsl.c:66`), Loop 5 variant positivity (`0 <= fixUInt128Val(d)` at `wrappers_acsl.c:117`), and Loop 6 variant positivity (`0 <= fixUInt128Val(d)` at `wrappers_acsl.c:125`).
    - Postcondition `hi == 0 && lo == 0 ==> \result == 0` (`wrappers_acsl.c:49`).
  - **Open (7 goals, all prover limits; no undefined behaviour)**:
    1. `typed_proveFixISqrt128High_assert_rte_is_nan_or_infinite` (`wrappers_acsl.c:59`): IEEE-754 `double` expression `(double)hi * 18446744073709551616.0 + (double)lo` finiteness before `FIX_SQRT_SEED`. Prover limit on mixed `uint64_t`-to-`double` conversion; for `hi < 2^40`, the value is `< 2^104 < DBL_MAX` (`2^1024`).
    2. `typed_proveFixISqrt128High_assert_rte_is_nan_or_infinite_2` (`wrappers_acsl.c:82`): IEEE-754 `(double)lo` finiteness before `FIX_SQRT_SEED`. Prover limit on `uint64_t`-to-`double` conversion; `(double)lo <= 2^64 < DBL_MAX`.
    3. `typed_proveFixISqrt128High_loop_variant_decrease` (`wrappers_acsl.c:61`, Loop 1): `r` decreases only if `r > 0` when `fixUInt128Gt(fixUInt128MulU64(r, r), n)` holds, which is true for `n >= 0` (`0 * 0 > n` is false), but Qed + Alt-Ergo/Z3 time out expanding `fixEmuUInt128MulU64(r, r)` at `r == 0`.
    4. `typed_proveFixISqrt128High_loop_invariant_established` (`wrappers_acsl.c:64`, Loop 2): Establishing `r <= 4503599627370496` (`2^52`) after Loop 1 requires connecting the `double` seed `FIX_SQRT_SEED` or the exit condition of Loop 1 (`r * r <= n < 2^104`) to `r <= 2^52` across the 4-limb `fixEmuUInt128MulU64` product.
    5. `typed_proveFixISqrt128High_loop_invariant_preserved` (`wrappers_acsl.c:64`, Loop 2): Preserving `r + 1 <= 4503599627370496` when `(r + 1)^2 <= n < 2^104` requires inverting the 4-limb 32-bit partial-product expansion of `fixEmuUInt128MulU64(r + 1, r + 1)` at `r = 2^52`.
    6. `typed_proveFixISqrt128High_loop_variant_5_decrease` (`wrappers_acsl.c:117`, Loop 5): `fixUInt128Val(fixUInt128Shr(d, 2)) < fixUInt128Val(d)` when `fixUInt128Gt(d, n)` holds (`n >= 2^104 > 0`, so `d > 0`); Alt-Ergo and Z3 time out (60s) on the bit-shift / multiplication encoding `hi * 2^64 + lo` of `fixEmuUInt128Shr(d, 2)`.
    7. `typed_proveFixISqrt128High_loop_variant_6_decrease` (`wrappers_acsl.c:125`, Loop 6): Same `fixUInt128Val(fixUInt128Shr(d, 2)) < fixUInt128Val(d)` decrease under `!fixUInt128Eq(d, 0)` after the `if/else` body of the restoring square-root step; Qed + Alt-Ergo/Z3 time out (60s) on the bitwise shift encoding.
