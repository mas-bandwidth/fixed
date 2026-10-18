/* SPDX-License-Identifier: MIT */
#include "fixed/fixed_sinh_cosh.h"
#include "fixed/fixed_int128.h"
#include "exp_kernel.h"

// Correctly rounded sinh and cosh for every Q48.16 input, per
// docs/design/transcendentals.md section 1.3.
//
// Both work on |raw|. sinh is odd exactly and cosh is even exactly; INT64_MIN takes the
// saturated branch, so its magnitude is never negated as a signed value.
//
//   |raw| >= 2180453              -> INT64_MAX (sinh with the input's sign, -INT64_MAX
//                                    for negatives, never INT64_MIN)
//   sinh, |raw| <= 2344           -> raw, exact (the identity cut-off)
//   otherwise                     -> fixExpKernel( |raw| ) = (Ma, ma) and
//                                    fixExpKernel( -|raw| ) = (Mb, mb), d = ma - mb,
//                                    Y = 2^(ma + 15 - 126) * ( Ma -/+ (Mb >> d) ),
//                                    one round to nearest by the shift 111 - ma.
//
// The -/+ is minus for sinh, plus for cosh; the 15 is the Q48.16 scale (the 1/2 of the
// identity and the 2^16 of the raw unit). Ma + Mb < 2^128, so the sum of the two
// mantissas fits the 128-bit arm.
//
// Budget (docs/design/transcendentals.md section 1.3): eps <= 2^62 * ( 2^-101.67 +
// 2^-102.67 + 2^-126 ) = 2^-39.09 raw, against the exhaustive margins 2^-32.75 raw for
// sinh (raw 441619) and 2^-20.58 raw for cosh. So both are correctly rounded on every
// input.
//
// Integer only, through fixUInt128, so the native and emulated 128-bit arms and gcc and
// clang give the same bits, and plain MSVC (no __int128) builds it.

static fixed_t fixSinhCoshKernel( uint64_t mag, int isSinh )
{
	fixUInt128 maMantissa;
	fixUInt128 mbMantissa;
	int ma;
	int mb;

	fixExpKernel( (int64_t)mag, &maMantissa, &ma );
	fixExpKernel( -(int64_t)mag, &mbMantissa, &mb );

	const int d = ma - mb;
	const fixUInt128 shifted = fixUInt128Shr( mbMantissa, d );
	const fixUInt128 y =
		isSinh ? fixUInt128Sub( maMantissa, shifted ) : fixUInt128Add( maMantissa, shifted );

	const int shift = 111 - ma;
	const fixUInt128 rounding = fixUInt128Shl( fixUInt128FromU64( 1 ), shift - 1 );
	const fixUInt128 rounded = fixUInt128Add( y, rounding );

	return (fixed_t)fixUInt128Lo( fixUInt128Shr( rounded, shift ) );
}

FIX_API fixed_t fixSinhExact( fixed_t x )
{
	const int negative = x < 0;
	const uint64_t mag = negative ? (uint64_t)0 - (uint64_t)x : (uint64_t)x;

	if ( mag >= UINT64_C( 2180453 ) )
	{
		return negative ? -INT64_MAX : INT64_MAX;
	}
	if ( mag <= UINT64_C( 2344 ) )
	{
		return x;
	}

	const fixed_t y = fixSinhCoshKernel( mag, 1 );
	return negative ? -y : y;
}

FIX_API fixed_t fixCoshExact( fixed_t x )
{
	const uint64_t mag = x < 0 ? (uint64_t)0 - (uint64_t)x : (uint64_t)x;

	if ( mag >= UINT64_C( 2180453 ) )
	{
		return INT64_MAX;
	}

	return fixSinhCoshKernel( mag, 0 );
}
