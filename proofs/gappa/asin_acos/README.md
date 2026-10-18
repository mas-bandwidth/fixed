# asin_acos kernel proof

Gappa version: 1.4.1 (`gappa --version`).

## What is proved

For the degree-15 Horner kernel of `fixAsinExact` / `fixAcosExact` on the reduced
interval [0, 1/2], evaluated in Q61 integer fixed point with a truncating shift
(Gappa operator `fixed<61, dn>`, downward rounding to multiples of 2^-61):

* `|e0| <= 9e-19` — the kernel's integer-evaluation error against the exact
  fixed-point polynomial. This is the part Gappa proves from the integer formats.
* `|e0| + 6.17624436320711e-15 <= 6.19e-15` — the total kernel error against
  `asin`, inside the 6.19e-15 design budget.

`e0` is the accumulation of the 15 per-step truncation residues (see `kernel.g`).
The recurrence decouples from the polynomial coefficients, so the proof needs only
the interval `x in [0, 1/2]` and the residue range `[-2^-61, 0]`.

## What is a hypothesis

The polynomial-versus-function bound

    |p(x) - asin(x)| <= 6.17624436320711e-15

is taken as a hypothesis, certified by Sollya's `supnorm` in `sollya/asin_acos.out`
(supremum of the quantised degree-15 polynomial against `asin` over [0, 1/2],
coefficient quantisation included). It is entered as the additive constant in the
second goal above; the first goal (the pure evaluation error) does not rely on it.

## Running

    make -C proofs/gappa/asin_acos

runs `gappa kernel.g` and prints the two proved goals.
