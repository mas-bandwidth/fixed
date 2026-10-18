// SPDX-License-Identifier: MIT
// fixComputeCosSinExact / fixSinExact / fixCosExact against CORE-MATH cr_sin / cr_cos, bound 2 raw.
//
// The reference, the generator and the input sets are those of test/sincos_core_math_test.c,
// reused, not copied: that file is included below with its main renamed, and its check_raw()
// evaluates whichever pair the three names fixComputeCosSin, fixSin and fixCos are pointed at
// before the include. Its own bound (107) and row printer are not used; this file prints rows
// against a bound of 2 and drives the same input sets in the same order from the same seed.
//
//   default                          the exact pair, fixComputeCosSinExact / fixSinExact / fixCosExact
//   SINCOS_EXACT_NEGATIVE_CONTROL    the 107-ulp pair, fixComputeCosSin / fixSin / fixCos: must fail
//   SINCOS_EXACT_MEASURE_PRECISE     fixComputeCosSinPrecise, a measurement only (no CTest)
//
// Then one piece more than the core-math test has: raws within 64 of odd multiples of pi/4, the
// quadrant boundaries of the exact reduction, at every octave of the quadrant count.
//
// The output hash (FNV-1a over every result of the exact pair, its own accumulator, never mixed
// into another test's) is frozen below, so the native and emulated 128-bit arms and gcc and
// clang are each held to the same bits. No clock, no network, no file.
#include "fixed/fixed_math.h"

#if defined( SINCOS_EXACT_MEASURE_PRECISE )
	#define fixComputeCosSin( raw ) fixComputeCosSinPrecise( raw )
	#define fixSin( raw ) ( fixComputeCosSinPrecise( raw ).sine )
	#define fixCos( raw ) ( fixComputeCosSinPrecise( raw ).cosine )
	#define PAIR_NAME "precise"
#elif !defined( SINCOS_EXACT_NEGATIVE_CONTROL )
	#define fixComputeCosSin( raw ) fixComputeCosSinExact( raw )
	#define fixSin( raw ) fixSinExact( raw )
	#define fixCos( raw ) fixCosExact( raw )
	#define PAIR_NAME "exact"
#else
	#define PAIR_NAME "107-ulp (negative control)"
#endif

#define main sincos_core_math_main
#include "sincos_core_math_test.c"
#undef main

#define EXACT_BOUND 2

static uint64_t exact_fnv = 0xCBF29CE484222325ULL;

static void exact_mix( int64_t v )
{
	for ( int i = 0; i < 8; i++ )
	{
		exact_fnv ^= (uint64_t)( v >> ( 8 * i ) ) & 0xFF;
		exact_fnv *= 0x00000100000001B3ULL;
	}
}

static void exact_check( int64_t raw, row* r )
{
	check_raw( raw, r );
	int64_t x = exact_raw( raw );
	fixCosSin cs = fixComputeCosSin( x );
	exact_mix( cs.cosine );
	exact_mix( cs.sine );
}

static void exact_row( const char* lo, const char* hi, row* r )
{
	printf( "core-math row=sincosexact[%s,%s) inputs=%lld maxulp=%lld bound=%d worst=%lld\n", lo, hi,
			(long long)r->inputs, (long long)r->maxulp, EXACT_BOUND, (long long)r->worst );
	if ( r->maxulp > EXACT_BOUND )
	{
		fails++;
	}
}

int main( void )
{
	printf( "pair: %s\n", PAIR_NAME );

	// (a) every 17th raw below 32 rad.
	{
		row r = { 0, 0, 0 };
		const int64_t limit = (int64_t)32 << 16;
		for ( int64_t raw = -limit + 1; raw < limit; raw += 17 )
		{
			exact_check( raw, &r );
		}
		exact_row( "-32", "32", &r );
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
			exact_check( raw, &r );
		}
		char lo[32], hi[32];
		snprintf( lo, sizeof lo, "%llu", (unsigned long long)( (uint64_t)1 << e ) );
		snprintf( hi, sizeof hi, "%llu", (unsigned long long)( (uint64_t)1 << ( e + 1 ) ) );
		exact_row( lo, hi, &r );
	}

	// (c) hard cases: near multiples of 2*pi and the edge of the 32 rad split.
	{
		row r = { 0, 0, 0 };
		int64_t min_raw = 0, max_raw = 0;
		int have = 0;
#define EXACT_TRACK( v )                                                                                               \
	do                                                                                                                 \
	{                                                                                                                  \
		int64_t tx = exact_raw( v );                                                                                   \
		if ( !have || tx < min_raw ) min_raw = tx;                                                                     \
		if ( !have || tx > max_raw ) max_raw = tx;                                                                     \
		have = 1;                                                                                                      \
	} while ( 0 )

		for ( int k = 2; k <= 43; k++ )
		{
			int64_t center = center_for_n( (unsigned long long)1 << k );
			for ( int d = -64; d <= 64; d++ )
			{
				int64_t raws[2] = { center + d, -( center + d ) };
				for ( int s = 0; s < 2; s++ )
				{
					exact_check( raws[s], &r );
					EXACT_TRACK( raws[s] );
				}
			}
		}
		const int64_t nvals[5] = { 1, 2, 3, 5, 7 };
		for ( int i = 0; i < 5; i++ )
		{
			int64_t center = center_for_n( (unsigned long long)nvals[i] );
			for ( int d = -64; d <= 64; d++ )
			{
				int64_t raws[2] = { center + d, -( center + d ) };
				for ( int s = 0; s < 2; s++ )
				{
					exact_check( raws[s], &r );
					EXACT_TRACK( raws[s] );
				}
			}
		}
		const int64_t edge = (int64_t)32 << 16;
		for ( int64_t raw = edge - 1024; raw <= edge + 1024; raw++ )
		{
			int64_t raws[2] = { raw, -raw };
			for ( int s = 0; s < 2; s++ )
			{
				exact_check( raws[s], &r );
				EXACT_TRACK( raws[s] );
			}
		}
		char lo[64], hi[64];
		snprintf( lo, sizeof lo, "%lld", (long long)( min_raw / 65536 ) );
		snprintf( hi, sizeof hi, "%lld", (long long)( max_raw / 65536 ) );
		exact_row( lo, hi, &r );
	}

	// (d) the largest double-exact raws below +-INT64_MAX.
	{
		row r = { 0, 0, 0 };
		int64_t largest = (int64_t)( (uint64_t)FIX_MAX & ~(uint64_t)1023 );
		exact_check( largest, &r );
		exact_check( -largest, &r );
		char lo[64], hi[64];
		snprintf( lo, sizeof lo, "%lld", (long long)( -largest / 65536 ) );
		snprintf( hi, sizeof hi, "%lld", (long long)( largest / 65536 ) );
		exact_row( lo, hi, &r );
	}

	// (e) quadrant boundaries: within 64 raws of j * pi/4 for odd j = 2^e - 1 and 2^e + 1,
	// e = 1..47, either sign. The center is round( j * round( pi/2 * 2^62 ) / 2^47 ), in the
	// compiler's 128-bit integers (the test needs them already); it is within one raw of
	// j * pi/4 * 2^16 and the +-64 window covers that.
	{
		row r = { 0, 0, 0 };
		int64_t max_raw = 0;
		const __int128 halfPi62 = (__int128)INT64_C( 7244019458077122842 );
		for ( int e = 1; e <= 47; e++ )
		{
			for ( int side = -1; side <= 1; side += 2 )
			{
				__int128 j = ( (__int128)1 << e ) + side;
				__int128 c128 = ( j * halfPi62 + ( (__int128)1 << 46 ) ) >> 47;
				if ( c128 + 64 > (__int128)FIX_MAX )
				{
					continue;
				}
				int64_t center = (int64_t)c128;
				for ( int d = -64; d <= 64; d++ )
				{
					int64_t raws[2] = { center + d, -( center + d ) };
					for ( int s = 0; s < 2; s++ )
					{
						exact_check( raws[s], &r );
						int64_t tx = exact_raw( raws[s] );
						if ( tx > max_raw ) max_raw = tx;
					}
				}
			}
		}
		char lo[64], hi[64];
		snprintf( lo, sizeof lo, "%lld", (long long)( -max_raw / 65536 ) );
		snprintf( hi, sizeof hi, "%lld", (long long)( max_raw / 65536 ) );
		exact_row( lo, hi, &r );
	}

	printf( "sincos exact hash=0x%016llx\n", (unsigned long long)exact_fnv );
	if ( fails != 0 || global_maxulp > EXACT_BOUND )
	{
		printf( "sincos exact FAIL raw=%lld maxulp=%lld bound=%d sin=%lld refsin=%lld cos=%lld refcos=%lld\n",
				(long long)global_worst, (long long)global_maxulp, EXACT_BOUND, (long long)global_sin_lib,
				(long long)global_sin_ref, (long long)global_cos_lib, (long long)global_cos_ref );
		return 1;
	}
#if !defined( SINCOS_EXACT_NEGATIVE_CONTROL ) && !defined( SINCOS_EXACT_MEASURE_PRECISE )
	// Checked after the bound, so an accuracy failure reports its worst input first.
	// FROZEN 2026-10-18 (fixed-sincos-exact): identical from gcc and clang, native and emulated
	// 128-bit, on x86-64 Linux. A different value is a determinism finding, not a re-freeze.
	if ( exact_fnv != 0x756298aa4e7967aaULL )
	{
		printf( "sincos exact FAIL determinism hash 0x%016llx, frozen 0x756298aa4e7967aa\n", (unsigned long long)exact_fnv );
		return 1;
	}
#endif
	printf( "sincos exact test passed\n" );
	return 0;
}
