# Results

Status: NOT RUN WITH THE NAMED PROVERS. alt-ergo and z3 are not installed on any bench
checked (amd-1, hetzner2) as of 2026-10-18 14:40 EDT, so `make all` cannot run as written.

## Preliminary: WP's own simplifier (Qed) only, emulated path, amd-1, Frama-C 33.0

Command per wrapper: `frama-c -cpp-extra-args="-I../../include -DFIX_FORCE_EMULATED_INT128"
kernels_acsl.c -inline-calls @inline -remove-inlined @inline -wp -wp-rte -wp-prover qed
-wp-fct <wrapper>`

| wrapper | proved / total | note |
|---|---|---|
| proveFixMul | 126 / 126 | all by Qed; emulated, without FIX_SATURATE only |
| proveFixDiv | no summary | run stopped at the 300 s cap; a Missing assigns warning at fixed_int128.h:417 (fixEmuUInt128DivMod's out-parameters) |
| proveFixISqrt128High | 81 / 143 | 62 Unknown under Qed alone, including the two __builtin_sqrt preconditions and assigns parts; these need an SMT prover |

Not run at all: proveFixMul under FIX_SATURATE and the 13 primitive wrappers. The 62
Unknown goals of proveFixISqrt128High are a count, not yet a named list. None of the
Unknown goals above is classified yet: a Qed Unknown is not a finding until
alt-ergo and z3 have run on it.

## Cross-check: Eva, emulated path, hetzner2, Frama-C 25.0

`frama-c ... -eva -main <wrapper> -eva-precision 3`: proveFixMul 0 alarms;
proveFixISqrt128High 0 alarms; proveFixDiv 2 alarms at include/fixed/fixed.h:235 (division
by zero and signed overflow on the fast-path divide), both imprecision of a non-relational
domain: the b == 0 return above it and the |a| < 2^47 guard rule both out.

## Native __int128 path

Expected when it runs: proveInt128Div native leaves the INT128_MIN / -1 overflow goal
unproved (its contract only excludes b == 0, as the library documents).

Not run: Frama-C 25 rejects `__int128` (include/fixed/fixed_int128.h:577, syntax error).

## Not in scope at this base

The 2^113/pi quarter-turn reduction is not on sprint/fixed-core at 4ad7979; it lives on the
unlanded branch sprint/fixed-sincos-exact.w1.g1.e21 (fc2be6d, src/fixed_math.c:128). No
int256 primitive is used by fixMul, fixDiv or fixISqrt128High.
