# Log-family Gappa proof

Tool: Gappa 1.6.0 (`gappa kernel.g`), on Linux. Not run on other versions.

Gappa models the shared `log1p` Horner evaluation from
`src/exact/log.c` (and the identical kernel in `src/exact/log_kernel.h`) over
the table-reduced input interval `u in [0, 2^-7 + 2^-16]`. The model uses the
stored Q.127 coefficient magnitudes, Q.127 truncating products
(`fixed<-127, dn>`), the Q.128 truncated square (`fixed<-128, dn>`), and the
final realignment, exactly as the source computes them. Each truncation is
`fixed<-k, dn>`, downward rounding to a multiple of `2^-k`: the source's
`mulhi` is `(a*b) >> 128` on unsigned integers, a floor of the exact product.

## What is proved

`kernel.g` defines two chains over the same coefficients and input:

* the computed value `comp`: each Horner step `w_k = a_k - fixed<-127,dn>(w_{k+1}*u)`,
  then `qq = fixed<-128,dn>(u*u)`, `vv = fixed<-127,dn>(qq*w_2)`,
  `comp = u - vv`, as the source does them;
* the exact stored-coefficient polynomial `p = u - u^2 H_2`, with
  `H_k = a_k - H_{k+1}*u` in exact arithmetic.

Gappa proves, from those definitions alone and with no hints:

* the kernel evaluation error `E = comp - p` lies in
  `[-3.60e-43, 7.3468e-39]`, so `|E| <= 7.35e-39` (`2^-126.678`);
* with the Sollya hypothesis below, the total kernel error
  `K = comp - log1p(u)` lies in `[-3.83734e-31, 3.83734e-31]`;
* the per-function totals in raw units meet these goals: log2 at most
  `2^-84.5` and log10 at most `2^-86.2` (their design budgets), and ln with
  its table and shift terms at most `2^-85.039` (its design budget) and at
  most `2^-85`. Gappa prints each enclosure itself (`in ?` goals);
  `results.md` tabulates them.

No error formula is stated by hand: Gappa's own rewriting splits each
`w_k - H_k` into the rounding error of its step, which it bounds from the
`fixed<>` operator, and the difference carried from the step before.

## What is a hypothesis

The polynomial-versus-`log1p` error `|p(u) - log1p(u)| <= h` is not
re-proved: `kernel.g` takes it as the hypothesis `p - L in [-h, h]`, with `L`
the exact `log1p(u)` and `h` the upper endpoint of the certified `supnorm`
interval in `sollya/log_kernel.out` (`3.8373354791488578913...e-31`, log2
`-101.03966522216796875`), Sollya's fpminimax bound including coefficient
quantisation.

The conversions from the kernel's natural-log value to raw units, and the
`log2(e)`/`log10(e)` rescaling (entered as the upper bounds `1.4427` and
`0.4343`), follow the design (`docs/design/transcendentals.md` 2.1-2.3); the
constant-rounding terms in the log2 and log10 totals are the design's
`2^-104` and `33*2^-129 + 2^-120` terms, and the ln sum's table and shift
truncations enter as the design's `2^-118` (2.1, `2^-118.4 < 2^-118`); log10
also carries that `2^-118`, scaled by `log10(e)`, which the design's 2.3
line leaves out. These outer terms are the design's; Gappa proves the kernel
and adds them.

## Result

`make -C proofs/gappa/log` runs the proof and prints one proved bound per
function. All three totals are within their design budgets: ln against
`2^-85.039` raw, the design's kernel line `2^-101.039` being Sollya's
certified supnorm `2^-101.03967` rounded up (an earlier `2^-101.04` rounded
it down; `results.md` records that finding), log2 against `2^-84.5` and
log10 against `2^-86.2`.

`ln_raw` is the kernel only (`|K|`), as the design's ln line is;
`ln_full_raw` adds the table and shift terms (`2^-118`, before the `2^16`
scale) and is proved below `2^-85.039` and below `2^-85`.
