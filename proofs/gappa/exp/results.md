# Results

Run: `make -C proofs/gappa/exp` (Gappa 1.6.0, `gappa kernel.g`). All goals are
proved (exit 0, no unsatisfied properties).

```
gappa kernel.g
exp        kernel |M - T*e^r| <= 2.4725e-31 (2^-101.67); eps <= 1.143247004185311e-12 (2^-39.67 raw), budget 2^-39.67
sinh/cosh  kernel |M - T*e^r| <= 2.4725e-31 (2^-101.67); eps <= 1.7089819252105546e-12 (2^-39.09 raw), budget 2^-39.09
tanh       kernel |M - T*e^r| <= 2.4725e-31 (2^-101.67); eps <= 8.123258540033696e-27 (2^-86.67 raw), budget 2^-86.67
```

## Proved bounds

| quantity | value | source |
| --- | --- | --- |
| kernel evaluation error `|E| = |M - T*P(r)|` | <= 2.4e-38 (~2^-124.97) | proved by Gappa |
| polynomial-versus-function supnorm `|P(r) - e^r|` | 1.236209838752694113105510039539433637113639803289646403008807013447042878301419192191865209492533254646400253656704902499e-31 (~2^-102.674) | hypothesis, Sollya `supnorm` |
| total kernel error `|M - T*e^r|` | <= 2.4725e-31 (~2^-101.67) | proved by Gappa |
| exp pre-rounding error | <= 1.143247004185311e-12 (~2^-39.67 raw) | proved by Gappa |
| sinh/cosh pre-rounding error (`ma >= 1`) | <= 1.7089819252105546e-12 (~2^-39.09 raw) | proved by Gappa |
| sinh/cosh pre-rounding error (`ma = 0`) | <= 1.7089819252105546e-12 (~2^-39.09 raw) | proved by Gappa |
| tanh pre-rounding error | <= 8.123258540033696e-27 (~2^-86.67 raw) | proved by Gappa |

## Against the design budget

`docs/design/transcendentals.md` section 1 gives the exp family these budgets:

| function | proved eps (raw) | design budget (raw) | verdict |
| --- | --- | --- | --- |
| exp (`fixExpExact`) | <= 2^-39.67 | 2^-39.67 | within budget |
| sinh (`fixSinhExact`) | <= 2^-39.09 | 2^-39.09 | within budget |
| cosh (`fixCoshExact`) | <= 2^-39.09 | 2^-39.09 | within budget |
| tanh (`fixTanhExact`) | <= 2^-86.67 | 2^-86.67 | within budget |

The budgets are theorems now, not hopes. The design margins they are compared
against are `2^-32.75` raw (sinh), `2^-20.58` raw (cosh), `2^-20.28` raw (tanh)
and `2^-21.15` raw (exp), all far above the proved errors.

## Scope note

The separate small-`|x|` sinh and tanh kernels of the design (1.3/1.4) are not
implemented at this BASE; `src/exact/tanh.c` and `src/exact/sinh_cosh.c` use the
shared exp kernel (and exact identity cut-offs) instead. This proof bounds the
code that is actually there. No library code was changed on this card.
