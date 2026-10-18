# Frama-C WP runtime-error proofs for the integer kernels

`kernels_acsl.c` wraps fixMul, fixDiv, fixISqrt128High and the int128 seam primitives
they use, each wrapper carrying an ACSL contract that states only what the library
already documents. No library header is edited. `make -C proofs/framac all` runs, one
wrapper at a time:

    frama-c -wp -wp-rte -wp-prover alt-ergo,z3 -wp-timeout 60

with `-inline-calls @inline` so that every inlined helper's RTE goals (signed overflow,
shift ranges, division by zero, float-to-int range) are proved in the calling context.
Results go to `out/` (not committed) and the table to `results.md` (committed).

This runs on Linux benches only. It is not part of CTest and not part of CI.

## Toolchain

- Frama-C with the WP plugin. Checked on amd-1: Frama-C 33.0 (Arsenic), WP loaded.
  hetzner2 carries Debian's frama-c-base 25.0-beta (Manganese), which has no WP plugin.
- SMT provers alt-ergo and z3, registered with Why3. On 2026-10-18 neither is installed on
  either bench, so the full run has not happened yet; see `results.md`.
- The native arm needs the front end to accept `__int128`. Frama-C 25 does not (syntax error
  at include/fixed/fixed_int128.h:577); Frama-C 33 is not yet checked.

## The one assumption

`fixISqrt128High` seeds from `__builtin_sqrt`, for which Frama-C has no contract. The file
declares one: finite, non-negative, and at most 2^52 or 2^32 for inputs at most 2^104 or
2^64. That is the IEEE square root's monotone, correctly rounded behaviour, and only the
range matters (the repair loops correct any seed).
