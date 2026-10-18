# exp family kernel proof

Gappa version: 1.6.0 (`gappa -v`).

## What is proved

For the shared exp kernel `fixExpKernel` (`src/exact/exp_kernel.h`) used by
`fixExpExact` (`src/exact/exp.c`), `fixSinhExact`/`fixCoshExact`
(`src/exact/sinh_cosh.c`) and `fixTanhExact` (`src/exact/tanh.c`), evaluated in
integer fixed point on the reduced interval `r in [0, ln2/128]`:

* **Kernel evaluation error** — `|E| = |M - T*P(r)| <= 2.4e-38` (`~2^-124.97`),
  where `M` is the Q2.126 mantissa the kernel returns, `T = T_j/2^126` is the
  table factor in `[1, 2)`, and `P(r) = 1 + r + sum C_k*2^-127 r^k` is the exact
  polynomial with the stored coefficients. This is the part Gappa proves from the
  integer formats alone.
* **Total kernel error** — `|M - T*e^r| <= |E| + T*|P(r) - e^r| <= 2.4725e-31`
  (`~2^-101.67`), the kernel budget line of `docs/design/transcendentals.md` 1.1,
  against the function.
* **exp raw budget** — `m <= 46`, so the pre-rounding error is
  `<= 2^(m+16) * 2.4725e-31 <= 2^62 * 2.4725e-31
  = 1.143247004185311e-12` (`2^-39.67` raw), the design's exp budget.
* **sinh/cosh raw budget** — `y_raw = 2^(ma+15) * (Ma -/+ Mb*2^-d)`. For
  `ma >= 1`, `d >= 2` (the two calls use `N` and `-N`), so the `Mb` term is
  damped by `1/4`, and `ma <= 47` gives `2^(ma+15) <= 2^62`; for `ma = 0` the
  scaling is `2^15`. Case `ma >= 1`: `<= 1.7089819252105546e-12`
  (`2^-39.09` raw). Case `ma = 0`: the same bound, with room to spare. This is
  the design's `2^-39.09` raw exp-path budget.
* **tanh raw budget** — `d/dM` of `(M-a)/(M+a)` is at most `1/2`, so the raw
  error is at most `2^15 * 2.4725e-31` plus the `2^-128` quotient truncation
  (`2^-112` raw), i.e. `<= 8.123258540033696e-27` (`2^-86.67` raw), the design's
  tanh budget.

Each truncating `mulhi(a,b) = (a*b) >> 128` is modelled by the fixed rounding
operator of its format. The Horner intermediates use Q.127, the final mantissa
product uses Q2.126, and Gappa proves from those operators that every truncation
residue lies in `[0, 2^-127>` (`[0, 2^-126>` for `M`). The evaluation error then
decouples from the coefficients and obeys the short backward recurrence in
`kernel.g`, whose result is exactly `M - T*P(r)`.

The final round to nearest (`(M + 2^(s-1)) >> s` for exp, the analogous shifts
for sinh/cosh/tanh) is a separate exact half-up shift and is not part of `eps`;
the design's budgets are on the pre-rounding value.

## What is a hypothesis

The polynomial-versus-function error comes from `sollya/exp_kernel.out` (supnorm
of the quantised degree-8 polynomial against `e^r` on the whole reduced interval,
coefficient quantisation included):

* `|P(r) - e^r| <= 1.236209838752694113105510039539433637113639803289646403008807013447042878301419192191865209492533254646400253656704902499e-31`
  (`log2 = -102.6738433837890625`).

The coefficients `C_2..C_8`, the table entries `T_j`, the interval
`[0, ln2/128]`, and the scaling bounds (`m <= 46`, `ma <= 47`) are read from the
source and the design, not proved here.

## The small-|x| kernels at this BASE

`docs/design/transcendentals.md` 1.3/1.4 describe separate small-`|x|` odd
polynomials for sinh and tanh, with their own Sollya supnorms. Those kernels are
**not present at this BASE**: `src/exact/tanh.c` says "There is no small-|x|
polynomial: the one-step rule above is the coordinator's", and
`src/exact/sinh_cosh.c` uses the shared exp kernel for every non-identity branch.
The proof therefore covers the path the library actually evaluates: the exp
kernel, the exact identity cut-offs (`sinh`/`tanh` near zero), and the exp-kernel
combinations. There is no unproved small-kernel code path to bound.

## Running

    make -C proofs/gappa/exp

runs `gappa kernel.g` and prints the proved bounds for exp, sinh/cosh and tanh.
Gappa exits 0 with no unsatisfied properties.
