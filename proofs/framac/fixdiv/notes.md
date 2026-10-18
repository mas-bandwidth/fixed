# fixDiv WP goal generation: what was tried

Attempt 1: sprint/fixed-framac-fixdiv-goal-generation.w1.g1.e21 at
386995803c6033845e1b79610745819826cc7910, from sprint/fixed-core.
Attempt 2 (this card): sprint/fixed-framac-fixdiv-goal-generation.w2.g1.e21 at
954b009 (carrying attempt 1's work), from sprint/fixed-core.

## Start conditions (attempt 2)

1. **The main card has landed.** `git ls-tree --name-only origin/sprint/fixed-core proofs/`
   now lists `proofs/framac` (and `proofs/gappa`). The checkout has `proofs/framac` with the
   wrappers, Makefile, README and results.md, including `proveFixDiv` in `kernels_acsl.c`
   and a `nightly` target. Nothing there was edited.
2. **The named toolchain is still not runnable here.** Probe at 2026-10-18, in this job's
   sandbox on the studio Mac:

   | tool | TOOLCHAIN says | what this machine has |
   |---|---|---|
   | Frama-C | 33.0 (Arsenic), with WP | `command -v frama-c` prints nothing; no `frama-c` found under `/usr`, `/opt`, `$HOME/sdk`, `/Users/glenn/sdk`, `/Users/glenn/.local`. No WP-capable Frama-C at all. |
   | Alt-Ergo | 2.4.3 | `command -v alt-ergo` prints nothing; not found. |
   | Z3 | 4.13.3 | `command -v z3` prints nothing; not found. |
   | Why3 | 1.8.2 | `command -v why3` prints nothing; not found. |

   So no WP-enabled Frama-C and no prover is reachable from this job. No system package was
   installed and none will be. The card's own rule applies: a tool TOOLCHAIN names that is
   not on PATH is a HOLD at once, naming it.

## Attempt 1's start conditions (for the record)

1. **The main card had not landed.** `git ls-tree --name-only origin/sprint/fixed-core proofs/`
   listed only `proofs/gappa`; `proofs/framac` did not exist in the checkout then.
2. **The named toolchain was not runnable.** Probe at 2026-10-18 22:21 UTC
   (`$TMPDIR/probe.txt`):

   | tool | TOOLCHAIN says | what this machine runs |
   |---|---|---|
	| Frama-C | 33.0 (Arsenic), with WP | on the bench: `command -v frama-c` prints `/home/nova/sdk/bin/frama-c`, a symlink to `/home/nova/.opam/default/bin/frama-c`; execution fails with `Permission denied`. Exec falls through to `/usr/bin/frama-c`, **25.0-beta (Manganese)** with **no WP plugin**. |
   | Alt-Ergo | 2.4.3 | `command -v alt-ergo` prints `/home/nova/sdk/bin/alt-ergo`, another symlink into the denied `~/.opam`; running it fails with `Permission denied`. No system copy exists. |
   | Z3 | 4.13.3 | `/usr/bin/z3` is **4.8.12**. |
   | Why3 | 1.8.2 | `why3` falls through to `/usr/bin/why3`, **1.6.0**. |

   So the WP-enabled Frama-C and Alt-Ergo are installed only under `/home/nova/.opam`, which
   this job's sandbox denies (reads and exec). The fallback Frama-C 25 cannot run `-wp` at all.
   No system package was installed and none will be.

## The ordered attempts

None of the Frama-C experiments could be run: there is no WP plugin to run them with. The
table records that fact rather than invented timings. The time is the tool probe run, which
ended at 22:21 UTC.

| # | attempt | run? | time | ended? |
|---|---|---|---|---|
| 0 | baseline `proveFixDiv` from the main card (`-inline-calls @inline`, `-wp-rte -wp-fct proveFixDiv`) | yes, but Frama-C 25 has no WP: `make fixdiv` aborted in 0.23 s with `[kernel] User Error: option '-wp-fct' is unknown.` | 0.23 s | no: front end rejects every `-wp-*` option |
| 1 | smaller contracts (only the precondition fixDiv documents: none; `assigns \nothing` plus the four saturation ensures) | no: same blocker | - | - |
| 2 | split the wrapper into the stages of fixDiv (zero/nonzero denominator, fast path, wide path) | no: same blocker | - | - |
| 3 | `-wp-split` | no: same blocker | - | - |
| 4 | `-wp-fct proveFixDiv` and `-wp-prop` one property at a time | no: same blocker | - | - |
| 5 | `-wp-timeout` / `-wp-steps` per-goal caps | no: same blocker | - | - |
| 6 | per-function `timeout 600` in the Makefile | written, but not reached (no successful step 0) | - | - |

Red line from the attempted `make -C proofs/framac/fixdiv fixdiv` (the fallback Frama-C 25):

```
make: Entering directory '.../proofs/framac/fixdiv'
proveFixDiv              emulated           NO SUMMARY: run cut off by LIMIT or stopped (see log)
real    0m0.231s
[kernel] User Error: option `-wp-fct' is unknown.
  use `frama-c -help' for more information.
[kernel] Frama-C aborted: invalid user input.
```

The main card's pass-5 observation (results.md, "fixDiv: no result") is the only evidence
for the generation blow-up: frama-c stayed in VC generation with no prover process and one
core at 100 % for over 15 minutes on every arm, with and without `-wp-no-simpl`, and a full
`-ulevel 128` run did not finish in 580 s. Its unconfirmed hypothesis is the inlined
128-iteration `fixEmuUInt128DivMod` loop at `include/fixed/fixed_int128.h:417` (the loop has
no loop assigns and no variant, so WP unrolls it). This card could not confirm or refute that
here.

## The one experiment that did run: is any goal a real undefined behaviour?

The Eva cross-check named two imprecision alarms at `include/fixed/fixed.h:235`, the fast
path `return fixShiftLeft( a, FIX_FRACTION_BITS ) / b;` (division by zero and signed
overflow). Both are guarded above the line: `fixDiv` returns at fixed.h:226-229 when
`b == 0`, and enters the fast path only when `-(1<<47) < a < (1<<47)` (fixed.h:233), so
`a << 16` is in `(-2^63, 2^63)` and the only signed-division trap left, `INT64_MIN / -1`,
cannot be formed.

Confirmed with a C program under `-fsanitize=undefined` (UBSan, gcc 13.3.0,
`-fno-sanitize-recover=all`) on both arms, 114 cases: `a` in
`{INT64_MAX, INT64_MIN, 0, ±1, ±(2^47-1), ±2^47, ±(2^47+1), ±2^46, FIX_MAX, FIX_MIN, ±FIX_ONE}`
and `b` in `{0, 1, -1, INT64_MIN, INT64_MAX, FIX_ONE}`. Pasted output:

```
===== emulated (-DFIX_FORCE_EMULATED_INT128) =====
fixDiv edge sweep done: 114 cases, sink=140737488355329
rc=0
===== native =====
fixDiv edge sweep done: 114 cases, sink=140737488355329
rc=0
```

No UBSan report on either arm, and the two arms agree on the sink. So the two alarms at
fixed.h:235 are not undefined behaviour; they are Eva imprecision (the guards are not
propagated into the alarm).

## What would make generation end (plan for the next run, unexecuted)

The divide loop is only forced into the `proveFixDiv` VC by the main card's
`-inline-calls @inline`. The small-contract wrapper in this directory therefore keeps
`proveFixDiv` but the Makefile does **not** inline `fixEmuUInt128DivMod` (no `-inline-calls`
at all), so the wide path is one call to `fixInt128Div` and the 128-iteration loop stays out
of the `proveFixDiv` VC. The loop's own goals belong to `proveInt128Div`, which the main card
already runs separately. Whether that alone ends generation, and what the summary is, is
exactly what could not be measured here.

## Verdict (attempt 2)

HOLD. The main card has landed (`proofs/framac` is on sprint/fixed-core and in this checkout),
but the named toolchain (Frama-C 33 + WP, Alt-Ergo 2.4.3, Z3 4.13.3, Why3 1.8.2) is still
not reachable from this job: no `frama-c` and no prover is on PATH or anywhere under `/usr`,
`/opt`, `$HOME/sdk`, `/Users/glenn/sdk`, `/Users/glenn/.local`. Without a WP-capable Frama-C,
`make -C proofs/framac/fixdiv fixdiv` cannot be run, so no summary line can be produced and
DONE-WHEN does not hold. What is done: the toolchain evidence above, the UBSan disproof of the
two Eva alarms, and the unrun small-contract wrapper and Makefile that leave fixDiv out of
`nightly` until a bench can show it ends within 600 s.
