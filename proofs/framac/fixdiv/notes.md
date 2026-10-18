# fixDiv WP goal-generation investigation (attempt 4)

The main Frama-C card is present in the carried tree. The prior pass spent more than
15 minutes in VC generation for `proveFixDiv` with `-inline-calls @inline`.
Attempt 3 could not run Frama-C 33 because its bench denied the opam toolchain.
This attempt used Frama-C 33.0, Alt-Ergo 2.4.3, Z3 4.13.3 and Why3 1.8.2 on
amd-2, with load guard <=16, CPU 2, nice 19, 8 GiB address-space cap and
wall-clock caps. Logs remain in the allocated gate job, not in this tree.

## Ordered attempts

| Step | Experiment | Elapsed / bound | Result |
|---|---|---|---|
| 1, smaller total contract | Carried wrapper, no inline: `make fixdiv`, four documented ensures and no precondition | about 2 minutes, 600 s cap | Goal generation ended; `Proved goals: 1 / 5`. This only reasons about an opaque call to fixDiv and is not an actual-body proof. |
| 2, split stages | Added zero, fast and wide branch wrappers, each calling the production fixDiv; no full-function assumption was added | under 300 s | Together with step 3, scheduled 77 goals and ended `73 / 77`; four goals remained open. |
| 3, `-wp-split` | Split the staged wrappers' conditional goals | under 300 s | The four open names were `typed_proveFixDivFast_ensures_part04`, `typed_proveFixDivFast_assert_rte_signed_overflow`, `typed_proveFixDivWide_ensures_part07`, `typed_proveFixDivWide_ensures_part10`; Z3 hit the 100000-step bound on each. |
| 4, `-wp-fct`/`-wp-prop` | Isolated `proveFixDiv` runtime assertions with `-wp-fct proveFixDiv -wp-prop=@assert`; selectively inlined fixDiv and fixShiftLeft | under 240 s | `Proved goals: 2 / 3`; the fixed.h:235 signed-overflow goal timed out. |
| 5, `-wp-timeout`/`-wp-steps` | Alt-Ergo/Z3 30 s per goal on the total wrapper; Z3 10 s and 100000 steps on staged/split goals | all jobs bounded as above | Limits prevented runaway proving; they did not close every goal. |
| 6, per-function wall cap | `LIMIT=timeout 600` in Makefile, plus independent guarded outer cap | selective-inline run ended inside the cap | `nightly` now invokes `fixdiv` because this target produces a summary inside its cap. |

The crucial change is **selectively inlining fixDiv** instead of every function tagged
`@inline`. With `-inline-calls fixDiv`, actual fixDiv branches enter the wrapper VC,
but the 128-iteration emulated divider at fixed_int128.h:417 stays out.
Goal generation ended and the emulated total wrapper printed
`Proved goals: 5 / 7` within about two minutes. This is a bounded partial
proof, not a complete proof of fixDiv. The helper calls have no assigns
specifications, and their own obligations remain separate.

The two Eva alarms at fixed.h:235 are division by zero and signed overflow.
The source returns before line 235 when b == 0; the fast-path strict range
`-2^47 < a < 2^47` makes its shifted numerator strictly between
`-2^63` and `2^63`, excluding the only signed quotient-overflow case
`INT64_MIN / -1`. Attempt 3 also ran a GCC UBSan edge sweep of 114 pairs on
native and emulated arms with no report. That finite sweep does not prove all
inputs; the open WP overflow goal remains a prover/abstraction limit, not a
confirmed real undefined behavior.

See results.md for the exact open total-function goals. No SDK header, main
Frama-C proof, CI, CTest or global toolchain was changed.

## Focused wide-path follow-up

With `-wp-prop=@ensures` and `-wp-fct proveFixDiv`, selectively inlining
`fixInt128Gt/Lt/ToI64/FromI64` still ended `3 / 4`; the result-range goal
timed out. Expanding their emulated counterparts also ended `3 / 4`.
Expanding `fixEmuInt128Make` as well also ended `3 / 4`, with the same
`typed_proveFixDiv_ensures_4` timeout. Each experiment finished inside its
240 s cap. The emulated divider remained opaque, and no contract about its
return value was assumed. These bounded expansions did not close the goal.
`-wp-prop=-@assigns,-@terminates` deliberately filters those categories
in the total-function target; neither category is claimed proved.

The carried attempt-3 UBSan output (GCC 13.3.0,
`-fsanitize=undefined -fno-sanitize-recover=all`, 114 edge pairs on each
arm) was:

```text
===== emulated (-DFIX_FORCE_EMULATED_INT128) =====
fixDiv edge sweep done: 114 cases, sink=140737488355329
rc=0
===== native =====
fixDiv edge sweep done: 114 cases, sink=140737488355329
rc=0
```

This run was not repeated in attempt 4. Its absence of a sanitizer report
supports the source guard analysis at the sampled boundaries; it is not a
substitute for closing the two WP goals.

## Final Makefile gate

With the finished own-directory Makefile and wrapper, the guarded exact
`make -C proofs/framac/fixdiv fixdiv` exited 0 in 128.78 real seconds.
It printed `[wp] Proved goals: 5 / 7`; the two open names in results.md
timed out. The goal-generation hang is removed for this selected body path.
The target is in `nightly` because it ended within 600 seconds; `nightly`
running it does not imply all goals are proved. This card remains HOLD on
the full-proof objective.
