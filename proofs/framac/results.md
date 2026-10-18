# Results

Run 2026-10-18 on amd-2 (16 cores, shared and loaded: load average 8 to 54 during the run),
from head 871bb61 plus this pass's edits. Frama-C 33.0 (Arsenic) with WP, Alt-Ergo 2.4.3,
Z3 4.13.3, Why3 1.8.2 (`why3 config detect` run for the bench user). Command: `make -C
proofs/framac all` (see the Makefile for the exact flags of each arm), with the frame arm
run alongside as `make OUT=outF LIMIT="timeout 900" frame`. The run was partial: the fixDiv
runs (and the last proveInt128Div frame run) were stopped by hand after 15 minutes, before
the 30-minute LIMIT, to fit the card deadline, and the frame rows of the last three
primitives were not reached.

Status: NOT COMPLETE. The gate (every RTE goal of fixMul, fixDiv and fixISqrt128High proved
on the emulated path) is not met: fixDiv produces no result within the bound, and two
fixISqrt128High RTE goals stay open.

## The three arms

- **emulated**: `-DFIX_FORCE_EMULATED_INT128`, RTE and ensures goals (`-wp-prop=-@assigns,-@terminates`),
  60 s per goal.
- **native**: no define, same goals. See the finding below: under Frama-C this arm does not
  reach the native path.
- **frame**: the assigns and terminates goals, emulated path, 10 s per goal. Split out
  because every library loop lacks a loop assigns and a loop variant, and no header is
  edited here, so WP falls back to `assigns everything` for each loop and cannot discharge
  them; at 60 s each they dominated the first run (pass 5, first try: 143 goals of
  fixISqrt128High took 20 minutes).

## Per function, proved / total

| wrapper | emulated | emulated + FIX_SATURATE | native arm | frame |
|---|---|---|---|---|
| proveFixMul | 14 / 14 | 15 / 15 | 14 / 14 | 112 / 112 |
| proveFixDiv | no result (stopped by hand after 15 min in VC generation) | no such arm | no result (stopped after 15 min) | no result (stopped) |
| proveFixISqrt128High | 34 / 36 | - | 34 / 36 | 49 / 107 |
| proveInt128MulI64 | 0 / 0 (no RTE goal) | - | 0 / 0 | 14 / 14 |
| proveInt128Add | 0 / 0 | - | 0 / 0 | 21 / 21 |
| proveInt128Shr | 14 / 14 | - | 14 / 14 | 68 / 68 |
| proveInt128ShiftLeft | 7 / 7 | - | 7 / 7 | 27 / 27 |
| proveUInt128Shl | 7 / 7 | - | 7 / 7 | 16 / 16 |
| proveUInt128Shr | 7 / 7 | - | 7 / 7 | 16 / 16 |
| proveUInt128MulU64 | 0 / 0 | - | 0 / 0 | 7 / 7 |
| proveUInt128Add | 0 / 0 | - | 0 / 0 | 8 / 8 |
| proveUInt128Sub | 0 / 0 | - | 0 / 0 | 8 / 8 |
| proveInt128Div | 22 / 27 | - | 22 / 27 | 86 scheduled; at least 16 open when stopped at 15 min; no total |
| proveInt128ToI64 | 0 / 0 | - | 0 / 0 | not run |
| proveInt128Compare | 3 / 3 | - | 3 / 3 | not run |
| proveUInt128Compare | 7 / 7 | - | 7 / 7 | not run |

"0 / 0" is WP's "No goal generated": the primitive is unsigned or bitwise lane arithmetic
with no runtime-error condition. The frame column for the last three was not reached
before the card deadline.

## Unproved goals, each with its reason

### fixDiv (gate item, open)

No summary on either arm. frama-c sits in its own VC generation (no prover process
spawned, one core at 100 %) for over 15 minutes, both with and without `-wp-no-simpl`,
and was stopped by hand;
pass 4 hit a 300 s cap the same way. A full `-ulevel 128` unroll of the divide loop did
not finish in 580 s either. The likely cause is the inlined fixEmuUInt128DivMod loop (WP's missing-assigns
warning for it is raised inside the proveFixDiv run, so it is inlined)
(include/fixed/fixed_int128.h:417), which WP havocs as `assigns everything`; not confirmed.
This is a tool limit, not a finding about the code: Eva on Frama-C 25 (pass 4) reported
only two imprecision alarms at include/fixed/fixed.h:235, both ruled out by the guards.

### fixISqrt128High, emulated and native arm: 2 RTE goals

- `assert_rte_is_nan_or_infinite`: `\is_finite(\mul_double((double)hi, 2^64))`, the seed
  input at include/fixed/fixed.h:300, under `hi != 0 && hi < 2^40`.
- `assert_rte_is_nan_or_infinite_2`: `\is_finite(\add_double(...,(double)lo))`, same line.

Prover limit: both hold (the operands are below 2^104, far under DBL_MAX), but the proof
needs IEEE-754 rounding reasoning that WP's float model hands to Alt-Ergo and Z3 as
uninterpreted rounding, and both time out at 60 s. Gappa 1.6.0, tried by hand on these two goals, returns Unknown. The FPA
variant of Alt-Ergo is listed by Why3 but could not be selected from `-wp-prover`.

### fixISqrt128High, frame: 58 goals

- 52 assigns goals: `assigns_normal_part07` to `part35`, `part41`, `part42`, `part73` to
  `part93`.
- 6 terminates goals: `terminates_part2` to `terminates_part7`.

Reason: the four repair and shift-subtract loops (include/fixed/fixed.h:301, 302, 326, 330,
342, 347; the lines WP names) have no loop assigns and no loop variant, and WP warns "Missing assigns clause
(assigns 'everything' instead)" for each. Discharging these needs loop annotations in the
header, which this card does not edit. Not a runtime-error goal.

### fixInt128Div primitive (proveInt128Div), both arms: 5 RTE goals

- `assert_rte_shift_2`: a lane shift by the loop counter `i` in the fixEmuUInt128DivMod
  loop (include/fixed/fixed_int128.h:417 to 427). With no loop invariant on `i`, WP cannot
  bound the count.
- `assert_rte_mem_access_5` to `_8`: the out-parameter writes after that loop
  (fixed_int128.h:430 and 431, and fixEmuInt128DivMod's writes at 539 to 546). The loop's
  `assigns everything` havocs what WP knows about the out-pointers.

Reason: the fixEmuUInt128DivMod loop carries no loop assigns or invariant (this is the
expected "fixEmuUInt128DivMod assigns clause" item). Frame arm: `terminates` and `assigns_part51` to `part65` had timed out when the run was stopped
(86 goals scheduled, no total), for the same reason.

## Finding: the native arm runs the emulated path under Frama-C

Frama-C 33 parses the headers, but the native rows are goal-for-goal identical to the
emulated ones, and proveInt128Div's native log names the emulated loop's
`assert_rte_shift_2`. include/fixed/fixed_int128.h:567 selects native arithmetic only when
`__SIZEOF_INT128__` is defined, which Frama-C's preprocessing does not appear to define,
so FIX_INT128_EMULATED becomes 1. The expected native items (INT128_MIN / -1 in the native
fixInt128Div, and the x86-64 `divq` inline asm) were therefore never reached. Running the
native arm needs `-D__SIZEOF_INT128__=16` (or a machdep that defines it), not tried here.

## Cross-check: Eva, emulated path, hetzner2, Frama-C 25.0 (pass 4)

proveFixMul 0 alarms; proveFixISqrt128High 0 alarms; proveFixDiv 2 alarms at
include/fixed/fixed.h:235 (division by zero and signed overflow on the fast-path divide),
both imprecision of a non-relational domain: the b == 0 return above it and the |a| < 2^47
guard rule both out.

## Not in scope at this base

The 2^113/pi quarter-turn reduction is not on sprint/fixed-core; it lives on the unlanded
branch sprint/fixed-sincos-exact.w1.g1.e21. No int256 primitive is used by fixMul, fixDiv or
fixISqrt128High.
