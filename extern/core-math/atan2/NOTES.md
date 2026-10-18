# CORE-MATH atan2, vendored for the test only

These files are a TEST-ONLY reference for `fixAtan2Exact` and the exact quaternion angles. They
are never linked into the fixed library; CMakeLists.txt builds them into their own object
library, linked only into the `quat_angles_exact` tests.

- URL: https://gitlab.inria.fr/core-math/core-math.git
- Commit: cac142bc65dc140d645ba3f79511a9c591d1746e (the pin of `../NOTES.md`)
- License: MIT (the repository's LICENSE, already copied unchanged to `../LICENSE`)
- Copyright (atan2.c): Copyright (c) 2024-2025 Paul Zimmermann and Alexei Sibidanov
- Copyright (tint.h): Copyright (c) 2023-2024 Paul Zimmermann and Alexei Sibidanov

Source files, copied unchanged from `src/binary64/atan2/atan2.c` and
`src/binary64/atan2/tint.h` at that commit:

    404b0db90e536f8248101544d0fcad9141fd76b2a6cc3d189dd80dbfc865bd91  atan2.c
    41264d54812a8ce2b3f8ca4c8c033094df7cbf2d214da1c7555783328171b921  tint.h

The test compares `cr_atan2` of the raws as doubles (scaled back to raw Q48.16 units by 2^16 and
rounded to nearest-even) against the library.
