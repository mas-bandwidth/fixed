# src/exact

One function family per `src/exact/<name>.c` and `include/fixed/fixed_<name>.h`; integer only, never an edit to an existing function.
Add a family by adding those two files plus `test/exact/<name>_test.c` (and optionally `<name>_negative_control.c`); nothing else is edited.
CMake globs `src/exact/*.c` and `test/exact/*_test.c`, so `fixed`, `fixed_emulated` and CTest pick the files up on the next configure.
A family joins the MPFR sweep by adding one `sweep/fns/<name>.c` descriptor.
Tests use no clock, socket or service; see `test/exact/exp2_exact_points_test.c` for the points-header shape.
