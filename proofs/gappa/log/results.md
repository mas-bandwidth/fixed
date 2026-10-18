# Results

Run: `make -C proofs/gappa/log` (Gappa 1.6.0, `gappa kernel.g`). Exit 0, all
goals proved.

## Proved bounds

| quantity | bound | source |
| --- | ---: | --- |
| kernel arithmetic error `|comp - p|` | <= 7.35e-39 (Gappa: 7.347e-39, 2^-126.68) | proved by Gappa |
| `fixLnExact` total kernel error, raw | <= 2.5149e-26 (Gappa: 2.514836e-26, 2^-85.0397) | proved by Gappa |
| `fixLog2Exact` total kernel error, raw | <= 3.6282e-26 (Gappa: 3.628159e-26) | proved by Gappa |
| `fixLog10Exact` total kernel error, raw | <= 1.0922e-26 (Gappa: 1.092199e-26) | proved by Gappa |
| polynomial-versus-`log1p` supnorm `h` | 3.8373354791488578913029786867e-31 (2^-101.03967) | hypothesis, Sollya `supnorm` |

"Total kernel error" is the proved arithmetic bound `|E|` plus the Sollya
supnorm hypothesis `h`, scaled to raw units as in the design.

## Against the design budget

Budgets are from `docs/design/transcendentals.md` 2.1-2.3.

| function | total proved bound (raw) | design budget (raw) | verdict |
| --- | ---: | ---: | --- |
| fixLnExact | <= 2.514836e-26 | 2^-85.04 = 2.51425411001007e-26 | **ABOVE budget** |
| fixLog2Exact | <= 3.628159e-26 | 2^-84.5 = 3.65565637751430e-26 | within budget |
| fixLog10Exact | <= 1.092199e-26 | 2^-86.2 = 1.12516023157136e-26 | within budget |

## Finding: the ln budget is 0.023% below the certified kernel supnorm

The ln total is `2.514836e-26` raw against the stated budget
`2.514254e-26` raw, a factor `1.0002315`, i.e. `5.82e-30` raw above it.
The excess is not arithmetic: the Gappa-proved kernel evaluation error is only
`7.35e-39` (`4.8e-34` raw). It is the budget itself. The design writes the
kernel line as `2^-101.04`, but Sollya's certified supnorm interval upper
endpoint is `2^-101.03966522216796875`, and

    2^-101.04 = 3.8364473114e-31 < 3.8373354791e-31 = h.

The design's two-decimal rounding of the exponent makes the budget smaller
than the certified approximation error it is meant to cover, so no proof can
put the ln total at or below `2^-85.04`.

The proved ln total is still below `2^-85 = 2.5849394e-26` raw, which is the
condition the design's BaCSeL hard-case argument actually uses (`eps < 2^-k`
for every integer `k <= 85`). So correct rounding is not at risk; the
`2^-85.04` line, and the very fine margin it advertises, is.

This is a finding against the design budget, so the card's DONE-WHEN (`no
larger than its design budget` for every function) is not established for
fixLnExact. No library code was changed.
