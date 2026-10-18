# fixDiv results

Attempt 3, base: sprint/fixed-core at 5e35893 (proofs/framac landed). No proof was run:
this job's sandbox on host b1 has no reachable WP (see notes.md). There is no `Proved goals:`
line for fixDiv, so no WP goal is listed below; the only goals this card could examine are the
two Eva imprecision alarms in the main card's pass-4 results.

## Status against DONE-WHEN

| item | state |
|---|---|
| `make -C proofs/framac/fixdiv fixdiv` ends within 600 s with a summary for the emulated path | **not done**: the WP-capable Frama-C 33, Alt-Ergo and Why3 exist only under `/home/nova/.opam`, which this sandbox denies (reads and exec); no runnable fallback is on PATH. There is no WP-capable Frama-C to run |
| every unproved goal listed with line and reason | **not done for WP**: no goals were generated. The two known Eva alarms are analysed below and are not undefined behaviour |
| notes.md records what was tried, with time and whether it ended | done for the start conditions and the UBSan check; the WP attempts are recorded as not runnable |
| Makefile `nightly` includes fixDiv only if it ends in its cap | done: `nightly` does not call fixDiv |
| `git diff --stat` shows only NEW files under proofs/framac/fixdiv | done |

No `make -C proofs/framac/fixdiv fixdiv` red line can be produced here: `frama-c` resolves to
a symlink into the denied `/home/nova/.opam` and every exec fails with `Permission denied`, so
the make target cannot start. See notes.md for the probe table and the bench-only toolchain.

## The two Eva alarms at fixed.h:235

`return fixShiftLeft( a, FIX_FRACTION_BITS ) / b;`:

- division by zero at `include/fixed/fixed.h:235`;
- signed overflow at `include/fixed/fixed.h:235`.

Reason they are open in Eva: the guards are not carried into the alarm. They are not real
undefined behaviour. `fixDiv` returns at fixed.h:226-229 when `b == 0`; the fast path at
fixed.h:233 is taken only for `-(1<<47) < a < (1<<47)`, so `a << 16` is strictly inside
`(-2^63, 2^63)` and a quotient overflow, including `INT64_MIN / -1`, cannot be formed.

Confirmed under UBSan (gcc 13.3.0, `-fsanitize=undefined -fno-sanitize-recover=all`) on
both arms, boundary sweep of 114 cases, no report; the two arms agree:

```
===== emulated (-DFIX_FORCE_EMULATED_INT128) =====
fixDiv edge sweep done: 114 cases, sink=140737488355329
rc=0
===== native =====
fixDiv edge sweep done: 114 cases, sink=140737488355329
rc=0
```

## What is left

1. The main card has landed; `proofs/framac` is on sprint/fixed-core.
2. Run `make -C proofs/framac/fixdiv fixdiv` on a bench where the named toolchain is
   reachable (Frama-C 33 with WP, Alt-Ergo 2.4.3, Z3 4.13.3, Why3 1.8.2), then paste its
   `Proved goals: N / M` line here and list each unproved goal with its line and reason.
3. Only if that run ends inside 600 s, add `fixdiv` to `nightly`.
