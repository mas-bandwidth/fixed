# Log-family Gappa proof

Tool: Gappa 1.6.0 (`gappa kernel.g`), on Linux. Not run on other versions.

Gappa models the shared `log1p` Horner evaluation from
`src/exact/log.c` (and the identical kernel in `src/exact/log_kernel.h`) over
the table-reduced input interval `u in [0, 2^-7 + 2^-16]`. The model uses the
stored Q.127 coefficient magnitudes, Q.127 truncating products
(`fixed<-127, dn>`), the Q.128 truncated square (`fixed<-128, dn>`), and the
final realignment, exactly as the source computes them. Each truncation is
`fixed<-k, dn>`, downward rounding to a multiple of `2^-k`, which is what the
source's shift does for the positive operands here.

## What is proved

Gappa proves interval bounds on formulas derived by hand (next section); it
does not check those formulas against the `fixed<>` intermediates.

* The kernel evaluation error against the stored-coefficient polynomial,
  `E = rho_q*w_2 + rho_v - u^2*d_2`, is bounded: `|E| <= 7.35e-39`
  (`2^-126.68`).
* The per-function totals in raw units meet these goals: log2 at most
  `2^-84.5` and log10 at most `2^-86.2` (their design budgets), and ln with
  its table and shift terms at most `2^-85`. Gappa prints each enclosure
  itself (`in ?` goals); `results.md` tabulates them.

## What is derived by hand (the same method as `proofs/gappa/tan`)

* Each truncation residue `rho = x - fixed<-k,dn>(x)` lies in `[0, 2^-k)`:
  this is the definition of the operator for positive operands, entered as
  an interval hypothesis on each `rho`. The `fixed<>` intermediates `w_k`,
  `qq`, `vv` and `comp` are written out in `kernel.g` as the source computes
  them, but the error formula `E` is stated in the residues; Gappa does not
  check it against `comp`.
* The local Horner errors `d_k = w_k - H_k` satisfy
  `d_k = -u*d_{k+1} + rho_k`, and `E` follows from `p(u) = u - u^2 H_2`.

## What is taken from Sollya

The polynomial-versus-`log1p` error `|p(u) - log1p(u)| <= h` is not
re-proved: `kernel.g` adds `h` as a constant, the upper endpoint of the
certified `supnorm` interval in `sollya/log_kernel.out`
(`3.8373354791488578913...e-31`, log2 `-101.03966522216796875`), Sollya's
fpminimax bound including coefficient quantisation.

The conversions from the kernel's natural-log value to raw units, and the
`log2(e)`/`log10(e)` rescaling, follow the design
(`docs/design/transcendentals.md` 2.1-2.3); the constant-rounding terms in
the log2 and log10 totals are the design's `2^-104` and
`33*2^-129 + 2^-120` terms; log10 also carries the ln sum's table and shift
truncations (`2^-118`, scaled by `log10(e)`), which the design's 2.3 line
leaves out.

## Result

`make -C proofs/gappa/log` runs the proof and prints one proved bound per
function. All three totals are within their design budgets: ln against
`2^-85.039` raw, the design's kernel line `2^-101.039` being Sollya's
certified supnorm `2^-101.03967` rounded up (an earlier `2^-101.04` rounded
it down; `results.md` records that finding), log2 against `2^-84.5` and
log10 against `2^-86.2`.

`ln_raw` is the kernel only (`|E| + h`), as the design's ln line is;
`ln_full_raw` adds the table and shift terms (`2^-118`, before the `2^16`
scale) and is proved below `2^-85`.
