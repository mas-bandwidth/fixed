// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
//
// tan_reduction_bound: how many bits of 2/pi an integer Payne-Hanek reduction of a Q48.16
// angle needs. A tool, never linked into the library or CTest. Build and run:
//
//   cc -O2 tools/tan_reduction_bound.c -lmpfr -lgmp -o tan_reduction_bound && ./tan_reduction_bound
//
// It needs MPFR and GMP only (libmpfr-dev), not libm, and runs in about a second.
//
// Every Q48.16 angle is x = raw * 2^-16 with |raw| <= 2^63, so its count of quarter turns is
// q = x / (pi/2) = raw * alpha with alpha = 2^-15 / pi. The reduced angle is r = f * pi/2 with
// f = q - round(q), so the raw closest to a multiple of pi/2 is the raw minimising ||raw * alpha||
// (distance to the nearest integer). By the best-approximation property of continued fractions,
// for every bound Q the minimum of ||n * alpha|| over 1 <= n <= Q is attained at the largest
// convergent denominator q_k <= Q, so the convergents of alpha up to 2^63 list every record. The
// tool prints them, checks the property by brute force over every raw below 2^21 (|x| < 32 rad), and derives
// the bit count:
//
//   truncating 2/pi after J fraction bits moves q by at most 2^63 * 2^-16 * 2^-J = 2^(47 - J),
//   so f is known to relative precision 2^-p wherever |f| >= f_floor when
//   J >= 47 + p + log2(1 / f_floor).
//
// In Q48.16 tan saturates near a pole once 2^16 * |cot r| >= 2^63 - 1.5 (RN would pass
// INT64_MAX), so no input needs relative precision below that point: f_floor is the larger of
// the closest approach f_min and that saturation edge f_sat. p is the relative precision the
// design asks of r (docs/design/transcendentals.md, the tan section).
#include <gmp.h>
#include <mpfr.h>
#include <stdint.h>
#include <stdio.h>

#define PREC 2048
#define P_TARGET 120 // relative precision of r, in bits, the tan design asks down to the saturation edge

// log2 |v| as a double, through MPFR so the tool needs no libm
static double log2_of( const mpfr_t v )
{
	mpfr_t l;
	mpfr_init2( l, 64 );
	mpfr_abs( l, v, MPFR_RNDN );
	mpfr_log2( l, l, MPFR_RNDN );
	double out = mpfr_get_d( l, MPFR_RNDN );
	mpfr_clear( l );
	return out;
}

static int ceil_int( double v )
{
	int i = (int)v;
	return ( (double)i < v ) ? i + 1 : i;
}

int main( void )
{
	mpfr_set_default_prec( PREC );
	mpfr_t alpha, pi, theta, a, t, d, best, r, halfpi, fsat, tmp;
	mpfr_inits( alpha, pi, theta, a, t, d, best, r, halfpi, fsat, tmp, (mpfr_ptr)0 );
	mpfr_const_pi( pi, MPFR_RNDN );
	mpfr_ui_div( alpha, 1, pi, MPFR_RNDN );
	mpfr_div_2ui( alpha, alpha, 15, MPFR_RNDN ); // alpha = 2^-15 / pi
	mpfr_div_2ui( halfpi, pi, 1, MPFR_RNDN );

	mpz_t p0, p1, q0, q1, p2, q2, ai, limit;
	mpz_inits( p0, p1, q0, q1, p2, q2, ai, limit, (mpz_ptr)0 );
	mpz_ui_pow_ui( limit, 2, 63 );

	// convergents: p_{-1} = 1, q_{-1} = 0; p_0 = a_0, q_0 = 1
	mpfr_set( theta, alpha, MPFR_RNDN );
	mpfr_floor( a, theta );
	mpfr_get_z( ai, a, MPFR_RNDN );
	mpz_set_ui( p0, 1 );
	mpz_set_ui( q0, 0 );
	mpz_set( p1, ai );
	mpz_set_ui( q1, 1 );

	printf( "tan_reduction_bound: alpha = 2^-15/pi, convergents p/q of alpha with q <= 2^63 (MPFR %s, %d bits)\n",
			mpfr_get_version(), PREC );
	printf( "%-4s %-20s %-6s %-12s %-10s %-10s\n", "k", "q (raw)", "parity", "||q alpha||", "log2 f", "log2 r" );

	mpz_t worst_q, worst_p, worst20_q;
	mpz_inits( worst_q, worst_p, worst20_q, (mpz_ptr)0 );
	double worst_log2f = 0.0, worst20_log2f = 0.0;
	for ( int k = 0; k < 400; ++k )
	{
		// next partial quotient
		mpfr_sub( t, theta, a, MPFR_RNDN );
		if ( mpfr_zero_p( t ) )
		{
			break;
		}
		mpfr_ui_div( theta, 1, t, MPFR_RNDN );
		mpfr_floor( a, theta );
		mpfr_get_z( ai, a, MPFR_RNDN );
		mpz_mul( p2, ai, p1 );
		mpz_add( p2, p2, p0 );
		mpz_mul( q2, ai, q1 );
		mpz_add( q2, q2, q0 );
		if ( mpz_cmp( q2, limit ) > 0 )
		{
			break;
		}
		mpz_set( p0, p1 );
		mpz_set( q0, q1 );
		mpz_set( p1, p2 );
		mpz_set( q1, q2 );

		// d = |q alpha - p|, recomputed directly at full precision
		mpfr_mul_z( d, alpha, q1, MPFR_RNDN );
		mpfr_sub_z( d, d, p1, MPFR_RNDN );
		mpfr_abs( d, d, MPFR_RNDN );
		mpfr_mul( r, d, halfpi, MPFR_RNDN );
		double lf = log2_of( d ), lr = log2_of( r );
		printf( "%-4d %-20s %-6s %-12.4e %-10.3f %-10.3f\n", k + 1, mpz_get_str( NULL, 10, q1 ),
				mpz_odd_p( p1 ) ? "pole" : "zero", mpfr_get_d( d, MPFR_RNDN ), lf, lr );
		mpz_set( worst_q, q1 );
		mpz_set( worst_p, p1 );
		worst_log2f = lf;
		if ( mpz_cmp_ui( q1, (unsigned long)1 << 21 ) < 0 )
		{
			mpz_set( worst20_q, q1 );
			worst20_log2f = lf;
		}
	}

	// brute-force check of the best-approximation property over every raw in [1, 2^21)
	{
		mpfr_t v, w;
		mpfr_init2( v, 256 );
		mpfr_init2( w, 256 );
		double bmin = 1.0;
		uint64_t braw = 0;
		mpfr_t bm;
		mpfr_init2( bm, 64 );
		for ( uint64_t n = 1; n < ( (uint64_t)1 << 21 ); ++n )
		{
			mpfr_mul_ui( v, alpha, (unsigned long)n, MPFR_RNDN );
			mpfr_rint( w, v, MPFR_RNDN );
			mpfr_sub( v, v, w, MPFR_RNDN );
			double dv = mpfr_get_d( v, MPFR_RNDN );
			dv = dv < 0 ? -dv : dv;
			if ( dv < bmin )
			{
				bmin = dv;
				braw = n;
			}
		}
		int ok = mpz_cmp_ui( worst20_q, (unsigned long)braw ) == 0;
		printf( "selfcheck brute force over raw in [1, 2^21): min at raw=%llu log2 f=%.3f, convergent says raw=%s: %s\n",
				(unsigned long long)braw, ( mpfr_set_d( bm, bmin, MPFR_RNDN ), log2_of( bm ) ), mpz_get_str( NULL, 10, worst20_q ), ok ? "agree" : "DISAGREE" );
		mpfr_clears( v, w, bm, (mpfr_ptr)0 );
		if ( !ok )
		{
			return 1;
		}
	}

	// the saturation edge: 2^16 * cot( r_sat ) = 2^63 - 1.5, f_sat = r_sat / (pi/2)
	mpfr_set_ui( tmp, 1, MPFR_RNDN );
	mpfr_mul_2ui( tmp, tmp, 63, MPFR_RNDN );
	mpfr_sub_d( tmp, tmp, 1.5, MPFR_RNDN );
	mpfr_div_2ui( tmp, tmp, 16, MPFR_RNDN ); // cot r_sat
	mpfr_ui_div( tmp, 1, tmp, MPFR_RNDN );
	mpfr_atan( r, tmp, MPFR_RNDN ); // r_sat = atan( 1 / cot )
	mpfr_div( fsat, r, halfpi, MPFR_RNDN );
	double lfsat = log2_of( fsat ), lrsat = log2_of( r );

	double lfloor = worst_log2f > lfsat ? worst_log2f : lfsat;
	int J = ceil_int( 47.0 + P_TARGET - lfloor );
	int Jclassic = ceil_int( 47.0 + P_TARGET - worst_log2f );
	printf( "worst raw (closest to a multiple of pi/2, 1 <= raw <= 2^63): %s, multiple %s of pi/2 (%s), log2 f_min = %.3f, log2 r_min = %.3f\n",
			mpz_get_str( NULL, 10, worst_q ), mpz_get_str( NULL, 10, worst_p ), mpz_odd_p( worst_p ) ? "a pole" : "a zero of tan",
			worst_log2f, worst_log2f + 0.651496129472318798 );
	printf( "worst raw with |x| < 32 rad (raw < 2^21): %s, log2 f = %.3f\n", mpz_get_str( NULL, 10, worst20_q ), worst20_log2f );
	printf( "saturation edge: 2^16 cot r = 2^63 - 1.5 at log2 r_sat = %.3f, log2 f_sat = %.3f\n", lrsat, lfsat );
	printf( "bits of 2/pi needed for relative precision 2^-%d of r: J >= 47 + %d + %.3f = %d (f_floor = max(f_min, f_sat))\n",
			P_TARGET, P_TARGET, -lfloor, J );
	printf( "bits of 2/pi without saturation (the floating-point count, f_floor = f_min): %d\n", Jclassic );
	printf( "tan_reduction_bound: worst_raw=%s bits=%d\n", mpz_get_str( NULL, 10, worst_q ), J );

	mpz_clears( p0, p1, q0, q1, p2, q2, ai, limit, worst_q, worst_p, worst20_q, (mpz_ptr)0 );
	mpfr_clears( alpha, pi, theta, a, t, d, best, r, halfpi, fsat, tmp, (mpfr_ptr)0 );
	return 0;
}
