/*
 * CBMC equivalence harness: native __int128 arm == emulated (FIX_FORCE_EMULATED_INT128)
 * arm, for fixMul, fixDiv, fixISqrt128High and the 128-bit primitives they use.
 *
 * HOW THE TWO ARMS GET DISTINCT NAMES: three translation units. The Makefile compiles
 * this one source three times, prefixed with #define CBMC_NATIVE_ARM, CBMC_EMULATED_ARM
 * or CBMC_HARNESS_TU. Each arm TU includes fixed/fixed.h once (so #pragma once is never
 * defeated and no header is edited) and exports small wrappers under a macro prefix,
 * cbmc_native_* or cbmc_emulated_*. The harness TU declares both sets and asserts they
 * agree. CBMC links the three TUs into one program.
 *
 * NATIVE ARM AND x86 ASM: the native arm #undefs __x86_64__ before including fixed.h,
 * so fixInt128Div and fixUInt128DivRemBy64 take their plain C `/` spelling instead of
 * the inline `divq` (CBMC does not model divq). On x86-64 the divq path computes the
 * same quotient as `/` under the guards that select it (high half below the divisor);
 * this harness checks the C spelling, not the instruction.
 *
 * NARROWING: CBMC_WIDTH (passed per property by the Makefile) bounds the inputs. A
 * 64-bit input is the sign extension of a nondet CBMC_WIDTH-bit two's complement value
 * (so it covers the values nearest 0 and nearest 2^64 when read unsigned). A 128-bit
 * input is the sign extension of a nondet CBMC_WIDTH-bit value (CBMC_WIDTH up to 128).
 * CBMC_WIDTH=64 for a 64-bit input, or 128 for a 128-bit input, is the full domain.
 */
#if defined( CBMC_NATIVE_ARM ) || defined( CBMC_EMULATED_ARM )
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#if defined( CBMC_NATIVE_ARM )
#ifdef __x86_64__
#undef __x86_64__
#endif
#define CBMC_PREFIX cbmc_native_
#else
#define FIX_FORCE_EMULATED_INT128
#define CBMC_PREFIX cbmc_emulated_
#endif
/* SQRT SEED: CBMC has no body for __builtin_sqrt; left alone it reports "no body for
   callee __builtin_sqrt" and returns a free value per call, a different one in each arm,
   so the repair loops in fixISqrt128High never bound. Both arms' seed call is routed here
   to cbmc_isqrt_seed (defined in the harness TU). The macro is defined before fixed.h is
   included, so FIX_SQRT_SEED expands through it; no header is edited. The stub returns
   uint64_t, so the library's (uint64_t) cast of the seed is the identity.
   What the stub returns (see SEED MODEL in the harness TU): the harness rebuilds the
   library's double argument x from (hi, lo) with the library's own expression, which
   CBMC models bit-exactly (integer to double conversion, the exact scale by 2^64, the
   rounded add); the stub ASSERTS that each arm passes exactly that x. The seed is then
   ANY integer r that the IEEE 754 contract of sqrt allows for that x, one value shared
   by both arms. Nothing ties r to the exact root by assumption: the window that
   contains every allowed r is checked, and so is the bound on r. CBMC's own math.h
   sqrt model is not used (it bounds the root through rounded squares, so it is not the
   IEEE operation). */
uint64_t cbmc_isqrt_seed( double x );
#define __builtin_sqrt( x ) cbmc_isqrt_seed( x )
#include "fixed/fixed.h"
#if defined( CBMC_NATIVE_ARM ) && FIX_INT128_EMULATED
#error native arm built without __int128
#endif
#if defined( CBMC_EMULATED_ARM ) && !FIX_INT128_EMULATED
#error emulated arm built with native __int128
#endif
#define CAT_RAW( p, n ) p##n
#define CAT( p, n ) CAT_RAW( p, n )
#define API( n ) CAT( CBMC_PREFIX, n )
/* CBMC_PLANT: a planted fault, emulated arm only. It flips bit 0 of the first output of
   every emulated wrapper, so each property must then print VERIFICATION FAILED. A planted
   run that still passes means that run's assertion is unreachable (its assumptions leave
   no input), so `make plant` is also the vacuity check for every sign run. */
#if defined( CBMC_EMULATED_ARM ) && defined( CBMC_PLANT )
#define PLANT( v ) ( ( v ) ^ 1 )
#else
#define PLANT( v ) ( v )
#endif

static void out_u( fixUInt128 v, uint64_t* h, uint64_t* l ) { *h = fixUInt128Hi( v ); *l = fixUInt128Lo( v ); }
static void out_s( fixInt128 v, uint64_t* h, uint64_t* l ) { *h = fixInt128Hi( v ); *l = fixInt128Lo( v ); }

int64_t API( fix_mul )( int64_t a, int64_t b ) { return PLANT( fixMul( a, b ) ); }
int64_t API( fix_div )( int64_t a, int64_t b ) { return PLANT( fixDiv( a, b ) ); }
uint64_t API( isqrt )( uint64_t hi, uint64_t lo ) { return PLANT( fixISqrt128High( hi, lo ) ); }

void API( mul_u64 )( uint64_t a, uint64_t b, uint64_t* o )
{
	out_u( fixUInt128MulU64( a, b ), &o[0], &o[1] );
	o[0] = PLANT( o[0] );
}
void API( mul_i64 )( int64_t a, int64_t b, uint64_t* o )
{
	out_s( fixInt128MulI64( a, b ), &o[0], &o[1] );
	o[0] = PLANT( o[0] );
}
void API( mul_128 )( uint64_t ah, uint64_t al, uint64_t bh, uint64_t bl, uint64_t* o )
{
	out_u( fixUInt128Mul( fixUInt128Make( ah, al ), fixUInt128Make( bh, bl ) ), &o[0], &o[1] );
	out_s( fixInt128Mul( fixInt128Make( ah, al ), fixInt128Make( bh, bl ) ), &o[2], &o[3] );
	o[0] = PLANT( o[0] );
}
void API( addsub )( uint64_t ah, uint64_t al, uint64_t bh, uint64_t bl, uint64_t* o )
{
	fixUInt128 ua = fixUInt128Make( ah, al ), ub = fixUInt128Make( bh, bl );
	fixInt128 sa = fixInt128Make( ah, al ), sb = fixInt128Make( bh, bl );
	out_u( fixUInt128Add( ua, ub ), &o[0], &o[1] );
	out_u( fixUInt128Sub( ua, ub ), &o[2], &o[3] );
	out_s( fixInt128Add( sa, sb ), &o[4], &o[5] );
	out_s( fixInt128Sub( sa, sb ), &o[6], &o[7] );
	out_s( fixInt128FromI64( (int64_t)al ), &o[8], &o[9] );
	o[10] = (uint64_t)fixInt128ToI64( sa );
	o[0] = PLANT( o[0] );
}
void API( shifts )( uint64_t ah, uint64_t al, int n, uint64_t* o )
{
	fixUInt128 u = fixUInt128Make( ah, al );
	fixInt128 s = fixInt128Make( ah, al );
	out_u( fixUInt128Shl( u, n ), &o[0], &o[1] );
	out_u( fixUInt128Shr( u, n ), &o[2], &o[3] );
	out_s( fixInt128Shr( s, n ), &o[4], &o[5] );
	out_s( fixInt128ShiftLeft( s, n ), &o[6], &o[7] );
	o[0] = PLANT( o[0] );
}
void API( compares )( uint64_t ah, uint64_t al, uint64_t bh, uint64_t bl, bool* o )
{
	fixUInt128 ua = fixUInt128Make( ah, al ), ub = fixUInt128Make( bh, bl );
	fixInt128 sa = fixInt128Make( ah, al ), sb = fixInt128Make( bh, bl );
	o[0] = fixUInt128Eq( ua, ub ); o[1] = fixUInt128Lt( ua, ub ); o[2] = fixUInt128Gt( ua, ub );
	o[3] = fixUInt128Le( ua, ub ); o[4] = fixUInt128Ge( ua, ub );
	o[5] = fixInt128Eq( sa, sb ); o[6] = fixInt128Lt( sa, sb ); o[7] = fixInt128Gt( sa, sb );
	o[8] = fixInt128Le( sa, sb ); o[9] = fixInt128Ge( sa, sb );
	o[0] = PLANT( o[0] );
}
/* Unsigned 128/128 quotient and remainder: the native operators against the emulated
   shift-subtract divide (the native arm has no named seam function for these). */
void API( udivmod )( uint64_t ah, uint64_t al, uint64_t bh, uint64_t bl, uint64_t* o )
{
	fixUInt128 ua = fixUInt128Make( ah, al ), ub = fixUInt128Make( bh, bl );
#if FIX_INT128_EMULATED
	fixUInt128 q = fixEmuUInt128Div( ua, ub ), r = fixEmuUInt128Mod( ua, ub );
#else
	fixUInt128 q = ua / ub, r = ua % ub;
#endif
	out_u( q, &o[0], &o[1] );
	out_u( r, &o[2], &o[3] );
	o[0] = PLANT( o[0] );
}
void API( sdiv )( uint64_t ah, uint64_t al, uint64_t bh, uint64_t bl, uint64_t* o )
{
	out_s( fixInt128Div( fixInt128Make( ah, al ), fixInt128Make( bh, bl ) ), &o[0], &o[1] );
	o[0] = PLANT( o[0] );
}
void API( divrem64 )( uint64_t ah, uint64_t al, uint64_t d, uint64_t* o )
{
	uint64_t rem = 0;
	o[0] = fixUInt128DivRemBy64( fixUInt128Make( ah, al ), d, &rem );
	o[1] = rem;
	o[0] = PLANT( o[0] );
}

#else /* CBMC_HARNESS_TU */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#define CAT_RAW( p, n ) p##n
#define CAT( p, n ) CAT_RAW( p, n )
#define NATIVE( n ) CAT( cbmc_native_, n )
#define EMULATED( n ) CAT( cbmc_emulated_, n )
#define BOTH( n, proto ) extern proto( NATIVE( n ) ); extern proto( EMULATED( n ) )

#define P_FIX( f ) int64_t f( int64_t, int64_t )
#define P_SQRT( f ) uint64_t f( uint64_t, uint64_t )
#define P_M64U( f ) void f( uint64_t, uint64_t, uint64_t* )
#define P_M64S( f ) void f( int64_t, int64_t, uint64_t* )
#define P_2X128( f ) void f( uint64_t, uint64_t, uint64_t, uint64_t, uint64_t* )
#define P_SHIFT( f ) void f( uint64_t, uint64_t, int, uint64_t* )
#define P_CMP( f ) void f( uint64_t, uint64_t, uint64_t, uint64_t, bool* )
#define P_D64( f ) void f( uint64_t, uint64_t, uint64_t, uint64_t* )
BOTH( fix_mul, P_FIX );
BOTH( fix_div, P_FIX );
BOTH( isqrt, P_SQRT );
BOTH( mul_u64, P_M64U );
BOTH( mul_i64, P_M64S );
BOTH( mul_128, P_2X128 );
BOTH( addsub, P_2X128 );
BOTH( shifts, P_SHIFT );
BOTH( compares, P_CMP );
BOTH( udivmod, P_2X128 );
BOTH( sdiv, P_2X128 );
BOTH( divrem64, P_D64 );

/* CBMC's convention: a declared nondet_* function with no body returns a free value. */
unsigned long long nondet_ulonglong( void );
void __CPROVER_assume( _Bool );

#ifndef CBMC_WIDTH
#define CBMC_WIDTH 128
#endif
/* CBMC_WIDTH_B narrows the second input (the divisor, or the second factor) on its own;
   it defaults to CBMC_WIDTH. Every other input uses CBMC_WIDTH. */
#ifndef CBMC_WIDTH_B
#define CBMC_WIDTH_B CBMC_WIDTH
#endif

/* CBMC_SIGNS: a compile-time bit per input, in the order the harness draws them. Under
   a narrowing (CBMC_WIDTH below the input's width) input k is the free low CBMC_WIDTH
   bits extended with constant zeros (bit k clear) or constant ones (bit k set), so the
   solver sees constants above the width. The Makefile runs every combination of the
   bits a property uses, so the union is every sign extension of a CBMC_WIDTH-bit value. */
#ifndef CBMC_SIGNS
#define CBMC_SIGNS 0
#endif
static int input_index = 0;
static uint64_t ext64( uint64_t bits, int w, int negative )
{
	if ( w >= 64 ) return bits;
	uint64_t mask = ( UINT64_C( 1 ) << w ) - 1;
	return negative ? ( ( bits & mask ) | ~mask ) : ( bits & mask );
}
static int next_sign( void ) { return ( CBMC_SIGNS >> input_index ) & 1; }
static int next_width( void ) { return input_index++ == 1 ? CBMC_WIDTH_B : CBMC_WIDTH; }
/* A 64-bit input. */
static uint64_t in64( void )
{
	int negative = next_sign(), w = next_width();
	return ext64( (uint64_t)nondet_ulonglong(), w < 64 ? w : 64, negative );
}
/* A 128-bit input (hi, lo). */
static void in128( uint64_t* hi, uint64_t* lo )
{
	int negative = next_sign(), w = next_width();
	if ( w >= 128 )
	{
		*lo = (uint64_t)nondet_ulonglong();
		*hi = (uint64_t)nondet_ulonglong();
	}
	else if ( w > 64 )
	{
		*lo = (uint64_t)nondet_ulonglong();
		*hi = ext64( (uint64_t)nondet_ulonglong(), w - 64, negative );
	}
	else
	{
		*lo = ext64( (uint64_t)nondet_ulonglong(), w, negative );
		*hi = negative ? UINT64_MAX : 0;
	}
}
void __CPROVER_assert( _Bool, const char* );
/* Every equivalence assertion carries the description "arms agree", which `make plant`
   looks for. */
#define EQUIV( c ) __CPROVER_assert( ( c ), "arms agree" )
#define SAME( n, k ) do { for ( int i_ = 0; i_ < ( k ); ++i_ ) EQUIV( nat[i_] == emu[i_] ); } while ( 0 )

/* fixMul over every (a, b) in the width. */
void harness_fixmul( void )
{
	int64_t a = (int64_t)in64(), b = (int64_t)in64();
	EQUIV( NATIVE( fix_mul )( a, b ) == EMULATED( fix_mul )( a, b ) );
}
/* fixDiv over every (a, b) in the width, b == 0 included. */
void harness_fixdiv( void )
{
	int64_t a = (int64_t)in64(), b = (int64_t)in64();
	EQUIV( NATIVE( fix_div )( a, b ) == EMULATED( fix_div )( a, b ) );
}
/* fixDiv's 128-bit path: |a| >= 2^47, where fixDiv leaves its 64-bit fast path. a is
   2^47 | x (sign bit 0 clear) or its complement ~(2^47 | x) = -(2^47 | x) - 1 (set), x a
   free zero-extended CBMC_WIDTH-bit value; b is any 64-bit input narrowed by
   CBMC_WIDTH_B (b == 0 included). */
void harness_fixdiv_big( void )
{
	int negative = next_sign(), w = next_width();
	uint64_t m = ( UINT64_C( 1 ) << 47 ) | ext64( (uint64_t)nondet_ulonglong(), w < 47 ? w : 47, 0 );
	int64_t a = negative ? (int64_t)~m : (int64_t)m;
	int64_t b = (int64_t)in64();
	EQUIV( NATIVE( fix_div )( a, b ) == EMULATED( fix_div )( a, b ) );
}
/* fixISqrt128High, split by the function's own three paths (hi == 0; 0 < hi < 2^40;
   hi >= 2^40); the three domains are disjoint and cover every (hi, lo). */
/* SEED MODEL. The production seed is r = (uint64_t)t, t = RN(sqrt(x)), x the double
   argument. sqrt is correctly rounded (IEEE 754 requires it of sqrtsd and of every
   conforming sqrt), so |t - sqrt(x)| <= ulp(t)/2; here x <= 2^104, so t <= 2^52 and
   ulp(t) <= 1, hence |t - sqrt(x)| <= 1/2. With r = floor(t) <= t < r + 1:
   r - 1/2 <= sqrt(x) < r + 3/2, i.e. (2r - 1)^2 <= 4x < (2r + 3)^2 (exact integers;
   x is integer valued, x >= 1 on every path that calls the seed). Every seed the
   hardware can produce is in that set, so proving agreement for every r in it covers
   the production seed. The harness draws r = s + d with d in [-CBMC_SEED_WINDOW,
   CBMC_SEED_WINDOW] (s the exact integer root, known here), assumes the integer
   condition, and CHECKS that s - CBMC_SEED_WINDOW - 1 and s + CBMC_SEED_WINDOW + 1
   both fail it: both sides of the condition are monotone in r, so the allowed set is an
   interval, and those two checks put all of it inside the drawn window. It also CHECKS
   the seed lemma |r - s| <= CBMC_SEED_BOUND for every allowed r. */
#ifndef CBMC_SEED_WINDOW
#define CBMC_SEED_WINDOW 4
#endif
#ifndef CBMC_SEED_BOUND
#define CBMC_SEED_BOUND 3
#endif
static double seed_arg;
static uint64_t seed_value;
uint64_t cbmc_isqrt_seed( double x )
{
	__CPROVER_assert( x == seed_arg, "seed argument is the library's double expression" );
	return seed_value;
}
typedef unsigned __int128 u128;
/* The IEEE condition for seed r against 4x: (2r - 1)^2 <= 4x < (2r + 3)^2. */
static int seed_allowed( int64_t r, u128 x4 )
{
	if ( r < 0 ) return 0;
	u128 m = r == 0 ? 1 : (u128)( 2 * r - 1 ); /* |2r - 1|; r == 0 gives 1 */
	u128 lo = m * m;
	u128 hi = (u128)( 2 * r + 3 ) * (u128)( 2 * r + 3 );
	return lo <= x4 && x4 < hi;
}
static void isqrt_on( uint64_t hi, uint64_t lo, uint64_t s )
{
	seed_arg = hi != 0 ? (double)hi * 18446744073709551616.0 + (double)lo : (double)lo;
	if ( hi == 0 && lo == 0 )
	{
		/* No seed call on this input (fixISqrt128High returns 0 first). */
		seed_value = 0;
	}
	else
	{
		u128 x4 = (u128)seed_arg * 4;
		int64_t sd = (int64_t)s;
		int64_t d = (int64_t)( nondet_ulonglong() % ( 2 * CBMC_SEED_WINDOW + 1 ) ) - CBMC_SEED_WINDOW;
		__CPROVER_assert( !seed_allowed( sd - CBMC_SEED_WINDOW - 1, x4 ), "every allowed seed is above the window floor" );
		__CPROVER_assert( !seed_allowed( sd + CBMC_SEED_WINDOW + 1, x4 ), "every allowed seed is below the window ceiling" );
		__CPROVER_assume( seed_allowed( sd + d, x4 ) );
		__CPROVER_assert( d <= CBMC_SEED_BOUND && -d <= CBMC_SEED_BOUND, "seed within CBMC_SEED_BOUND of the exact root" );
		seed_value = (uint64_t)( sd + d );
	}
	EQUIV( NATIVE( isqrt )( hi, lo ) == EMULATED( isqrt )( hi, lo ) );
}
/* n = s*s + e with e in [0, 2s] is exactly the set of n whose integer root is s. */
static void isqrt_with_root( uint64_t s, uint64_t base, int w )
{
	/* e <= 2s < 2^(w+2) when s < 2^(w+1); for the mid path s < 2^52, so 54 bits hold e. */
	int we = base == 0 ? w + 2 : 54;
	uint64_t e = ext64( (uint64_t)nondet_ulonglong(), we < 64 ? we : 64, 0 );
	__CPROVER_assume( e <= 2 * s );
	unsigned __int128 n = (unsigned __int128)s * s + e;
	/* On the hi == 0 path pass a literal 0, so CBMC prunes the other two paths instead of
	   carrying them under a guard it would have to prove false through the multiply. */
	if ( base == 0 )
		isqrt_on( 0, (uint64_t)n, s );
	else
		isqrt_on( (uint64_t)( n >> 64 ), (uint64_t)n, s );
}
/* hi == 0: the exact root s < 2^32. Narrowed, s is a zero-extended CBMC_WIDTH-bit value
   (CBMC_WIDTH=32 is the whole path). */
void harness_isqrt_low( void )
{
	int w = CBMC_WIDTH < 32 ? CBMC_WIDTH : 32;
	uint64_t s = ext64( (uint64_t)nondet_ulonglong(), w, 0 );
	/* s < 2^32 makes n = s*s + e <= (s + 1)^2 - 1 < 2^64, so hi really is 0. */
	isqrt_with_root( s, 0, w );
}
/* 0 < hi < 2^40: the exact root s in [2^32, 2^52). Narrowed, s = CBMC_SQRT_BASE + x with
   x a zero-extended CBMC_WIDTH-bit value. */
#ifndef CBMC_SQRT_BASE
#define CBMC_SQRT_BASE ( UINT64_C( 1 ) << 32 )
#endif
void harness_isqrt_mid( void )
{
	int w = CBMC_WIDTH < 52 ? CBMC_WIDTH : 52;
	uint64_t x = ext64( (uint64_t)nondet_ulonglong(), w, 0 );
	uint64_t s = (uint64_t)( CBMC_SQRT_BASE ) + x;
	__CPROVER_assume( s >= ( UINT64_C( 1 ) << 32 ) && s < ( UINT64_C( 1 ) << 52 ) );
	isqrt_with_root( s, (uint64_t)( CBMC_SQRT_BASE ), w );
}
/* hi >= 2^40: the shift-subtract path; no seed. hi is input 0 and lo input 1. Narrowed
   (CBMC_WIDTH below 40), hi is 2^40 + x (sign bit clear) or the ones extension of x (set),
   x a free CBMC_WIDTH-bit value; lo narrows by CBMC_WIDTH_B as any 64-bit input. */
void harness_isqrt_high( void )
{
	int negative = next_sign(), w = next_width();
	uint64_t hi = ext64( (uint64_t)nondet_ulonglong(), w < 64 ? w : 64, negative );
	if ( w < 40 && !negative ) hi |= UINT64_C( 1 ) << 40;
	uint64_t lo = in64();
	__CPROVER_assume( hi >= ( UINT64_C( 1 ) << 40 ) );
	/* This path makes no seed call; call the arms directly. */
	EQUIV( NATIVE( isqrt )( hi, lo ) == EMULATED( isqrt )( hi, lo ) );
}
/* The 64x64 -> 128 products (unsigned and signed widening). */
void harness_mul_u64( void )
{
	uint64_t a = in64(), b = in64(), nat[2], emu[2];
	NATIVE( mul_u64 )( a, b, nat ); EMULATED( mul_u64 )( a, b, emu ); SAME( nat, 2 );
}
void harness_mul_i64( void )
{
	int64_t a = (int64_t)in64(), b = (int64_t)in64();
	uint64_t nat[2], emu[2];
	NATIVE( mul_i64 )( a, b, nat ); EMULATED( mul_i64 )( a, b, emu ); SAME( nat, 2 );
}
/* The 128x128 -> 128 wrapping products (unsigned and signed). */
void harness_mul_128( void )
{
	uint64_t ah, al, bh, bl, nat[4], emu[4];
	in128( &ah, &al ); in128( &bh, &bl );
	NATIVE( mul_128 )( ah, al, bh, bl, nat ); EMULATED( mul_128 )( ah, al, bh, bl, emu ); SAME( nat, 4 );
}
void harness_addsub( void )
{
	uint64_t ah, al, bh, bl, nat[11], emu[11];
	in128( &ah, &al ); in128( &bh, &bl );
	NATIVE( addsub )( ah, al, bh, bl, nat ); EMULATED( addsub )( ah, al, bh, bl, emu ); SAME( nat, 11 );
}
/* Shift counts in [0, 127]: outside that range native __int128 shifts are undefined
   (fixed_int128.h says so), so there is no native answer to match. */
void harness_shifts( void )
{
	uint64_t ah, al, nat[8], emu[8];
	in128( &ah, &al );
	int n = (int)( (uint64_t)nondet_ulonglong() % 128 );
	NATIVE( shifts )( ah, al, n, nat ); EMULATED( shifts )( ah, al, n, emu ); SAME( nat, 8 );
}
void harness_compares( void )
{
	uint64_t ah, al, bh, bl;
	bool nat[10], emu[10];
	in128( &ah, &al ); in128( &bh, &bl );
	NATIVE( compares )( ah, al, bh, bl, nat ); EMULATED( compares )( ah, al, bh, bl, emu ); SAME( nat, 10 );
}
/* Divisor zero is undefined natively and excluded (the emulated arm returns 0). */
void harness_udivmod( void )
{
	uint64_t ah, al, bh, bl, nat[4], emu[4];
	in128( &ah, &al ); in128( &bh, &bl );
	__CPROVER_assume( bh != 0 || bl != 0 );
	NATIVE( udivmod )( ah, al, bh, bl, nat ); EMULATED( udivmod )( ah, al, bh, bl, emu ); SAME( nat, 4 );
}
/* Divisor zero and INT128_MIN / -1 are undefined natively and excluded. */
void harness_sdiv( void )
{
	uint64_t ah, al, bh, bl, nat[2], emu[2];
	in128( &ah, &al ); in128( &bh, &bl );
	__CPROVER_assume( bh != 0 || bl != 0 );
	__CPROVER_assume( !( ah == UINT64_C( 0x8000000000000000 ) && al == 0 && bh == UINT64_MAX && bl == UINT64_MAX ) );
	NATIVE( sdiv )( ah, al, bh, bl, nat ); EMULATED( sdiv )( ah, al, bh, bl, emu ); SAME( nat, 2 );
}
/* fixUInt128DivRemBy64's documented precondition: hi < d (so d != 0). The two limbs and
   the divisor are three separate 64-bit inputs (hi, lo, d), so a narrowed run still has
   nonzero high limbs below d; a sign-extended 128-bit input would make hi 0 or ~0, and
   ~0 < d never holds. The sign runs with hi ones-extended and d zero-extended (CBMC_SIGNS
   1 and 3) leave no input (hi >= 2^64 - 2^w > d) and are not run; `make plant` checks
   that every run that is run reaches its assertion. */
void harness_divrem64( void )
{
	uint64_t nat[2], emu[2];
	uint64_t ah = in64();
	uint64_t al = in64();
	uint64_t d = in64();
	__CPROVER_assume( ah < d );
	NATIVE( divrem64 )( ah, al, d, nat ); EMULATED( divrem64 )( ah, al, d, emu ); SAME( nat, 2 );
}
#endif
