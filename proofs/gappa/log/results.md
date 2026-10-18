# Results

Run: `make -C proofs/gappa/log` (Gappa 1.6.0, `gappa kernel.g`, on amd-1).
Exit 0, all goals proved, including the budget goals `log2_raw <= 2^-84.5`,
`log10_raw <= 2^-86.2` and `ln_full_raw <= 2^-85`. Gappa's own output (the
`in ?` goals):

    Results:
      ln_raw in [140206802144100517b-142 {2.51484e-26, 2^(-85.0397)}, 1121654438628701599b-145 {2.51484e-26, 2^(-85.0397)}]
      |E| in [0, 720611540997592279b-186 {7.3472e-39, 2^(-126.678)}]
      log2_raw in [809106513324803039b-144 {3.62816e-26, 2^(-84.5109)}, 202276632204110419b-142 {3.62816e-26, 2^(-84.5109)}]
      log10_raw in [487140674269838701b-145 {1.09221e-26, 2^(-86.2429)}, 243570341798410485b-144 {1.09221e-26, 2^(-86.2429)}]
      ln_full_raw in [140207901655728293b-142 {2.51486e-26, 2^(-85.0397)}, 1121663234721723807b-145 {2.51486e-26, 2^(-85.0397)}]

## Proved bounds

Gappa proves these from hand-derived error formulas (README.md, "What is
derived by hand"); it does not check the formulas against the `fixed<>`
intermediates. `h` is Sollya's certified supnorm, added as a constant.

| quantity | upper bound | source |
| --- | ---: | --- |
| kernel arithmetic error `E` | 7.3472e-39 (2^-126.678) | Gappa, from the residue formula |
| `ln_raw` = (h + \|E\|) * 2^16, kernel only | 2.51484e-26 (2^-85.0397) | Gappa |
| `ln_full_raw`, ln with table and shift terms (+2^-118) | 2.51486e-26 (2^-85.0397) | Gappa |
| `log2_raw` | 3.62816e-26 (2^-84.5109) | Gappa |
| `log10_raw`, ln sum truncations included | 1.09221e-26 (2^-86.2429) | Gappa |
| polynomial-versus-`log1p` supnorm `h` | 3.8373354791488578913e-31 (2^-101.03967) | Sollya `supnorm`, not re-proved |

## Against the design budget

Budgets are from `docs/design/transcendentals.md` 2.1-2.3.

| function | proved bound (raw) | design budget (raw) | verdict |
| --- | ---: | ---: | --- |
| fixLnExact | 2.51484e-26 | 2^-85.04 = 2.51425411001007e-26 | **ABOVE budget** (below 2^-85) |
| fixLog2Exact | 3.62816e-26 | 2^-84.5 = 3.65565637751430e-26 | within budget (Gappa goal) |
| fixLog10Exact | 1.09221e-26 | 2^-86.2 = 1.12516023157136e-26 | within budget (Gappa goal) |

## Finding: the ln budget is 0.023% below the certified kernel supnorm

The ln total is `2.51484e-26` raw against the stated budget
`2.514254e-26` raw, a factor `1.0002315`, i.e. `5.82e-30` raw above it.
The excess is not arithmetic: the Gappa-proved kernel evaluation error is only
`7.35e-39` (`4.8e-34` raw). It is the budget itself. The design writes the
kernel line as `2^-101.04`, but Sollya's certified supnorm interval upper
endpoint is `2^-101.03966522216796875`, and

    2^-101.04 = 3.8364473114e-31 < 3.8373354791e-31 = h.

The design's two-decimal rounding of the exponent makes the budget smaller
than the certified approximation error it is meant to cover, so no proof can
put the ln total at or below `2^-85.04`.

The proved ln total, with its table and shift terms (`ln_full_raw`), is
still below `2^-85 = 2.5849394e-26` raw (a Gappa goal), which is the
condition the design's BaCSeL hard-case argument actually uses (`eps < 2^-k`
for every integer `k <= 85`). So correct rounding is not at risk; the
`2^-85.04` line, and the very fine margin it advertises, is.

This is a finding against the design budget, so the card's DONE-WHEN (`no
larger than its design budget` for every function) is not established for
fixLnExact. No library code was changed.

What would close it (outside this card, which adds only proof files): the
design's ln kernel line and budget restated from the certified supnorm,
`2^-101.039` and `2^-85.039` raw (both rounded up; still below `2^-85`), or a ln budget
written as `< 2^-85`. The proved ln total then meets it with no proof change.
