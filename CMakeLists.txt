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

# THE 256-BIT DIVISION, against the shift-subtract implementation it replaced. That old
# implementation is kept inside the test as the oracle: known-correct, already shipped, and
# sharing no code with the limb algorithm that replaced it.
add_executable(fixed_int256_division_test test/int256_division_test.c)
target_link_libraries(fixed_int256_division_test PRIVATE fixed)
add_test(NAME int256_division COMMAND fixed_int256_division_test)

# Its control removes the ADD-BACK, deliberately rather than perturbing something generic:
# that branch fires for about one input in 2^63, and the first version of the test reached
# it zero times while looking thorough. MUST fail.
add_executable(fixed_int256_division_negative_control test/int256_division_test.c)
target_link_libraries(fixed_int256_division_negative_control PRIVATE fixed)
target_compile_definitions(fixed_int256_division_negative_control PRIVATE FIX_INT256_NO_ADDBACK)
add_test(NAME int256_division_negative_control COMMAND fixed_int256_division_negative_control)
set_tests_properties(int256_division_negative_control PROPERTIES WILL_FAIL TRUE)

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

add_executable(fixed_numerical_regression_test test/numerical_regression_test.c)
target_link_libraries(fixed_numerical_regression_test PRIVATE fixed)
add_test(NAME numerical_regression COMMAND fixed_numerical_regression_test)

# Validators and bounds must also be safe with the optional saturation policy.
add_executable(fixed_numerical_saturated_test test/numerical_regression_test.c src/fixed_math.c src/fixed_vec.c)
target_include_directories(fixed_numerical_saturated_test PRIVATE include)
target_compile_definitions(fixed_numerical_saturated_test PRIVATE FIX_SATURATE)
if (NOT WIN32)
    target_link_libraries(fixed_numerical_saturated_test PRIVATE m)
endif()
add_test(NAME numerical_saturated COMMAND fixed_numerical_saturated_test)

# THE BOUNDARY SUITE: every scalar entry point over INT64_MIN, FIX_MIN, -1, 0,
# 1 and FIX_MAX in every argument position. It is built for both 128-bit arms by
# the foreach below.
add_executable(fixed_boundary_test test/boundary_test.c)
target_link_libraries(fixed_boundary_test PRIVATE fixed)
add_test(NAME boundary COMMAND fixed_boundary_test)

# Its planted removal, the card's negative control. At configure time the headers
# and the two library sources are copied, every INT64_MIN guard this card added is
# deleted from the copies, and the suite is built against the unguarded copy. The
# deleted text is replaced exactly; a replacement that changes nothing is a
# configure error, so the control cannot go vacuous. The suite MUST fail (WILL_FAIL)
# -- it traps with SIGFPE through fixAbs and mishandles the other edges.
set(FIX_UNGUARDED_DIR "${CMAKE_BINARY_DIR}/unguarded")
file(REMOVE_RECURSE "${FIX_UNGUARDED_DIR}")
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/include" DESTINATION "${FIX_UNGUARDED_DIR}")
file(MAKE_DIRECTORY "${FIX_UNGUARDED_DIR}/src")
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/src/fixed_math.c" "${CMAKE_CURRENT_SOURCE_DIR}/src/fixed_vec.c"
	 DESTINATION "${FIX_UNGUARDED_DIR}/src")

set(FIX_GUARD_ABS "\t// INT64_MIN policy: it is reserved and taken as FIX_MIN, so its magnitude saturates to FIX_MAX.\n\tmagnitude = magnitude > (uint64_t)FIX_MAX ? (uint64_t)FIX_MAX : magnitude;\n")
set(FIX_GUARD_LERP "\t// INT64_MIN policy: the subtract is unsigned so INT64_MIN is taken as FIX_MIN, not a trap.\n\tfixed_t oneMinusAlpha = (fixed_t)( (uint64_t)FIX_ONE - (uint64_t)alpha );\n\t// INT64_MIN policy: the sum is unsigned so extremes wrap in two's complement, never signed overflow.\n\treturn (fixed_t)( (uint64_t)fixMul( oneMinusAlpha, a ) + (uint64_t)fixMul( alpha, b ) );\n")
set(FIX_GUARD_SMOOTH "\tfixed_t omega = fixDiv( 2 * FIX_PI, smoothTime );\n\tfixed_t onePlus = (fixed_t)( (uint64_t)FIX_ONE + (uint64_t)fixMul( omega, deltaTime ) ); // INT64_MIN policy: unsigned add, so extremes wrap instead of trapping.\n\tfixed_t denominator = fixMul( onePlus, onePlus );\n\n\tfixed_t difference = (fixed_t)( (uint64_t)current - (uint64_t)target ); // INT64_MIN policy: unsigned subtract keeps INT64_MIN defined.\n\tfixed_t spring = fixMul( fixMul( fixMul( omega, omega ), deltaTime ), difference );\n\n\t*velocity = fixDiv( (fixed_t)( (uint64_t)*velocity - (uint64_t)spring ), denominator ); // INT64_MIN policy: unsigned subtract keeps INT64_MIN defined.\n\treturn (fixed_t)( (uint64_t)current + (uint64_t)fixMul( *velocity, deltaTime ) ); // INT64_MIN policy: unsigned add, so extremes wrap instead of trapping.\n")
set(FIX_UNGUARDED_SMOOTH "\tfixed_t omega = fixDiv( 2 * FIX_PI, smoothTime );\n\tfixed_t onePlus = FIX_ONE + fixMul( omega, deltaTime );\n\tfixed_t denominator = fixMul( onePlus, onePlus );\n\n\tfixed_t spring = fixMul( fixMul( fixMul( omega, omega ), deltaTime ), current - target );\n\n\t*velocity = fixDiv( *velocity - spring, denominator );\n\n\treturn current + fixMul( *velocity, deltaTime );\n")

function(fix_delete_guard path guard replacement)
	file(READ "${path}" content)
	string(FIND "${content}" "${guard}" found)
	if(found EQUAL -1)
		message(FATAL_ERROR "negative control: guard not found in ${path}")
	endif()
	string(REPLACE "${guard}" "${replacement}" unguarded "${content}")
	if(unguarded STREQUAL content)
		message(FATAL_ERROR "negative control: deleting the guard in ${path} changed nothing")
	endif()
	file(WRITE "${path}" "${unguarded}")
endfunction()

fix_delete_guard("${FIX_UNGUARDED_DIR}/include/fixed/fixed.h" "${FIX_GUARD_ABS}" "")
fix_delete_guard("${FIX_UNGUARDED_DIR}/include/fixed/fixed_math.h" "${FIX_GUARD_LERP}" "\treturn fixMul( ( FIX( 1.0f ) - alpha ) , a ) + fixMul( alpha , b );\n")
fix_delete_guard("${FIX_UNGUARDED_DIR}/src/fixed_math.c" "${FIX_GUARD_SMOOTH}" "${FIX_UNGUARDED_SMOOTH}")

add_library(fixed_unguarded STATIC "${FIX_UNGUARDED_DIR}/src/fixed_math.c" "${FIX_UNGUARDED_DIR}/src/fixed_vec.c")
target_include_directories(fixed_unguarded PUBLIC "${FIX_UNGUARDED_DIR}/include")
if (NOT WIN32)
	target_link_libraries(fixed_unguarded PUBLIC m)
endif()

add_executable(fixed_boundary_negative_control test/boundary_test.c)
target_link_libraries(fixed_boundary_negative_control PRIVATE fixed_unguarded)
add_test(NAME boundary_negative_control COMMAND fixed_boundary_negative_control)
set_tests_properties(boundary_negative_control PROPERTIES WILL_FAIL TRUE)

# CORE-MATH as a test-only oracle. The two vendored files are built into an object library
# with their own flags and warnings off, and linked only into the sincos tests below; the
# fixed library itself gains no dependency. ClangCL defines MSVC, and that arm has no
# __int128 for this code, so it is skipped there with the rest of the MSVC-only paths.
if (NOT MSVC)
    add_library(fixed_core_math_object OBJECT extern/core-math/sin.c extern/core-math/cos.c)
    target_compile_options(fixed_core_math_object PRIVATE -O2 -ffp-contract=off -w)

    add_executable(fixed_sincos_core_math_test test/sincos_core_math_test.c)
    target_link_libraries(fixed_sincos_core_math_test PRIVATE fixed fixed_core_math_object)
    add_test(NAME sincos_core_math COMMAND fixed_sincos_core_math_test)

    # The same oracle against the emulated 128-bit arm: the reduction must be bit-identical.
    if (TARGET fixed_emulated)
        add_executable(fixed_sincos_core_math_emulated_test test/sincos_core_math_test.c)
        target_link_libraries(fixed_sincos_core_math_emulated_test PRIVATE fixed_emulated fixed_core_math_object)
        add_test(NAME sincos_core_math_emulated COMMAND fixed_sincos_core_math_emulated_test)
    endif()

    # The planted single-word 2*pi reduction, fed through fixUnwindAngle exactly as the old
    # fixComputeCosSin did. It must exceed the bound and fail, so CTest expects failure.
    add_executable(fixed_sincos_negative_control test/sincos_core_math_test.c)
    target_link_libraries(fixed_sincos_negative_control PRIVATE fixed fixed_core_math_object)
    target_compile_definitions(fixed_sincos_negative_control PRIVATE SINCOS_NEGATIVE_CONTROL)
    add_test(NAME sincos_negative_control COMMAND fixed_sincos_negative_control)
    set_tests_properties(sincos_negative_control PROPERTIES WILL_FAIL TRUE)

    # The correctly-rounded exp2 oracle for fixExp2Exact. Same pattern as the sincos oracle:
    # a test-only object library with its own flags and warnings off, linked only into the
    # exp2 tests below.
    add_library(fixed_core_math_exp2_object OBJECT extern/core-math/exp2/exp2.c)
    target_compile_options(fixed_core_math_exp2_object PRIVATE -O2 -ffp-contract=off -w)

    add_executable(fixed_exp2_exact_test test/exp2_exact_test.c)
    target_link_libraries(fixed_exp2_exact_test PRIVATE fixed fixed_core_math_exp2_object)
    add_test(NAME exp2_exact COMMAND fixed_exp2_exact_test)

    # Same oracle against the emulated 128-bit arm: fixExp2Exact must be bit-identical.
    if (TARGET fixed_emulated)
        add_executable(fixed_exp2_exact_emulated_test test/exp2_exact_test.c)
        target_link_libraries(fixed_exp2_exact_emulated_test PRIVATE fixed_emulated fixed_core_math_exp2_object)
        add_test(NAME exp2_exact_emulated COMMAND fixed_exp2_exact_emulated_test)
    endif()

    # Negative control: the same test evaluating fixExp2 instead of fixExp2Exact. It must
    # exceed the bound and fail, so CTest expects failure.
    add_executable(fixed_exp2_exact_negative_control test/exp2_exact_test.c)
    target_link_libraries(fixed_exp2_exact_negative_control PRIVATE fixed fixed_core_math_exp2_object)
    target_compile_definitions(fixed_exp2_exact_negative_control PRIVATE EXP2_NEGATIVE_CONTROL)
    add_test(NAME exp2_exact_negative_control COMMAND fixed_exp2_exact_negative_control)
    set_tests_properties(exp2_exact_negative_control PROPERTIES WILL_FAIL TRUE)
endif()

# The double/float to Q48.16 conversions, checked against an integer-only bit-level
# reference. Its negative control builds the same file with fixFromDouble/fixFromFloat
# replaced by the old buggy +-0.5 formula; those checks must fail (WILL_FAIL).
add_executable(fixed_from_double_test test/from_double_test.c)
target_link_libraries(fixed_from_double_test PRIVATE fixed)
add_test(NAME from_double COMMAND fixed_from_double_test)

add_executable(fixed_from_double_negative_control test/from_double_test.c)
target_link_libraries(fixed_from_double_negative_control PRIVATE fixed)
target_compile_definitions(fixed_from_double_negative_control PRIVATE FROM_DOUBLE_NEGATIVE_CONTROL)
add_test(NAME from_double_negative_control COMMAND fixed_from_double_negative_control)
set_tests_properties(from_double_negative_control PROPERTIES WILL_FAIL TRUE)

if (TARGET fixed_emulated)
    add_executable(fixed_from_double_emulated_test test/from_double_test.c)
    target_link_libraries(fixed_from_double_emulated_test PRIVATE fixed_emulated)
    if (NOT WIN32)
        target_link_libraries(fixed_from_double_emulated_test PRIVATE m)
    endif()
    add_test(NAME from_double_emulated COMMAND fixed_from_double_emulated_test)
endif()
option(FIX_BUILD_BENCHMARKS "Build the numerical public-API microbenchmarks" OFF)
if (FIX_BUILD_BENCHMARKS)
    add_executable(fixed_numerical_bench benchmark/numerical_bench.c)
    target_link_libraries(fixed_numerical_bench PRIVATE fixed)
endif()

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
    foreach(suite determinism transcendental wide_time exhaustive aabb port_equality int128 geometry inverse_envelope int256_division numerical_regression boundary)
        add_executable(fixed_${suite}_emulated_test test/${suite}_test.c)
        target_link_libraries(fixed_${suite}_emulated_test PRIVATE fixed_emulated)
        if (NOT WIN32)
            target_link_libraries(fixed_${suite}_emulated_test PRIVATE m)
        endif()
        add_test(NAME ${suite}_emulated COMMAND fixed_${suite}_emulated_test)
    endforeach()

    add_executable(fixed_numerical_saturated_emulated_test test/numerical_regression_test.c src/fixed_math.c src/fixed_vec.c)
    target_include_directories(fixed_numerical_saturated_emulated_test PRIVATE include)
    target_compile_definitions(fixed_numerical_saturated_emulated_test PRIVATE FIX_SATURATE FIX_FORCE_EMULATED_INT128)
    if (NOT WIN32)
        target_link_libraries(fixed_numerical_saturated_emulated_test PRIVATE m)
    endif()
    add_test(NAME numerical_saturated_emulated COMMAND fixed_numerical_saturated_emulated_test)

    # And the control for the emulated arm: the exhaustive suite with the same deliberate
    # 1-ulp error, compiled against the emulated library. Without this, an emulated build
    # whose sweeps had gone blind would pass exactly as it does now. MUST fail.
    add_executable(fixed_emulated_negative_control test/exhaustive_test.c)
    target_link_libraries(fixed_emulated_negative_control PRIVATE fixed_emulated)
    target_compile_definitions(fixed_emulated_negative_control PRIVATE FIXED_NEGATIVE_CONTROL)
    add_test(NAME emulated_negative_control COMMAND fixed_emulated_negative_control)
    set_tests_properties(emulated_negative_control PROPERTIES WILL_FAIL TRUE)
endif()

# ---------------------------------------------------------------------------------------------
# THE EXACT-FUNCTION PLUG-IN LAYOUT.
#
# A later function card adds src/exact/<name>.c, include/fixed/fixed_<name>.h and
# test/exact/<name>_test.c and edits nothing here. The globs are CONFIGURE_DEPENDS, so a new
# file is picked up by the next build without touching this file.
# ---------------------------------------------------------------------------------------------
file( GLOB FIX_EXACT_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/exact/*.c" )
if (FIX_EXACT_SOURCES)
    target_sources(fixed PRIVATE ${FIX_EXACT_SOURCES})
    if (TARGET fixed_emulated)
        target_sources(fixed_emulated PRIVATE ${FIX_EXACT_SOURCES})
    endif()
endif()

file( GLOB FIX_EXACT_TESTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/test/exact/*_test.c" )
foreach(exact_test ${FIX_EXACT_TESTS})
    get_filename_component(exact_test_name "${exact_test}" NAME_WE)
    string(REGEX REPLACE "_test$" "" exact_name "${exact_test_name}")
    add_executable(fixed_exact_${exact_name}_test "${exact_test}")
    target_link_libraries(fixed_exact_${exact_name}_test PRIVATE fixed)
    if (NOT WIN32)
        target_link_libraries(fixed_exact_${exact_name}_test PRIVATE m)
    endif()
    add_test(NAME ${exact_name} COMMAND fixed_exact_${exact_name}_test)
    if (TARGET fixed_emulated)
        add_executable(fixed_exact_${exact_name}_emulated_test "${exact_test}")
        target_link_libraries(fixed_exact_${exact_name}_emulated_test PRIVATE fixed_emulated)
        if (NOT WIN32)
            target_link_libraries(fixed_exact_${exact_name}_emulated_test PRIVATE m)
        endif()
        add_test(NAME ${exact_name}_emulated COMMAND fixed_exact_${exact_name}_emulated_test)
    endif()
endforeach()

file( GLOB FIX_EXACT_NEGATIVE_TESTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/test/exact/*_negative_control.c" )
foreach(exact_control ${FIX_EXACT_NEGATIVE_TESTS})
    get_filename_component(exact_control_name "${exact_control}" NAME_WE)
    string(REGEX REPLACE "_negative_control$" "" exact_name "${exact_control_name}")
    add_executable(fixed_exact_${exact_name}_negative_control "${exact_control}")
    target_link_libraries(fixed_exact_${exact_name}_negative_control PRIVATE fixed)
    add_test(NAME ${exact_name}_negative_control COMMAND fixed_exact_${exact_name}_negative_control)
    set_tests_properties(${exact_name}_negative_control PROPERTIES WILL_FAIL TRUE)
endforeach()

# ---------------------------------------------------------------------------------------------
# THE MPFR SWEEP AND ITS SCOREBOARD.
#
# Off by default, and only built on a machine with MPFR and GMP. Missing either names the
# missing library at configure time and skips the sweep; it is never a configure error.
# Descriptors live in sweep/fns/*.c and the registry is generated here, so a function joins
# the sweep by adding one file.
# ---------------------------------------------------------------------------------------------
option(FIX_BUILD_SWEEPS "Build the MPFR accuracy sweeps and scoreboard (slow tier)" OFF)
if (FIX_BUILD_SWEEPS)
    find_path(FIX_MPFR_INCLUDE_DIR NAMES mpfr.h)
    find_library(FIX_MPFR_LIBRARY NAMES mpfr)
    find_path(FIX_GMP_INCLUDE_DIR NAMES gmp.h)
    find_library(FIX_GMP_LIBRARY NAMES gmp)
    if (NOT FIX_MPFR_LIBRARY)
        message(STATUS "fixed: sweep skipped: libmpfr not found")
    elseif (NOT FIX_GMP_LIBRARY)
        message(STATUS "fixed: sweep skipped: libgmp not found")
    else()
        find_package(Threads REQUIRED)
        file(GLOB FIX_SWEEP_FNS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/sweep/fns/*.c")
        set(FIX_SWEEP_REGISTRY "${CMAKE_CURRENT_BINARY_DIR}/sweep_registry.c")
        set(FIX_SWEEP_REGISTRY_TEXT "/* generated at configure time from sweep/fns/*.c */\n#include \"sweep.h\"\n")
        set(FIX_SWEEP_REGISTRY_ENTRIES "")
        foreach(fn_source ${FIX_SWEEP_FNS})
            get_filename_component(fn_name "${fn_source}" NAME_WE)
            string(APPEND FIX_SWEEP_REGISTRY_TEXT "extern const sweep_fn sweep_fn_${fn_name};\n")
            string(APPEND FIX_SWEEP_REGISTRY_ENTRIES "    &sweep_fn_${fn_name},\n")
        endforeach()
        string(APPEND FIX_SWEEP_REGISTRY_TEXT "const sweep_fn *const sweep_fns[] = {\n${FIX_SWEEP_REGISTRY_ENTRIES}};\nconst size_t sweep_nfns = sizeof( sweep_fns ) / sizeof( sweep_fns[0] );\n")
        file(WRITE "${FIX_SWEEP_REGISTRY}" "${FIX_SWEEP_REGISTRY_TEXT}")

        set(FIX_SWEEP_COMMON sweep/sweep.c "${FIX_SWEEP_REGISTRY}" ${FIX_SWEEP_FNS})
        add_executable(fixed_sweep ${FIX_SWEEP_COMMON})
        target_include_directories(fixed_sweep PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/sweep" ${FIX_MPFR_INCLUDE_DIR} ${FIX_GMP_INCLUDE_DIR})
        target_link_libraries(fixed_sweep PRIVATE fixed ${FIX_MPFR_LIBRARY} ${FIX_GMP_LIBRARY} Threads::Threads)
        if (NOT WIN32)
            target_link_libraries(fixed_sweep PRIVATE m)
        endif()
        target_compile_definitions(fixed_sweep PRIVATE FIX_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}" FIX_SWEEP_ARM="${CMAKE_C_COMPILER_ID}-native")
        foreach(fn_source ${FIX_SWEEP_FNS})
            get_filename_component(fn_name "${fn_source}" NAME_WE)
            add_test(NAME sweep_${fn_name} COMMAND fixed_sweep --ctest-label-guard --fn ${fn_name} --threads 3)
            set_tests_properties(sweep_${fn_name} PROPERTIES LABELS slow EXCLUDE_FROM_ALL TRUE)
        endforeach()

        if (TARGET fixed_emulated)
            add_executable(fixed_sweep_emulated ${FIX_SWEEP_COMMON})
            target_include_directories(fixed_sweep_emulated PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/sweep" ${FIX_MPFR_INCLUDE_DIR} ${FIX_GMP_INCLUDE_DIR})
            target_link_libraries(fixed_sweep_emulated PRIVATE fixed_emulated ${FIX_MPFR_LIBRARY} ${FIX_GMP_LIBRARY} Threads::Threads)
            if (NOT WIN32)
                target_link_libraries(fixed_sweep_emulated PRIVATE m)
            endif()
            target_compile_definitions(fixed_sweep_emulated PRIVATE FIX_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}" FIX_SWEEP_ARM="${CMAKE_C_COMPILER_ID}-emulated")
        endif()
    endif()
endif()
