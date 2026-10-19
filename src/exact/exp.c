/* SPDX-License-Identifier: MIT */
#include "fixed/fixed_exp.h"
#include "fixed/fixed_int128.h"
#include "exp_kernel.h"

#include <stdint.h>

// Correctly rounded natural exponential exp(x) for every Q48.16 input, per
// docs/design/transcendentals.md section 1.1.
//
//   raw <= -772244                -> 0 (exact, since exp(x) * 2^16 < 1/2)
//   raw >= 2135027                -> INT64_MAX (saturated, since RN(exp(x) * 2^16) >= 2^63 - 1)
//   -772243 <= raw <= 2135026     -> fixExpKernel( raw ) = (M, m), with m in [-17, 46]
//                                    and M in [2^126, 2^127), then one round to nearest
//                                    by the shift s = 110 - m in [64, 127]:
//                                    raw_out = ( M + 2^(s - 1) ) >> s.
//
// Budget (docs/design/transcendentals.md section 1.1): eps <= 2^-101.67 * 2^(m + 16)
// <= 2^-39.67 raw, against the exhaustive margin 2^-21.15 raw (at raw = -129660), so
// fixExpExact is correctly rounded on every Q48.16 input.
//
// Integer only, through fixUInt128/fixUInt256, so the native and emulated 128-bit arms
// and gcc and clang produce identical bits, and plain MSVC builds it.

FIX_API fixed_t fixExpExact( fixed_t x )
{
	if ( x <= INT64_C( -772244 ) )
	{
		return 0;
	}
	if ( x >= INT64_C( 2135027 ) )
	{
		return INT64_MAX;
	}

	fixUInt128 mantissa;
	int exponent;
	fixExpKernel( x, &mantissa, &exponent );

	const int shift = 110 - exponent;
	const fixUInt128 rounding = fixUInt128Shl( fixUInt128FromU64( 1 ), shift - 1 );
	const fixUInt128 rounded = fixUInt128Add( mantissa, rounding );

	return (fixed_t)fixUInt128Lo( fixUInt128Shr( rounded, shift ) );
}
