# CORE-MATH exp2, vendored for the test only

Reference for test/exp2_exact_test.c. TEST-ONLY; never linked into the fixed library.

- URL: https://gitlab.inria.fr/core-math/core-math.git
- Commit: cac142bc65dc140d645ba3f79511a9c591d1746e
- License: MIT (the repository's LICENSE, copied unchanged to ../LICENSE)
- Copyright (exp2.c): Copyright (c) 2023-2026 Alexei Sibidanov

Source file, copied unchanged from `src/binary64/exp2/exp2.c` at that commit:

    a4e892adf5e30ddde023b74881173e170748431979e999db8e206f221174064d  exp2.c

The test compares `cr_exp2` of the exact binary64 value of the raw Q48.16
argument, scaled back by 2^16 (exact) and rounded to nearest-even by llrint in
the default rounding mode, against the library.
