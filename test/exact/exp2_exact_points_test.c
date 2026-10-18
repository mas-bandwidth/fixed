/* SPDX-License-Identifier: MIT */
/* Fast points test for fixExp2Exact, with no MPFR.
 *
 * The header is written by `fixed_sweep --fn exp2_exact --emit-points`: every libfixmath
 * edge, 4096 seeded draws and the 256 worst inputs of the MPFR sweep, each with its
 * correctly rounded raw output. fixExp2Exact is documented within 2 raw of correctly
 * rounded, so every point is checked against that bound.
 */
#include "fixed/fixed_math.h"

#include <stdint.h>
#include <stdio.h>

#include "data/exp2_exact_points.h"

static uint64_t diff( int64_t a, int64_t b )
{
	if ( a >= b )
	{
		return (uint64_t)a - (uint64_t)b;
	}
	return (uint64_t)b - (uint64_t)a;
}

int main( void )
{
	uint64_t maxerr = 0;
	int fails = 0;
	for ( size_t i = 0; i < EXP2_EXACT_POINTS_COUNT; ++i )
	{
		int64_t raw = exp2_exact_points_raw[i];
		int64_t expected = exp2_exact_points_expected[i];
		uint64_t e = diff( fixExp2Exact( raw ), expected );
		if ( e > maxerr )
		{
			maxerr = e;
		}
		if ( e > 2 )
		{
			printf( "FAIL raw=%lld result=%lld expected=%lld error=%llu\n", (long long)raw,
					(long long)fixExp2Exact( raw ), (long long)expected, (unsigned long long)e );
			fails++;
		}
	}
	printf( "exact points fn=exp2_exact inputs=%zu maxerr=%llu\n", (size_t)EXP2_EXACT_POINTS_COUNT,
			(unsigned long long)maxerr );
	if ( fails )
	{
		printf( "exp2 exact points test failed: %d points over 2 raw\n", fails );
		return 1;
	}
	printf( "exp2 exact points test passed\n" );
	return 0;
}
