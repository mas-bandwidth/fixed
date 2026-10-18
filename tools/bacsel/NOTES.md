# BaCSeL hard-case lists

BaCSeL is GPL-2.  It is used here only as a separate tool: this directory
contains no BaCSeL source, no BaCSeL object code and nothing links against it.
The lists in `test/data/hardcases/` are output data.

Pinned BaCSeL commits (https://gitlab.inria.fr/zimmerma/bacsel.git, GPL-2,
built against system GMP/MPFR and FPLLL 5.5.0):

* `e372414bc6af4a2911e07abe03de9854f32561fd` for `ln` (`-DLOG`), `log2`
  (`-DLOG2`), `log10` (`-DLOG10`), `rsqrt` (`-DRSQRT`), `cos_big` (`-DCOS`),
  and `tan_big` (`-DTAN`).
* `e17f67c` (the parent of `e4bda4c`) for `sin_big` (`-DSINBIG`), the newest
  revision at which `-DSINBIG` functions properly.

## Mapping from Q48.16 to BaCSeL arguments

A Q48.16 raw `r` represents the value `v = r / 2^16`.  For an input binade
`[2^e, 2^(e+1))`, `r` lies in `[2^(e+16), 2^(e+17))` and has `e + 17`
significant bits.  BaCSeL is invoked with:

    -n e+17  -e_in e+1  -nn eo+16

so that its input `x * 2^(e_in - n)` is `r / 2^16` and its output scale
`2^(nn - eo)` is `2^16` (distances to a rounding midpoint in units of `2^-16`).

For the monotonic functions (`ln`, `log2`, `log10`, `rsqrt`) over `e = -16 .. 46`:

* `check_hard --window <fn> <e> 12` locates a `2^12`-wide window `[t0, t1)`
  inside `[2^(e+16), 2^(e+17))` with constant MPFR output exponent `eo`,
  centered on a half-integer crossing of `2^16 * f(r * 2^-16)` via 256-bit MPFR
  bisection (avoiding power-of-2 exponent boundaries at `t0 = 2^(e+16)`).
* For `e <= -4` (where the binade has `<= 4096` inputs), BaCSeL runs with
  `-d 0 -t 1`.  For `e >= -3`, BaCSeL runs its LLL search (`-d 2 -alpha 2 -t 12`)
  over `[t0, t1)`.

For large-argument trigonometric functions (`e = 16 .. 46`):

* `sin_big`: uses BaCSeL `e17f67c` built with `-DSINBIG` and period step
  `-Q 2470649 -nbr 32 -t 4 -d 1 -alpha 1`, where `2470649 / 2^16` is a
  continued-fraction convergent to `6 * 2 * pi` (`tau = (Q * 2^-16) mod pi = 9.54e-8`).
* `cos_big` and `tan_big`: use BaCSeL `e372414` (`-DCOS`, `-DTAN`) with
  `-t 7 -d 2 -alpha 2` over `[2^(e+16), 2^(e+16) + 2^16)` for every binade
  `e = 16 .. 46`.

`k` is recorded in each list header (`# k per binade:`).  Each binade starts at
`k = min(16, max(1, e + 11))` for the monotonic functions (`k = 10` for the
trig lists), lowers `k` only if needed to collect at least `min(8, 2^(e+16))`
cases, and truncates to at most 256 closest cases per binade.

## Coverage

All seven lists cover their complete required Q48.16 binade ranges and pass
`check_hard`:

* `ln.txt`: 7571 cases across all 63 binades `-16 .. 46`
* `log2.txt`: 7277 cases across all 63 binades `-16 .. 46`
* `log10.txt`: 7808 cases across all 63 binades `-16 .. 46`
* `rsqrt.txt`: 9228 cases across all 63 binades `-16 .. 46`
* `sin_big.txt`: 3027 cases across all 31 binades `16 .. 46`
* `cos_big.txt`: 6746 cases across all 31 binades `16 .. 46`
* `tan_big.txt`: 7936 cases across all 31 binades `16 .. 46`

## Verification (`check_hard` and `run.sh --check-only`)

    gcc -O2 -Wall -Wextra -Werror tools/bacsel/check_hard.c -o check_hard -lmpfr -lgmp
    ./check_hard ln    test/data/hardcases/ln.txt
    ./check_hard log2  test/data/hardcases/log2.txt
    ./check_hard log10 test/data/hardcases/log10.txt
    ./check_hard rsqrt test/data/hardcases/rsqrt.txt
    ./check_hard sin   test/data/hardcases/sin_big.txt
    ./check_hard cos   test/data/hardcases/cos_big.txt
    ./check_hard tan   test/data/hardcases/tan_big.txt

Or in a single command:

    bash tools/bacsel/run.sh --check-only

`check_hard` recomputes every row with MPFR at 256 bits, checks that `raw` lies
in `[2^(e+16), 2^(e+17))`, verifies that `|2^16 * f(raw * 2^-16) - (N + 0.5)| <= 2^-k`
and agrees with the recorded distance, and verifies that every required binade
is present and holds at least `min(8, 2^(e+16))` cases.
