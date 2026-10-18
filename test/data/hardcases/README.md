# Hard-case lists

Each `*.txt` file holds inputs for one function at which Q48.16 round-to-nearest
is hardest: the exact value lies unusually close to a rounding midpoint.

Format: a `#` header naming the BaCSeL commit, the command, the date and `k`
(including `# k per binade:`); then one case per line,

    <raw input> <input binade e> <distance in 2^-16 units>

sorted by `(e, raw)`.  The value of the input is `raw / 2^16`, in `[2^e, 2^(e+1))`.

All seven files cover their complete Q48.16 binade range (`-16 .. 46` for `ln`,
`log2`, `log10`, `rsqrt`; `16 .. 46` for `sin_big`, `cos_big`, `tan_big`) with
at least `min(8, 2^(e+16))` cases per binade and at most 256 cases per binade.

The data was produced by BaCSeL (GPL-2), run as a separate tool.  None of its
source or object code is in this repository and nothing links against it; these
files are plain data.  `tools/bacsel/check_hard.c` is our MPFR checker, not
BaCSeL code.

Regenerate with `bash tools/bacsel/run.sh` or verify the committed lists with
`bash tools/bacsel/run.sh --check-only`; see `tools/bacsel/NOTES.md` for details.
