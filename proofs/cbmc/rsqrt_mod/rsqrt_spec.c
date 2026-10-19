/*
 * CBMC specification harness for fixRsqrtExact.
 *
 * The source states (src/exact/rsqrt.c, include/fixed/fixed_rsqrt.h):
 *
 *     q is the unique non-negative integer with
 *         (2q-1)^2 * raw <= 2^50 < (2q+1)^2 * raw
 *     and q == 0 whenever raw > 2^50; x <= 0 returns INT64_MAX.
 *
 * This harness asserts that inequality independently, in unsigned __int128
 * arithmetic, and separately asserts the x <= 0 result. It is built once per
 * arm: CBMC_NATIVE_ARM (with __x86_64__ undefined, so the library's division
 * takes its plain C spelling) and CBMC_EMULATED_ARM (FIX_FORCE_EMULATED_INT128).
 *
 * THE SQRT SEED. fixRsqrtExact calls fixISqrt128High, whose seed is
 * __builtin_sqrt. CBMC has no body for that builtin, so the harness routes it to
 * cbmc_rsqrt_seed. A seed is any integer a correctly rounded IEEE sqrt may
 * return for the library's double argument x. For seed r that is exactly
 *
 *     (2r - 1)^2 <= 4x < (2r + 3)^2     (both sides exact integers)
 *
 * (write t = RN(sqrt(x)); |t - sqrt(x)| <= 1/2 for x <= 2^104, so
 * r = floor(t) satisfies it, and every r satisfying it is a possible floor of
 * an allowed t). The harness draws a nondeterministic r and ASSUMES that
 * condition, so the proof covers every seed the hardware can produce. The seed
 * argument is checked bit-exactly against the library's own double expression.
 * The allowed set is never empty: r = floor(sqrt(n)) satisfies it for every
 * integer n >= 1.
 *
 * NARROWING. CBMC_WIDTH free low bits of raw, sign-extended by CBMC_SIGNS bit
 * 0 (0 = zero extension, 1 = ones extension), as proofs/cbmc/int128_equiv.c
 * does. results.md gives each target's exact domain. rsqrt_high narrows the
 * top of the positive range instead: raw = INT64_MAX - u, u the free low
 * CBMC_WIDTH bits (so raw >= 2^63 - 2^CBMC_WIDTH). rsqrt_edges are fixed
 * constants, including the tie raw = 2^50.
 */
/* System headers first: the native arm undefines __x86_64__ below, and glibc's
   headers must see it to pick their 64-bit stubs (as int128_equiv.c does). */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined( CBMC_NATIVE_ARM )
#ifdef __x86_64__
#undef __x86_64__
#endif
#elif defined( CBMC_EMULATED_ARM )
#define FIX_FORCE_EMULATED_INT128
#else
#error "define CBMC_NATIVE_ARM or CBMC_EMULATED_ARM"
#endif

typedef unsigned __int128 cbmc_u128;

unsigned long long nondet_ulonglong( void );
void __CPROVER_assume( _Bool );
void __CPROVER_assert( _Bool, const char* );

static double cbmc_seed_arg;
static cbmc_u128 cbmc_seed_four_n;
static uint64_t cbmc_seed_value;

uint64_t cbmc_rsqrt_seed( double x )
{
	__CPROVER_assert( x == cbmc_seed_arg, "seed argument is the library's double expression" );
	return cbmc_seed_value;
}
#define __builtin_sqrt( x ) cbmc_rsqrt_seed( x )
#include "fixed/fixed.h"
#include "../../../src/exact/rsqrt.c"

#ifndef CBMC_WIDTH
#define CBMC_WIDTH 8
#endif
#ifndef CBMC_SIGNS
#define CBMC_SIGNS 0
#endif
/* raw < 2^CBMC_WIDTH, so the exact root s < 2^(CBMC_WIDTH/2 + 24). */
#define CBMC_SEED_WIDTH ( CBMC_WIDTH / 2 + 24 )

static uint64_t ext64( uint64_t bits, int w, int negative )
{
	if ( w >= 64 ) return bits;
	uint64_t mask = ( UINT64_C( 1 ) << w ) - 1;
	return negative ? ( ( bits & mask ) | ~mask ) : ( bits & mask );
}
static uint64_t in_raw( void )
{
	return ext64( (uint64_t)nondet_ulonglong(), CBMC_WIDTH < 64 ? CBMC_WIDTH : 64,
				  ( CBMC_SIGNS >> 0 ) & 1 );
}

/* n = raw * 2^48 as the 128-bit (hi, lo) the library passes to fixISqrt128High. */
static cbmc_u128 rsqrt_n( uint64_t raw )
{
	return ( (cbmc_u128)( raw >> 16 ) << 64 ) | (cbmc_u128)(uint64_t)( raw << 48 );
}

static double rsqrt_seed_argument( uint64_t raw )
{
	uint64_t hi = raw >> 16;
	uint64_t lo = (uint64_t)( raw << 48 );
	return hi != 0 ? (double)hi * 18446744073709551616.0 + (double)lo : (double)lo;
}

/* The IEEE contract for a seed r against 4x: (2r - 1)^2 <= 4x < (2r + 3)^2. */
static int seed_allowed( uint64_t r, cbmc_u128 four_n )
{
	cbmc_u128 m = r == 0 ? 1 : (cbmc_u128)( 2 * r - 1 );
	cbmc_u128 b = (cbmc_u128)( 2 * r + 3 );
	return m * m <= four_n && four_n < b * b;
}

/* Set the seed model for a positive raw that calls the seed (hi < 2^40). */
static void seed_setup( uint64_t raw )
{
	cbmc_seed_arg = rsqrt_seed_argument( raw );
	cbmc_seed_four_n = rsqrt_n( raw ) * 4;
	cbmc_seed_value = ext64( (uint64_t)nondet_ulonglong(), CBMC_SEED_WIDTH, 0 );
	__CPROVER_assume( seed_allowed( cbmc_seed_value, cbmc_seed_four_n ) );
}

/* The independent integer specification, exactly as the source states it. */
static int rsqrt_matches( uint64_t raw, fixed_t q )
{
	cbmc_u128 bound = (cbmc_u128)UINT64_C( 1 ) << 50;
	if ( raw > ( UINT64_C( 1 ) << 50 ) )
	{
		return q == 0;
	}
	if ( q < 0 || (uint64_t)q > ( UINT64_C( 1 ) << 24 ) )
	{
		return 0;
	}
	cbmc_u128 qq = (cbmc_u128)(uint64_t)q;
	cbmc_u128 below = qq == 0 ? 1 : ( 2 * qq - 1 ) * ( 2 * qq - 1 );
	cbmc_u128 above = ( 2 * qq + 1 ) * ( 2 * qq + 1 );
	return below * (cbmc_u128)raw <= bound && bound < above * (cbmc_u128)raw;
}

static void check( uint64_t raw )
{
	int64_t x = (int64_t)raw;
	if ( x > 0 && ( raw >> 16 ) < ( UINT64_C( 1 ) << 40 ) )
	{
		seed_setup( raw );
	}
	fixed_t q = fixRsqrtExact( x );
	if ( x <= 0 )
	{
		__CPROVER_assert( q == INT64_MAX, "x <= 0 gives INT64_MAX" );
	}
	else
	{
		__CPROVER_assert( q <= (fixed_t)( UINT64_C( 1 ) << 24 ), "q <= 2^24" );
		__CPROVER_assert( rsqrt_matches( raw, q ), "fixRsqrtExact meets the integer inequality" );
	}
}

void harness_rsqrt( void )
{
	check( in_raw() );
}

/* The top of the positive range; hi >= 2^40, so fixISqrt128High takes its
   restoring path and makes no seed call. */
void harness_rsqrt_high( void )
{
	uint64_t u = ext64( (uint64_t)nondet_ulonglong(), CBMC_WIDTH < 64 ? CBMC_WIDTH : 64, 0 );
	check( (uint64_t)INT64_MAX - u );
}

/* Fixed constants: the smallest positive raw, the tie raw = 2^50 (q must be 1,
   ties away from zero), the first raw above it (q must be 0), and INT64_MAX.
   The seed for a constant raw is pinned to the exact integer root s, which the
   IEEE contract allows, so no nonlinear seed assumption is needed here. */
static void check_edge( uint64_t raw, uint64_t s )
{
	int64_t x = (int64_t)raw;
	if ( x > 0 && ( raw >> 16 ) < ( UINT64_C( 1 ) << 40 ) )
	{
		cbmc_seed_arg = rsqrt_seed_argument( raw );
		cbmc_seed_four_n = rsqrt_n( raw ) * 4;
		cbmc_seed_value = s;
		__CPROVER_assert( seed_allowed( s, cbmc_seed_four_n ), "pinned seed is an IEEE sqrt result" );
	}
	fixed_t q = fixRsqrtExact( x );
	__CPROVER_assert( q <= (fixed_t)( UINT64_C( 1 ) << 24 ), "q <= 2^24 (edge)" );
	__CPROVER_assert( rsqrt_matches( raw, q ), "fixRsqrtExact meets the inequality (edge)" );
}

void harness_rsqrt_edges( void )
{
	check_edge( 1, UINT64_C( 1 ) << 24 );		   /* s = 2^24 */
	check_edge( 2, UINT64_C( 23726566 ) );		   /* s = floor(sqrt(2) 2^24) */
	check_edge( UINT64_C( 1 ) << 50, UINT64_C( 1 ) << 49 ); /* tie: q = 1 */
	check_edge( ( UINT64_C( 1 ) << 50 ) + 1, UINT64_C( 1 ) << 49 );
	check_edge( (uint64_t)INT64_MAX, 0 );		   /* no seed call */
}
