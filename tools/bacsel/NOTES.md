# BaCSeL hard-case lists

BaCSeL is GPL-2.  It is used here only as a separate tool: this directory
contains no BaCSeL source, no BaCSeL object code and nothing links against it.
The lists in `test/data/hardcases/` are output data.

Pinned BaCSeL commit: `e17f67c` (the parent of `e4bda4c`, "added new bug").
This is the newest revision at which the `-DSINBIG` large-argument build
still reproduces BaCSeL's own README example (13 cases).  `e4bda4c` and
every later revision, including `e372414`, silently return no SINBIG cases
("Final size of subintervals (log[2](T)): INF"), so they cannot produce the
large-argument sin data.  https://gitlab.inria.fr/zimmerma/bacsel.git,
GPL-2.  Built against the system GMP/MPFR and FPLLL 5.5.0.

## Mapping from Q48.16 to BaCSeL arguments

A Q48.16 raw `r` is the value `v = r / 2^16`.  For an input binade
`[2^e, 2^(e+1))`, `r` has `e + 17` significant bits.  BaCSeL is run with

    -n e+17  -e_in e+1  -t0 2^(e+16)  -t1 2^(e+17)

so that its input `x * 2^(e_in - n)` is exactly `r / 2^16`.  The output is
round-to-nearest to 2^-16, so the output binade exponent `eo` gets
`-nn eo+16`; then BaCSeL's scale `2^(nn-eo)` is exactly `2^16` and its printed
distance is the distance to a rounding midpoint in units of 2^-16.

`k` is stated in each list header (or its `k per binade:` map).  The searches
ask for distance <= 2^-k; `k = 10` is lowered only for a binade that yields
fewer than 8 cases.  A full-binade run is exhaustive: `t0 .. t1-1` covers
every raw in the binade, unlike the earlier 2^16-raw window samples.

## Large-argument sin, cos and tan

BaCSeL's `SINBIG` build covers only `sin`; it requires FPLLL and a `-Q`
period argument at run time.  `run.sh` now pins the working sha (`e17f67c`),
probes it with the README example, and uses it for the `sin` sweep over every
input binade `16 .. 46`.  `cos` and `tan` have no `SINBIG` equivalent in
BaCSeL 4.0, so they can only use the ordinary `COS`/`TAN` build.

## Coverage status (attempt 5)

The card asks for a per-binade exhaustive sweep over every binade from
`2^-16` to `2^46` (sin/cos/tan from `2^16`).  That has **not** been achieved:

* The committed `ln`, `log2`, `log10`, `rsqrt` rows still cover only the
  ranges recorded in their headers (currently `-10 .. 20`, with the gaps
  named there); they are not exhaustive over `-16 .. 46`.
* The committed `sin`, `cos`, `tan` rows are still the earlier samples of
  the first 2^16 raws of binades `16 .. 20`; they are not from `SINBIG`
  and do not reach `21 .. 46`.

Measured on this member (3 threads, nice 19, FPLLL 5.5.0 built from source
because the system has no FPLLL development headers):

* `ln`, full binade with BaCSeL LLL (`d=2`, `t=12`): `e=8` takes ~8 s with
  the exhaustive `d=0` path (16.7M inputs); `e=12` is still running after
  25 s (268M inputs); `e=16` (4.3G inputs) is out of reach.
* `ln`, LLL path at `e=46`, `t=12`: 2^(46+5) subintervals, not finishable.
  Raising `t` to 30/51 makes BaCSeL reject the run ("Initial value of -t is
  too large") or run for minutes without completing.
* `-DSINBIG` at `e=46` with `t=12`: 2^51 arithmetic-progression steps, not
  finishable.

The remaining work is a machine/time budget large enough to run `e=10 .. 46`
for the monotonic functions and the `SINBIG`/`COS`/`TAN` sweeps for
`e=16 .. 46`, then regenerating the lists with `run.sh`.

## check_hard

    cc -O2 tools/bacsel/check_hard.c -lmpfr -lgmp -o check_hard
    ./check_hard --check ln test/data/hardcases/ln.txt

It recomputes every value with MPFR at 256 bits, checks that the raw is a
Q48.16 raw, that the raw lies in the binade `e` the row declares, that the
distance to the nearest rounding midpoint is within `2^-k`, and that the stored
decimal agrees with the recomputed distance to a relative tolerance.
`--convert` turns BaCSeL output into list lines; `--getexp` prints an output
binade exponent.

Regenerating the lists needs a build machine with GMP/MPFR and FPLLL (BaCSeL's
dependency).  `run.sh` pins the BaCSeL commit and builds it in `$BAC_DIR`
(default `.bacsel/`); the tool is never committed.
