/* SPDX-License-Identifier: MIT */
/* One sweep descriptor, two functions: fixSinExact and fixCosExact.
 *
 * The sweep engine binds one function per input, so cos inputs are carried negated: a raw
 * input `a >= 0` is a sine angle, and `a < 0` is the cosine angle `-a`. decode() turns the
 * carrier back into the exact angle before the call and before the MPFR reference, so both
 * functions see their real inputs and the emitted header carries the same encoding.
 *
 * Pieces:
 *   - every case of test/data/hardcases/sin_big.txt and cos_big.txt (read at start-up),
 *     loaded into the descriptor's edge list so --emit-points carries them too;
 *   - 2^18 seeded raws per binade e = 16..45 for each function. Binade 46 is sampled into
 *     the points list because its positive and negative carriers together span more than
 *     int64 allows in the engine's emit-points domain computation.
 */
#include "../sweep.h"
#include "fixed/fixed_math.h"

#include <stdio.h>
#include <stdlib.h>

#ifndef FIX_SOURCE_DIR
#define FIX_SOURCE_DIR "."
#endif

#define PER_BINADE ( 1 << 18 )
#define E46_POINTS ( 2 * PER_BINADE )
#define EDGE_CAP 16384
#define SEED 0x9E3779B97F4A7C15ULL

static int64_t decode( int64_t a, int* isCos )
{
	if ( a < 0 )
	{
		*isCos = 1;
		return -a;
	}
	*isCos = 0;
	return a;
}

static int64_t eval( int64_t a, int64_t b )
{
	( void )b;
	int isCos;
	int64_t raw = decode( a, &isCos );
	return isCos ? fixCosExact( raw ) : fixSinExact( raw );
}

static int64_t ref( int64_t a, int64_t b )
{
	( void )b;
	int isCos;
	int64_t raw = decode( a, &isCos );
	mpfr_t* v = sweep_mpfr_tmp();
	mpfr_set_si( *v, (long)raw, MPFR_RNDN );
	mpfr_div_2ui( *v, *v, 16, MPFR_RNDN );
	if ( isCos )
	{
		mpfr_cos( *v, *v, MPFR_RNDN );
	}
	else
	{
		mpfr_sin( *v, *v, MPFR_RNDN );
	}
	return sweep_mpfr_round( *v );
}

static uint64_t xs_next( uint64_t* s )
{
	uint64_t x = *s;
	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	*s = x;
	return x;
}

static int64_t xs_draw( uint64_t* s, int64_t lo, int64_t hi )
{
	uint64_t span = (uint64_t)( hi - lo );
	return lo + (int64_t)( xs_next( s ) % span );
}

static sweep_piece pieces[60];
static sweep_point points[E46_POINTS];
static int64_t edges[EDGE_CAP];
static size_t edge_n = 0;

static void load_hardcases( const char* path, int negate )
{
	FILE* fp = fopen( path, "r" );
	if ( !fp )
	{
		fprintf( stderr, "sincos_big: cannot read %s\n", path );
		return;
	}
	char line[512];
	while ( fgets( line, sizeof( line ), fp ) != NULL )
	{
		if ( line[0] == '#' || line[0] == '\n' || line[0] == '\r' )
		{
			continue;
		}
		long long raw = 0;
		int e = 0;
		double d = 0;
		if ( sscanf( line, "%lld %d %lf", &raw, &e, &d ) == 3 && edge_n < EDGE_CAP )
		{
			edges[edge_n++] = negate ? -(int64_t)raw : (int64_t)raw;
		}
	}
	fclose( fp );
}

#if defined( __GNUC__ ) || defined( __clang__ )
__attribute__( ( constructor ) )
#endif
static void sincos_big_init( void )
{
	int p = 0;
	for ( int e = 16; e <= 45; ++e )
	{
		int64_t r0 = (int64_t)( INT64_C( 1 ) << ( e + 16 ) );
		int64_t r1 = (int64_t)( INT64_C( 1 ) << ( e + 17 ) );
		pieces[p].lo = r0;
		pieces[p].hi = r1;
		pieces[p].blo = 0;
		pieces[p].bhi = 0;
		pieces[p].count = PER_BINADE;
		pieces[p].mode = SWEEP_SAMPLED;
		p++;
		pieces[p].lo = -( r1 - 1 );
		pieces[p].hi = -( r0 - 1 );
		pieces[p].blo = 0;
		pieces[p].bhi = 0;
		pieces[p].count = PER_BINADE;
		pieces[p].mode = SWEEP_SAMPLED;
		p++;
	}

	uint64_t s = SEED;
	for ( int i = 0; i < PER_BINADE; ++i )
	{
		points[i].a = xs_draw( &s, (int64_t)( INT64_C( 1 ) << 62 ), INT64_MAX );
		points[i].b = 0;
	}
	for ( int i = 0; i < PER_BINADE; ++i )
	{
		int64_t raw = xs_draw( &s, (int64_t)( INT64_C( 1 ) << 62 ), INT64_MAX );
		points[PER_BINADE + i].a = -raw;
		points[PER_BINADE + i].b = 0;
	}

	load_hardcases( FIX_SOURCE_DIR "/test/data/hardcases/sin_big.txt", 0 );
	load_hardcases( FIX_SOURCE_DIR "/test/data/hardcases/cos_big.txt", 1 );
}

const sweep_fn sweep_fn_sincos_big = {
	"sincos_big", 1, eval, ref, pieces, sizeof( pieces ) / sizeof( pieces[0] ), points, sizeof( points ) / sizeof( points[0] ),
	edges, sizeof( edges ) / sizeof( edges[0] ),
};
