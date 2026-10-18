// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
//
// tanh, correctly rounded for every Q48.16 raw in [INT64_MIN, INT64_MAX].
//
// METHOD. tanh is odd, so the magnitude is computed and the sign is applied at the end.
// For 1861 <= |raw| <= 408834, E = e^(2x): fixExpKernel( 2 * |raw| ) gives (M, m), so
// |x| = M * 2^(m - 126) and
//
//     tanh x = ( e^(2x) - 1 ) / ( e^(2x) + 1 ) = ( M - a ) / ( M + a ),
//     a = 2^(126 - m),
//
// with num = M - a and den = M + a both below 2^128. Q = floor( num * 2^128 / den ) by
// fixUInt256DivMod is tanh x in Q0.128, and one round to nearest gives
// raw_out = ( Q + 2^111 ) >> 112. The identity and saturation ranges are exact by the
// design's measurements: |raw| <= 1860 returns raw, |raw| >= 408835 returns 65536.
// There is no small-|x| polynomial: the one-step rule above is the coordinator's.
//
// BUDGET. On the exp path the quotient's absolute error is 0.5 * err(M) + 2^-128,
// because d/dM of (M - a)/(M + a) is at most 1/2, so eps <= 2^16 * ( 2^-102.67 + 2^-128 )
// = 2^-86.67 raw, against the exhaustive margin 2^-20.28 raw (design section 1.4).
//
// Integer only, through the fixInt128/fixUInt128/fixUInt256 vocabulary, so the native
// and emulated 128-bit arms, gcc and clang, produce identical bits. No gcc builtin outside
// the library's wrappers and no variable-length array: plain MSVC builds it.
#include "fixed/fixed_tanh.h"
#include "fixed/fixed_int128.h"
#include "fixed/fixed_int256.h"

#include "exp_kernel.h"

#include <stdint.h>

FIX_API fixed_t fixTanhExact( fixed_t x )
{
	int negative = x < 0;
	uint64_t magnitude = negative ? (uint64_t)0 - (uint64_t)x : (uint64_t)x;

	if ( magnitude >= 408835 )
	{
		return negative ? -65536 : 65536;
	}
	if ( magnitude <= 1860 )
	{
		return x;
	}

	fixUInt128 M;
	int exponent;
	fixExpKernel( (int64_t)( 2 * magnitude ), &M, &exponent );

	// a = 2^(126 - exponent); exponent is at most 18 on this range, so the shift is exact.
	fixUInt128 a = fixUInt128Shl( fixUInt128FromU64( 1 ), 126 - exponent );
	fixUInt128 num = fixUInt128Sub( M, a );
	fixUInt128 den = fixUInt128Add( M, a );

	fixUInt256 dividend;
	dividend.hi = num;
	dividend.lo = FIX_UINT128_ZERO;
	fixUInt256 quotient;
	fixUInt256 remainder;
	fixUInt256DivMod( dividend, fixUInt256FromU128( den ), &quotient, &remainder );

	// quotient = tanh x * 2^128, in Q0.128. Add 2^111 in 256 bits before the shift so the
	// carry near saturation cannot wrap a 128-bit value.
	fixUInt256 half = fixUInt256FromU128( fixUInt128Shl( fixUInt128FromU64( 1 ), 111 ) );
	uint64_t raw = fixUInt128Lo( fixUInt256Shr( fixUInt256Add( quotient, half ), 112 ).lo );

	return negative ? -(fixed_t)raw : (fixed_t)raw;
}
