# Hard-case lists

Each `*.txt` file holds inputs for one function at which Q48.16 round-to-nearest
is hardest: the exact value lies unusually close to a rounding midpoint.

Format: a `#` header naming the BaCSeL commit, the command, the date and `k`;
then one case per line,

    <raw input> <input binade e> <distance in 2^-16 units>

sorted by raw.  The value of the input is `raw / 2^16`, in `[2^e, 2^(e+1))`.

The data was produced by BaCSeL (GPL-2), run as a separate tool.  None of its
source or object code is in this repository and nothing links against it; these
files are plain data.  `tools/bacsel/check_hard.c` is our MPFR checker, not
BaCSeL code.

Regenerate with `tools/bacsel/run.sh`; see `tools/bacsel/NOTES.md` for the
mapping, the pinned commit and the coverage that was cut.
