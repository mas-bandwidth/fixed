/* SPDX-License-Identifier: MIT */
#pragma once

#include "fixed/base.h"
#include "fixed/fixed.h"

/**
 * @brief Correctly rounded natural exponential in Q48.16.
 *
 * Returns RN( exp(raw * 2^-16) * 2^16 ) for every Q48.16 raw in [INT64_MIN, INT64_MAX].
 * Returns 0 for raw <= -772244 (where exp(x) * 2^16 < 1/2) and saturates to INT64_MAX
 * for raw >= 2135027 (where RN(exp(x) * 2^16) >= 2^63 - 1).
 *
 * Built on the shared Tang 1/128-ln2 table + degree-8 polynomial kernel in
 * src/exact/exp_kernel.h (docs/design/transcendentals.md section 1.1), followed by a
 * single round to nearest by the shift 110 - m. Integer only, bit-identical on the
 * native and emulated 128-bit arms. Test: test/exact/exp_test.c.
 */
FIX_API fixed_t fixExpExact( fixed_t x );
