/* SPDX-License-Identifier: MIT */
#include "../sweep.h"
#include "../data/libfixmath_edges.h"
#include "fixed/fixed_math.h"

static int64_t eval( int64_t a, int64_t b )
{
	( void )b;
	return fixExp2( a );
}

static int64_t ref( int64_t a, int64_t b )
{
	( void )b;
	mpfr_t* v = sweep_mpfr_tmp();
	mpfr_set_si( *v, (long)a, MPFR_RNDN );
	mpfr_div_2ui( *v, *v, 16, MPFR_RNDN );
	mpfr_exp2( *v, *v, MPFR_RNDN );
	return sweep_mpfr_round( *v );
}

static const sweep_piece pieces[] = {
	{ -17 * 65536, 47 * 65536, 0, 0, 0, SWEEP_EXHAUSTIVE },
};

const sweep_fn sweep_fn_exp2 = {
	"exp2", 1, eval, ref, pieces, sizeof( pieces ) / sizeof( pieces[0] ), NULL, 0, sweep_libfixmath_edges,
	sweep_libfixmath_edge_count,
};
