# Loop-annotation results

This directory holds the loop invariants, loop assigns and loop variants for the
fixISqrt128High loops (`include/fixed/fixed.h:301, 302, 326, 330, 342, 347`) and for
the shift-subtract loop of `fixEmuUInt128DivMod` (`include/fixed/fixed_int128.h:417`).
No library header is edited: `wrappers_acsl.c` copies the two functions and the divide
chain with only the annotations added, and `loops_acsl.acsl` is imported with
`-acsl-import`.

## Attempt status: not run, toolchain unavailable in this job (attempt 2)

The base state was re-read from the landed card (head 5e35893):
`make -C proofs/framac sqrt` was 49 / 107 (52 assigns and 6 terminates open) and
`make -C proofs/framac primitives` left proveInt128Div at 22 / 27 (shift_2 and
mem_access_5..8 open).

This attempt runs on the studio machine (`uname -s` = Darwin), not the Linux bench the
toolchain names. `command -v frama-c`, `alt-ergo` and `why3` all print nothing with and
without `$HOME/sdk/bin` on PATH, and no Frama-C binary was reachable from this job. No
Frama-C process could be started, so no goal was proved or disproved in this attempt and
no log exists. `make -C proofs/framac/loops sqrt` and `... prims` are for the nightly
Linux bench only and were not runnable here.

What is done: the annotations, the Makefile with `sqrt`, `prims` and `nightly`, and the
imported ACSL file are committed. The loop annotations were re-checked by hand in this
attempt against the source loops (`include/fixed/fixed.h:301, 302, 326, 330, 342, 347`
and `include/fixed/fixed_int128.h:417`) and state only what the code documents. The
copied bodies were checked against the originals with gcc
(`-DFIX_FORCE_EMULATED_INT128`, 2,000,000 random pairs per function, plus the 128-bit
endpoint) and returned bit-identical results; that is a differential test, not a proof,
and none of the counts below come from it.

## The annotations

- fixISqrt128High, 104-bit repair (`fixed.h:301`, `:302`): `r <= 2^52`, assigns `r`,
  variants `r` and `2^52 - r`. 2^52 is the square root of the branch bound 2^104, the
  value FIX_SQRT_SEED's range contract gives.
- fixISqrt128High, 64-bit repair (`fixed.h:326`, `:330`): `r <= UINT32_MAX` and
  `r*r <= lo` (the header's "r <= UINT32_MAX, so its square fits uint64_t" and "after
  the downward repair, lo-r*r is nonnegative"), assigns `r`, variants `r` and
  `UINT32_MAX - r`.
- fixISqrt128High, restoring square root (`fixed.h:342`, `:347`): assigns `d` and
  `x, c, d`, variant the unsigned 128-bit value of `d` defined in `loops_acsl.acsl`
  (the shift by two quarters it, so it strictly decreases).
- fixEmuUInt128DivMod (`fixed_int128.h:417`): `-1 <= i <= 127`, assigns
  `i, quotient, remainder`, variant `i + 1`. The bound is what closes shift_2: the
  `i - 64` lane shift is only evaluated for 64 <= i <= 127.

## Goals still open (base figures, no run in this attempt)

### fixISqrt128High frame: 58 goals

- 52 assigns goals: `assigns_normal_part07` to `part35`, `part41`, `part42`, and
  `part73` to `part93`.
- 6 terminates goals: `terminates_part2` to `terminates_part7`.

Base reason: the repair and shift-subtract loops had no loop assigns and no loop
variant, so WP fell back to `assigns everything` (include/fixed/fixed.h:301, 302, 326,
330, 342, 347). The annotations above are the proposed close. This attempt could not
rerun them.

### Int128Div, emulated: 5 runtime-error goals

- `assert_rte_shift_2`: the lane shift `dividend.hi >> ( i - 64 )` at
  include/fixed/fixed_int128.h:421, inside the loop at fixed_int128.h:417.
- `assert_rte_mem_access_5` and `_6`: the out-parameter writes after that loop, at
  fixed_int128.h:433 and 434.
- `assert_rte_mem_access_7` and `_8`: the writes of fixEmuInt128DivMod's results
  (fixed_int128.h:541 and 545), reported at fixed_int128.h:184, the inlined
  fixEmuToSigned.

Base reason: the loop had no invariant or loop assigns. `shift_2` needs the i bound;
mem_access_5..8 need the loop assigns to preserve the out-pointer knowledge. The
annotations above are the proposed close. This attempt could not rerun them.

### fixISqrt128High, emulated: 2 float seed goals (prover limit, out of scope)

- `assert_rte_is_nan_or_infinite`: `\is_finite` of `(double)hi * 2^64`, the seed input
  at include/fixed/fixed.h:300.
- `assert_rte_is_nan_or_infinite_2`: `\is_finite` of the sum with `(double)lo`, also at
  include/fixed/fixed.h:300.

Reason: a prover limit, not undefined behaviour. Both hold because the operands are
below 2^104, far under DBL_MAX. WP's float model gives the rounding as an
uninterpreted function; Alt-Ergo and Z3 time out at 60 s, and Gappa 1.6.0 returned
Unknown when tried by hand in pass 5. Unchanged by this card.

## Toolchain and commands

Frama-C 33.0 (Arsenic), Alt-Ergo 2.4.3, Z3 4.13.3 and Why3 1.8.2 on the Linux bench,
nightly only; the Makefile uses `-wp -wp-rte -wp-prover alt-ergo,z3 -wp-timeout 60
-wp-par 4 -wp-cache none`, `-inline-calls @inline`, `-DFIX_FORCE_EMULATED_INT128` and
`-acsl-import loops_acsl.acsl`, with `timeout 1800` per run. The main card's `nightly`
is extended to call `make -C proofs/framac/loops nightly`.
