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

# THE EMULATED ARM OF THE WHOLE SUITE. Same sources, same frozen hashes, 128-bit
# arithmetic done in software. If the emulation and the compiler's __int128 ever disagree
# by one bit, these go red on every platform at once instead of only on Windows.
if (TARGET fixed_emulated)
    foreach(suite determinism transcendental wide_time exhaustive aabb port_equality int128)
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
