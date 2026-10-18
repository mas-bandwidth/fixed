# exp2_exact

Arm: `GNU-native`. Reference: MPFR 256 bits, RN(f(raw * 2^-16) * 2^16) to nearest-even,
error in raw units (2^-16). Saturating and documented out-of-domain results are
computed as exact values.

Command: `fixed_sweep --fn exp2_exact --threads 3`

| piece | mode | inputs | max error | not correctly rounded | worst input | hash |
|---|---|---|---|---|---|---|
| [-9223372036854775808,9223372036854775807) | exhaustive | 376 | 0 | 0 | 0 | 6558da448ac3bb9b |
| [-1114112,3080192) | exhaustive | 4194304 | 1 | 1 | -1114112 | b6336dfed0370eb4 |

## Worst 32 inputs

| input | result | reference | error |
|---|---|---|---|
| -1114112 | 1 | 0 | 1 |
| 0 | 65536 | 65536 | 0 |
| 1 | 65537 | 65537 | 0 |
| -1114111 | 1 | 1 | 0 |
| -1 | 65535 | 65535 | 0 |
| -1114110 | 1 | 1 | 0 |
| 65535 | 131071 | 131071 | 0 |
| -1114109 | 1 | 1 | 0 |
| 65536 | 131072 | 131072 | 0 |
| -1114108 | 1 | 1 | 0 |
| 131071 | 262141 | 262141 | 0 |
| -1114107 | 1 | 1 | 0 |
| 2147418112 | 9223372036854775807 | 9223372036854775807 | 0 |
| -1114106 | 1 | 1 | 0 |
| -2147418112 | 0 | 0 | 0 |
| -1114105 | 1 | 1 | 0 |
| -9223372036854775808 | 0 | 0 | 0 |
| -1114104 | 1 | 1 | 0 |
| -9223372036854775807 | 0 | 0 | 0 |
| -1114103 | 1 | 1 | 0 |
| 9223372036854775807 | 9223372036854775807 | 9223372036854775807 | 0 |
| -1114102 | 1 | 1 | 0 |
| 2 | 65537 | 65537 | 0 |
| -1114101 | 1 | 1 | 0 |
| -2 | 65535 | 65535 | 0 |
| -1114100 | 1 | 1 | 0 |
| 3 | 65538 | 65538 | 0 |
| -1114099 | 1 | 1 | 0 |
| -3 | 65534 | 65534 | 0 |
| -1114098 | 1 | 1 | 0 |
| 4 | 65539 | 65539 | 0 |
| -1114097 | 1 | 1 | 0 |

The hash column is FNV-1a over every output, little-endian bytes, in input order;
it is thread-count independent. Regenerate with the command above; the nightly
workflow runs every descriptor on Linux with MPFR.
