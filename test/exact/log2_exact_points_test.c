/* SPDX-License-Identifier: MIT */
/* Fast exact points test for fixLog2Exact, with no MPFR.
 *
 * The header is written by `fixed_sweep --fn log2_exact --emit-points`: every libfixmath
 * edge, 4096 seeded draws and the 256 worst inputs of the MPFR sweep, each with its
 * correctly rounded raw output. fixLog2Exact is correctly rounded by design, so every
 * point must match bit for bit: any nonzero error is a failure.
 */
#include "fixed/fixed_log.h"

#include <stdint.h>
#include <stdio.h>

#include "data/log2_exact_points.h"

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
	for ( size_t i = 0; i < LOG2_EXACT_POINTS_COUNT; ++i )
	{
		int64_t raw = log2_exact_points_raw[i];
		int64_t expected = log2_exact_points_expected[i];
		int64_t result = fixLog2Exact( raw );
		uint64_t e = diff( result, expected );
		if ( e > maxerr )
		{
			maxerr = e;
		}
		if ( result != expected )
		{
			if ( fails == 0 )
			{
				printf( "FAIL raw=%lld result=%lld expected=%lld error=%llu\n", (long long)raw,
						(long long)result, (long long)expected, (unsigned long long)e );
			}
			fails++;
		}
	}
	printf( "exact points fn=log2_exact inputs=%zu maxerr=%llu\n", (size_t)LOG2_EXACT_POINTS_COUNT,
			(unsigned long long)maxerr );
	if ( fails )
	{
		printf( "log2_exact points test failed: %d points differ\n", fails );
		return 1;
	}
	printf( "log2_exact points test passed\n" );
	return 0;
}
