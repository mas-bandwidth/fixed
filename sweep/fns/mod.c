/* SPDX-License-Identifier: MIT */
#include "../sweep.h"
#include "../data/libfixmath_edges.h"
#include "fixed/fixed_mod.h"

#include <limits.h>

/* One descriptor covers both remainders: b < 0 exercises the truncated C fmod against
 * MPFR's mpfr_fmod, and b >= 0 exercises the floored Lua modulo against MPFR's
 * floor(a/b)*b subtraction. The edge grid and the seeded pairs carry both signs, so
 * every pair of the edge list and both reference definitions are swept. */

static int64_t eval( int64_t a, int64_t b )
{
	if ( b < 0 )
	{
		return fixFmod( a, b );
	}
	return fixMod( a, b );
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
	if ( b < 0 )
	{
		mpfr_fmod( *v, *v, u, MPFR_RNDN );
	}
	else
	{
		mpfr_div( *v, *v, u, MPFR_RNDN );
		mpfr_floor( *v, *v );
		mpfr_mul( *v, *v, u, MPFR_RNDN );
		mpfr_set_si( u, (long)a, MPFR_RNDN );
		mpfr_div_2ui( u, u, 16, MPFR_RNDN );
		mpfr_sub( *v, u, *v, MPFR_RNDN );
	}
	mpfr_clear( u );
	return sweep_mpfr_round( *v );
}

static const sweep_piece pieces[] = {
	{ -140737488355328, 140737488355328, -140737488355328, 140737488355328, (uint64_t)1 << 24, SWEEP_SAMPLED },
};

static const sweep_point points[] = {
	{ (int64_t)INT64_MIN, -1 },
	{ (int64_t)INT64_MIN, 1 },
	{ (int64_t)INT64_MIN, (int64_t)INT64_MIN },
	{ (int64_t)INT64_MAX, -1 },
	{ (int64_t)INT64_MAX, 1 },
	{ (int64_t)INT64_MAX, (int64_t)INT64_MAX },
	{ 0, 0 },
	{ 65536, 0 },
	{ -65536, -65536 },
	{ 65535, 65536 },
	{ -65535, 65536 },
	{ 3, -2 },
	{ -3, 2 },
	{ 131071, 65536 },
};

const sweep_fn sweep_fn_mod = {
	"mod", 2, eval, ref, pieces, sizeof( pieces ) / sizeof( pieces[0] ), points,
	sizeof( points ) / sizeof( points[0] ), sweep_libfixmath_edges, sweep_libfixmath_edge_count,
};
