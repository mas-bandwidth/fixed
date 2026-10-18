# CORE-MATH pow, vendored for the test only

Reference for test/pow_exact_test.c. TEST-ONLY; never linked into the fixed library.

- URL: https://gitlab.inria.fr/core-math/core-math.git
- Commit: cac142bc65dc140d645ba3f79511a9c591d1746e
- Commit date: 2026-10-01 17:55:16 +0200
- License: MIT (the repository's LICENSE, copied unchanged to ../LICENSE)
- Copyright (pow.c, pow.h): Copyright (c) 2022-2025 CERN and Inria (authors Tom Hubrecht and Paul Zimmermann)
- Copyright (dint.h, qint.h): Copyright (c) 2022, 2023 CERN and Inria (authors Tom Hubrecht and Paul Zimmermann)

Source files, copied unchanged from `src/binary64/pow/` at that commit:

    abec16a142154d9de8ea9e14f333c335ecc69f7b3106d66dc573a84e1a15a95c  pow.c
    b7c21d4cf273c09e5d0d347907832de6013d18199bdf67f532132567dd9fcb0c  pow.h
    c62a9788d872c314c743a821602d6819e5c13113b70f6d4a5d0076695dc413ef  dint.h
    32de9c10388056e2294339e3302c3695cdf4abc87ca790ad48917d9794bfb6e4  qint.h

The test compares `cr_pow` of the exact binary64 values of the raw Q48.16 base and
exponent (cleared to 53 significant bits above 2^53 raw), scaled back by 2^16 (exact)
and rounded to nearest-even by llrint in the default rounding mode, saturating at
INT64_MAX, against the library.
