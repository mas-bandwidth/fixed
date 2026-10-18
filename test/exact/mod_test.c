/* SPDX-License-Identifier: MIT */
/* Exact points and boundary test for fixMod and fixFmod, with no MPFR.
 *
 * data/mod_points.h holds a generated grid of (a, b) pairs over the Q48.16 boundary
 * classes with their independently computed truncated (fmod) and floored (mod)
 * remainders. Both functions must match bit for bit. The out-of-domain and trap
 * classes are checked too: b == 0 returns 0 and a == INT64_MIN with b == -1 returns 0.
 */
#include "fixed/fixed_mod.h"

#include <stdint.h>
#include <stdio.h>

#include "data/mod_points.h"

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
	int64_t worst_a = 0;
	int64_t worst_b = 0;
	int64_t worst_r = 0;
	int64_t worst_exp = 0;
	for ( size_t i = 0; i < MOD_POINTS_COUNT; ++i )
	{
		int64_t a = mod_points_a[i];
		int64_t b = mod_points_b[i];
		int64_t ef = mod_points_fmod[i];
		int64_t em = mod_points_mod[i];
		int64_t rf = fixFmod( a, b );
		int64_t rm = fixMod( a, b );
		uint64_t df = diff( rf, ef );
		uint64_t dm = diff( rm, em );
		uint64_t e = df > dm ? df : dm;
		if ( e > maxerr )
		{
			maxerr = e;
		}
		if ( e != 0 )
		{
			printf( "FAIL a=%lld b=%lld fmod=%lld/%lld mod=%lld/%lld error=%llu\n", (long long)a, (long long)b,
					(long long)rf, (long long)ef, (long long)rm, (long long)em, (unsigned long long)e );
			fails++;
			if ( fails == 1 )
			{
				worst_a = a;
				worst_b = b;
				worst_r = rf;
				worst_exp = ef;
			}
		}
	}

	if ( fixFmod( 12345, 0 ) != 0 || fixMod( 12345, 0 ) != 0 )
	{
		printf( "FAIL b=0 must return 0\n" );
		fails++;
	}
	if ( fixFmod( (int64_t)INT64_MIN, -1 ) != 0 || fixMod( (int64_t)INT64_MIN, -1 ) != 0 )
	{
		printf( "FAIL a=INT64_MIN b=-1 must return 0, never SIGFPE\n" );
		fails++;
	}
	if ( fixFmod( (int64_t)INT64_MIN, 1 ) != 0 || fixMod( (int64_t)INT64_MIN, 1 ) != 0 )
	{
		printf( "FAIL INT64_MIN % 1 must return 0\n" );
		fails++;
	}
	if ( fixFmod( (int64_t)INT64_MAX, -1 ) != 0 || fixMod( (int64_t)INT64_MAX, -1 ) != 0 )
	{
		printf( "FAIL INT64_MAX % -1 must return 0\n" );
		fails++;
	}

	printf( "exact points fn=mod inputs=%zu maxerr=%llu\n", (size_t)MOD_POINTS_COUNT, (unsigned long long)maxerr );
	if ( fails )
	{
		printf( "mod exact test failed: worst a=%lld b=%lld fmod=%lld expected=%lld\n", (long long)worst_a,
				(long long)worst_b, (long long)worst_r, (long long)worst_exp );
		return 1;
	}
	printf( "mod exact test passed\n" );
	return 0;
}
