# Results

Run 2026-10-18 on amd-2 (16 cores, shared: load average 6.7 to 15.7, sampled between runs), with
Frama-C 33.0 (Arsenic) and WP, Alt-Ergo 2.4.3, Z3 4.13.3 and Why3 1.8.2. Command:
`make -C proofs/framac nightly`, with `-wp-cache none`, so every goal went to the provers
and no result came from the cache. `make -C proofs/framac fixmul` and `make -C proofs/framac
sqrt` were then run again on their own, into fresh output directories, and gave the same
rows.

The scope is what is proved now. Three follow-on cards hold the rest: fixDiv
(fixed-framac-fixdiv-goal-generation), the loop annotations
(fixed-framac-isqrt-loop-annotations) and the native `__int128` path
(fixed-framac-native-int128-path).

## The arms

- **emulated**: `-DFIX_FORCE_EMULATED_INT128`, the runtime-error and ensures goals
  (`-wp-prop=-@assigns,-@terminates`), 60 s per goal.
- **emulated-saturate**: the same, with `-DFIX_SATURATE` (fixMul only).
- **frame**: the assigns and terminates goals, emulated path, 10 s per goal. These are
  split out because no library loop has loop assigns or a loop variant, and this card edits
  no header. WP falls back to `assigns everything` for each loop and cannot discharge them.
- **native**: no define. It is not in `nightly`; see "The native path is not checked".

## Per function, proved / total (the nightly set)

| wrapper | emulated | emulated + FIX_SATURATE | frame |
|---|---|---|---|
| proveFixMul | 14 / 14 | 15 / 15 | 112 / 112 |
| proveFixISqrt128High | 34 / 36 | - | 49 / 107 |
| proveInt128MulI64 | 0 / 0 | - | 14 / 14 |
| proveInt128Add | 0 / 0 | - | 21 / 21 |
| proveInt128Shr | 14 / 14 | - | 68 / 68 |
| proveInt128ShiftLeft | 7 / 7 | - | 27 / 27 |
| proveUInt128Shl | 7 / 7 | - | 16 / 16 |
| proveUInt128Shr | 7 / 7 | - | 16 / 16 |
| proveUInt128MulU64 | 0 / 0 | - | 7 / 7 |
| proveUInt128Add | 0 / 0 | - | 8 / 8 |
| proveUInt128Sub | 0 / 0 | - | 8 / 8 |
| proveInt128Div | 22 / 27 | - | not in nightly (see below) |
| proveInt128ToI64 | 0 / 0 | - | 4 / 4 |
| proveInt128Compare | 3 / 3 | - | 15 / 15 |
| proveUInt128Compare | 7 / 7 | - | 40 / 40 |
| proveFixDiv | not in nightly (see below) | - | not in nightly |

"0 / 0" is WP's "No goal generated": the primitive is unsigned or bitwise lane arithmetic
with no runtime-error condition.

Totals for the nightly set: runtime-error and ensures goals 130 / 137 proved; frame goals
405 / 463 proved.

## Every open goal, with its line and reason

### fixISqrt128High, emulated: 2 runtime-error goals (prover limit)

- `assert_rte_is_nan_or_infinite`: `\is_finite` of `(double)hi * 2^64`, the seed input at
  include/fixed/fixed.h:300, under `hi != 0 && hi < 2^40`.
- `assert_rte_is_nan_or_infinite_2`: `\is_finite` of the sum with `(double)lo`, also at
  include/fixed/fixed.h:300.

Reason: a prover limit, not undefined behaviour. Both goals hold, because the operands are
below 2^104, far under DBL_MAX. The proof needs IEEE-754 rounding reasoning. WP's float
model gives Alt-Ergo and Z3 the rounding as an uninterpreted function, and both time out at
60 s. Gappa 1.6.0, tried by hand on these two goals in pass 5, also returned Unknown. The FPA variant
of Alt-Ergo is listed by Why3, but it could not be selected from `-wp-prover`.

### fixISqrt128High, frame: 58 goals (owed to loop annotations)

- 52 assigns goals: `assigns_normal_part07` to `part35`, `part41`, `part42`, and `part73`
  to `part93`.
- 6 terminates goals: `terminates_part2` to `terminates_part7`.

Reason: the repair and shift-subtract loops at include/fixed/fixed.h:301, 302, 326, 330,
342 and 347 have no loop assigns and no loop variant. WP warns "Missing assigns clause
(assigns 'everything' instead)" for each loop. These goals need loop annotations, which
moves to fixed-framac-isqrt-loop-annotations. None of them is a runtime-error goal.

### Int128Div primitive (proveInt128Div), emulated: 5 runtime-error goals (owed to loop annotations)

WP places them as follows, from `-wp-gen -wp-print`:

- `assert_rte_shift_2`: the lane shift `dividend.hi >> ( i - 64 )` at
  include/fixed/fixed_int128.h:421, inside the fixEmuUInt128DivMod loop at
  fixed_int128.h:417. With no loop invariant on `i`, WP cannot bound the shift count.
- `assert_rte_mem_access_5` and `_6`: the out-parameter writes after that loop, at
  fixed_int128.h:433 and 434.
- `assert_rte_mem_access_7` and `_8`: the writes of fixEmuInt128DivMod's results
  (fixed_int128.h:541 and 545), which WP reports at fixed_int128.h:184, the inlined body of
  fixEmuToSigned.

Reason: the fixEmuUInt128DivMod loop has no loop assigns or invariant. Its
`assigns everything` throws away what WP knows about `i` and about the out-pointers. This
moves to fixed-framac-isqrt-loop-annotations. The proveInt128Div frame run is not in
`nightly`: it schedules 86 goals, and in pass 5 at least 16 were still open (`terminates`,
`assigns_part51` to `part65`) when it was stopped after 15 minutes, for the same reason.

### fixDiv: no result (goal generation does not end)

fixDiv is not in `nightly`, and it was not run in this pass. In pass 5, frama-c stayed in
its own VC generation, with no prover process started and one core at 100 %, for over 15
minutes on every arm. This happened both with and without `-wp-no-simpl`, and it was
stopped by hand. Pass 4 hit a 300 s cap the same way. A full `-ulevel 128` unroll of the
divide loop did not finish in 580 s either. The likely cause is the inlined
fixEmuUInt128DivMod loop at include/fixed/fixed_int128.h:417, but this is not confirmed.
This moves to fixed-framac-fixdiv-goal-generation. Eva on Frama-C 25 (pass 4) gave only two
imprecision alarms, at include/fixed/fixed.h:235 (division by zero and signed overflow on
the fast-path divide). Both are ruled out by the guards above that line: the `b == 0`
return and the `|a| < 2^47` test. `make all` still includes fixDiv, so do not run `all`
unattended.

### The native path is not checked

`make native` does not reach the native `__int128` code. include/fixed/fixed_int128.h:567
selects native arithmetic only when `__SIZEOF_INT128__` is defined, and Frama-C's
preprocessing does not define it. FIX_INT128_EMULATED therefore becomes 1, and the native
rows of pass 5 matched the emulated rows goal for goal. proveInt128Div's native log names
the emulated loop's `assert_rte_shift_2`. As a result, two items are unchecked: INT128_MIN
/ -1 in the native fixInt128Div, and the x86-64 `divq` inline asm, which WP cannot read in
any case. This moves to fixed-framac-native-int128-path (for example, with
`-D__SIZEOF_INT128__=16` or a machdep that defines it). Frama-C 25 rejected `__int128` with
a syntax error at include/fixed/fixed_int128.h:577.

## Not in scope at this base

The 2^113/pi quarter-turn reduction is not on sprint/fixed-core. It is on the unlanded
branch sprint/fixed-sincos-exact.w1.g1.e21. fixMul, fixDiv and fixISqrt128High use no int256
primitive.
