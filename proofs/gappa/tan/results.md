# Results

Run: `make -C proofs/gappa/tan` (Gappa 1.4.1, `gappa kernel.g`). All four goals
are proved (exit 0, no unsatisfied properties).

## Proved bounds

| quantity | value | source |
| --- | --- | --- |
| table-path kernel evaluation error `|E| = |t - p(x)|` | <= 7.0e-39 (~2^-126.8) | proved by Gappa |
| polynomial-versus-function supnorm `|p - tan|` | 8.460112648223753998e-44 | hypothesis, Sollya `supnorm` (log2 -143.084) |
| pole-path Horner error `|Er| = |w - r^2 q(r^2)|` | <= 7.0e-39 | proved by Gappa |
| pole-path relative Horner error `|Er|/(1+w)` | <= 7.0e-39 | proved by Gappa |
| polynomial-versus-function relative supnorm `|(s+p)/tan - 1|` | 9.100984361674279281e-41 | hypothesis, Sollya `supnorm` (log2 -133.013) |

## Against the design budget

The design (`docs/design/transcendentals.md` 3.4) gives the tan kernel two budget
lines: `2^-125` absolute on the table path (the kernel line) and `2^-126` on the
pole path ("Horner for w", absolute on `1+w`). Both are theorems now:

| quantity | total proved error | budget | verdict |
| --- | --- | --- | --- |
| table path | 7.0e-39 + 8.460112648223753998e-44 = 7.0008e-39 | 2^-125 = 2.3510e-38 | within budget |
| pole path | 7.0e-39 (and relative 7.0e-39) | 2^-126 = 1.1755e-38 | within budget |

7.0008e-39 < 2.3510e-38 and 7.0e-39 < 1.1755e-38, so the design budgets are
theorems, not hopes.
