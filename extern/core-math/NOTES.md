# CORE-MATH, vendored for the test only

These files are a TEST-ONLY reference. They are never linked into the fixed library.

- URL: https://gitlab.inria.fr/core-math/core-math.git
- Commit: cac142bc65dc140d645ba3f79511a9c591d1746e
- Commit date: 2026-10-01 17:55:16 +0200
- License: MIT (the repository's LICENSE, copied unchanged to LICENSE here)
- Copyright (sin.c): Copyright (c) 2022-2026 Paul Zimmermann and Tom Hubrecht and Alexei Sibidanov
- Copyright (cos.c): Copyright (c) 2022-2025 Paul Zimmermann and Tom Hubrecht

Source files, copied unchanged from `src/binary64/sin/sin.c` and
`src/binary64/cos/cos.c` at that commit:

    a3278038675d4f69567445deac9a0f790a64b614a3621db1ffdadd9421592884  sin.c
    04bffbc8b483c886f78f2c9ab3f2a1ca318dda78ab637a2983a4a9d82374e9d9  cos.c
    81830875362f4d477637de58eecd6d458db108cc90c40904fca3694807b86de3  LICENSE

The test compares `cr_sin` and `cr_cos` of the exact binary64 value of the raw
angle against the library, scaled back to raw Q48.16 units.
