/* SPDX-License-Identifier: MIT */
#include "../sweep.h"
#include "../data/libfixmath_edges.h"
#include "fixed/fixed_mod.h"

#include <limits.h>

static int64_t eval( int64_t a, int64_t b )
{
	return fixFmod( a, b );
}

static int64_t ref( int64_t a, int64_t b )
{
	if ( b == 0 )
	{
		return 0;
	}
	if ( a == (int64_t)INT64_MIN && b == -1 )
	{
		return 0;
	}
	mpfr_t* v = sweep_mpfr_tmp();
	mpfr_t u;
	mpfr_init2( u, 256 );
	mpfr_set_si( *v, (long)a, MPFR_RNDN );
	mpfr_div_2ui( *v, *v, 16, MPFR_RNDN );
	mpfr_set_si( u, (long)b, MPFR_RNDN );
	mpfr_div_2ui( u, u, 16, MPFR_RNDN );
	mpfr_fmod( *v, *v, u, MPFR_RNDN );
	mpfr_clear( u );
	return sweep_mpfr_round( *v );
}

static const sweep_piece pieces[] = {
	{ -140737488355328, 140737488355328, -140737488355328, 140737488355328, (uint64_t)1 << 24, SWEEP_SAMPLED },
};

const sweep_fn sweep_fn_fmod = {
	"fmod", 2, eval, ref, pieces, sizeof( pieces ) / sizeof( pieces[0] ), NULL, 0,
	sweep_libfixmath_edges, sweep_libfixmath_edge_count,
};
