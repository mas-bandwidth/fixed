/* SPDX-License-Identifier: MIT */
/* fixSinExact / fixCosExact on the points emitted by `fixed_sweep --fn sincos_big
 * --emit-points`: every BaCSeL sin_big/cos_big hard case, the libfixmath edges, 4096 seeded
 * draws over the sampled binades, and the 256 worst sweep inputs. The emitted header carries
 * cos angles negated; decode() restores the exact Q48.16 angle before the call.
 *
 * The bound is the documented 2 raw for both functions. No MPFR, no clock, no file beyond the
 * generated header.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "fixed/fixed_math.h"
#include "data/sincos_hard_cases.h"

#define EXACT_BOUND 2

static uint64_t absdiff( int64_t a, int64_t b )
{
	if ( a >= b )
	{
		return (uint64_t)a - (uint64_t)b;
	}
	return (uint64_t)b - (uint64_t)a;
}

int main( void )
{
	uint64_t sin_max = 0;
	uint64_t cos_max = 0;
	uint64_t sin_notcr = 0;
	uint64_t cos_notcr = 0;
	uint64_t sin_inputs = 0;
	uint64_t cos_inputs = 0;
	int failed = 0;

	for ( size_t i = 0; i < SINCOS_BIG_POINTS_COUNT; ++i )
	{
		int64_t carrier = sincos_big_points_raw[i];
		int is_cos = carrier < 0;
		int64_t raw = is_cos ? -carrier : carrier;
		int64_t got = is_cos ? fixCosExact( raw ) : fixSinExact( raw );
		uint64_t err = absdiff( got, sincos_big_points_expected[i] );

		if ( is_cos )
		{
			cos_inputs++;
			if ( err > cos_max )
			{
				cos_max = err;
			}
			if ( got != sincos_big_points_expected[i] )
			{
				cos_notcr++;
			}
		}
		else
		{
			sin_inputs++;
			if ( err > sin_max )
			{
				sin_max = err;
			}
			if ( got != sincos_big_points_expected[i] )
			{
				sin_notcr++;
			}
		}

		if ( err > EXACT_BOUND )
		{
			printf( "sincos hard case OVER BOUND fn=%s raw=%lld got=%lld expected=%lld err=%llu bound=%d\n",
					is_cos ? "cos_big" : "sin_big", (long long)raw, (long long)got, (long long)sincos_big_points_expected[i],
					(unsigned long long)err, EXACT_BOUND );
			failed = 1;
		}
	}

	printf( "exact points fn=sin_big inputs=%llu maxerr=%llu notcr=%llu\n", (unsigned long long)sin_inputs,
			(unsigned long long)sin_max, (unsigned long long)sin_notcr );
	printf( "exact points fn=cos_big inputs=%llu maxerr=%llu notcr=%llu\n", (unsigned long long)cos_inputs,
			(unsigned long long)cos_max, (unsigned long long)cos_notcr );

	if ( failed || sin_max > EXACT_BOUND || cos_max > EXACT_BOUND )
	{
		printf( "sincos hard cases test FAIL\n" );
		return 1;
	}
	printf( "sincos hard cases test passed\n" );
	return 0;
}
