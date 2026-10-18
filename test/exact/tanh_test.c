/* SPDX-License-Identifier: MIT */
/* Fast points test for fixTanhExact, with no MPFR.
 *
 * The expected integers are RN( tanh( raw * 2^-16 ) * 2^16 ) computed by a throwaway
 * 256-bit MPFR program, round to nearest-even; the committed test holds the integers
 * only. Named cases are the design's section 5 set (1860, 1861, 408834, 408835,
 * INT64_MIN and the margin raw 144246) plus 64 raws spread over |raw| <= 408834.
 * Every point is also checked for exact oddness.
 */
#include "fixed/fixed_tanh.h"

#include <stdint.h>
#include <stdio.h>

static int failures = 0;

static void check_pair( int64_t raw, int64_t expected )
{
	int64_t result = fixTanhExact( (fixed_t)raw );
	if ( result != expected )
	{
		printf( "FAIL raw=%lld result=%lld expected=%lld\n", (long long)raw, (long long)result,
				(long long)expected );
		failures++;
	}

	if ( raw != INT64_MIN )
	{
		int64_t negated = fixTanhExact( (fixed_t)( -raw ) );
		if ( negated != -result )
		{
			printf( "FAIL oddness raw=%lld result=%lld negated=%lld\n", (long long)raw, (long long)result,
					(long long)negated );
			failures++;
		}
	}
}

int main( void )
{
	static const struct
	{
		int64_t raw;
		int64_t expected;
	} named[] = {
		{ 0, 0 },
		{ 1860, 1860 },
		{ 1861, 1860 },
		{ 408834, 65535 },
		{ 408835, 65536 },
		{ INT64_MIN, -65536 },
		{ 144246, 63950 },
	};

	for ( size_t i = 0; i < sizeof( named ) / sizeof( named[0] ); ++i )
	{
		check_pair( named[i].raw, named[i].expected );
	}

	static const struct
	{
		int64_t raw;
		int64_t expected;
	} spread[64] = {
		{ 0LL, 0LL },
		{ -6489LL, -6468LL },
		{ 12978LL, 12811LL },
		{ -19468LL, -18915LL },
		{ 25957LL, 24680LL },
		{ -32447LL, -30032LL },
		{ 38936LL, 34921LL },
		{ -45426LL, -39322LL },
		{ 51915LL, 43230LL },
		{ -58404LL, -46660LL },
		{ 64894LL, 49640LL },
		{ -71383LL, -52205LL },
		{ 77873LL, 54397LL },
		{ -84362LL, -56257LL },
		{ 90852LL, 57826LL },
		{ -97341LL, -59143LL },
		{ 103830LL, 60246LL },
		{ -110320LL, -61164LL },
		{ 116809LL, 61928LL },
		{ -123299LL, -62562LL },
		{ 129788LL, 63086LL },
		{ -136278LL, -63519LL },
		{ 142767LL, 63877LL },
		{ -149256LL, -64172LL },
		{ 155746LL, 64415LL },
		{ -162235LL, -64615LL },
		{ 168725LL, 64780LL },
		{ -175214LL, -64915LL },
		{ 181704LL, 65026LL },
		{ -188193LL, -65117LL },
		{ 194682LL, 65192LL },
		{ -201172LL, -65254LL },
		{ 207661LL, 65305LL },
		{ -214151LL, -65346LL },
		{ 220640LL, 65380LL },
		{ -227130LL, -65408LL },
		{ 233619LL, 65431LL },
		{ -240108LL, -65450LL },
		{ 246598LL, 65465LL },
		{ -253087LL, -65478LL },
		{ 259577LL, 65488LL },
		{ -266066LL, -65497LL },
		{ 272556LL, 65504LL },
		{ -279045LL, -65510LL },
		{ 285534LL, 65514LL },
		{ -292024LL, -65518LL },
		{ 298513LL, 65522LL },
		{ -305003LL, -65524LL },
		{ 311492LL, 65526LL },
		{ -317982LL, -65528LL },
		{ 324471LL, 65529LL },
		{ -330960LL, -65531LL },
		{ 337450LL, 65532LL },
		{ -343939LL, -65532LL },
		{ 350429LL, 65533LL },
		{ -356918LL, -65534LL },
		{ 363408LL, 65534LL },
		{ -369897LL, -65534LL },
		{ 376386LL, 65535LL },
		{ -382876LL, -65535LL },
		{ 389365LL, 65535LL },
		{ -395855LL, -65535LL },
		{ 402344LL, 65535LL },
		{ -408834LL, -65535LL },
	};

	for ( size_t i = 0; i < sizeof( spread ) / sizeof( spread[0] ); ++i )
	{
		check_pair( spread[i].raw, spread[i].expected );
	}

	if ( failures != 0 )
	{
		printf( "tanh exact test failed: %d\n", failures );
		return 1;
	}
	printf( "tanh exact test passed\n" );
	return 0;
}
