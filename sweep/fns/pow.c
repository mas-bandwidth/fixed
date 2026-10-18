/* SPDX-License-Identifier: MIT */
#include "../sweep.h"
#include "../data/glibc_pow_log_points.h"
#include "../data/libfixmath_edges.h"
#include "fixed/fixed_math.h"

static int64_t eval( int64_t a, int64_t b )
{
	return fixPow( a, b );
}

static int64_t ref( int64_t a, int64_t b )
{
	if ( a <= 0 )
	{
		return 0; /* documented */
	}
	if ( b == 0 )
	{
		return 65536; /* documented: one for a zero exponent */
	}
	mpfr_t* v = sweep_mpfr_tmp();
	mpfr_t* e = sweep_mpfr_tmp();
	/* one scratch value per thread is not enough for pow: make a second. */
	mpfr_t base;
	mpfr_init2( base, 256 );
	mpfr_set_si( base, (long)a, MPFR_RNDN );
	mpfr_div_2ui( base, base, 16, MPFR_RNDN );
	mpfr_set_si( *e, (long)b, MPFR_RNDN );
	mpfr_div_2ui( *e, *e, 16, MPFR_RNDN );
	mpfr_pow( *v, base, *e, MPFR_RNDN );
	mpfr_clear( base );
	return sweep_mpfr_round( *v );
}

static const sweep_piece pieces[] = {
	{ 0, 1048576, -524288, 524288, 1048576, SWEEP_SAMPLED },
};

static const sweep_point points[] = {
	{ 2 * 65536, -8 * 65536 },
	{ 2 * 65536, -1074 * 65536 },
	{ 65536 / 2, 1074 * 65536 },
	{ 3 * 65536 / 2, 7 * 65536 / 2 },
	{ 3 * 65536, 3 * 65536 },
	{ 10 * 65536, 3 * 65536 },
	{ 3 * 65536, -5 * 65536 },
	{ 6554, 5 * 65536 / 2 },
	{ 73 * 65536 / 16, 11 * 65536 / 2 },
	{ 65543, 1000 * 65536 },
	{ 65536 * 65536, 65536 / 2 },
	{ 7, -65536 / 2 },
	{ 65536, 65536 },
	{ -2 * 65536, 3 * 65536 },
	{ -65536, -3 * 65536 },
	{ 0, 0 },
};

const sweep_fn sweep_fn_pow = {
	"pow", 2, eval, ref, pieces, sizeof( pieces ) / sizeof( pieces[0] ), points,
	sizeof( points ) / sizeof( points[0] ), sweep_libfixmath_edges, sweep_libfixmath_edge_count,
};
