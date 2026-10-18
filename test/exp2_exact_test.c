// SPDX-License-Identifier: MIT
// fixExp2Exact against CORE-MATH cr_exp2.
//
// The reference is the correctly-rounded binary64 exp2 of the exact double value of the raw
// Q48.16 argument, scaled back by 2^16 (exact) and rounded to nearest-even by llrint in the
// default rounding mode. Below 37 rad the result is below 2^53 raw, where that reference is
// exact to far under a raw unit, so the bound is 2 raw. At and above 37 rad the double
// reference is coarser than a raw unit, so there the bound is 2 raw plus half the spacing of
// the double reference at that value.
//
// CORE-MATH is vendored in extern/core-math/exp2, pinned at
// cac142bc65dc140d645ba3f79511a9c591d1746e. It is a TEST-ONLY reference; the library never
// links it. With EXP2_NEGATIVE_CONTROL the test evaluates fixExp2 in place of fixExp2Exact;
// that build must fail.
#include "fixed/fixed_math.h"
#include <stdint.h>
#include <stdio.h>
#include <math.h>

double cr_exp2( double x );

#define BOUND 2
#define RAD ( 1LL << 16 )

static uint64_t rng_state = 0x9E3779B97F4A7C15ULL;

static uint64_t rng_next( void )
{
	uint64_t x = rng_state;
	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	rng_state = x;
	return x;
}

#ifdef EXP2_NEGATIVE_CONTROL
#define COMPUTE( raw ) fixExp2( raw )
#else
#define COMPUTE( raw ) fixExp2Exact( raw )
#endif

typedef struct row
{
	int64_t inputs;
	int64_t maxulp;
	int64_t worst;
	int64_t bound; // largest allowed bound used across the inputs of this piece
} row;

static int fails = 0;

static int64_t diff( int64_t a, int64_t b )
{
	return a > b ? a - b : b - a;
}

static void check_raw( int64_t raw, row* r )
{
	double d = cr_exp2( (double)raw / 65536.0 ) * 65536.0;
	int64_t ref = llrint( d );
	int64_t lib = COMPUTE( raw );
	int64_t u = diff( lib, ref );

	int64_t bound = BOUND;
	if ( (uint64_t)raw >= ( (uint64_t)37 << 16 ) )
	{
		// At and above 37 rad the double reference carries its own rounding error: half the
		// ulp spacing of the reference value at this raw.
		double ulp = fabs( nextafter( d, HUGE_VAL ) - d );
		bound = BOUND + (int64_t)( 0.5 * ulp + 0.999999 );
	}
	if ( bound > r->bound )
	{
		r->bound = bound;
	}

	r->inputs++;
	if ( u > r->maxulp )
	{
		r->maxulp = u;
		r->worst = raw;
	}
	if ( u > bound )
	{
		fails++;
		if ( fails == 1 )
		{
			printf( "exp2 exact FAIL raw=%lld lib=%lld ref=%lld ulp=%lld bound=%lld\n",
					(long long)raw, (long long)lib, (long long)ref, (long long)u, (long long)bound );
		}
	}
}

static void check_sat( int64_t raw )
{
	if ( COMPUTE( raw ) != INT64_MAX )
	{
		fails++;
		printf( "exp2 exact FAIL saturate raw=%lld\n", (long long)raw );
	}
}

static void print_row( const char* lo, const char* hi, row* r )
{
	printf( "core-math row=exp2exact[%s,%s) inputs=%lld maxulp=%lld bound=2 worst=%lld\n",
			lo, hi, (long long)r->inputs, (long long)r->maxulp, (long long)r->worst );
	if ( r->maxulp > r->bound )
	{
		fails++;
	}
}

static void random_piece( const char* lo, const char* hi, int64_t lo_rad, int64_t hi_rad, int n )
{
	row r = { 0, 0, 0, 2 };
	int64_t lo_raw = lo_rad << 16;
	int64_t hi_raw = ( hi_rad << 16 ) - 1;
	uint64_t span = (uint64_t)( hi_raw - lo_raw ) + 1;
	for ( int i = 0; i < n; i++ )
	{
		check_raw( lo_raw + (int64_t)( rng_next() % span ), &r );
	}
	print_row( lo, hi, &r );
}

int main( void )
{
	// every raw in [-17, 16) rad, split so the negative control reports [8, 16) separately.
	{
		row r = { 0, 0, 0, 2 };
		for ( int64_t raw = -17 * RAD; raw < 8 * RAD; raw++ )
		{
			check_raw( raw, &r );
		}
		print_row( "-17", "8", &r );
	}
	{
		row r = { 0, 0, 0, 2 };
		for ( int64_t raw = 8 * RAD; raw < 16 * RAD; raw++ )
		{
			check_raw( raw, &r );
		}
		print_row( "8", "16", &r );
	}

	random_piece( "16", "24", 16, 24, 1 << 18 );
	random_piece( "24", "32", 24, 32, 1 << 18 );
	random_piece( "32", "37", 32, 37, 1 << 18 );
	random_piece( "37", "44", 37, 44, 1 << 18 );
	random_piece( "44", "47", 44, 47, 1 << 18 );

	// the 4096 raws just below 47 rad.
	{
		row r = { 0, 0, 0, 2 };
		for ( int64_t raw = 47 * RAD - 4096; raw < 47 * RAD; raw++ )
		{
			check_raw( raw, &r );
		}
		print_row( "47", "47", &r );
	}

	// every raw within 64 of -17 rad.
	{
		row r = { 0, 0, 0, 2 };
		for ( int64_t raw = -17 * RAD - 64; raw <= -17 * RAD + 64; raw++ )
		{
			check_raw( raw, &r );
		}
		print_row( "-17", "-17", &r );
	}

	// saturation at and above 47 rad.
	check_sat( 47 * RAD );
	check_sat( 47 * RAD + 1 );
	check_sat( INT64_MAX );

	if ( fails != 0 )
	{
		printf( "exp2 exact test FAILED\n" );
		return 1;
	}
	printf( "exp2 exact test passed\n" );
	return 0;
}
