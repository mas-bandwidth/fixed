/* SPDX-License-Identifier: MIT */
#include "fixed/fixed_exp.h"
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static fixed_t realExp(fixed_t x) { return fixExpExact(x); }

#define FIX_EXP_LN2_128_HI UINT64_C(12786308645202655660)
#define FIX_EXP_LN2_128_LO UINT64_C(0)
#define fixExpExact fixExpExactLn2x64
FIX_API fixed_t fixExpExactLn2x64(fixed_t x);
#include "../../src/exact/exp.c"
#undef fixExpExact

static size_t differences;
static int64_t first_raw;
static fixed_t first_real;
static fixed_t first_weak;

static void compare(int64_t raw)
{
    const fixed_t expected = realExp((fixed_t)raw);
    const fixed_t weakened = fixExpExactLn2x64((fixed_t)raw);
    if (expected != weakened) {
        if (differences == 0) {
            first_raw = raw;
            first_real = expected;
            first_weak = weakened;
        }
        ++differences;
    }
}

int main(void)
{
    static const int64_t named[] = {
        INT64_MIN, INT64_C(-772244), INT64_C(-772243), INT64_C(-129660),
        INT64_C(0), INT64_C(2135026), INT64_C(2135027), INT64_MAX
    };
    for (int64_t raw = INT64_C(1900000); raw <= INT64_C(2135026); ++raw)
        compare(raw);
    for (size_t i = 0; i < sizeof(named) / sizeof(named[0]); ++i)
        if (named[i] < INT64_C(1900000) || named[i] > INT64_C(2135026))
            compare(named[i]);

    printf("exp negative control differing raws: %zu\n", differences);
    if (differences != 0) {
        printf("first differing raw=%" PRId64 " real=%" PRId64 " ln2x64=%" PRId64 "\n",
               first_raw, (int64_t)first_real, (int64_t)first_weak);
        return 1;
    }
    return 0;
}
