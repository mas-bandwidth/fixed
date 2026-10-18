/* SPDX-License-Identifier: MIT */
#include "../sweep.h"
#include "../data/libfixmath_edges.h"
#include "fixed/fixed_sinh_cosh.h"

static int64_t eval( int64_t a, int64_t b )
{
	( void )b;
	return fixCoshExact( a );
}

static int64_t ref( int64_t a, int64_t b )
{
	( void )b;
	mpfr_t* v = sweep_mpfr_tmp();
	mpfr_set_si( *v, (long)a, MPFR_RNDN );
	mpfr_div_2ui( *v, *v, 16, MPFR_RNDN );
	mpfr_cosh( *v, *v, MPFR_RNDN );
	return sweep_mpfr_round( *v );
}

static const sweep_piece pieces[] = {
	{ -2180452, 2180453, 0, 0, 0, SWEEP_EXHAUSTIVE },
	{ 2180453, INT64_MAX, 0, 0, 524288, SWEEP_SAMPLED },
	{ -INT64_MAX, -2180452, 0, 0, 524288, SWEEP_SAMPLED },
};

const sweep_fn sweep_fn_cosh = {
	"cosh", 1, eval, ref, pieces, sizeof( pieces ) / sizeof( pieces[0] ), NULL, 0, sweep_libfixmath_edges,
	sweep_libfixmath_edge_count,
};
