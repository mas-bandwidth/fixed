cmake_minimum_required(VERSION 3.16)
project(fixed C)

# Determinism: forbid fused multiply-add. Core ops are pure integer; this keeps
# the few float conversion helpers from fusing across platforms.
if (NOT MSVC)
    add_compile_options(-ffp-contract=off)
endif()

add_library(fixed STATIC src/fixed_math.c)
target_include_directories(fixed PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

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

add_executable(fixed_exhaustive_test test/exhaustive_test.c)
target_link_libraries(fixed_exhaustive_test PRIVATE fixed)
add_test(NAME exhaustive COMMAND fixed_exhaustive_test)

# Negative control: the SAME exhaustive suite with a deliberate 1-ulp error
# injected into multiply. This build MUST fail (WILL_FAIL) — a test of the
# tests, proving the suite actually detects breakage rather than silently
# passing. If this test ever "passes", the suite has gone blind.
add_executable(fixed_negative_control test/exhaustive_test.c)
target_link_libraries(fixed_negative_control PRIVATE fixed)
target_compile_definitions(fixed_negative_control PRIVATE FIXED_NEGATIVE_CONTROL)
add_test(NAME negative_control COMMAND fixed_negative_control)
set_tests_properties(negative_control PROPERTIES WILL_FAIL TRUE)
