/* SPDX-License-Identifier: MIT */
/* Exact oracle test for fixRsqrtExact, with no MPFR and no clock.
 *
 * The oracle is a direct binary search over the exact integer inequality
 *     (2q-1)^2 * raw <= 2^50 < (2q+1)^2 * raw
 * evaluated with 256-bit arithmetic. It shares no code with the implementation,
 * which uses an integer isqrt plus one correction instead of searching the
 * inequality directly.
 */
#include "fixed/fixed_rsqrt.h"
#include "fixed/fixed_int256.h"

#include <stdint.h>
#include <stdio.h>

// True when q is not too large: q == 0 always qualifies; otherwise the box's left
// inequality must hold.
static int oracle_pred( uint64_t q, uint64_t raw )
{
	if ( q == 0 )
	{
		return 1;
	}
	uint64_t a = 2 * q - 1;
	fixUInt256 prod = fixUInt256MulU128ByU64( fixUInt128FromU64( a * a ), raw );
	fixUInt256 lim = fixUInt256FromU64( UINT64_C( 1 ) << 50 );
	return !fixUInt256Lt( lim, prod ); // prod <= 2^50
}

// Largest q in [0, 2^24] with oracle_pred(q, raw) true: the correctly rounded
// integer nearest to 2^24/sqrt(raw), ties rounding up.
static uint64_t oracle_rsqrt( uint64_t raw )
{
	uint64_t lo = 0;
	uint64_t hi = UINT64_C( 1 ) << 24;
	while ( lo < hi )
	{
		uint64_t mid = lo + ( hi - lo + 1 ) / 2;
		if ( oracle_pred( mid, raw ) )
		{
			lo = mid;
		}
		else
		{
			hi = mid - 1;
		}
	}
	return lo;
}

static void check_raw( uint64_t raw, int* fails )
{
	uint64_t got = (uint64_t)fixRsqrtExact( (fixed_t)raw );
	uint64_t exp = oracle_rsqrt( raw );
	if ( got != exp )
	{
		printf( "FAIL raw=%llu got=%llu exp=%llu\n", (unsigned long long)raw,
				(unsigned long long)got, (unsigned long long)exp );
		(*fails)++;
	}
}

int main( void )
{
	int fails = 0;

	if ( fixRsqrtExact( 0 ) != INT64_MAX || fixRsqrtExact( -1 ) != INT64_MAX ||
		 fixRsqrtExact( INT64_MIN ) != INT64_MAX )
	{
		printf( "FAIL edge inputs must saturate to INT64_MAX\n" );
		fails++;
	}

	if ( fixRsqrtExact( 1 ) != 16777216 )
	{
		printf( "FAIL 1 -> 16777216\n" );
		fails++;
	}
	if ( fixRsqrtExact( 4 ) != 8388608 )
	{
		printf( "FAIL 4 -> 8388608\n" );
		fails++;
	}
	if ( fixRsqrtExact( 65536 ) != 65536 )
	{
		printf( "FAIL 65536 -> 65536\n" );
		fails++;
	}

	for ( uint64_t raw = 1; raw <= 4096; ++raw )
	{
		check_raw( raw, &fails );
	}

	check_raw( (uint64_t)INT64_MAX, &fails );

	// 256 raws spread over every binade: four per binade 1..62 plus eight across the
	// top binade 63.
	for ( int b = 1; b <= 62; ++b )
	{
		uint64_t base = UINT64_C( 1 ) << b;
		for ( int k = 0; k < 4; ++k )
		{
			check_raw( base + ( (uint64_t)k * base ) / 4, &fails );
		}
	}
	for ( uint64_t k = 0; k < 8; ++k )
	{
		check_raw( (uint64_t)INT64_MAX - k, &fails );
	}

	if ( fails )
	{
		printf( "rsqrt exact test failed: %d mismatches\n", fails );
		return 1;
	}
	printf( "rsqrt exact test passed\n" );
	return 0;
}
