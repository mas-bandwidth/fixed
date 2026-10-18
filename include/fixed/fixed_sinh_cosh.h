/* SPDX-License-Identifier: MIT */
#pragma once

#include "fixed/base.h"
#include "fixed/fixed.h"

/**
 * @brief Correctly rounded hyperbolic sine in Q48.16.
 *
 * Returns RN( sinh(raw * 2^-16) * 2^16 ) for every raw. Odd exactly, including the
 * saturated branch: |raw| >= 2180453 returns INT64_MAX with the input's sign, so a
 * negative input gives -INT64_MAX and never INT64_MIN. |raw| <= 2344 returns raw exactly.
 *
 * Built on the identity sinh x = (e^x - e^-x) / 2 with the shared exp kernel of
 * docs/design/transcendentals.md section 1.3: two fixExpKernel calls, the difference in
 * 128 bits, and one round to nearest by the shift 111 - ma. Integer only, bit-identical on
 * the native and emulated 128-bit arms. Test: test/exact/sinh_cosh_test.c.
 */
FIX_API fixed_t fixSinhExact( fixed_t x );

/**
 * @brief Correctly rounded hyperbolic cosine in Q48.16.
 *
 * Returns RN( cosh(raw * 2^-16) * 2^16 ) for every raw. Even exactly. |raw| >= 2180453
 * returns INT64_MAX.
 *
 * Built on the identity cosh x = (e^x + e^-x) / 2 with the shared exp kernel of
 * docs/design/transcendentals.md section 1.3: two fixExpKernel calls, the sum in 128 bits,
 * and one round to nearest by the shift 111 - ma. Integer only, bit-identical on the native
 * and emulated 128-bit arms. Test: test/exact/sinh_cosh_test.c.
 */
FIX_API fixed_t fixCoshExact( fixed_t x );
