# BaCSeL hard-case lists

BaCSeL is GPL-2.  It is used here only as a separate tool: this directory
contains no BaCSeL source, no BaCSeL object code and nothing links against it.
The lists in `test/data/hardcases/` are output data.

Pinned BaCSeL commit: `e372414bc6af4a2911e07abe03de9854f32561fd`
(https://gitlab.inria.fr/zimmerma/bacsel.git, version 4.0, GPL-2).
Built against the system GMP/MPFR and FPLLL 5.5.0.

## Mapping from Q48.16 to BaCSeL arguments

A Q48.16 raw `r` is the value `v = r / 2^16`.  For an input binade
`[2^e, 2^(e+1))`, `r` has `e + 17` significant bits.  BaCSeL is run with

    -n e+17  -e_in e+1  -t0 2^(e+16)  -t1 2^(e+17)

so that its input `x * 2^(e_in - n)` is exactly `r / 2^16`.  The output is
round-to-nearest to 2^-16, so the output binade exponent `eo` gets
`-nn eo+16`; then BaCSeL's scale `2^(nn-eo)` is exactly `2^16` and its printed
distance is the distance to a rounding midpoint in units of 2^-16.

`k` is stated in each list header.  For the monotonic functions the target was
about 16-64 cases per input binade, so `k = e + 11` (lowered for a binade that
otherwise had fewer than 8).  For sin/cos/tan `k = 10` over a window of the
first 2^16 raw inputs of each searched binade.

## Large-argument sin, cos and tan

`-DSINBIG` was built and tried first (as the card asks), but with FPLLL 5.5.0
and this BaCSeL revision every SINBIG run reported `Final size of subintervals
(LOG[2](T)): INF` with 0 successes, including the run printed in BaCSeL's own
README.  The committed sin/cos/tan lists therefore come from the ordinary
`SIN`/`COS`/`TAN` builds with `-DMONOTONIC`, searching a 2^16-raw window at
the start of each covered binade.  They are a sample of the large-argument
region, not an exhaustive per-binade sweep.

## Coverage and cuts

Covered: ln, log2, log10 and rsqrt over input binades with exponents
`-13 .. 20` (binades `-16 .. -14` hold fewer than 8 representable raws at all,
so they cannot meet the 8-case floor; the upper binades `21 .. 46` were cut for
the wall clock).  sin, cos and tan over `16 .. 20`.

## check_hard

    cc -O2 tools/bacsel/check_hard.c -lmpfr -lgmp -o check_hard
    ./check_hard --check ln test/data/hardcases/ln.txt

It recomputes every value with MPFR at 256 bits, checks the distance is within
`2^-k`, checks the stored decimal, and checks that the raw is a Q48.16 raw.
`--convert` turns BaCSeL output into list lines; `--getexp` prints an output
binade exponent.
