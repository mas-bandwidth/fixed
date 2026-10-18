# Frama-C WP runtime-error proofs for the integer kernels

`kernels_acsl.c` wraps fixMul, fixDiv, fixISqrt128High and the int128 seam primitives
they use, each wrapper carrying an ACSL contract that states only what the library
already documents. No library header is edited. `make -C proofs/framac all` runs, one
wrapper at a time:

    frama-c -wp -wp-rte -wp-prover alt-ergo,z3 -wp-timeout 60

with `-inline-calls @inline` so that inlined helpers' RTE goals (signed overflow, shift
ranges, division by zero, float-to-int range) are proved in the calling context. The library loops carry no loop assigns or loop variant (no header is edited
here), so WP warns "Missing assigns clause" for each and its assigns and terminates goals
cannot close. Those frame goals therefore run in their own arm (`make frame`, 10 s per goal);
the emulated and native arms prove the RTE and ensures goals at 60 s per goal. Every
frama-c run is bounded by `timeout 1800` (the LIMIT variable). Logs go to `out/` (not
committed); `make summary` prints the table, which is copied by hand into `results.md`. A
row marked PROVER MISSING counts Qed goals only.

This runs on Linux benches only. It is not part of CTest and not part of CI.

## Toolchain

- Frama-C 33.0 (Arsenic) with the WP plugin, from opam in `~/sdk/bin` on the Linux benches
  (checked on amd-2, 2026-10-18).
- Alt-Ergo 2.4.3 (`~/sdk/bin`), Z3 4.13.3 (`/usr/bin`), Why3 1.8.2, registered with
  `why3 config detect` for the bench user. Gappa 1.6.0 is also detected but not used.
- The native arm: Frama-C 33 parses the headers, but its preprocessing does not appear to
  define `__SIZEOF_INT128__`, so fixed_int128.h:567 selects the emulated path and the
  native rows repeat the emulated ones (see `results.md`). Frama-C 25 rejected `__int128`
  outright (syntax error at include/fixed/fixed_int128.h:577).

## Assumptions

`fixISqrt128High` seeds from `__builtin_sqrt`, for which Frama-C has no contract. The file
declares one: finite, non-negative, and at most 2^52 or 2^32 for inputs at most 2^104 or
2^64. That is the IEEE square root's monotone, correctly rounded behaviour, and only the
range matters (the repair loops correct any seed).

On the native arm under x86-64, fixInt128Div's fast path is inline asm (`divq`), which WP
cannot read: its outputs are unconstrained, a second trusted step on that arm only.
