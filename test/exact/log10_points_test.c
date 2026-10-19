/* SPDX-License-Identifier: MIT */
/* Fast points test for fixLog10Exact, with no MPFR.
 *
 * The header is written by `fixed_sweep --fn log10 --emit-points`: every libfixmath
 * edge, 4096 seeded draws and the 256 worst inputs of the MPFR sweep, each with its
 * correctly rounded raw output. fixLog10Exact is correctly rounded, so every point must
 * match bit for bit.
 */
#include "fixed/fixed_log.h"

#include <stdint.h>
#include <stdio.h>

#include "data/log10_points.h"

int main( void )
{
	uint64_t maxerr = 0;
	int fails = 0;
	for ( size_t i = 0; i < LOG10_POINTS_COUNT; ++i )
	{
		int64_t raw = log10_points_raw[i];
		int64_t expected = log10_points_expected[i];
		int64_t got = fixLog10Exact( raw );
		uint64_t e = got >= expected ? (uint64_t)got - (uint64_t)expected : (uint64_t)expected - (uint64_t)got;
		if ( e > maxerr )
		{
			maxerr = e;
		}
		if ( e != 0 )
		{
			if ( fails == 0 )
			{
				printf( "FAIL raw=%lld result=%lld expected=%lld error=%llu\n", (long long)raw, (long long)got,
						(long long)expected, (unsigned long long)e );
			}
			fails++;
		}
	}
	printf( "exact points fn=log10 inputs=%zu maxerr=%llu\n", (size_t)LOG10_POINTS_COUNT, (unsigned long long)maxerr );
	if ( fails )
	{
		printf( "log10 points test failed: %d points differ\n", fails );
		return 1;
	}
	printf( "log10 points test passed\n" );
	return 0;
}
