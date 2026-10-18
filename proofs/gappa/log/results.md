# Results

Run: `make -C proofs/gappa/log` (Gappa 1.6.0, `gappa kernel.g`, on amd-2).
Exit 0, all goals proved, including the budget goals `ln_full_raw <= 2^-85.039`,
`log2_raw <= 2^-84.5`, `log10_raw <= 2^-86.2` and `ln_full_raw <= 2^-85`. Gappa's own output (the
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
| fixLnExact | 2.51486e-26 (with table and shift terms) | 2^-85.039 = 2.51599746228771e-26 | within budget (Gappa goal; also below 2^-85) |
| fixLog2Exact | 3.62816e-26 | 2^-84.5 = 3.65565637751430e-26 | within budget (Gappa goal) |
| fixLog10Exact | 1.09221e-26 | 2^-86.2 = 1.12516023157136e-26 | within budget (Gappa goal) |

## Finding (resolved): the ln budget was rounded below the certified supnorm

The design first wrote the ln kernel line as `2^-101.04`, but Sollya's
certified supnorm interval upper endpoint is `2^-101.03966522216796875`, and

    2^-101.04 = 3.8364473114e-31 < 3.8373354791e-31 = h,

so the old ln budget `2^-85.04` raw was 0.023% below the certified
approximation error it must cover, and no proof could meet it. The kernel
evaluation error itself is only `7.35e-39` (`4.8e-34` raw).

The design now rounds the endpoint up: kernel line `2^-101.039`, ln budget
`2^-85.039` raw (coordinator decision, 2026-10-18). The proved ln total, with
its table and shift terms (`ln_full_raw`, 2^-85.0397), meets it as a Gappa
goal, and is also proved below `2^-85 = 2.5849394e-26` raw, the condition
the design's BaCSeL hard-case argument uses (`eps < 2^-k` for every integer
`k <= 85`). No library code was changed.
