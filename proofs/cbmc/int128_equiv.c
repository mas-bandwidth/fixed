/*
 * CBMC equivalence harness. The Makefile compiles this source as separate native,
 * forced-emulated, and harness translation units so each library arm gets its own
 * fixed.h inclusion while the harness links both sets of wrappers.
 */
#if defined(CBMC_NATIVE_ARM) || defined(CBMC_EMULATED_ARM)
#if defined(CBMC_NATIVE_ARM)
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Keep the native arithmetic operator but avoid fixed.h's x86-only divq inline asm;
   CBMC models C's native unsigned/signed 128-bit division directly. */
#ifdef __x86_64__
#undef __x86_64__
#endif
#define CBMC_PREFIX cbmc_native_
#else
#define FIX_FORCE_EMULATED_INT128
#define CBMC_PREFIX cbmc_emulated_
#endif
#include "fixed/fixed.h"
#define CAT_RAW(p, n) p##n
#define CAT(p, n) CAT_RAW(p, n)
#define API(n) CAT(CBMC_PREFIX, n)

int64_t API(fix_mul)( int64_t a, int64_t b ) { return fixMul( a, b ); }
int64_t API(fix_div)( int64_t a, int64_t b ) { return fixDiv( a, b ); }
uint64_t API(isqrt)( uint64_t hi, uint64_t lo ) { return fixISqrt128High( hi, lo ); }
void API(u128_mul)( uint64_t ah, uint64_t al, uint64_t bh, uint64_t bl, uint64_t* rh, uint64_t* rl )
{
	fixUInt128 r = fixUInt128Mul( fixUInt128Make( ah, al ), fixUInt128Make( bh, bl ) );
	*rh = fixUInt128Hi( r ); *rl = fixUInt128Lo( r );
}
void API(i128_mul)( uint64_t ah, uint64_t al, uint64_t bh, uint64_t bl, uint64_t* rh, uint64_t* rl )
{
	fixInt128 r = fixInt128Mul( fixInt128Make( ah, al ), fixInt128Make( bh, bl ) );
	*rh = fixInt128Hi( r ); *rl = fixInt128Lo( r );
}
void API(mul_i64)( int64_t a, int64_t b, uint64_t* rh, uint64_t* rl )
{
	fixInt128 r = fixInt128MulI64( a, b );
	*rh = fixInt128Hi( r ); *rl = fixInt128Lo( r );
}
void API(shifts)( uint64_t ah, uint64_t al, int n, uint64_t* ulh, uint64_t* ull,
	uint64_t* urh, uint64_t* url, uint64_t* srh, uint64_t* srl )
{
	fixUInt128 u = fixUInt128Make( ah, al );
	fixInt128 s = fixInt128Make( ah, al );
	fixUInt128 ul = fixUInt128Shl( u, n ), ur = fixUInt128Shr( u, n );
	fixInt128 sr = fixInt128Shr( s, n );
	*ulh = fixUInt128Hi( ul ); *ull = fixUInt128Lo( ul );
	*urh = fixUInt128Hi( ur ); *url = fixUInt128Lo( ur );
	*srh = fixInt128Hi( sr ); *srl = fixInt128Lo( sr );
}
void API(compares)( uint64_t ah, uint64_t al, uint64_t bh, uint64_t bl,
	bool* ueq, bool* ult, bool* seq, bool* slt )
{
	fixUInt128 ua = fixUInt128Make( ah, al ), ub = fixUInt128Make( bh, bl );
	fixInt128 sa = fixInt128Make( ah, al ), sb = fixInt128Make( bh, bl );
	*ueq = fixUInt128Eq( ua, ub ); *ult = fixUInt128Lt( ua, ub );
	*seq = fixInt128Eq( sa, sb ); *slt = fixInt128Lt( sa, sb );
}
void API(divisions)( uint64_t ah, uint64_t al, uint64_t bh, uint64_t bl, uint64_t d,
	uint64_t* uqh, uint64_t* uql, uint64_t* urh, uint64_t* url,
	uint64_t* sqh, uint64_t* sql, uint64_t* by64q, uint64_t* by64r )
{
	fixUInt128 ua = fixUInt128Make( ah, al ), ub = fixUInt128Make( bh, bl );
	#if FIX_INT128_EMULATED
	fixUInt128 uq = fixEmuUInt128Div( ua, ub ), ur = fixEmuUInt128Mod( ua, ub );
#else
	fixUInt128 uq = ua / ub, ur = ua % ub;
#endif
	fixInt128 sa = fixInt128Make( ah, al ), sb = fixInt128Make( bh, bl );
	fixInt128 sq = fixInt128Div( sa, sb );
	uint64_t rem = 0, q = fixUInt128DivRemBy64( ua, d, &rem );
	*uqh = fixUInt128Hi( uq ); *uql = fixUInt128Lo( uq );
	*urh = fixUInt128Hi( ur ); *url = fixUInt128Lo( ur );
	*sqh = fixInt128Hi( sq ); *sql = fixInt128Lo( sq );
	*by64q = q; *by64r = rem;
}
#undef API
#undef CAT
#undef CAT_RAW
#undef CBMC_PREFIX

#else
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#define CAT_RAW(p, n) p##n
#define CAT(p, n) CAT_RAW(p, n)
#define NATIVE(n) CAT(cbmc_native_, n)
#define EMULATED(n) CAT(cbmc_emulated_, n)

extern int64_t NATIVE(fix_mul)( int64_t, int64_t );
extern int64_t EMULATED(fix_mul)( int64_t, int64_t );
extern int64_t NATIVE(fix_div)( int64_t, int64_t );
extern int64_t EMULATED(fix_div)( int64_t, int64_t );
extern uint64_t NATIVE(isqrt)( uint64_t, uint64_t );
extern uint64_t EMULATED(isqrt)( uint64_t, uint64_t );
extern void NATIVE(u128_mul)( uint64_t, uint64_t, uint64_t, uint64_t, uint64_t*, uint64_t* );
extern void EMULATED(u128_mul)( uint64_t, uint64_t, uint64_t, uint64_t, uint64_t*, uint64_t* );
extern void NATIVE(i128_mul)( uint64_t, uint64_t, uint64_t, uint64_t, uint64_t*, uint64_t* );
extern void EMULATED(i128_mul)( uint64_t, uint64_t, uint64_t, uint64_t, uint64_t*, uint64_t* );
extern void NATIVE(mul_i64)( int64_t, int64_t, uint64_t*, uint64_t* );
extern void EMULATED(mul_i64)( int64_t, int64_t, uint64_t*, uint64_t* );
extern void NATIVE(shifts)( uint64_t, uint64_t, int, uint64_t*, uint64_t*, uint64_t*, uint64_t*, uint64_t*, uint64_t* );
extern void EMULATED(shifts)( uint64_t, uint64_t, int, uint64_t*, uint64_t*, uint64_t*, uint64_t*, uint64_t*, uint64_t* );
extern void NATIVE(compares)( uint64_t, uint64_t, uint64_t, uint64_t, bool*, bool*, bool*, bool* );
extern void EMULATED(compares)( uint64_t, uint64_t, uint64_t, uint64_t, bool*, bool*, bool*, bool* );
extern void NATIVE(divisions)( uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t*, uint64_t*, uint64_t*, uint64_t*, uint64_t*, uint64_t*, uint64_t*, uint64_t* );
extern void EMULATED(divisions)( uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t*, uint64_t*, uint64_t*, uint64_t*, uint64_t*, uint64_t*, uint64_t*, uint64_t* );
extern long long nondet_longlong( void );
extern unsigned long long nondet_ulonglong( void );
extern void __CPROVER_assume( _Bool );
#ifndef CBMC_WIDTH
#define CBMC_WIDTH 64
#endif
static int64_t nondet_i64( void )
{
#if CBMC_WIDTH < 64
	uint64_t mask = ( UINT64_C(1) << CBMC_WIDTH ) - 1;
	uint64_t sign = UINT64_C(1) << ( CBMC_WIDTH - 1 );
	uint64_t bits = (uint64_t)nondet_ulonglong() & mask;
	if ( ( bits & sign ) == 0 ) return (int64_t)bits;
	return -(int64_t)( ( ( ~bits ) & mask ) + 1 );
#else
	return (int64_t)nondet_longlong();
#endif
}

void harness_fixmul( void )
{
	int64_t a = nondet_i64(), b = nondet_i64();
	assert( NATIVE(fix_mul)( a, b ) == EMULATED(fix_mul)( a, b ) );
}
void harness_fixdiv( void )
{
	int64_t a = nondet_i64(), b = nondet_i64();
	assert( NATIVE(fix_div)( a, b ) == EMULATED(fix_div)( a, b ) );
}
void harness_isqrt( void )
{
	uint64_t hi = (uint64_t)nondet_ulonglong(), lo = (uint64_t)nondet_ulonglong();
	assert( NATIVE(isqrt)( hi, lo ) == EMULATED(isqrt)( hi, lo ) );
}
void harness_multiply( void )
{
	uint64_t ah = (uint64_t)nondet_ulonglong(), al = (uint64_t)nondet_ulonglong();
	uint64_t bh = (uint64_t)nondet_ulonglong(), bl = (uint64_t)nondet_ulonglong();
	int64_t a = (int64_t)nondet_longlong(), b = (int64_t)nondet_longlong();
	uint64_t nh, nl, eh, el, nsh, nsl, esh, esl;
	NATIVE(u128_mul)( ah, al, bh, bl, &nh, &nl ); EMULATED(u128_mul)( ah, al, bh, bl, &eh, &el ); assert( nh == eh && nl == el );
	NATIVE(i128_mul)( ah, al, bh, bl, &nh, &nl ); EMULATED(i128_mul)( ah, al, bh, bl, &eh, &el ); assert( nh == eh && nl == el );
	NATIVE(mul_i64)( a, b, &nsh, &nsl ); EMULATED(mul_i64)( a, b, &esh, &esl ); assert( nsh == esh && nsl == esl );
}
void harness_shifts( void )
{
	uint64_t ah = (uint64_t)nondet_ulonglong(), al = (uint64_t)nondet_ulonglong();
	int n = (int)((uint64_t)nondet_ulonglong() % 128);
	uint64_t a[6], b[6];
	NATIVE(shifts)( ah, al, n, &a[0], &a[1], &a[2], &a[3], &a[4], &a[5] );
	EMULATED(shifts)( ah, al, n, &b[0], &b[1], &b[2], &b[3], &b[4], &b[5] );
	for ( int i = 0; i < 6; ++i ) assert( a[i] == b[i] );
}
void harness_compares( void )
{
	uint64_t ah = (uint64_t)nondet_ulonglong(), al = (uint64_t)nondet_ulonglong();
	uint64_t bh = (uint64_t)nondet_ulonglong(), bl = (uint64_t)nondet_ulonglong();
	bool a[4], b[4];
	NATIVE(compares)( ah, al, bh, bl, &a[0], &a[1], &a[2], &a[3] );
	EMULATED(compares)( ah, al, bh, bl, &b[0], &b[1], &b[2], &b[3] );
	for ( int i = 0; i < 4; ++i ) assert( a[i] == b[i] );
}
void harness_divisions( void )
{
	uint64_t ah = (uint64_t)nondet_ulonglong(), al = (uint64_t)nondet_ulonglong();
	uint64_t bh = (uint64_t)nondet_ulonglong(), bl = (uint64_t)nondet_ulonglong();
	uint64_t d = (uint64_t)nondet_ulonglong();
	uint64_t nq[5], nr[5], eq[5], er[5];
	__CPROVER_assume( bh != 0 || bl != 0 );
	/* Native signed division is undefined for INT128_MIN / -1. */
	__CPROVER_assume( !( ah == UINT64_C(0x8000000000000000) && al == 0 && bh == UINT64_MAX && bl == UINT64_MAX ) );
	/* Native 128/64 divide has the documented Knuth precondition. */
	__CPROVER_assume( d != 0 && ah < d );
	NATIVE(divisions)( ah, al, bh, bl, d, &nq[0], &nq[1], &nr[0], &nr[1], &nq[2], &nq[3], &nq[4], &nr[4] );
	EMULATED(divisions)( ah, al, bh, bl, d, &eq[0], &eq[1], &er[0], &er[1], &eq[2], &eq[3], &eq[4], &er[4] );
	for ( int i = 0; i < 5; ++i ) { assert( nq[i] == eq[i] ); assert( nr[i] == er[i] ); }
}
#undef NATIVE
#undef EMULATED
#undef CAT
#undef CAT_RAW
#endif
