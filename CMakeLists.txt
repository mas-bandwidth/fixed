cmake_minimum_required(VERSION 3.16)
project(fixed C)

# C11, and it is load-bearing on MSVC rather than a preference: the vector headers
# return compound literals (FIX_LITERAL), which MSVC's C compiler only accepts under
# /std:c11 or later. The plain-MSVC arm of the CI matrix is the one that needs it.
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if (MSVC)
    add_compile_options(/std:c11)
endif()

# Determinism: forbid fused multiply-add. Core ops are pure integer; this keeps
# the few float conversion helpers from fusing across platforms.
if (NOT MSVC)
    add_compile_options(-ffp-contract=off)
endif()

add_library(fixed STATIC src/fixed_math.c src/fixed_vec.c)
target_include_directories(fixed PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# THE SAME LIBRARY, FORCED ONTO THE EMULATED 128-BIT PATH.
#
# Plain MSVC has no __int128, so fixed_int128.h emulates the pair. That arm needs to be
# exercised somewhere other than one CI runner: this target compiles the whole library
# with FIX_FORCE_EMULATED_INT128, and every test below is built a second time against it
# and asserts THE SAME FROZEN HASHES. So the emulated arithmetic is held to bit-identity
# with native by the entire test suite, on every operating system in the matrix, rather
# than by one dedicated test on one platform.
#
# ON A COMPILER WITHOUT __int128 THE DUPLICATE IS SKIPPED, because there the plain library
# IS the emulated one: FIX_FORCE_EMULATED_INT128 changes nothing, so the second set of
# targets would compile and run the same code twice and prove nothing. Not a time
# optimization dressed up as a principle, though it is also that -- building the suite
# twice under MSBuild was most of the plain-MSVC arm's CI time.
include(CheckCSourceCompiles)
check_c_source_compiles("int main(void){ __int128 x = 1; return (int)x; }" FIX_COMPILER_HAS_INT128)

if (FIX_COMPILER_HAS_INT128)
    add_library(fixed_emulated STATIC src/fixed_math.c src/fixed_vec.c)
    target_include_directories(fixed_emulated PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_definitions(fixed_emulated PUBLIC FIX_FORCE_EMULATED_INT128)
endif()

# gcc keeps a libm sqrt() call as the errno fallback for __builtin_sqrt (used
# by the fixSqrt seed), so consumers need -lm on Linux. PUBLIC propagates it
# to every test; harmless on macOS.
if (NOT WIN32)
    target_link_libraries(fixed PUBLIC m)
    if (TARGET fixed_emulated)
        target_link_libraries(fixed_emulated PUBLIC m)
    endif()
endif()

enable_testing()

add_executable(fixed_determinism_test test/determinism_test.c)
target_link_libraries(fixed_determinism_test PRIVATE fixed)
add_test(NAME determinism COMMAND fixed_determinism_test)

add_executable(fixed_transcendental_test test/transcendental_test.c)
target_link_libraries(fixed_transcendental_test PRIVATE fixed)
if (NOT WIN32)
    target_link_libraries(fixed_transcendental_test PRIVATE m)
endif()
add_test(NAME transcendental COMMAND fixed_transcendental_test)

add_executable(fixed_wide_time_test test/wide_time_test.c)
target_link_libraries(fixed_wide_time_test PRIVATE fixed)
add_test(NAME wide_time COMMAND fixed_wide_time_test)

add_executable(fixed_exhaustive_test test/exhaustive_test.c)
target_link_libraries(fixed_exhaustive_test PRIVATE fixed)
add_test(NAME exhaustive COMMAND fixed_exhaustive_test)

add_executable(fixed_aabb_test test/aabb_test.c)
target_link_libraries(fixed_aabb_test PRIVATE fixed)
add_test(NAME aabb COMMAND fixed_aabb_test)

# A consumer that overrides only FIX_API. Guards the per-macro guarding in base.h --
# nothing else in this build overrides anything, so that whole path is dark code from in
# here and a regression would only surface in someone else's repo.
add_executable(fixed_override_api_test test/override_api_test.c)
target_link_libraries(fixed_override_api_test PRIVATE fixed)
add_test(NAME override_api COMMAND fixed_override_api_test)

# Bit-equality against the fixed3d implementations these functions were ported from,
# for the whole ported set. Carries its own always-on negative controls (each sweep is
# also run against a reference perturbed by one quantum and must detect it), plus a
# frozen cross-platform hash over its structured sweeps.
add_executable(fixed_port_equality_test test/port_equality_test.c)
target_link_libraries(fixed_port_equality_test PRIVATE fixed)
add_test(NAME port_equality COMMAND fixed_port_equality_test)

# Negative control: the SAME exhaustive suite with a deliberate 1-ulp error
# injected into multiply. This build MUST fail (WILL_FAIL) — a test of the
# tests, proving the suite actually detects breakage rather than silently
# passing. If this test ever "passes", the suite has gone blind.
add_executable(fixed_negative_control test/exhaustive_test.c)
target_link_libraries(fixed_negative_control PRIVATE fixed)
target_compile_definitions(fixed_negative_control PRIVATE FIXED_NEGATIVE_CONTROL)
add_test(NAME negative_control COMMAND fixed_negative_control)
set_tests_properties(negative_control PROPERTIES WILL_FAIL TRUE)

# The same control for the port suite, injecting the error on the LIBRARY side rather
# than the reference side. The in-test controls prove the sweeps can see a difference;
# this proves they catch a regression in the shipped code. MUST fail.
add_executable(fixed_port_negative_control test/port_equality_test.c)
target_link_libraries(fixed_port_negative_control PRIVATE fixed)
target_compile_definitions(fixed_port_negative_control PRIVATE PORT_NEGATIVE_CONTROL)
add_test(NAME port_negative_control COMMAND fixed_port_negative_control)
set_tests_properties(port_negative_control PROPERTIES WILL_FAIL TRUE)

# The emulated 128-bit pair against the compiler's native __int128, operation by
# operation, plus a frozen hash that pins the same values on the compiler that has no
# native to compare against. Built against the plain library: this test names the fixEmu*
# types directly, which exist on every build regardless of which arm fixInt128 selects.
add_executable(fixed_int128_test test/int128_test.c)
target_link_libraries(fixed_int128_test PRIVATE fixed)
add_test(NAME int128 COMMAND fixed_int128_test)

# The geometric layer -- vectors, quaternions, matrices, transforms, world positions and
# the validity predicates. Most of that surface had no test at all before this target
# existed: it compiled, so CI was green, which says nothing about a header-only function
# nobody calls. Exact identities and hand values where fixed point gives them, measured
# tolerances where it rounds, and a frozen cross-platform hash over the whole sweep.
add_executable(fixed_geometry_test test/geometry_test.c)
target_link_libraries(fixed_geometry_test PRIVATE fixed)
add_test(NAME geometry COMMAND fixed_geometry_test)

# Its control: a one-ulp error injected into fixMul, which nearly everything in that layer
# runs through. A suite carrying this many tolerances is exactly the kind that can go blind
# without saying so. MUST fail.
add_executable(fixed_geometry_negative_control test/geometry_test.c)
target_link_libraries(fixed_geometry_negative_control PRIVATE fixed)
target_compile_definitions(fixed_geometry_negative_control PRIVATE GEOMETRY_NEGATIVE_CONTROL)
add_test(NAME geometry_negative_control COMMAND fixed_geometry_negative_control)
set_tests_properties(geometry_negative_control PROPERTIES WILL_FAIL TRUE)

# Bit-equality against the delta bodies this family was ported from, since delta is about
# to depend on this library and delete its own copies. Carries its own always-on negative
# controls, a frozen cross-platform hash, and the check that the general family really is
# the general form of the pinned Q48.16 and Q2.30 converters.
add_executable(fixed_quantize_test test/quantize_test.c)
target_link_libraries(fixed_quantize_test PRIVATE fixed)
add_test(NAME quantize COMMAND fixed_quantize_test)

# The control for it, injecting the error on the LIBRARY side. MUST fail.
add_executable(fixed_quantize_negative_control test/quantize_test.c)
target_link_libraries(fixed_quantize_negative_control PRIVATE fixed)
target_compile_definitions(fixed_quantize_negative_control PRIVATE QUANTIZE_NEGATIVE_CONTROL)
add_test(NAME quantize_negative_control COMMAND fixed_quantize_negative_control)
set_tests_properties(quantize_negative_control PROPERTIES WILL_FAIL TRUE)

# THE SCALE ENVELOPE of the 3x3 inverse and solve. Every other suite here asks whether a
# function is right; this one asks over what range of inputs it is right, because the
# inverse is the one operation whose correctness is a function of magnitude. Its oracle is
# the exact integer 2^32/d, which shares no arithmetic with the implementation.
add_executable(fixed_inverse_envelope_test test/inverse_envelope_test.c)
target_link_libraries(fixed_inverse_envelope_test PRIVATE fixed)
add_test(NAME inverse_envelope COMMAND fixed_inverse_envelope_test)

# Its control is the historical bug rather than an invented perturbation: the wide arm as
# it was, reducing cofactors by 16 bits and accumulating the determinant through an int64
# cast. So this proves the suite catches the specific thing it was written for. MUST fail.
add_executable(fixed_inverse_envelope_negative_control test/inverse_envelope_test.c)
target_link_libraries(fixed_inverse_envelope_negative_control PRIVATE fixed)
target_compile_definitions(fixed_inverse_envelope_negative_control PRIVATE INVERSE_NEGATIVE_CONTROL)
add_test(NAME inverse_envelope_negative_control COMMAND fixed_inverse_envelope_negative_control)
set_tests_properties(inverse_envelope_negative_control PROPERTIES WILL_FAIL TRUE)

# USAGE.md, asserted. Every number, rounding rule and boundary value the guide states that
# can be written as an expression is written as one, so the document cannot drift away from
# the code without CI saying so.
add_executable(fixed_usage_test test/usage_test.c)
target_link_libraries(fixed_usage_test PRIVATE fixed)
add_test(NAME usage COMMAND fixed_usage_test)

# THE EMULATED ARM OF THE WHOLE SUITE. Same sources, same frozen hashes, 128-bit
# arithmetic done in software. If the emulation and the compiler's __int128 ever disagree
# by one bit, these go red on every platform at once instead of only on Windows.
if (TARGET fixed_emulated)
    # quantize is deliberately absent from this list: fixNarrow, fixWiden and the
    # arbitrary-scale family are pure 64-bit arithmetic with no 128-bit intermediate, so an
    # emulated build of that suite would compile and run identical code.
    # inverse_envelope is in this list for a specific reason: the 256-bit layer the wide
    # arm runs on is written entirely in the fixUInt128 vocabulary, so an emulated build
    # is the only thing that checks the emulated 256-bit arithmetic at all.
    foreach(suite determinism transcendental wide_time exhaustive aabb port_equality int128 geometry inverse_envelope)
        add_executable(fixed_${suite}_emulated_test test/${suite}_test.c)
        target_link_libraries(fixed_${suite}_emulated_test PRIVATE fixed_emulated)
        if (NOT WIN32)
            target_link_libraries(fixed_${suite}_emulated_test PRIVATE m)
        endif()
        add_test(NAME ${suite}_emulated COMMAND fixed_${suite}_emulated_test)
    endforeach()

    # And the control for the emulated arm: the exhaustive suite with the same deliberate
    # 1-ulp error, compiled against the emulated library. Without this, an emulated build
    # whose sweeps had gone blind would pass exactly as it does now. MUST fail.
    add_executable(fixed_emulated_negative_control test/exhaustive_test.c)
    target_link_libraries(fixed_emulated_negative_control PRIVATE fixed_emulated)
    target_compile_definitions(fixed_emulated_negative_control PRIVATE FIXED_NEGATIVE_CONTROL)
    add_test(NAME emulated_negative_control COMMAND fixed_emulated_negative_control)
    set_tests_properties(emulated_negative_control PROPERTIES WILL_FAIL TRUE)
endif()
