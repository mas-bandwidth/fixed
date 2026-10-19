# Results: CBMC rsqrt and mod specifications

CBMC 5.95.1, CaDiCaL as the external SAT solver, `timeout 120`, `make -C
proofs/cbmc/rsqrt_mod -j3 all`. Every run exited 0 and printed `VERIFICATION
SUCCESSFUL`. Times are the Makefile's wall clock, one run each.

## Widths and properties

| property | arm | inputs | domain | run time |
|---|---|---|---|---|
| `rsqrt` | native | `raw` narrowed to `CBMC_WIDTH=4`, `CBMC_SIGNS=0 1` | every signed 64-bit value that is the sign extension of a 4-bit value: `0..15` and `~0..~15` (the latter `x <= 0`) | 96 s / 2 s |
| `rsqrt` | emulated | same | same | 75 s / 3 s |
| `rsqrt_high` | native | `raw = INT64_MAX - u`, `u` narrowed to 4 bits | the 16 largest positive raws, `INT64_MAX-15 .. INT64_MAX` | 30 s |
| `rsqrt_high` | emulated | same | same | 69 s |
| `rsqrt_edges` | native | constants | `raw = 1, 2, 2^50, 2^50+1, INT64_MAX`; the tie `2^50` must give `q = 1` | 0 s |
| `rsqrt_edges` | emulated | constants | same | 0 s |
| `mod`, `mod_edges` | native | see below | see below | 2/3/3/1 s; edges 0 s |
| `mod`, `mod_edges` | emulated | see below | see below | 1/3/3/1 s; edges 0 s |

`rsqrt` (both arms, every sign run) asserts, for positive `raw`:
`q <= 2^24`, `(2q-1)^2 * raw <= 2^50 < (2q+1)^2 * raw`, and `q == 0` when
`raw > 2^50`; and for `x <= 0`: `q == INT64_MAX`. Both arms' runs print:

    ** 0 of N failed (1 iterations)
    VERIFICATION SUCCESSFUL

`rsqrt_high` (both arms) asserts the same inequality for the 16 largest positive
raws, where `fixISqrt128High` takes its restoring path and makes no seed call.

`rsqrt_edges` (both arms) asserts the inequality at `raw = 1`, `2`, `2^50`
(the tie, `q = 1`), `2^50+1` (`q = 0`) and `INT64_MAX`, with the seed pinned to the
exact integer root.

## mod

`mod` narrows both inputs to `CBMC_WIDTH=6`, with `CBMC_SIGNS=0 1 2 3` covering
every sign combination: each input is in `0..63` or `~0..~63`. The union is every
sign extension of a 6-bit value for each input. For `b == 0` the sign-0 runs
include `0`; the exact zero-divisor pairs are also in `mod_edges`. Every run
asserts `fixFmod = a - trunc(a/b)*b`, the floored `fixMod`, and both result signs.

`mod_edges` checks fixed constants in both arms: `(INT64_MIN, -1)`,
`(INT64_MIN, 1)`, `(INT64_MIN, INT64_MIN)`, `(INT64_MAX, -1)`,
`(INT64_MAX, INT64_MIN)`, `(INT64_MAX, 0)`, `(INT64_MIN, 0)`, `(0, 0)`,
`(0, -1)`, `(1, -1)`, `(-1, 1)`, `(INT64_MAX, INT64_MAX)`.

All runs use `--signed-overflow-check` and `--div-by-zero-check`; CBMC reports
`SUCCESS` for each `overflow` and `division-by-zero` property it inserts in the
library and in the independent specifications.

## What is not proved

The full 64-bit domains were not completed: `rsqrt` at `CBMC_WIDTH=6` (sign 0,
native) did not finish in 180 s, and at 8 it did not finish in 240 s. The narrowed
domains above are what the green runs cover, and they are not called full proofs.
`rsqrt` covers only small positive raws (plus the negative sign-extension run) and
the top 16 positive values; the middle of the positive 64-bit range is covered only
at those widths.
