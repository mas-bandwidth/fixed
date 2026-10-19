# fixDiv Frama-C WP results (attempt 4)

## Helper-contract card, attempt 2: HOLD before the red run

The helper-contract card was staged on `Glenns-Mac-Mini.local` (the studio
Mac), not on the Linux bench amd-2 that its toolchain names. After
`export PATH="$HOME/sdk/bin:$PATH"`, `command -v` reports `frama-c`,
`why3`, `alt-ergo` and `z3` all missing; only `gcc`, `make` and `git` are
present. Per the card rules this is a HOLD naming the absent tools, so no
`make -C proofs/framac/fixdiv fixdiv` run was started and no helper contract
was written: without WP there is no way to prove a body goal or classify a
contract PROVED/ASSUMED, and writing unverified contracts would violate the
"never state an ensures the code does not honour" rule. Nothing in this
file below was changed by attempt 2.

Work left for a bench run: list the fixDiv call tree under
`-DFIX_FORCE_EMULATED_INT128`, add `proofs/framac/fixdiv/helpers_acsl.h`
with `requires`/`assigns`/`terminates`/`ensures` for each helper, attach
the contracts by declaration from `fixdiv_acsl.c`, add per-helper prove
targets to the Makefile, then record the pre/post `Proved goals:` lines and
each helper's PROVED/ASSUMED status here.

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

## Helper-contract card, attempt 3: HOLD before the red run

Same environment failure as attempt 2. The checkout was staged on
`Glenns-Mac-Mini.local` (Darwin 27.0.0, arm64, the studio Mac), not on the
Linux bench amd-2 named by the card (`GATE-NEEDS: linux`). After
`export PATH="$HOME/sdk/bin:$PATH"`, `command -v` reports `frama-c`,
`why3`, `alt-ergo` and `z3` all missing. `$HOME/sdk/bin` does not exist, and no `frama-c`
executable is present at `/Users/glenn/sdk/bin/frama-c` or
`/opt/homebrew/bin/frama-c`.
The `timeout` utility is also absent, so the Makefile's `LIMIT = timeout
600` cannot run at all.

Red-first attempt, unmodified tree:

```text
$ nice -n 19 make -C proofs/framac/fixdiv fixdiv
mkdir -p out
NO SUMMARY: see out/fixdiv.emulated.log
make: *** [fixdiv] Error 1
$ cat out/fixdiv.emulated.log
/bin/sh: timeout: command not found
```

No WP run, no helper contract and no PROVED/ASSUMED classification was
produced. Per the card rules this is a HOLD naming the absent tools and the
wrong host. No library header, Makefile, `fixdiv_acsl.c`, `notes.md`,
CI or CTest file was changed by attempt 3; the `out/` directory from the
red run was removed before committing.

Work left for a bench run: list the fixDiv call tree under
`-DFIX_FORCE_EMULATED_INT128`, add `proofs/framac/fixdiv/helpers_acsl.h`
with `requires`/`assigns`/`terminates`/`ensures` for each helper (including
the INT128_MIN / -1 exclusion), attach the contracts by declaration from
`fixdiv_acsl.c`, add per-helper prove targets to the Makefile, then record
the pre/post `Proved goals:` lines and each helper's PROVED/ASSUMED status
here.
