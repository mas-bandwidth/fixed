# asin_acos

Arm: `GNU-native`. Reference: MPFR 256 bits, RN(f(raw * 2^-16) * 2^16) to nearest-even,
error in raw units (2^-16). Saturating and documented out-of-domain results are
computed as exact values.

Command: `fixed_sweep --fn asin_acos --threads 3`

| piece | mode | inputs | max error | not correctly rounded | worst input | hash |
|---|---|---|---|---|---|---|
| [983040,2097171) | exhaustive | 131081 | 0 | 0 | 983040 | e59cf7b89034f047 |
| [-65536,65537) | exhaustive | 131073 | 0 | 0 | -65536 | 1680472a1eaee3fe |

## Worst 32 inputs

| input | result | reference | error |
|---|---|---|---|
| 983040 | 102944 | 102944 | 0 |
| -65536 | -102944 | -102944 | 0 |
| 983041 | 102944 | 102944 | 0 |
| -65535 | -102582 | -102582 | 0 |
| 983042 | 102944 | 102944 | 0 |
| -65534 | -102432 | -102432 | 0 |
| 983043 | 102944 | 102944 | 0 |
| -65533 | -102317 | -102317 | 0 |
| 983044 | 102944 | 102944 | 0 |
| -65532 | -102220 | -102220 | 0 |
| 983045 | 102944 | 102944 | 0 |
| -65531 | -102134 | -102134 | 0 |
| 983046 | 102944 | 102944 | 0 |
| -65530 | -102057 | -102057 | 0 |
| 983047 | 102944 | 102944 | 0 |
| -65529 | -101986 | -101986 | 0 |
| 983048 | 102944 | 102944 | 0 |
| -65528 | -101920 | -101920 | 0 |
| 983049 | 102944 | 102944 | 0 |
| -65527 | -101858 | -101858 | 0 |
| 983050 | 102944 | 102944 | 0 |
| -65526 | -101799 | -101799 | 0 |
| 983051 | 102944 | 102944 | 0 |
| -65525 | -101743 | -101743 | 0 |
| 983052 | 102944 | 102944 | 0 |
| -65524 | -101690 | -101690 | 0 |
| 983053 | 102944 | 102944 | 0 |
| -65523 | -101638 | -101638 | 0 |
| 983054 | 102944 | 102944 | 0 |
| -65522 | -101589 | -101589 | 0 |
| 983055 | 102944 | 102944 | 0 |
| -65521 | -101542 | -101542 | 0 |

The hash column is FNV-1a over every output, little-endian bytes, in input order;
it is thread-count independent. Regenerate with the command above; the nightly
workflow runs every descriptor on Linux with MPFR.
