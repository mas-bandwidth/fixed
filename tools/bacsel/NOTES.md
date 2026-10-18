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

BaCSeL's `SINBIG` build covers only `sin` and requires FPLLL plus a `-Q`
period argument at run time (BaCSeL's README uses `-Q 15106909301`).  `run.sh`
tries that build first and, if it builds and a probe search (`e = 16`) returns
a case, uses it for the `sin` list as a full per-binade sweep over `16 .. 46`.
If the build fails or the probe returns nothing (with FPLLL 5.5.0 and this
BaCSeL revision each SINBIG run reported `Final size of subintervals
(LOG[2](T)): INF` with 0 successes, including the run printed in BaCSeL's
README), `run.sh` falls back to the ordinary `SIN` window scan.  `cos`/`tan`
have no `SINBIG` equivalent and always use the ordinary `COS`/`TAN` builds,
searching a 2^16-raw window at the start of each covered binade.  The committed
sin/cos/tan lists come from that fallback: they are a sample of the
large-argument region, not an exhaustive per-binade sweep.  The `-Q` value must
be re-verified on a machine where the `SINBIG` build actually runs.

## Coverage and cuts

The card asks for a per-binade exhaustive sweep over the ranges below.  Because
a full sweep over the whole range at the requested precision is heavy and the
SINBIG/FPLLL path does not run in the environment used to produce the committed
data, the committed lists are **not exhaustive**: they cover a sample of the
requested binades and were cut for the wall clock.  The sweep is therefore not
exhaustive; the ranges below name every binade that is cut or omitted.

Requested binades (input binade exponent `e`, from `2^e` to `2^(e+1)`):

* `ln`, `log2`, `log10`: every `e` in `-16 .. 46`.
* `sin`, `cos`, `tan`: every `e` in `16 .. 46` (the large-argument region).
* `rsqrt`: every `e` in `-16 .. 46`.

Committed coverage (from each list's `k per binade:` header; every *present*
binade has >= 8 and <= 256 cases, and all rows are sorted by raw):

* `ln`: `-10 .. 20`.  Cut: `-16 .. -11` and `21 .. 46`.
* `log10`: `-10 .. -1` and `1 .. 20`.  Cut: `-16 .. -11`, `21 .. 46`,
  and binade `0` (yields fewer than 8 cases in the search window).
* `log2`: `-10,-9,-7,-6,-5,-3,0 .. 20`.  Cut: `-16 .. -11`, `21 .. 46`,
  and binades `-8,-4,-2,-1` (too few cases in the search window).
* `rsqrt`: `-10,-9,-7,-6,-5,-3,-1` and the odd binades `1,3,5,...,19`.
  Cut: `-16 .. -11`, `21 .. 46`, and the even binades `0,2,4,...,18`
  (too few cases in the search window).
* `sin`/`cos`/`tan`: `16 .. 20` (first 2^16 raws of each binade).  Cut:
  `21 .. 46`; if the `SINBIG` probe succeeds, `sin` instead covers `16 .. 46`.

Every *present* binade has at least 8 and at most 256 cases.  The cut binades
`-16 .. -11` and `21 .. 46` are in scope for a later, longer regeneration run
on a machine where the full toolchain (FPLLL and, for `sin`, a working
`SINBIG` build) is available.

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
