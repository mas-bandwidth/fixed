# fixDiv Frama-C WP results (attempt 4)

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
