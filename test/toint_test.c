// SPDX-License-Identifier: MIT
// Q48.16 -> int32 conversions and Floor/Ceil at the 64-bit edges.
//
// The library converts a raw Q48.16 value to a 32-bit integer with truncation
// toward zero (fixTruncToInt), floor (fixFloorToInt) or round-half-up toward
// +inf (fixRoundToInt), and exposes fixFloor / fixCeilSat on the raw itself.
// Before this card the int32 casts wrapped silently above +/-2^31 (truncating
// the high bits of the int64), and fixCeil returned INT64_MIN at INT64_MAX.
// Every result here is compared against an independent __int128 reference that
// clamps to INT32_MIN/INT32_MAX (or FIX_MAX for ceil) and rounds exactly the
// way the library does. The CTest negative control (TOINT_NEGATIVE_CONTROL)
// applies the old wrapping casts verbatim and must fail (WILL_FAIL).
#include "fixed/fixed_math.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef __int128 Ref128;

static void mismatch( const char* f, int64_t raw, long long got, long long exp )
{
	fprintf( stderr, "toint mismatch %s raw=%lld got=%lld expected=%lld\n", f,
			(long long)raw, got, exp );
	exit( 1 );
}

#ifdef TOINT_NEGATIVE_CONTROL
#define TRUNC( a )  ( (int)( (a) / FIX_ONE ) )
#define FLOORI( a ) ( (int)( (a) >> FIX_FRACTION_BITS ) )
#define ROUNDI( a ) ( (int)( ( (fixed_t)( (uint64_t)(a) + FIX_HALF ) ) >> FIX_FRACTION_BITS ) )
#define CEILR( a )  ( (fixed_t)( (uint64_t)(a) + FIX_ONE - 1 ) & ~( FIX_ONE - 1 ) )
#else
#define TRUNC( a )  fixTruncToInt( a )
#define FLOORI( a ) fixFloorToInt( a )
#define ROUNDI( a ) fixRoundToInt( a )
#define CEILR( a )  fixCeilSat( a )
#endif

static int ref_trunc( int64_t a )
{
	Ref128 q = (Ref128)a / FIX_ONE;
	if ( q > INT32_MAX ) return INT32_MAX;
	if ( q < INT32_MIN ) return INT32_MIN;
	return (int)q;
}

static int ref_floor( int64_t a )
{
	Ref128 q = (Ref128)a / FIX_ONE;
	Ref128 r = (Ref128)a % FIX_ONE;
	if ( r != 0 && a < 0 ) q--;
	if ( q > INT32_MAX ) return INT32_MAX;
	if ( q < INT32_MIN ) return INT32_MIN;
	return (int)q;
}

static int ref_round( int64_t a )
{
	Ref128 t = (Ref128)a + FIX_HALF;
	Ref128 q = t / FIX_ONE;
	Ref128 r = t % FIX_ONE;
	if ( r != 0 && t < 0 ) q--;
	if ( q > INT32_MAX ) return INT32_MAX;
	if ( q < INT32_MIN ) return INT32_MIN;
	return (int)q;
}

static int64_t ref_ceil( int64_t a )
{
	// ceil(a/FIX_ONE) = -floor(-a/FIX_ONE); saturate to FIX_MAX on overflow.
	Ref128 na = -(Ref128)a;
	Ref128 q = na / FIX_ONE;
	Ref128 r = na % FIX_ONE;
	if ( r != 0 && na < 0 ) q--;
	Ref128 res = -q * FIX_ONE;
	if ( res > (Ref128)FIX_MAX ) return FIX_MAX;
	return (int64_t)res;
}

static void check_one( int64_t a )
{
	int tr = TRUNC( a ), fr = FLOORI( a ), rr = ROUNDI( a );
	int etr = ref_trunc( a ), efr = ref_floor( a ), err_ = ref_round( a );
	if ( tr != etr ) mismatch( "fixTruncToInt", a, tr, etr );
	if ( fr != efr ) mismatch( "fixFloorToInt", a, fr, efr );
	if ( rr != err_ ) mismatch( "fixRoundToInt", a, rr, err_ );

	int64_t ce = CEILR( a ), ece = ref_ceil( a );
	if ( ce != ece ) mismatch( "fixCeilSat", a, ce, ece );

	int64_t fl = fixFloor( a );
	int64_t efl = (int64_t)( (uint64_t)a & ~(uint64_t)( FIX_ONE - 1 ) );
	if ( fl != efl ) mismatch( "fixFloor", a, fl, efl );
}

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

int main( void )
{
	static const int64_t cases[] = {
		(int64_t)1 << 47,              // 2^31 << 16
		((int64_t)1 << 47) - 1,
		-((int64_t)1 << 47),
		-((int64_t)1 << 47) - 1,
		INT64_MAX, INT64_MIN,
		0, 1, -1,
		65535, -65535, 65536, -65536, 65537, -65537,
		INT64_MIN + 1,
		INT64_MAX - 65535,
	};
	for ( unsigned i = 0; i < sizeof( cases ) / sizeof( cases[0] ); i++ )
	{
		check_one( cases[i] );
	}

	for ( int i = 0; i < 65536; i++ )
	{
		uint64_t u = rng_next();
		check_one( (int64_t)u );
	}

	printf( "toint test passed\n" );
	return 0;
}
