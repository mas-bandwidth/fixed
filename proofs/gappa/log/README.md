# Log-family Gappa proof

Tool: Gappa 1.6.0 (`gappa kernel.g`). The proof source is compatible with
Gappa 1.4.1; the numbers below were produced with 1.6.0 on Linux.

Gappa models the shared `log1p` Horner evaluation from
`src/exact/log.c` (and the identical kernel in `src/exact/log_kernel.h`) over
the table-reduced input interval `u in [0, 2^-7 + 2^-16]`. The model uses the
stored Q.127 coefficient magnitudes, Q.127 truncating products
(`fixed<-127, dn>`), the Q.128 truncated square (`fixed<-128, dn>`), and the
final realignment, exactly as the source computes them. Each truncation is
`fixed<-k, dn>`, downward rounding to a multiple of `2^-k`, which is what the
source's shift does for the positive operands here.

## What is proved

* Given the truncation residues and the Horner error recurrence below, the
  kernel evaluation error against the stored-coefficient polynomial,
  `E = comp - p(u) = rho_q*w_2 + rho_v - u^2*d_2`, is bounded:
  `|E| <= 7.35e-39` (`2^-126.68`).
* The per-function totals `|E| + h` converted to raw units are bounded by
  Gappa as tabulated in `results.md`. Gappa prints each enclosure itself
  (`in ?` goals); the Makefile then prints the design budgets beside them.

## What is derived by hand (the same method as `proofs/gappa/tan`)

* Each truncation residue `rho = x - fixed<-k,dn>(x)` lies in `[0, 2^-k)`:
  this is the definition of the operator for positive operands, entered as
  an interval hypothesis on each `rho`. The `fixed<>` intermediates `w_k`,
  `qq`, `vv` and `comp` are written out in `kernel.g` as the source computes
  them, but the error formula `E` is stated in the residues; Gappa does not
  check it against `comp`.
* The local Horner errors `d_k = w_k - H_k` satisfy
  `d_k = -u*d_{k+1} + rho_k`, and `E` follows from `p(u) = u - u^2 H_2`.

## What is a hypothesis

The polynomial-versus-`log1p` error
`|p(u) - log1p(u)| <= h` with
`h = 3.837335479148857891302978686748981565437784801207944958327721352762597264308934632253007019224816838110169786977841947941e-31`
(the upper endpoint of the certified `supnorm` interval in
`sollya/log_kernel.out`, log2 `-101.03966522216796875`) is **not** re-proved
here. It is Sollya's certified fpminimax bound, including coefficient
quantisation.

The conversions from the kernel's natural-log value to raw units, and the
`log2(e)`/`log10(e)` rescaling, follow the design
(`docs/design/transcendentals.md` 2.1-2.3); the constant-rounding terms in
the log2 and log10 totals are the design's `2^-104` and
`33*2^-129 + 2^-120` terms.

## Result

`make -C proofs/gappa/log` runs the proof and prints one proved bound per
function. The ln total exceeds the design's stated budget by 0.023% because
the design writes its kernel line as `2^-101.04`, a two-decimal exponent
slightly below Sollya's certified supnorm `2^-101.03967`; `results.md` records the finding. The log2 and
log10 totals are within their budgets.

The ln total here is the kernel only (`|E| + h`), as the design's ln kernel
line is; the design's table and shift terms (`2^-118.4` together, before the
`2^16` scale) are not added, and would only widen the ln excess.
