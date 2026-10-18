/* SPDX-License-Identifier: MIT */
#pragma once

#include "fixed/base.h"
#include "fixed/fixed.h"

/**
 * @brief Correctly rounded reciprocal square root in Q48.16.
 *
 * Returns RN( 2^24 / sqrt(raw) ) in raw units for every positive raw, where raw is
 * the Q48.16 input value x: the correctly rounded integer q nearest to 2^24/sqrt(raw),
 * ties rounding up toward +infinity. For x <= 0 returns INT64_MAX.
 *
 * Exact by construction, with no floating point and no seed table. The integer
 * specification, which the CBMC card proves, is:
 *
 *     q is the unique non-negative integer satisfying
 *         (2q-1)^2 * raw <= 2^50 < (2q+1)^2 * raw
 *     with q == 0 taken as the answer whenever raw > 2^50 (result below 0.5 raw).
 *
 * The method: an exact integer square root of a widened value (fixISqrt128High) gives
 * s = floor(sqrt(raw) * 2^24); an integer quotient gives an initial q within 1 of the
 * true rounded result; one correction step compares q and q +/- 1 against the exact
 * inequality above. Every step is exact integer arithmetic, so the result is
 * bit-identical on the native and emulated 128-bit arms and on every compiler.
 */
FIX_API fixed_t fixRsqrtExact( fixed_t x );
