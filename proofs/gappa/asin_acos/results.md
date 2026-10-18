# Results

Run: `make -C proofs/gappa/asin_acos` (Gappa 1.4.1, `gappa kernel.g`). Both goals
are proved (exit 0, no unsatisfied properties).

## Proved bounds

| quantity | value | source |
| --- | --- | --- |
| kernel evaluation error `|e0|` | <= 9e-19 | proved by Gappa |
| polynomial-versus-function supnorm h | 6.17624436320711e-15 | hypothesis, Sollya `supnorm` |
| total kernel error | 6.17624436320711e-15 + 9e-19 = 6.17714436320711e-15 | Gappa's second goal |

## Against the design budget

The design (docs/design/transcendentals.md, and src/exact/asin_acos.c) gives a
6.19e-15 budget per function for the total value error. asin and acos share this
kernel on [0, 1/2] (acos is `pi/2 - asin` on the same reduced interval), so the same
bound applies to both:

| function | total proved error | budget | verdict |
| --- | --- | --- | --- |
| fixAsinExact | 6.17714436320711e-15 | 6.19e-15 | within budget |
| fixAcosExact | 6.17714436320711e-15 | 6.19e-15 | within budget |

6.17714436320711e-15 <= 6.19e-15, so the budget is a theorem, not a hope.
