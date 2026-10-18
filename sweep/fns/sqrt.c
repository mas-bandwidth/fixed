/* SPDX-License-Identifier: MIT */
#include "../sweep.h"
#include "../data/libfixmath_edges.h"
#include "fixed/fixed.h"

static int64_t eval( int64_t a, int64_t b )
{
	( void )b;
	return fixSqrt( a );
}

static int64_t ref( int64_t a, int64_t b )
{
	( void )b;
	if ( a < 0 )
	{
		return 0; /* documented: negative input returns 0 */
	}
	mpfr_t* v = sweep_mpfr_tmp();
	mpfr_set_si( *v, (long)a, MPFR_RNDN );
	mpfr_div_2ui( *v, *v, 16, MPFR_RNDN );
	mpfr_sqrt( *v, *v, MPFR_RNDN );
	return sweep_mpfr_round( *v );
}

static const sweep_piece pieces[] = {
	{ 0, 16777217, 0, 0, 0, SWEEP_EXHAUSTIVE },
	{ 16777216, INT64_MAX, 0, 0, 262144, SWEEP_PER_BINADE },
};

const sweep_fn sweep_fn_sqrt = {
	"sqrt", 1, eval, ref, pieces, sizeof( pieces ) / sizeof( pieces[0] ), NULL, 0, sweep_libfixmath_edges,
	sweep_libfixmath_edge_count,
};
