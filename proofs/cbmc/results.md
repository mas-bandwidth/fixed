# CBMC equivalence results

Environment: CBMC 6.6.0, Linux x86_64, Z3, with `--bounds-check --signed-overflow-check --undefined-shift-check --unwinding-assertions --slice-formula`. Commands use GNU `timeout`; CBMC 6.6.0 rejects `--timeout` as an unknown option. Full raw logs are in the allocated job evidence directory (see `JOB.md` outside this repository).

## Completed bounded checks

| Harness | Width / unwind | Status | Properties | Duration |
|---|---|---|---:|---:|
| shifts | `CBMC_WIDTH=32`, unwind 8 | PASS | 428/428 | 3.31 s |
| compares | `CBMC_WIDTH=32`, unwind 8 | PASS | 428/428 | 3.31 s |

The shift/compare harnesses use full 64-bit words for operands; `CBMC_WIDTH` only narrows the signed scalar generator used by fixed-point multiply/divide. Their loops enumerate 6 shift results and 4 comparison results, respectively, so unwind 8 covers the complete harness loops. Arithmetic helpers and all assertions instrumented in the three translation units were checked by CBMC. Logs: `cbmc.shifts.log`, `cbmc.compares.log`.

## Incomplete or not established

The full `make all` target is not established. The full-domain `fixmul` attempts did not finish within 1200 s (SAT and Z3); no proof result is inferred from timeout. A separate `WIDTH=32` fixmul attempt used an earlier harness with invalid unresolved nondeterministic names and was stopped; it is not evidence. The corrected width-32 multiply proof timed out at the 180 s cap (exit 124); no proof result is inferred. `fixdiv`, `isqrt`, and `divisions` were not run against the corrected harness. Do not infer 128-bit-wide agreement from these results.

The bounded claim is limited to the listed harnesses and configuration. It is not a whole-program proof, does not cover compiler code generation or ABI behavior, and has no claim for unlisted inputs or operations. The division harness deliberately excludes zero divisors, signed minimum divided by -1, and 128/64 cases where the high limb is not less than the divisor, as required for defined native operations / the helper precondition.

## Harness correction

Initial output included `harness_shifts.no-body.__CPROVER_nondet_ulonglong` (and related `no-body` properties). The harness had declared a reserved `__CPROVER_nondet_*` identifier instead of using CBMC's documented `nondet_` user-stub naming convention. The corrected source declares `nondet_longlong` and `nondet_ulonglong`; this fixes the harness model. The earlier failed output was a missing-body modeling diagnostic, not evidence of a deterministic return value or a defect in fixed-point arithmetic. All subsequent claims use the corrected source; the initial failure log is retained for audit.
