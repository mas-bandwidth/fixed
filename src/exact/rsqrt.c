/* SPDX-License-Identifier: MIT */
#include "fixed/fixed_rsqrt.h"
#include "fixed/fixed.h"

// Exact integer helpers for the correctness inequality. The compared products
// (2q±1)^2 * raw fit well inside 128 bits (raw < 2^63 and (2q+1)^2 < 2^51), so a
// single widening multiply and compare is exact on both 128-bit arms.
//
// The integer specification:
//     q is the unique non-negative integer with
//         (2q-1)^2 * raw <= 2^50 < (2q+1)^2 * raw
//     and q == 0 is the answer whenever raw > 2^50.

// (2q+1)^2 * raw <= 2^50 ? (true means q is still too small).
static int rsqrt_up_ok( uint64_t q, uint64_t raw )
{
	uint64_t a = 2 * q + 1;
	fixUInt128 prod = fixUInt128MulU64( a * a, raw );
	return !fixUInt128Gt( prod, fixUInt128FromU64( UINT64_C( 1 ) << 50 ) );
}

// (2q-1)^2 * raw > 2^50 ? (true means q is too large; q == 0 is never too large).
static int rsqrt_down_bad( uint64_t q, uint64_t raw )
{
	if ( q == 0 )
	{
		return 0;
	}
	uint64_t a = 2 * q - 1;
	fixUInt128 prod = fixUInt128MulU64( a * a, raw );
	return fixUInt128Gt( prod, fixUInt128FromU64( UINT64_C( 1 ) << 50 ) );
}

FIX_API fixed_t fixRsqrtExact( fixed_t x )
{
	if ( x <= 0 )
	{
		return INT64_MAX;
	}

	uint64_t raw = (uint64_t)x;

	// s = floor( sqrt(raw) * 2^24 ) = floor( sqrt(raw * 2^48) ), exact. Since
	// raw < 2^63, raw*2^48 < 2^111 fits the 128-bit input of fixISqrt128High.
	uint64_t s = fixISqrt128High( raw >> 16, raw << 48 );

	// Initial q = round( 2^48 / s ) = floor( 2^48/s + 1/2 ). Because s is within 1
	// of sqrt(raw)*2^24, this initial q is within 1 of the true rounded result.
	uint64_t q = ( ( UINT64_C( 1 ) << 48 ) + ( s >> 1 ) ) / s;

	// One correction step against the exact inequality, up then down. Each loop runs
	// at most a couple of times because q starts within 1 of the answer.
	while ( q < ( UINT64_C( 1 ) << 24 ) && rsqrt_up_ok( q, raw ) )
	{
		++q;
	}
	while ( q > 0 && rsqrt_down_bad( q, raw ) )
	{
		--q;
	}

	return (fixed_t)q;
}
