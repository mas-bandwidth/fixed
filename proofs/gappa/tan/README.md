# tan kernel proof

Gappa version: 1.4.1 (`gappa --version`).

## What is proved

For the odd degree-13 tan kernel of `fixTanKernel`
(`src/exact/tan_kernel.h`) and the pole-path Horner `fixTanExactPoleW`
(`src/exact/tan.c`), evaluated in integer fixed point on the reduced interval:

* **Table path kernel evaluation error** — `|E| = |t - p(x)| <= 7.0e-39`
  (`~2^-126.8`), where `x = |s| in [0, 2^-8 + 2^-100]`, `t` is the kernel
  computed by Horner with the source's fixed rounding operators (Q0.128 for `x`
  and `s2`, Q0.127 for the Horner accumulators, `c` and the final `x*c` product),
  and `p(x)` is the exact polynomial with the stored coefficients. This is the part
  Gappa proves from the integer formats alone.
* **Total table-path kernel error** — `|E| + 8.460112648223753998e-44 <= 2^-125` (the design's kernel budget line) against `tan`.
* **Pole-path Horner error** — `|Er| = |w - r^2*q(r^2)| <= 7.0e-39` and relative
  `|Er|/(1+w) <= 7.0e-39` on `|r| < 1/256`, within the design's `2^-126` pole-path
  "Horner for w" line.

The proof models each truncating product by its fixed rounding residue: every
`mulhi(a,b) = (a*b) >> 128` is `fixed<-k, dn>` (downward rounding to a multiple of
`2^-k`), losing less than one unit of the target format (`2^-128` for `s2`,
`2^-127` everywhere else). The evaluation error then decouples from the
coefficients and obeys a short backward recurrence (see `kernel.g`), which is what
Gappa bounds.

## What is a hypothesis

The polynomial-versus-function error comes from `sollya/tan_kernel.out` (supnorm
of the quantised polynomial against `tan` over the whole kernel interval,
coefficient quantisation included):

* `|p(x) - tan(x)| <= 8.460112648223753998e-44` (absolute, log2 -143.084),
* `|p(x)/tan(x) - 1| <= 9.100984361674279281e-41` (relative, log2 -133.013).

The absolute one enters the table-path budget goal above; the relative one is the
pole-path kernel hypothesis the design combines with the Horner-for-w bound.

## Running

    make -C proofs/gappa/tan

runs `gappa kernel.g` and verifies all four goals (exit 0, no unsatisfied
properties).
