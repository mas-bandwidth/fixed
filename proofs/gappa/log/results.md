# Results

Run: `make -C proofs/gappa/log` (Gappa 1.6.0, `gappa kernel.g`, on amd-2).
Exit 0, all goals proved, including the budget goals `|E| <= 7.35e-39`,
`ln_full_raw <= 2^-85.039`, `log2_raw <= 2^-84.5`, `log10_raw <= 2^-86.2` and
`ln_full_raw <= 2^-85`. Gappa's own output (the `in ?` goals):

    Results:
      E in [-578750213369621327b-200 {-3.60157e-43, -2^(-140.994)}, 45036013555493097b-182 {7.34684e-39, 2^(-126.678)}]
      K in [-560827208576928439b-160 {-3.83734e-31, -2^(-101.04)}, 560827219313824429b-160 {3.83734e-31, 2^(-101.04)}]
      ln_raw in [0, 560827219313824429b-144 {2.51484e-26, 2^(-85.0397)}]
      log2_raw in [1b-104 {4.93038e-32, 2^(-104)}, 809106528815682281b-144 {3.62816e-26, 2^(-84.5109)}]
      log10_raw in [807521534065368681b-162 {1.38132e-31, 2^(-102.514)}, 974281367192727529b-146 {1.09221e-26, 2^(-86.2429)}]
      ln_full_raw in [1b-102 {1.97215e-31, 2^(-102)}, 560831617360335533b-144 {2.51486e-26, 2^(-85.0397)}]

## Proved bounds

Gappa proves these from the computed chain (the `fixed<>` intermediates, as
the source computes them) and the exact polynomial, with no hints and no
hand-derived error formula. `h` is Sollya's certified supnorm, taken as the
hypothesis `|p - log1p(u)| <= h`.

| quantity | upper bound | source |
| --- | ---: | --- |
| kernel arithmetic error `E = comp - p` | 7.34684e-39 (2^-126.678) | Gappa, from the `fixed<>` chain |
| total kernel error `K = comp - log1p(u)` | 3.83734e-31 (2^-101.0397) | Gappa, `E` plus the Sollya hypothesis |
| `ln_raw` = \|K\| * 2^16, kernel only | 2.51484e-26 (2^-85.0397) | Gappa |
| `ln_full_raw`, ln with table and shift terms (+2^-118) | 2.51486e-26 (2^-85.0397) | Gappa |
| `log2_raw` | 3.62816e-26 (2^-84.5109) | Gappa |
| `log10_raw`, ln sum truncations included | 1.09221e-26 (2^-86.2429) | Gappa |
| polynomial-versus-`log1p` supnorm `h` | 3.8373354791488578913e-31 (2^-101.03967) | Sollya `supnorm`, hypothesis, not re-proved |

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

## Earlier model (attempt 5), replaced

The attempt-5 `kernel.g` stated the error as a hand-derived formula in free
residue variables (`E = rho_q*w_2 + rho_v - u^2*d_2`, each `rho` an
independent interval), so Gappa bounded that formula but never proved it
equal to `comp - p`. This model defines `E` as `comp - p` directly; the
bounds it proves are the same to the printed digits.
