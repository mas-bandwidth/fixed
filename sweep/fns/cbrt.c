/* SPDX-License-Identifier: MIT */
#include "../sweep.h"
#include "../data/libfixmath_edges.h"
#include "fixed/fixed_cbrt.h"

static int64_t eval( int64_t a, int64_t b )
{
	( void )b;
	return fixCbrtExact( a );
}

static int64_t ref( int64_t a, int64_t b )
{
	( void )b;
	mpfr_t* v = sweep_mpfr_tmp();
	mpfr_set_si( *v, (long)a, MPFR_RNDN );
	mpfr_div_2ui( *v, *v, 16, MPFR_RNDN );
	mpfr_cbrt( *v, *v, MPFR_RNDN );
	return sweep_mpfr_round( *v );
}

// A negative magnitude binade [-2^k, -2^(k-1)), sampled with 2^18 seeded raws. k runs
// from 23 to 61; the k = 62 binade reaches INT64_MIN and is written out below.
#define NEGATIVE_BINADE( k )                                                                                                   \
	{                                                                                                                          \
		-( INT64_C( 1 ) << ( k ) ), -( INT64_C( 1 ) << ( ( k ) - 1 ) ), 0, 0, 262144, SWEEP_SAMPLED                           \
	}

static const sweep_piece pieces[] = {
	{ -4194304, 4194305, 0, 0, 0, SWEEP_EXHAUSTIVE },
	{ 4194304, INT64_MAX, 0, 0, 262144, SWEEP_PER_BINADE },
	NEGATIVE_BINADE( 23 ),
	NEGATIVE_BINADE( 24 ),
	NEGATIVE_BINADE( 25 ),
	NEGATIVE_BINADE( 26 ),
	NEGATIVE_BINADE( 27 ),
	NEGATIVE_BINADE( 28 ),
	NEGATIVE_BINADE( 29 ),
	NEGATIVE_BINADE( 30 ),
	NEGATIVE_BINADE( 31 ),
	NEGATIVE_BINADE( 32 ),
	NEGATIVE_BINADE( 33 ),
	NEGATIVE_BINADE( 34 ),
	NEGATIVE_BINADE( 35 ),
	NEGATIVE_BINADE( 36 ),
	NEGATIVE_BINADE( 37 ),
	NEGATIVE_BINADE( 38 ),
	NEGATIVE_BINADE( 39 ),
	NEGATIVE_BINADE( 40 ),
	NEGATIVE_BINADE( 41 ),
	NEGATIVE_BINADE( 42 ),
	NEGATIVE_BINADE( 43 ),
	NEGATIVE_BINADE( 44 ),
	NEGATIVE_BINADE( 45 ),
	NEGATIVE_BINADE( 46 ),
	NEGATIVE_BINADE( 47 ),
	NEGATIVE_BINADE( 48 ),
	NEGATIVE_BINADE( 49 ),
	NEGATIVE_BINADE( 50 ),
	NEGATIVE_BINADE( 51 ),
	NEGATIVE_BINADE( 52 ),
	NEGATIVE_BINADE( 53 ),
	NEGATIVE_BINADE( 54 ),
	NEGATIVE_BINADE( 55 ),
	NEGATIVE_BINADE( 56 ),
	NEGATIVE_BINADE( 57 ),
	NEGATIVE_BINADE( 58 ),
	NEGATIVE_BINADE( 59 ),
	NEGATIVE_BINADE( 60 ),
	NEGATIVE_BINADE( 61 ),
	{ INT64_MIN, -( INT64_C( 1 ) << 62 ), 0, 0, 262144, SWEEP_SAMPLED },
};

const sweep_fn sweep_fn_cbrt = {
	"cbrt", 1, eval, ref, pieces, sizeof( pieces ) / sizeof( pieces[0] ), NULL, 0, sweep_libfixmath_edges,
	sweep_libfixmath_edge_count,
};
