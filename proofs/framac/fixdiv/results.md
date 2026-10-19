# fixDiv Frama-C WP results

## Helper contracts (card fixed-framac-fixdiv-helper-contracts)

Every helper in fixDiv's call tree on the emulated arm now has an ACSL contract in
`helpers_acsl.h` (call tree at the top of that file), attached by redeclaration so no
library header is edited. amd-2, Frama-C 33.0, Alt-Ergo 2.6.4 / Z3, nice 19, 600 s cap.

`make -C proofs/framac/fixdiv fixdiv`, before (base d77cbcc, no helper contracts) and after:

```text
before: [wp] Proved goals:    5 / 7      (real 127.49)
after:  [wp] Proved goals:   10 / 10     (real 2.71)
```

Goals that moved:

| Goal | Before | After |
|---|---|---|
| `typed_proveFixDiv_ensures_4` (fixdiv_acsl.c:19, result range) | Timeout | Valid (Alt-Ergo) |
| `typed_proveFixDiv_assert_rte_signed_overflow` (fixed.h:239, `fixShiftLeft(a, 16) / b`) | Timeout | Valid (Alt-Ergo) |
| `typed_proveFixDiv_call_fixShiftLeft_requires` | not generated | Valid (Qed) |
| `typed_proveFixDiv_call_fixInt128Div_requires_divisor_nonzero` | not generated | Valid (Qed) |
| `typed_proveFixDiv_call_fixInt128Div_requires_not_min_by_minus_one` | not generated | Valid (Alt-Ergo) |

The three new goals are the helper preconditions at fixDiv's call sites; WP proves fixDiv
never calls a helper outside its contract. (The base results below cite the overflow check
as fixed.h:235; at d77cbcc the divide is on fixed.h:239.)

**This fixDiv proof is conditional.** It takes every helper by its contract, and six of
those contracts are ASSUMED (table below), so the 10 / 10 holds only as far as those six
summaries hold. The `@assigns` and `@terminates` categories are still filtered out of the
fixdiv target. Only the emulated arm is proved; the native `__int128` arm is not modelled.

`make -C proofs/framac/fixdiv helpers` proves the 23 PROVED helper bodies against their
contracts, with `-wp-rte` and with assigns and terminates goals included:

```text
[wp] Proved goals:  162 / 162      (real 10.92 alone; 296.36 while the planted run shared the bench)
```

The target fails unless every goal is proved (Frama-C itself exits 0 with goals open):
with `CPP_BASE='-I../../../include -DFIX_ACSL_PLANT=1'` it printed `Proved goals: 154 / 162`
and `NOT ALL PROVED`, and make exited 2.

### Helper status

| Helper | Status | Reason |
|---|---|---|
| fixEmuUInt128Make, fixEmuInt128Make, fixEmuUInt128FromU64, fixEmuInt128FromI64, fixInt128FromI64 | PROVED (`helpers`) | |
| fixEmuToUnsigned, fixEmuToSigned, fixInt128ToUnsigned, fixInt128FromUnsigned | PROVED (`helpers`) | |
| fixEmuInt128ToI64, fixInt128ToI64 | PROVED (`helpers`) | |
| fixEmuUInt128Or, fixUInt128Shl, fixEmuUInt128Sub, fixEmuUInt128Neg | PROVED (`helpers`) | fixUInt128Shl is proved from the fixEmuUInt128Shl contract |
| fixEmuInt128Div, fixInt128Div | PROVED (`helpers`) | proved from the fixEmuInt128DivMod contract |
| fixEmuUInt128Lt, fixEmuUInt128Ge, fixEmuInt128Lt, fixEmuInt128Gt, fixInt128Lt, fixInt128Gt | PROVED (`helpers`) | |
| fixEmuUInt128DivMod | ASSUMED | 128-iteration loop at fixed_int128.h:417 has no loop invariant or variant, and none can be attached without editing the library header |
| fixEmuInt128DivMod | ASSUMED | `ensures quotient` and `ensures remainder` time out at 30 s and 120 s (prover limit: sign/magnitude reasoning on truncating division). They prove once lemmas `(-x)/y == -(x/y)` and `x/(-y) == -(x/y)` are added, but neither prover proves those lemmas, so they are not kept |
| fixEmuInt128IsNegative | ASSUMED | `ensures value` times out at 30 s and 120 s: neither prover links `a.hi >> 63` to `a.hi >= 2^63`; the lemma stating it also failed |
| fixEmuUInt128Shl | ASSUMED | `out_of_range` and RTE prove; `in_range` (value as `(u * 2^shift) mod 2^128`) times out at 30 s and 120 s: non-linear modular arithmetic over a symbolic shift count |
| fixShiftLeft | ASSUMED | `exact` and `in_range` time out at 30 s and 120 s, the same non-linear limit; RTE (shift count in range) proves |
| fixInt128ShiftLeft | ASSUMED | `exact` and `out_of_range` prove from callee contracts; `in_range` times out at 30 s and 120 s (non-linear modular step for negative values) |

Each ASSUMED contract is held on boundary values by `helpers_check.c` (source and output in
notes.md): 1544 cases against native `unsigned __int128` / `__int128`, built with GCC 15.2
`-fsanitize=undefined -fno-sanitize-recover=all -DFIX_FORCE_EMULATED_INT128`. Values cover
0, 1, 2, -1, INT64_MIN, INT64_MAX, UINT64_MAX, 2^64, 2^127 - 1, INT128_MIN, UINT128_MAX and
the fixDiv-reachable extremes INT64_MIN * 2^16 and INT64_MAX * 2^16: 0 mismatches, no sanitizer
report. The planted build (`-DCHECK_PLANT=1`) gives 1126 mismatches and rc=1.

INT128_MIN / -1: card fixed-int128-div-min-by-minus-one-defined has landed, so both arms
now wrap it to INT128_MIN. That result is defined, but it is not the exact quotient 2^127.
fixEmuInt128DivMod, fixEmuInt128Div and fixInt128Div therefore **require** the pair
(dividend == INT128_MIN, divisor == -1) to be excluded (`not_min_by_minus_one`), and
fixDiv's call satisfies this, proved as `call_fixInt128Div_requires_not_min_by_minus_one`.
Division by zero is excluded too (`divisor_nonzero`), because the header says the zero
result is "not a contract".

### Planted faults

`make -C proofs/framac/fixdiv planted` proves each PROVED helper k with `FIX_PLANT(k)`
falsifying its main ensures, then proveFixDiv with plant 100 (range ensure tightened past
FIX_MIN) and plant 101 (fixShiftLeft's value facts switched off). Every run must leave a
goal unproved. A plant counts as caught when any goal of that run is open, not only its own
ensure; that is a sound check here because the clean `helpers` run is 162 / 162 and the clean
fixdiv run 10 / 10, so a run that leaves a goal open differs from its clean run only by the plant.

```text
planted 1 fixEmuUInt128Make: fails as required (4 / 5)
planted 2 fixEmuInt128Make: fails as required (4 / 5)
planted 3 fixEmuUInt128FromU64: fails as required (6 / 7)
planted 4 fixEmuInt128FromI64: fails as required (10 / 11)
planted 5 fixInt128FromI64: fails as required (6 / 7)
planted 6 fixEmuToUnsigned: fails as required (6 / 7)
planted 7 fixEmuToSigned: fails as required (6 / 7)
planted 8 fixInt128ToUnsigned: fails as required (6 / 7)
planted 9 fixInt128FromUnsigned: fails as required (6 / 7)
planted 10 fixEmuInt128ToI64: fails as required (4 / 5)
planted 11 fixInt128ToI64: fails as required (6 / 7)
planted 12 fixEmuUInt128Or: fails as required (5 / 6)
planted 14 fixUInt128Shl: fails as required (6 / 7)
planted 15 fixEmuUInt128Sub: fails as required (9 / 10)
planted 16 fixEmuUInt128Neg: fails as required (7 / 8)
planted 20 fixEmuInt128Div: fails as required (9 / 10)
planted 21 fixInt128Div: fails as required (7 / 8)
planted 23 fixEmuUInt128Lt: fails as required (5 / 6)
planted 24 fixEmuInt128Lt: fails as required (4 / 5)
planted 25 fixEmuInt128Gt: fails as required (5 / 6)
planted 26 fixInt128Lt: fails as required (5 / 6)
planted 27 fixInt128Gt: fails as required (5 / 6)
planted 28 fixEmuUInt128Ge: fails as required (8 / 9)
planted 100 proveFixDiv: fails as required (9 / 10)
planted 101 proveFixDiv: fails as required (8 / 10)
real 1547.93
exit=0
```

## Before helper contracts (card fixed-framac-fixdiv-goal-generation)

Final guarded `make -C proofs/framac/fixdiv fixdiv` on amd-2, emulated arm,
Frama-C 33.0, Alt-Ergo/Z3, CPU 2, nice 19, 8 GiB address-space cap and
600 s outer wall cap:

```text
Gate start: host=b2 load1=14.62 ceiling=16.00
[wp] 7 goals scheduled
[wp] [Timeout] typed_proveFixDiv_ensures_4 (Qed 550ms) (Z3)
[wp] [Timeout] typed_proveFixDiv_assert_rte_signed_overflow (Qed 12ms) (Alt-Ergo)
[wp] Proved goals:    5 / 7
  Qed:             5 (12ms-80ms-550ms)
  Timeout:         2
real 128.78
```

Selective `-inline-calls fixDiv` reaches the actual fixDiv branch logic and
ends goal generation inside the cap. It does **not** prove the whole function.
Two of the seven generated goals remain open:

| Open goal | Source line | Reason |
|---|---|---|
| `typed_proveFixDiv_ensures_4` | fixdiv_acsl.c:16, result range | Z3 timed out. Wide-path divider and helper conversions/comparisons are opaque in the final VC; selectively expanding these helpers in focused runs still left this goal open at 3/4. Prover/abstraction limit, not a confirmed counterexample. |
| `typed_proveFixDiv_assert_rte_signed_overflow` | include/fixed/fixed.h:235, signed quotient | Alt-Ergo timed out. WP did not discharge the guard/shift/division relation; selectively inlining fixShiftLeft and isolating `@assert` still left this goal open at 2/3. Source guard reasoning and the carried 114-case UBSan sweep found no real UB, but they do not constitute a universal proof. |

The opaque-call-only run ended `1 / 5`, showing why a summary without
inlining fixDiv would be misleading. Split branch wrappers ended `73 / 77`
with four step-limited goals; see notes.md.

The `@assigns` and `@terminates` categories are filtered by WPFLAGS, so
this summary does not establish those properties. `nightly` calls the
bounded target because it terminates inside 600 s. There is no per-commit CI
or CTest target. Completion of every fixDiv obligation remains **not done**.

The supplemental staged/split run generated these additional open
diagnostics. They are not part of the seven-goal final target:

| Open staged goal | Source line | Reason |
|---|---|---|
| `typed_proveFixDivFast_ensures_part04` | fixdiv_acsl.c:37 | Z3 hit the 100000-step bound on a split of the fast-path result-range ensure; prover limit. |
| `typed_proveFixDivFast_assert_rte_signed_overflow` | include/fixed/fixed.h:235 | Z3 hit the same step bound on the fast-path quotient-overflow assertion; prover limit, not a confirmed UB. |
| `typed_proveFixDivWide_ensures_part07` | fixdiv_acsl.c:47 | Z3 hit the step bound on a wide-path result-range split; opaque 128-bit helper behavior remains. |
| `typed_proveFixDivWide_ensures_part10` | fixdiv_acsl.c:47 | Same step-bound and helper-abstraction limit on another wide-path range split. |

## Full proof (card fixed-framac-fixdiv-full-proof)

amd-2, Frama-C 33.0, Alt-Ergo 2.6.4, Z3 4.13.3, Why3 1.8.2, nice 3, `timeout 600` per Frama-C run.
The assigns/terminates filter is gone, so every goal WP generates for proveFixDiv is counted.

`make -C proofs/framac/fixdiv fixdiv` (exit 0, 5.5 s real):

```text
[wp] Proved goals:   41 / 41
WP strict: 41/41 Valid
```

The 41 are 30 assigns goals, `typed_proveFixDiv_terminates`, the four range/saturation ensures
(`ensures_4` among them), `exits`, the two RTE goals at fixed.h (division by zero and
`rte_signed_overflow`) and the three helper preconditions at fixDiv's call sites. No goal is
open, so none is listed as PROVER-LIMIT or REAL-UB. The proof is still conditional on the six
ASSUMED helper contracts above and covers the emulated int128 arm only.

### Failed versus Timeout

`strict_check.sh` reads the WP JSON report and exits non-zero naming each goal that is not Valid
with its verdict. When the first pass (WPFLAGS, provers Alt-Ergo and Z3) leaves a goal open, the
Makefile recipe reruns the wrapper under a second prover, Z3qi: Z3 4.13.3 with
`smt.mbqi=false auto_config=false`, defined in `out/why3-z3qi.conf` by the Makefile. It uses E-matching only, so it stops with `unknown` instead
of searching to the timeout; an unsat it finds is still sound. Both passes take the same WPCOMMON
flags, so they generate the same goals. strict_check.sh merges the two reports and prints each
open goal with both verdicts; a goal missing from the second report, or a second pass that fails,
still fails the target.

`planted_bug_acsl.c` claims `ensures planted_zero_result: \result == 1` under `requires a == 0 && b == 0`;
fixDiv( 0, 0 ) returns 0. `make -C proofs/framac/fixdiv planted-bug` (make exit 2, 66.3 s real):

```text
[wp] Proved goals:   37 / 38
typed_proveFixDiv_ensures_planted_zero_result: Unknown (Z3qi; first pass Timeout)
WP strict: 37/38 Valid
make: *** [Makefile:62: planted-bug] Error 1
```

No prover here reports Failed (a counterexample) for this goal. Qed reduces it to `false` (the Z3
task ends `(assert (not false))`), and the rest of the task is quantified axioms (division,
modulo, the memory model). Each try, on the planted goal alone (`-wp-prop planted_zero_result`):

| Try | Result |
|---|---|
| Alt-Ergo, `-wp-timeout 30` | Timeout (31.3 s) |
| Z3, `-wp-timeout 30` | Timeout (30.9 s) |
| Z3, `-wp-steps 200000` | Stepout (0.8 s) |
| Alt-Ergo, `-wp-steps 2000` | Stepout (0.9 s) |
| Z3 and Alt-Ergo with `-wp-counter-examples`, `-wp-timeout 10` | Timeout, No Model (10.8 s, 13.2 s) |
| Z3 on the WP task, `smt.qi.max_instances=100`, `=1000`, or `smt.mbqi=false` | timeout at `-T:20` |
| Z3 on the WP task, `smt.mbqi=false auto_config=false` | unknown (0.12 s) |
| Z3qi through WP, five runs | Unknown each time (0.7 to 0.8 s) |

So the harness reports the planted goal honestly as Unknown, never as proved and never as Timeout
dressed as Failed.
