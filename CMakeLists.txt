cmake_minimum_required(VERSION 3.16)
project(fixed C)

# Determinism: forbid fused multiply-add. The whole point of this library is
# bit-identical results across platforms; -ffp-contract=off keeps the (rare)
# float conversion helpers from fusing. The core ops are pure integer anyway.
if (NOT MSVC)
    add_compile_options(-ffp-contract=off)
endif()

add_library(fixed INTERFACE)
target_include_directories(fixed INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

enable_testing()
add_executable(fixed_determinism_test test/determinism_test.c)
target_link_libraries(fixed_determinism_test PRIVATE fixed)
add_test(NAME determinism COMMAND fixed_determinism_test)
