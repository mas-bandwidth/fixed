# Hard-case lists

Each `*.txt` file holds inputs for one function at which Q48.16 round-to-nearest
is hardest: the exact value lies unusually close to a rounding midpoint.

Format: a `#` header naming the BaCSeL commit, the command, the date and `k`
(including `# k per binade:`); then one case per line,

    <raw input> <input binade e> <distance in 2^-16 units>

sorted by `(e, raw)`.  The value of the input is `raw / 2^16`, in `[2^e, 2^(e+1))`.

All seven files cover their complete Q48.16 binade range (`-16 .. 46` for `ln`,
`log2`, `log10`, `rsqrt`; `16 .. 46` for `sin_big`, `cos_big`, `tan_big`) with
at least 8 cases per binade (for `ln`, `log2`, `log10`, `rsqrt`, the binades
`e = -16 .. -9` list every positive raw, since they hold at most 128) and at
most 256 cases per binade.

These are **windowed searches, not exhaustive sweeps**.  Every binade `e >= -8`
is searched with 4 windows of `2^16` consecutive raws starting at the binade's
`0`, `1/4`, `1/2` and `3/4` points, one BaCSeL search per window (the `sin_big`
list uses the BaCSeL `-DSINBIG -Q` build; `cos_big`/`tan_big` use the ordinary
`COS`/`TAN` builds over the same windows).  Where the LLL search is run with
`k <= 3`, BaCSeL's naive full-window search (`-m 1 -d 0 -t 1`) searches all
raws of the same window.  Binades `e = -16 .. -9` of the monotonic functions
are enumerated exhaustively by `check_hard --enumerate`, with no BaCSeL.

The data was produced by BaCSeL (GPL-2), run as a separate tool; it also uses
FPLLL 5.5.0 (LGPL-2.1+).  None of their source or object code is in this
repository and nothing links against them; these files are plain data.
`tools/bacsel/check_hard.c` is our MPFR checker, not BaCSeL code.

Regenerate with `bash tools/bacsel/run.sh` or verify the committed lists with
`bash tools/bacsel/run.sh --check-only`; see `tools/bacsel/NOTES.md` for details.
