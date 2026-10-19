# fixDiv Frama-C WP results (attempt 4)

## Helper-contract card, attempt 3: HOLD on the bench's unexecutable toolchain

This run is on a Linux bench (`hulk`, x86_64), not the studio Mac of attempt
2, and `proofs/framac/fixdiv` is present at
`8de09685fde743527101d0b78aa20a5ab061853c`. The named toolchain is installed
but cannot be executed from this run's Landlock ruleset:

```text
$ command -v frama-c
/home/nova/sdk/bin/frama-c
$ ls -l /home/nova/sdk/bin/frama-c
... /home/nova/sdk/bin/frama-c -> /home/nova/.opam/default/bin/frama-c
$ /home/nova/.opam/default/bin/frama-c -version
bash: /home/nova/.opam/default/bin/frama-c: Permission denied
$ /home/nova/.opam/default/bin/alt-ergo --version
bash: /home/nova/.opam/default/bin/alt-ergo: Permission denied
```

The harness starts this run as
`nova-sandbox --read /home/nova/sdk ... --read-noexec /home/nova/go/pkg/mod
--read-noexec /home/nova/nova-bench/mirror/fixed.git/objects`; the real
Frama-C 33.0, Alt-Ergo 2.4.3 and Why3 1.8.2 binaries live in
`/home/nova/.opam/default/bin`, outside every `--read` root, so the kernel
returns `EACCES` on `execve` for each symlink. Only `/usr/bin/frama-c`
25.0-beta runs, and its `-plugins` list has no WP plugin; `/usr/bin/why3` is
1.6.0 and `/usr/bin/z3` is 4.8.12, not the named 1.8.2 and 4.13.3. There is
no Frama-C 33 under `/home/nova/sdk`, and this card may not install packages
or write outside its job directory, so the WP toolchain cannot be assembled
here.

Red run attempted as required, three times capped by `timeout 600` and
`nice -n 19`, with `OUT` under `$TMPDIR`:

```text
$ make -C proofs/framac/fixdiv fixdiv
NO SUMMARY: see .../fixdiv-out-red/fixdiv.emulated.log
[kernel] User Error: option `-wp-fct' is unknown.
[kernel] Frama-C aborted: invalid user input.
```

That is Frama-C 25 rejecting the WP option, not a `Proved goals:` line, so
there is no valid before/after baseline and no helper body could be proved or
classified PROVED/ASSUMED. No `helpers_acsl.h` and no Makefile edit were made:
unverified contracts would violate the card's "never state an ensures the
code does not honour" rule. No library header or production code changed.

The fixDiv call tree that the next bench run must cover, under the Makefile's
`-DFIX_FORCE_EMULATED_INT128`:

```text
fixDiv
  fast path: fixShiftLeft
  wide path:
    fixInt128FromI64 -> fixEmuInt128FromI64 -> fixEmuInt128Make
    fixInt128ShiftLeft -> fixInt128ToUnsigned (fixEmuToUnsigned)
                       -> fixUInt128Shl (fixEmuUInt128Shl)
                       -> fixInt128FromUnsigned (fixEmuToSigned -> fixEmuInt128Make)
    fixInt128Div -> fixEmuInt128Div -> fixEmuInt128DivMod
        -> fixEmuInt128IsNegative, fixEmuToUnsigned,
           fixEmuUInt128Neg -> fixEmuUInt128Sub -> fixEmuUInt128Make,
           fixEmuUInt128DivMod, fixEmuToSigned
    fixInt128Gt -> fixEmuInt128Gt -> fixEmuInt128Lt
    fixInt128Lt -> fixEmuInt128Lt
    fixInt128ToI64 -> fixEmuInt128ToI64
fixEmuUInt128DivMod
    -> fixEmuUInt128Make, fixEmuUInt128FromU64, fixEmuUInt128Shl,
       fixEmuUInt128Ge -> fixEmuUInt128Lt, fixEmuUInt128Sub,
       fixEmuUInt128Or
```

Work left for a bench whose `--read`/exec rule includes
`/home/nova/.opam/default/bin` (or whose `$HOME/sdk/bin` holds real binaries):
list the tree, write `helpers_acsl.h` bottom-up with
`requires`/`assigns`/`terminates`/`ensures`, attach the contracts by
declaration from `fixdiv_acsl.c`, add capped per-helper prove targets to the
Makefile, then record the pre/post `Proved goals:` lines and each helper's
PROVED/ASSUMED status here.

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
