/* SPDX-License-Identifier: MIT */
#include "../sweep.h"
#include "../data/glibc_pow_log_points.h"
#include "../data/libfixmath_edges.h"
#include "fixed/fixed_math.h"

static int64_t eval( int64_t a, int64_t b )
{
	( void )b;
	return fixLog2( a );
}

static int64_t ref( int64_t a, int64_t b )
{
	( void )b;
	if ( a <= 0 )
	{
		return INT64_MIN; /* documented: no value there */
	}
	mpfr_t* v = sweep_mpfr_tmp();
	mpfr_set_si( *v, (long)a, MPFR_RNDN );
	mpfr_div_2ui( *v, *v, 16, MPFR_RNDN );
	mpfr_log2( *v, *v, MPFR_RNDN );
	return sweep_mpfr_round( *v );
}

static const sweep_piece pieces[] = {
	{ 1, 1048577, 0, 0, 0, SWEEP_EXHAUSTIVE },
	{ 1048576, INT64_MAX, 0, 0, 262144, SWEEP_PER_BINADE },
};

static const sweep_point points[] = {
	{ 2 * 65536, 0 },  { 10 * 65536, 0 }, { 65536, 0 },      { 65536 / 2, 0 }, { 3 * 65536, 0 },
	{ 7 * 65536 / 2, 0 }, { 65543, 0 },   { 655361, 0 },     { 100000 * 65536, 0 }, { 12345, 0 },
	{ 1, 0 },          { 0, 0 },          { -65536, 0 },     { -1, 0 },        { 65536 * 65536, 0 },
	{ 3 * 65536 * 65536, 0 },
};

const sweep_fn sweep_fn_log2 = {
	"log2", 1, eval, ref, pieces, sizeof( pieces ) / sizeof( pieces[0] ), points,
	sizeof( points ) / sizeof( points[0] ), sweep_libfixmath_edges, sweep_libfixmath_edge_count,
};
