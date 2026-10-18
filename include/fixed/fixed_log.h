/* SPDX-License-Identifier: MIT */
#pragma once

#include "fixed/base.h"
#include "fixed/fixed.h"

/**
 * @brief Correctly rounded natural logarithm in Q48.16.
 *
 * Returns RN( ln(raw * 2^-16) * 2^16 ) for every positive raw, the correctly rounded
 * integer nearest to the scaled exact value. For raw <= 0 returns INT64_MIN, as
 * fixLog2 does.
 *
 * Built on the ln reduction and kernel of docs/design/transcendentals.md section 2.1:
 * Tang reduction into 2^7 cells, a degree-10 fpminimax log1p kernel, and the three
 * terms summed in Q7.120 before one round to nearest by the shift (S + 2^103) >> 104.
 * Integer only, bit-identical on the native and emulated 128-bit arms.
 */
FIX_API fixed_t fixLnExact( fixed_t x );

/**
 * @brief Correctly rounded logarithm base 2 in Q48.16.
 *
 * Returns RN( log2(raw * 2^-16) * 2^16 ) for every positive raw; INT64_MIN for
 * raw <= 0. Uses the ln reduction and kernel with the integer exponent e added exactly
 * in raw units after the fractional part is rounded (design section 2.2), with
 * LOG2E_127 = round( log2(e) * 2^127 ).
 */
FIX_API fixed_t fixLog2Exact( fixed_t x );

/**
 * @brief Correctly rounded logarithm base 10 in Q48.16.
 *
 * Returns RN( log10(raw * 2^-16) * 2^16 ) for every positive raw; INT64_MIN for
 * raw <= 0. The ln sum S (Q7.120) times LOG10E_128 = round( log10(e) * 2^128 ) as a
 * 128x128 product on |S|, sign applied after, shifted right 128 to Q.120, then rounded
 * as ln (design section 2.3).
 */
FIX_API fixed_t fixLog10Exact( fixed_t x );
