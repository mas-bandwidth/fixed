// SPDX-License-Identifier: MIT
// fixComputeCosSin / fixSin / fixCos against CORE-MATH cr_sin / cr_cos.
//
// The reference is the correctly-rounded binary64 sine and cosine of the exact double value of
// the raw Q48.16 angle, scaled back by 2^16 (exact) and rounded to nearest-even by llrint in the
// default rounding mode. A raw at or above 2^53 in magnitude is first cleared in its low bits so
// that the angle is exactly representable as a double on both sides of the comparison.
//
// CORE-MATH is vendored in extern/core-math, pinned at
// cac142bc65dc140d645ba3f79511a9c591d1746e. It is a TEST-ONLY reference; the library never
// links it. With SINCOS_NEGATIVE_CONTROL the test feeds fixUnwindAngle( raw ) through the
// library, reproducing the old single-word reduction at every angle. That build must fail.
#include "fixed/fixed_math.h"
#include <stdint.h>
#include <stdio.h>
#include <math.h>

double cr_sin( double x );
double cr_cos( double x );

#define BOUND 107

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

// Leave at most 53 significant bits, so the raw value converts to binary64 exactly.
static int64_t exact_raw( int64_t raw )
{
	if ( raw == 0 )
	{
		return 0;
	}
	uint64_t mag = raw < 0 ? (uint64_t)( -raw ) : (uint64_t)raw;
	int bits = 64 - __builtin_clzll( mag );
	if ( bits > 53 )
	{
		int shift = bits - 53;
		mag &= ~( ( (uint64_t)1 << shift ) - 1 );
	}
	return raw < 0 ? -(int64_t)mag : (int64_t)mag;
}

#ifdef SINCOS_NEGATIVE_CONTROL
#define COMPUTE_CS( raw ) fixComputeCosSin( fixUnwindAngle( raw ) )
#define COMPUTE_SIN( raw ) fixSin( fixUnwindAngle( raw ) )
#define COMPUTE_COS( raw ) fixCos( fixUnwindAngle( raw ) )
#else
#define COMPUTE_CS( raw ) fixComputeCosSin( raw )
#define COMPUTE_SIN( raw ) fixSin( raw )
#define COMPUTE_COS( raw ) fixCos( raw )
#endif

typedef struct row
{
	int64_t inputs;
	int64_t maxulp;
	int64_t worst;
} row;

static int fails = 0;
static int64_t global_maxulp = 0;
static int64_t global_worst = 0;
static int64_t global_sin_lib = 0, global_sin_ref = 0;
static int64_t global_cos_lib = 0, global_cos_ref = 0;

static int64_t diff( int64_t a, int64_t b )
{
	return a > b ? a - b : b - a;
}

static void check_raw( int64_t raw_in, row* r )
{
	int64_t raw = exact_raw( raw_in );
	double a = (double)raw / 65536.0;
	int64_t ref_cos = llrint( cr_cos( a ) * 65536.0 );
	int64_t ref_sin = llrint( cr_sin( a ) * 65536.0 );

	fixCosSin cs = COMPUTE_CS( raw );
	int64_t sin_lib = COMPUTE_SIN( raw );
	int64_t cos_lib = COMPUTE_COS( raw );

	int64_t dc = diff( cs.cosine, ref_cos );
	int64_t ds = diff( cs.sine, ref_sin );
	int64_t dsc = diff( cos_lib, ref_cos );
	int64_t dss = diff( sin_lib, ref_sin );
	int64_t ulp = dc;
	if ( ds > ulp ) ulp = ds;
	if ( dsc > ulp ) ulp = dsc;
	if ( dss > ulp ) ulp = dss;

	r->inputs++;
	if ( ulp > r->maxulp )
	{
		r->maxulp = ulp;
		r->worst = raw;
	}
	if ( ulp > global_maxulp )
	{
		global_maxulp = ulp;
		global_worst = raw;
		global_cos_lib = cs.cosine;
		global_cos_ref = ref_cos;
		global_sin_lib = cs.sine;
		global_sin_ref = ref_sin;
	}
}

static void print_row( const char* lo, const char* hi, row* r )
{
	printf( "core-math row=sincos[%s,%s) inputs=%lld maxulp=%lld bound=%d worst=%lld\n", lo, hi,
			(long long)r->inputs, (long long)r->maxulp, BOUND, (long long)r->worst );
	if ( r->maxulp > BOUND )
	{
		fails++;
	}
}

int main( void )
{
	// (a) every 17th raw below 32 rad: the range the fix leaves untouched.
	{
		row r = { 0, 0, 0 };
		const int64_t limit = (int64_t)32 << 16;
		for ( int64_t raw = -limit + 1; raw < limit; raw += 17 )
		{
			check_raw( raw, &r );
		}
		print_row( "-32", "32", &r );
	}

	// (b) each octave of |x| in rad, 16384 random raws of either sign.
	for ( int e = 5; e <= 46; e++ )
	{
		int64_t lo_raw = (int64_t)1 << ( e + 16 );
		int64_t hi_raw = e <= 45 ? ( ( (int64_t)1 << ( e + 17 ) ) - 1 ) : FIX_MAX;
		uint64_t span = (uint64_t)( hi_raw - lo_raw ) + 1;
		row r = { 0, 0, 0 };
		for ( int i = 0; i < 16384; i++ )
		{
			uint64_t u = rng_next();
			int64_t raw = lo_raw + (int64_t)( u % span );
			if ( u & ( (uint64_t)1 << 63 ) )
			{
				raw = -raw;
			}
			check_raw( raw, &r );
		}
		char lo[32], hi[32];
		snprintf( lo, sizeof lo, "%llu", (unsigned long long)( (uint64_t)1 << e ) );
		snprintf( hi, sizeof hi, "%llu", (unsigned long long)( (uint64_t)1 << ( e + 1 ) ) );
		print_row( lo, hi, &r );
	}

	// (c) hard cases: near multiples of 2*pi and the edge of the unchanged range.
	{
		row r = { 0, 0, 0 };
		int64_t min_raw = 0, max_raw = 0;
		int have = 0;
		const long double twoPi = 2.0L * 3.141592653589793238462643383279502884L;

		for ( int k = 0; k <= 43; k++ )
		{
			int duplicate = k <= 1; // 2^0 and 2^1 are already in the short list
			if ( duplicate )
			{
				continue;
			}
			long double n = (long double)( (uint64_t)1 << k );
			int64_t center = (int64_t)llrintl( n * twoPi * 65536.0L );
			for ( int d = -64; d <= 64; d++ )
			{
				int64_t raws[2] = { center + d, -( center + d ) };
				for ( int s = 0; s < 2; s++ )
				{
					check_raw( raws[s], &r );
					int64_t x = exact_raw( raws[s] );
					if ( !have || x < min_raw ) min_raw = x;
					if ( !have || x > max_raw ) max_raw = x;
					have = 1;
				}
			}
		}
		const int64_t nvals[5] = { 1, 2, 3, 5, 7 };
		for ( int i = 0; i < 5; i++ )
		{
			int64_t center = (int64_t)llrintl( (long double)nvals[i] * twoPi * 65536.0L );
			for ( int d = -64; d <= 64; d++ )
			{
				int64_t raws[2] = { center + d, -( center + d ) };
				for ( int s = 0; s < 2; s++ )
				{
					check_raw( raws[s], &r );
					int64_t x = exact_raw( raws[s] );
					if ( !have || x < min_raw ) min_raw = x;
					if ( !have || x > max_raw ) max_raw = x;
					have = 1;
				}
			}
		}
		const int64_t edge = (int64_t)32 << 16;
		for ( int64_t raw = edge - 1024; raw <= edge + 1024; raw++ )
		{
			int64_t raws[2] = { raw, -raw };
			for ( int s = 0; s < 2; s++ )
			{
				check_raw( raws[s], &r );
				int64_t x = exact_raw( raws[s] );
				if ( !have || x < min_raw ) min_raw = x;
				if ( !have || x > max_raw ) max_raw = x;
				have = 1;
			}
		}
		char lo[64], hi[64];
		snprintf( lo, sizeof lo, "%lld", (long long)( min_raw / 65536 ) );
		snprintf( hi, sizeof hi, "%lld", (long long)( max_raw / 65536 ) );
		print_row( lo, hi, &r );
	}

	// (d) the largest double-exact raws below +-INT64_MAX. INT64_MIN is another card's case.
	{
		row r = { 0, 0, 0 };
		int64_t largest = (int64_t)( (uint64_t)FIX_MAX & ~(uint64_t)1023 );
		check_raw( largest, &r );
		check_raw( -largest, &r );
		char lo[64];
		snprintf( lo, sizeof lo, "%lld", (long long)( -largest / 65536 ) );
		char hi[64];
		snprintf( hi, sizeof hi, "%lld", (long long)( largest / 65536 ) );
		print_row( lo, hi, &r );
	}

	if ( fails != 0 || global_maxulp > BOUND )
	{
		printf( "core-math FAIL raw=%lld maxulp=%lld bound=%d sin=%lld refsin=%lld cos=%lld refcos=%lld\n",
				(long long)global_worst, (long long)global_maxulp, BOUND, (long long)global_sin_lib,
				(long long)global_sin_ref, (long long)global_cos_lib, (long long)global_cos_ref );
		return 1;
	}
	printf( "sincos core-math test passed\n" );
	return 0;
}
