// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT

#include "fixed/fixed_cbrt.h"
#include "fixed/fixed_int256.h"

// floor( cbrt( n ) ) for n < 2^96, so the root is below 2^32.
//
// Bitwise from the top of a 32-bit root: a candidate bit is kept exactly when its cube
// does not exceed n. A candidate is below 2^32, so candidate^2 is below 2^64 and fits a
// uint64_t exactly, and candidate^3 is below 2^96 and fits fixUInt128 exactly. No step
// rounds, compares a rounded value, or leaves the integer types.
static uint64_t fixCbrtFloor( fixUInt128 n )
{
	uint64_t root = 0;
	for ( int bit = 31; bit >= 0; --bit )
	{
		uint64_t candidate = root | ( UINT64_C( 1 ) << bit );
		uint64_t square = candidate * candidate;
		fixUInt128 cube = fixUInt128MulU64( square, candidate );
		if ( fixUInt128Le( cube, n ) )
		{
			root = candidate;
		}
	}
	return root;
}

// RN( cbrt( n ) ) for integer n < 2^96, from the exact floor q.
//
// cbrt( n ) is above q + 1/2 exactly when ( 2q + 1 )^3 > 8n, and below it when the
// inequality points the other way, so comparing the two exact integers in 256 bits decides
// the rounding without evaluating a root. ( 2q + 1 ) is below 2^33, its square below 2^66
// and its cube below 2^99; 8n is below 2^98. Both are formed exactly by
// fixUInt256MulU128ByU64 and a 3-place shift.
static uint64_t fixCbrtRound( fixUInt128 n )
{
	uint64_t q = fixCbrtFloor( n );

	uint64_t odd = 2 * q + 1;
	fixUInt128 oddSquared = fixUInt128MulU64( odd, odd );
	fixUInt256 left = fixUInt256MulU128ByU64( oddSquared, odd );
	fixUInt256 right = fixUInt256Shl( fixUInt256FromU128( n ), 3 );

	if ( fixUInt256Lt( right, left ) )
	{
		return q;
	}
	if ( fixUInt256Lt( left, right ) )
	{
		return q + 1;
	}
	// Exact half, round to even. Unreachable for an integer n: the left side is odd and
	// the right side is divisible by eight. Written out so the rounding rule is complete.
	return ( q & 1 ) ? q + 1 : q;
}

// Correctly rounded cube root of a Q48.16 value, every step integer and exact.
//
// cbrt( raw / 2^16 ) * 2^16 = cbrt( raw * 2^32 ). The magnitude is below 2^63, so
// |raw| * 2^32 is below 2^95 and fits fixUInt128 with room to spare. The integer cube
// root and the half-comparison are exact (see above), and the sign is applied afterwards
// through unsigned negation, which is defined at INT64_MIN. The result is below 2^32 in
// magnitude, so negating it cannot overflow int64 either.
FIX_API fixed_t fixCbrtExact( fixed_t x )
{
	if ( x == 0 )
	{
		return 0;
	}

	uint64_t magnitude = x < 0 ? (uint64_t)0 - (uint64_t)x : (uint64_t)x;
	fixUInt128 n = fixUInt128Shl( fixUInt128FromU64( magnitude ), 32 );
	uint64_t result = fixCbrtRound( n );
	return x < 0 ? -(fixed_t)result : (fixed_t)result;
}
