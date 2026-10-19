/*
 * CBMC specification harness for fixFmod and fixMod.
 *
 * The source states (src/exact/mod.c, include/fixed/fixed_mod.h):
 *     fixFmod(a, b) = a - trunc(a / b) * b      (C fmod, sign of a)
 *     fixMod(a, b)  = the floored remainder      (Lua, sign of b)
 *     b == 0 gives 0 for both; a == INT64_MIN, b == -1 gives 0.
 *
 * This harness computes both expectations independently in signed __int128
 * arithmetic, so the specification itself cannot overflow: for b != 0,
 * trunc(a/b) fits __int128 even at INT64_MIN / -1, and a - trunc(a/b)*b is
 * exactly the truncated remainder. The floored form subtracts one from the
 * quotient when the truncated remainder is nonzero and the signs differ. The
 * result's sign is asserted too (sign of a for fixFmod, sign of b for fixMod).
 *
 * Built once per arm: CBMC_NATIVE_ARM and CBMC_EMULATED_ARM
 * (FIX_FORCE_EMULATED_INT128). The Makefile runs with --signed-overflow-check
 * and --div-by-zero-check, so the calls inside the library are checked for
 * undefined behaviour as well.
 *
 * NARROWING. CBMC_WIDTH free low bits per input, sign-extended by CBMC_SIGNS
 * bits 0 (a) and 1 (b); results.md gives each target's exact domain. The
 * INT64_MIN / -1 and zero-divisor pairs are checked as fixed constants in
 * mod_edges, not by narrowing.
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

typedef __int128 cbmc_s128;

unsigned long long nondet_ulonglong( void );
void __CPROVER_assume( _Bool );
void __CPROVER_assert( _Bool, const char* );

#include "../../../src/exact/mod.c"

#ifndef CBMC_WIDTH
#define CBMC_WIDTH 6
#endif
#ifndef CBMC_SIGNS
#define CBMC_SIGNS 0
#endif

static uint64_t ext64( uint64_t bits, int w, int negative )
{
	if ( w >= 64 ) return bits;
	uint64_t mask = ( UINT64_C( 1 ) << w ) - 1;
	return negative ? ( ( bits & mask ) | ~mask ) : ( bits & mask );
}
static int64_t in64( int index )
{
	return (int64_t)ext64( (uint64_t)nondet_ulonglong(), CBMC_WIDTH < 64 ? CBMC_WIDTH : 64,
						   ( CBMC_SIGNS >> index ) & 1 );
}

/* a - trunc(a/b)*b, independent of the library. */
static int64_t fmod_spec( int64_t a, int64_t b )
{
	if ( b == 0 ) return 0;
	cbmc_s128 wide_a = (cbmc_s128)a;
	cbmc_s128 wide_b = (cbmc_s128)b;
	return (int64_t)( wide_a - ( wide_a / wide_b ) * wide_b );
}

/* The floor of a/b as one correction to the truncated quotient. */
static int64_t mod_spec( int64_t a, int64_t b )
{
	if ( b == 0 ) return 0;
	cbmc_s128 wide_a = (cbmc_s128)a;
	cbmc_s128 wide_b = (cbmc_s128)b;
	cbmc_s128 r = wide_a - ( wide_a / wide_b ) * wide_b;
	if ( r != 0 && ( ( a < 0 ) != ( b < 0 ) ) )
	{
		r += wide_b;
	}
	return (int64_t)r;
}

static void check( int64_t a, int64_t b )
{
	int64_t f = fixFmod( a, b );
	int64_t m = fixMod( a, b );
	__CPROVER_assert( f == fmod_spec( a, b ), "fixFmod is a - trunc(a/b)*b" );
	__CPROVER_assert( m == mod_spec( a, b ), "fixMod is the floored remainder" );
	__CPROVER_assert( f == 0 || ( ( f < 0 ) == ( a < 0 ) ), "fixFmod has the sign of a, or is 0" );
	__CPROVER_assert( m == 0 || ( ( m < 0 ) == ( b < 0 ) ), "fixMod has the sign of b, or is 0" );
}

void harness_mod( void )
{
	check( in64( 0 ), in64( 1 ) );
}

void harness_mod_edges( void )
{
	check( INT64_MIN, -1 ); /* the quotient 2^63 is not an int64 */
	check( INT64_MIN, 1 );
	check( INT64_MIN, INT64_MIN );
	check( INT64_MAX, -1 );
	check( INT64_MAX, INT64_MIN );
	check( INT64_MAX, 0 );
	check( INT64_MIN, 0 );
	check( 0, 0 );
	check( 0, -1 );
	check( 1, -1 );
	check( -1, 1 );
	check( INT64_MAX, INT64_MAX );
}
