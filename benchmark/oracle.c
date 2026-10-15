// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
// THE OUTPUT ORACLE: one 64-bit hash per public hot operation, over a defined input set.
//
// This translation unit is the ENGINE and has no main(). Three programs drive it:
//   benchmark/oracle_check.c    diffs a run against benchmark/oracle-baseline.txt
//   test/oracle_unit_test.c     diffs a 1,000-input prefix against hashes baked in the test
//   benchmark/op_bench.c        #includes this file so a timed input is this same input
//
// THE GUARANTEE THIS EXISTS TO POLICE: every result in this library is bit-identical on
// every platform, architecture and compiler. A hash here is a claim about OUTPUT BITS, so
// the same build of the same tree must produce the same number everywhere, and a change
// that is faster on one arm and different on another shows up as a moved hash rather than
// as a benchmark that looks good. Running the whole file a second time under
// FIX_FORCE_EMULATED_INT128 and getting the same hashes is itself a cross-check of the
// emulated 128-bit pair.
//
// INPUT SETS, per operation, in this order (all three feed ONE running hash):
//   EDGE    the structured edge grid: 0, +-1, +-2^k, +-(2^k-1) for every k, INT64_MIN,
//           INT64_MAX and the saturation values. Unary operations walk it once; binary
//           and wider operations walk it CROSSED WITH ITSELF (grid x grid).
//   EXHAUST where feasible: fixSqrt over every raw input below 2^32, fixSin/fixCos/
//           fixComputeCosSin over every raw angle in [-2 pi, 2 pi], fixLog2/fixExp2 over
//           a dense grid. Flagged per operation in the table below.
//   RANDOM  seeded pseudo-random inputs from the fixed xoshiro256** stream below. The
//           seed is a constant in this file, so the stream is identical everywhere.
//           Composite operations draw seeded random vectors, quaternions, matrices and
//           AABBs from the same stream.
//
// RANDOM COUNT. The base count is a parameter of every run and is RECORDED IN THE
// BASELINE FILE, so a hash is reproducible rather than merely frozen. The committed
// base is 50000000 seeded xoshiro256** draws per operation, BEFORE the weight divisor.
// Each operation divides that base by its own cost weight (in the table): weight 100
// (fixUInt256DivMod) is 500,000 draws, not 50,000,000. A flat 50 million on every row
// would make the oracle's runtime a property of its most expensive row.
//
// ASSERT SAFETY. The oracle runs under ctest, where NDEBUG is NOT defined and FIX_ASSERT
// is live, and under -DNDEBUG for the benchmark build. Every fill below therefore
// produces inputs the operation's own contract accepts: quaternion slots are normalized
// (and fall back to the identity if normalization lands outside the validity tolerance),
// alpha slots are inside [0, 1], quantize scales are strictly positive, and 128/256-bit
// divisors are non-zero and bounded so no quotient can overflow its width. An oracle that
// aborts on an assert proves nothing about output bits.
#include "fixed/fixed.h"
#include "fixed/fixed_math.h"
#include "fixed/fixed_vec.h"
#include "fixed/fixed_quantize.h"
#include "fixed/fixed_int256.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FIX_ORACLE_SLOTS 16
#define FIX_ORACLE_SEGMENTS 16

typedef struct fixOracleInput
{
	int64_t s[FIX_ORACLE_SLOTS];
} fixOracleInput;

typedef struct fixOracleOutput
{
	int64_t s[FIX_ORACLE_SLOTS];
	int n;
} fixOracleOutput;

// --------------------------------------------------------------------------------------
// The seeded stream. xoshiro256**, seeded from four constants that live here so that the
// input set is a property of the source and not of a machine.
// --------------------------------------------------------------------------------------
#define FIX_ORACLE_SEED0 UINT64_C( 0x9E3779B97F4A7C15 )
#define FIX_ORACLE_SEED1 UINT64_C( 0xBF58476D1CE4E5B9 )
#define FIX_ORACLE_SEED2 UINT64_C( 0x94D049BB133111EB )
#define FIX_ORACLE_SEED3 UINT64_C( 0x243F6A8885A308D3 )

static uint64_t oracleRotl( const uint64_t x, int k ) { return ( x << k ) | ( x >> ( 64 - k ) ); }

typedef struct fixOracleRng
{
	uint64_t s[4];
} fixOracleRng;

static void oracleRngSeed( fixOracleRng* r )
{
	r->s[0] = FIX_ORACLE_SEED0;
	r->s[1] = FIX_ORACLE_SEED1;
	r->s[2] = FIX_ORACLE_SEED2;
	r->s[3] = FIX_ORACLE_SEED3;
}

static uint64_t oracleRngNext( fixOracleRng* r )
{
	const uint64_t result = oracleRotl( r->s[1] * 5, 7 ) * 9;
	const uint64_t t = r->s[1] << 17;
	r->s[2] ^= r->s[0];
	r->s[3] ^= r->s[1];
	r->s[1] ^= r->s[2];
	r->s[0] ^= r->s[3];
	r->s[2] ^= t;
	r->s[3] = oracleRotl( r->s[3], 45 );
	return result;
}

// --------------------------------------------------------------------------------------
// Hash: FNV-1a over the output words. Order and width are part of the frozen value.
// --------------------------------------------------------------------------------------
static uint64_t oracleHashWord( uint64_t h, uint64_t v )
{
	for ( int i = 0; i < 8; ++i )
	{
		h ^= ( v >> ( i * 8 ) ) & 0xFF;
		h *= UINT64_C( 0x00000100000001B3 );
	}
	return h;
}

static uint64_t oracleHashInit( const char* name )
{
	uint64_t h = UINT64_C( 0xCBF29CE484222325 );
	for ( const char* p = name; *p; ++p )
	{
		h ^= (uint64_t)(unsigned char)*p;
		h *= UINT64_C( 0x00000100000001B3 );
	}
	return h;
}

// --------------------------------------------------------------------------------------
// THE STRUCTURED EDGE GRID.
// --------------------------------------------------------------------------------------
// 252 distinct values (counted from the pushes below). 160 was a buffer overrun: the
// writes past the array are what a 252-long grid used to live in, and the frozen hashes
// were taken from those values. The cap is the real set, in bounds, so the same values
// stay the hashed ones.
enum { ORACLE_GRID_CAP = 320 };
static int64_t oracleGrid[ORACLE_GRID_CAP];
static int oracleGridCount = 0;

static void oracleGridPush( int64_t v )
{
	for ( int i = 0; i < oracleGridCount; ++i )
	{
		if ( oracleGrid[i] == v ) return;
	}
	if ( oracleGridCount >= ORACLE_GRID_CAP )
	{
		fprintf( stderr, "oracle grid exceeded %d\n", ORACLE_GRID_CAP );
		abort();
	}
	oracleGrid[oracleGridCount++] = v;
}

static void oracleGridBuild( void )
{
	if ( oracleGridCount != 0 ) return;
	oracleGridPush( 0 );
	oracleGridPush( 1 );
	oracleGridPush( -1 );
	oracleGridPush( FIX_ONE );
	oracleGridPush( -FIX_ONE );
	oracleGridPush( FIX_HALF );
	oracleGridPush( INT64_MAX );
	oracleGridPush( INT64_MIN );
	oracleGridPush( FIX_MAX );
	oracleGridPush( FIX_MIN );
	for ( int k = 1; k <= 62; ++k )
	{
		const int64_t p = (int64_t)1 << k;
		oracleGridPush( p );
		oracleGridPush( -p );
		oracleGridPush( p - 1 );
		oracleGridPush( -( p - 1 ) );
		oracleGridPush( (int64_t)( (uint64_t)p << 16 ) ); // the same k in the Q48.16 domain
		oracleGridPush( -(int64_t)( (uint64_t)p << 16 ) );
	}
}

// --------------------------------------------------------------------------------------
// Slot fills. Every one is a pure function of the drawn words, so a run is repeatable.
// --------------------------------------------------------------------------------------
static fixQuat oracleQuat( fixOracleRng* rng_or_null, uint64_t r0, uint64_t r1 )
{
	fixQuat raw = { { (int64_t)( r0 % 200001 ) - 100000, (int64_t)( ( r0 >> 21 ) % 200001 ) - 100000,
					  (int64_t)( r1 % 200001 ) - 100000 },
					(int64_t)( ( r1 >> 21 ) % 200001 ) - 100000 };
	( void )rng_or_null;
	fixQuat q = fixNormalizeQuat( raw );
	// The validity tolerance is the operation's own contract. If normalization lands
	// outside it, the identity is used rather than feeding the next call an assert.
	if ( !fixIsNormalizedQuat( q ) )
	{
		fixQuat id = { { 0, 0, 0 }, FIX_ONE };
		return id;
	}
	return q;
}

// Draw the two words of a random quaternion in a SEQUENCED order. The draws used to be
// written as two arguments of one call, oracleQuat( NULL, next( rng ), next( rng ) ), and
// C leaves the order of evaluation of function arguments unspecified (C11 6.5.2.2p10):
// GCC 13 on x86-64 drew the second argument first, Apple clang 21 on arm64 drew the first
// argument first, so the two machines hashed the same operation over swapped quaternions
// and six rows disagreed (benchmark/findings/oracle-rows-x86-arm64.md). The baseline
// was written by the GCC order, so that order is the one kept: r1 is drawn, then r0.
static fixQuat oracleQuatDraw( fixOracleRng* rng )
{
	const uint64_t r1 = oracleRngNext( rng );
	const uint64_t r0 = oracleRngNext( rng );
	return oracleQuat( NULL, r0, r1 );
}

static fixQuat oracleQuatFrom( int64_t a, int64_t b, int64_t c, int64_t d )
{
	fixQuat raw = { { a, b, c }, d };
	fixQuat q = fixNormalizeQuat( raw );
	if ( !fixIsNormalizedQuat( q ) )
	{
		fixQuat id = { { 0, 0, 0 }, FIX_ONE };
		return id;
	}
	return q;
}

static fixed_t oracleAlpha( uint64_t r ) { return (fixed_t)( r % (uint64_t)( FIX_ONE + 1 ) ); }

// fixQuantize's domain: the rounded scaled value must fit int64_t. Outside it the
// double-to-int64 conversion is undefined behaviour (C11 6.3.1.4p1) and the hardware
// answers differ: x86-64 cvttsd2si returns INT64_MIN, arm64 fcvtzs saturates to
// INT64_MAX. The oracle polices output bits over inputs the operation's contract accepts,
// so a raw value whose scaled magnitude would leave int64_t is halved (toward zero) until
// it is inside. The test is the same double arithmetic applyQuantize and fixQuantize do,
// which is IEEE-754 and identical everywhere, so the input set is the same on every target.
static int64_t oracleQuantizeInRange( int64_t raw, int64_t scaleSlot )
{
	const int64_t scale = scaleSlot > 0 ? scaleSlot : 1;
	for ( ;; )
	{
		const double scaled = ( ( double )raw / ( double )FIX_ONE ) * ( double )scale;
		const double rounded = scaled >= 0.0 ? scaled + 0.5 : -scaled + 0.5;
		if ( rounded < 9223372036854775808.0 ) return raw;
		raw /= 2;
	}
}

static fixVec3 oracleVec( int64_t a, int64_t b, int64_t c )
{
	fixVec3 v = { a, b, c };
	return v;
}

// --------------------------------------------------------------------------------------
// THE OPERATION TABLE.
//   arity    slots the operation reads
//   weight   divides the committed base (50000000) : weight 100 is 500,000 draws.
//            A flat count would make the runtime a property of the most expensive row.
//   exhaustive  0 none, 1 every raw input below 2^32, 2 every raw angle in [-2 pi, 2 pi],
//               3 the dense log2/exp2 grid
// --------------------------------------------------------------------------------------
typedef void ( *fixOracleFill )( fixOracleInput*, uint64_t index, fixOracleRng* );
typedef void ( *fixOracleApply )( const fixOracleInput*, fixOracleOutput* );

typedef struct fixOracleOp
{
	const char* name;
	int arity;
	int weight;
	int exhaustive;
	fixOracleFill fill;
	fixOracleApply apply;
} fixOracleOp;

#define ORACLE_OUT1( out, v )     \
	do                            \
	{                             \
		( out )->s[0] = ( v );    \
		( out )->n = 1;           \
	} while ( 0 )
#define ORACLE_OUT_VEC( out, v )                       \
	do                                                 \
	{                                                  \
		( out )->s[0] = ( v ).x;                       \
		( out )->s[1] = ( v ).y;                       \
		( out )->s[2] = ( v ).z;                       \
		( out )->n = 3;                                \
	} while ( 0 )
#define ORACLE_OUT_QUAT( out, q )                      \
	do                                                 \
	{                                                  \
		( out )->s[0] = ( q ).v.x;                     \
		( out )->s[1] = ( q ).v.y;                     \
		( out )->s[2] = ( q ).v.z;                     \
		( out )->s[3] = ( q ).s;                       \
		( out )->n = 4;                                \
	} while ( 0 )
#define ORACLE_OUT_MAT( out, m )                       \
	do                                                 \
	{                                                  \
		( out )->s[0] = ( m ).cx.x;                    \
		( out )->s[1] = ( m ).cx.y;                    \
		( out )->s[2] = ( m ).cx.z;                    \
		( out )->s[3] = ( m ).cy.x;                    \
		( out )->s[4] = ( m ).cy.y;                    \
		( out )->s[5] = ( m ).cy.z;                    \
		( out )->s[6] = ( m ).cz.x;                    \
		( out )->s[7] = ( m ).cz.y;                    \
		( out )->s[8] = ( m ).cz.z;                    \
		( out )->n = 9;                                \
	} while ( 0 )

// A double output is hashed as its bits: the value is exact (a quotient of two integers)
// and its representation is IEEE-754 on every supported target.
static int64_t oracleDoubleBits( double v )
{
	uint64_t bits = 0;
	memcpy( &bits, &v, sizeof bits );
	return (int64_t)bits;
}

#define FILL_SCALAR1( NAME, EXPR )                       \
	static void NAME( fixOracleInput* in, uint64_t index, fixOracleRng* rng ) \
	{                                                    \
		( void )index;                                   \
		in->s[0] = ( int64_t )oracleRngNext( rng );      \
		EXPR;                                            \
	}

// --- scalar unary ---
static void fillSqrtSmall( fixOracleInput* in, uint64_t index, fixOracleRng* rng )
{
	( void )index;
	in->s[0] = ( int64_t )( oracleRngNext( rng ) % ( uint64_t )( 8 * FIX_ONE + 1 ) );
}
static void applySqrt( const fixOracleInput* in, fixOracleOutput* out ) { ORACLE_OUT1( out, fixSqrt( in->s[0] ) ); }

static void fillScalar1( fixOracleInput* in, uint64_t index, fixOracleRng* rng )
{
	( void )index;
	in->s[0] = ( int64_t )oracleRngNext( rng );
}
static void applySin( const fixOracleInput* in, fixOracleOutput* out ) { ORACLE_OUT1( out, fixSin( in->s[0] ) ); }
static void applyCos( const fixOracleInput* in, fixOracleOutput* out ) { ORACLE_OUT1( out, fixCos( in->s[0] ) ); }
static void applyCosSin( const fixOracleInput* in, fixOracleOutput* out )
{
	fixCosSin cs = fixComputeCosSin( in->s[0] );
	out->s[0] = cs.cosine;
	out->s[1] = cs.sine;
	out->n = 2;
}
static void applyLog2( const fixOracleInput* in, fixOracleOutput* out ) { ORACLE_OUT1( out, fixLog2( in->s[0] ) ); }
static void applyExp2( const fixOracleInput* in, fixOracleOutput* out ) { ORACLE_OUT1( out, fixExp2( in->s[0] ) ); }

// --- the integer root seam: 64-bit inputs, and the >104-bit arm fixSqrt cannot reach ---
static void fillISqrt64( fixOracleInput* in, uint64_t index, fixOracleRng* rng )
{
	( void )index;
	in->s[0] = 0;
	in->s[1] = ( int64_t )oracleRngNext( rng );
}
static void applyISqrt( const fixOracleInput* in, fixOracleOutput* out )
{
	ORACLE_OUT1( out, ( int64_t )fixISqrt128High( ( uint64_t )in->s[0], ( uint64_t )in->s[1] ) );
}
static void fillISqrtWide( fixOracleInput* in, uint64_t index, fixOracleRng* rng )
{
	( void )index;
	// hi >= 2^40 is the restoring shift-subtract arm; hi < 2^40 is the repaired seed arm.
	// Both are covered: the draw picks the arm, so the hash spans the seam.
	uint64_t hi = oracleRngNext( rng );
	in->s[0] = ( int64_t )( ( hi & 1 ) ? ( UINT64_C( 1 ) << 40 ) + ( hi % ( UINT64_C( 1 ) << 20 ) )
									   : ( hi % ( UINT64_C( 1 ) << 40 ) ) );
	in->s[1] = ( int64_t )oracleRngNext( rng );
}

// --- scalar binary ---
static void fillScalar2( fixOracleInput* in, uint64_t index, fixOracleRng* rng )
{
	( void )index;
	in->s[0] = ( int64_t )oracleRngNext( rng );
	in->s[1] = ( int64_t )oracleRngNext( rng );
}
static void applyMul( const fixOracleInput* in, fixOracleOutput* out ) { ORACLE_OUT1( out, fixMul( in->s[0], in->s[1] ) ); }
// fixAbs(INT64_MIN) is undefined. On x86-64 it wraps, the following min/max treats the
// still-negative value as a zero divisor, and the native 128-bit divide traps (SIGFPE).
// The emulated arm returns instead of trapping, so the same input is not one result.
// The neighbor is the edge the oracle can actually hash on every arm.
static int64_t oracleAbsSafe( int64_t v ) { return v == INT64_MIN ? INT64_MIN + 1 : v; }

static void applyAtan2( const fixOracleInput* in, fixOracleOutput* out )
{
	ORACLE_OUT1( out, fixAtan2( oracleAbsSafe( in->s[0] ), oracleAbsSafe( in->s[1] ) ) );
}
static void applyPow( const fixOracleInput* in, fixOracleOutput* out )
{
	ORACLE_OUT1( out, fixPow( in->s[0], in->s[1] ) );
}

// fixDiv has two tiers, selected by the magnitude of the numerator against 2^47. They are
// measured and hashed separately because they are different code.
static void fillDivBelow( fixOracleInput* in, uint64_t index, fixOracleRng* rng )
{
	( void )index;
	const int64_t limit = ( int64_t )1 << 47;
	int64_t a = ( int64_t )( oracleRngNext( rng ) % ( uint64_t )limit );
	in->s[0] = a;
	in->s[1] = ( int64_t )oracleRngNext( rng );
}
static void fillDivAbove( fixOracleInput* in, uint64_t index, fixOracleRng* rng )
{
	( void )index;
	const uint64_t limit = UINT64_C( 1 ) << 47;
	// |a| >= 2^47: the 128-bit arm of fixDiv.
	uint64_t magnitude = limit + ( oracleRngNext( rng ) % ( UINT64_MAX - limit ) );
	in->s[0] = ( int64_t )( ( oracleRngNext( rng ) & 1 ) ? ( int64_t )magnitude : -( int64_t )magnitude );
	in->s[1] = ( int64_t )oracleRngNext( rng );
}
static void applyDiv( const fixOracleInput* in, fixOracleOutput* out ) { ORACLE_OUT1( out, fixDiv( in->s[0], in->s[1] ) ); }

static void fillLerp( fixOracleInput* in, uint64_t index, fixOracleRng* rng )
{
	( void )index;
	in->s[0] = ( int64_t )oracleRngNext( rng );
	in->s[1] = ( int64_t )oracleRngNext( rng );
	in->s[2] = oracleAlpha( oracleRngNext( rng ) );
}
static void applyLerp( const fixOracleInput* in, fixOracleOutput* out )
{
	ORACLE_OUT1( out, fixLerp( in->s[0], in->s[1], in->s[2] ) );
}

// --- the 128-bit seam ---
static void fillInt128Div( fixOracleInput* in, uint64_t index, fixOracleRng* rng )
{
	( void )index;
	in->s[0] = ( int64_t )oracleRngNext( rng ); // dividend high
	in->s[1] = ( int64_t )oracleRngNext( rng ); // dividend low
	// Divisor: non-zero, positive, below 2^40. That keeps every quotient inside 128 bits
	// on every arm, including the native `a / b` fall-through, so no arm can trap.
	uint64_t lo = oracleRngNext( rng ) | 1;
	uint64_t hi = oracleRngNext( rng ) % ( UINT64_C( 1 ) << 40 );
	if ( hi == 0 && lo == 1 ) lo = 3; // a divisor of 1 with a minimum dividend would overflow
	in->s[2] = ( int64_t )hi;
	in->s[3] = ( int64_t )lo;
}
static void applyInt128Div( const fixOracleInput* in, fixOracleOutput* out )
{
	fixInt128 a = fixInt128FromUnsigned( fixUInt128Make( ( uint64_t )in->s[0], ( uint64_t )in->s[1] ) );
	fixInt128 b = fixInt128FromUnsigned( fixUInt128Make( ( uint64_t )in->s[2], ( uint64_t )in->s[3] ) );
	fixInt128 q = fixInt128Div( a, b );
	out->s[0] = fixInt128ToI64( q );
	out->s[1] = ( int64_t )fixInt128Hi( q );
	out->n = 2;
}

// --- the 256-bit divide ---
static void fillUInt256Div( fixOracleInput* in, uint64_t index, fixOracleRng* rng )
{
	( void )index;
	for ( int i = 0; i < 4; ++i ) in->s[i] = ( int64_t )oracleRngNext( rng );   // dividend
	for ( int i = 4; i < 8; ++i ) in->s[i] = ( int64_t )oracleRngNext( rng );   // divisor
	in->s[7] |= 1; // the divisor is odd, therefore non-zero
	// Keep the dividend's top limb inside 2^40 so the shift-subtract stays in the range the
	// implementation documents, while every limb below it is full-width.
	in->s[4] = ( int64_t )( ( uint64_t )in->s[4] % ( UINT64_C( 1 ) << 40 ) );
}
static void applyUInt256Div( const fixOracleInput* in, fixOracleOutput* out )
{
	fixUInt256 a;
	a.hi = fixUInt128Make( ( uint64_t )in->s[0], ( uint64_t )in->s[1] );
	a.lo = fixUInt128Make( ( uint64_t )in->s[2], ( uint64_t )in->s[3] );
	fixUInt256 b;
	b.hi = fixUInt128Make( ( uint64_t )in->s[4], ( uint64_t )in->s[5] );
	b.lo = fixUInt128Make( ( uint64_t )in->s[6], ( uint64_t )in->s[7] );
	fixUInt256 q;
	fixUInt256 r;
	fixUInt256DivMod( a, b, &q, &r );
	// Every limb of the quotient and the remainder. Dropping a limb would let that limb
	// move without moving the hash.
	out->s[0] = ( int64_t )fixUInt128Lo( q.lo );
	out->s[1] = ( int64_t )fixUInt128Hi( q.lo );
	out->s[2] = ( int64_t )fixUInt128Lo( q.hi );
	out->s[3] = ( int64_t )fixUInt128Hi( q.hi );
	out->s[4] = ( int64_t )fixUInt128Lo( r.lo );
	out->s[5] = ( int64_t )fixUInt128Hi( r.lo );
	out->s[6] = ( int64_t )fixUInt128Lo( r.hi );
	out->s[7] = ( int64_t )fixUInt128Hi( r.hi );
	out->n = 8;
}

// --- vec3 ---
static void fillVec3( fixOracleInput* in, uint64_t index, fixOracleRng* rng )
{
	( void )index;
	// Simulation-shaped magnitudes (about +-8 units) plus the full-range draw that the
	// edge pass supplies: the random pass stays in the ordinary distribution.
	for ( int i = 0; i < 3; ++i )
	{
		uint64_t r = oracleRngNext( rng );
		in->s[i] = ( int64_t )( r % 1048577 ) - 524288;
	}
}
static void fillVec3x2( fixOracleInput* in, uint64_t index, fixOracleRng* rng )
{
	( void )index;
	for ( int i = 0; i < 6; ++i )
	{
		uint64_t r = oracleRngNext( rng );
		in->s[i] = ( int64_t )( r % 1048577 ) - 524288;
	}
}
static void applyDot( const fixOracleInput* in, fixOracleOutput* out )
{
	ORACLE_OUT1( out, fixDot( oracleVec( in->s[0], in->s[1], in->s[2] ), oracleVec( in->s[3], in->s[4], in->s[5] ) ) );
}
static void applyCross( const fixOracleInput* in, fixOracleOutput* out )
{
	ORACLE_OUT_VEC( out, fixCross( oracleVec( in->s[0], in->s[1], in->s[2] ), oracleVec( in->s[3], in->s[4], in->s[5] ) ) );
}
static void applyLength( const fixOracleInput* in, fixOracleOutput* out )
{
	ORACLE_OUT1( out, fixLength( oracleVec( in->s[0], in->s[1], in->s[2] ) ) );
}
static void applyNormalize( const fixOracleInput* in, fixOracleOutput* out )
{
	ORACLE_OUT_VEC( out, fixNormalize( oracleVec( in->s[0], in->s[1], in->s[2] ) ) );
}

// --- quaternion ---
static void fillQuat( fixOracleInput* in, uint64_t index, fixOracleRng* rng )
{
	( void )index;
	fixQuat q = oracleQuatDraw( rng );
	in->s[0] = q.v.x;
	in->s[1] = q.v.y;
	in->s[2] = q.v.z;
	in->s[3] = q.s;
}
static void fillQuatx2( fixOracleInput* in, uint64_t index, fixOracleRng* rng )
{
	( void )index;
	fixQuat a = oracleQuatDraw( rng );
	fixQuat b = oracleQuatDraw( rng );
	in->s[0] = a.v.x;
	in->s[1] = a.v.y;
	in->s[2] = a.v.z;
	in->s[3] = a.s;
	in->s[4] = b.v.x;
	in->s[5] = b.v.y;
	in->s[6] = b.v.z;
	in->s[7] = b.s;
}
static void applyMulQuat( const fixOracleInput* in, fixOracleOutput* out )
{
	fixQuat a = { { in->s[0], in->s[1], in->s[2] }, in->s[3] };
	fixQuat b = { { in->s[4], in->s[5], in->s[6] }, in->s[7] };
	ORACLE_OUT_QUAT( out, fixMulQuat( a, b ) );
}
static void applyNormalizeQuat( const fixOracleInput* in, fixOracleOutput* out )
{
	ORACLE_OUT_QUAT( out, fixNormalizeQuat( oracleQuatFrom( in->s[0], in->s[1], in->s[2], in->s[3] ) ) );
}

static void fillQuatVec( fixOracleInput* in, uint64_t index, fixOracleRng* rng )
{
	( void )index;
	fixQuat q = oracleQuatDraw( rng );
	in->s[0] = q.v.x;
	in->s[1] = q.v.y;
	in->s[2] = q.v.z;
	in->s[3] = q.s;
	for ( int i = 4; i < 7; ++i )
	{
		uint64_t r = oracleRngNext( rng );
		in->s[i] = ( int64_t )( r % 1048577 ) - 524288;
	}
}
static void applyRotate( const fixOracleInput* in, fixOracleOutput* out )
{
	fixQuat q = { { in->s[0], in->s[1], in->s[2] }, in->s[3] };
	ORACLE_OUT_VEC( out, fixRotateVector( q, oracleVec( in->s[4], in->s[5], in->s[6] ) ) );
}
static void applyInvRotate( const fixOracleInput* in, fixOracleOutput* out )
{
	fixQuat q = { { in->s[0], in->s[1], in->s[2] }, in->s[3] };
	ORACLE_OUT_VEC( out, fixInvRotateVector( q, oracleVec( in->s[4], in->s[5], in->s[6] ) ) );
}

static void fillNLerp( fixOracleInput* in, uint64_t index, fixOracleRng* rng )
{
	fillQuatx2( in, index, rng );
	in->s[8] = oracleAlpha( oracleRngNext( rng ) );
}
static void applyNLerp( const fixOracleInput* in, fixOracleOutput* out )
{
	fixQuat a = { { in->s[0], in->s[1], in->s[2] }, in->s[3] };
	fixQuat b = { { in->s[4], in->s[5], in->s[6] }, in->s[7] };
	ORACLE_OUT_QUAT( out, fixNLerp( a, b, in->s[8] ) );
}

// --- matrix and solve ---
static void fillMatrix( fixOracleInput* in, uint64_t index, fixOracleRng* rng )
{
	( void )index;
	// Inertia-shaped: non-singular, magnitudes that keep the 128-bit arm and the wide arm
	// both reachable across the random stream.
	for ( int i = 0; i < 9; ++i )
	{
		uint64_t r = oracleRngNext( rng );
		in->s[i] = ( int64_t )( r % 4194305 ) - 2097152;
	}
	in->s[0] += FIX_ONE * 8;
	in->s[4] += FIX_ONE * 8;
	in->s[8] += FIX_ONE * 8;
}
static void applyInvertMatrix( const fixOracleInput* in, fixOracleOutput* out )
{
	fixMatrix3 m = { { in->s[0], in->s[1], in->s[2] }, { in->s[3], in->s[4], in->s[5] }, { in->s[6], in->s[7], in->s[8] } };
	ORACLE_OUT_MAT( out, fixInvertMatrix( m ) );
}
static void fillSolve3( fixOracleInput* in, uint64_t index, fixOracleRng* rng )
{
	fillMatrix( in, index, rng );
	for ( int i = 9; i < 12; ++i )
	{
		uint64_t r = oracleRngNext( rng );
		in->s[i] = ( int64_t )( r % 1048577 ) - 524288;
	}
}
static void applySolve3( const fixOracleInput* in, fixOracleOutput* out )
{
	fixMatrix3 m = { { in->s[0], in->s[1], in->s[2] }, { in->s[3], in->s[4], in->s[5] }, { in->s[6], in->s[7], in->s[8] } };
	ORACLE_OUT_VEC( out, fixSolve3( m, oracleVec( in->s[9], in->s[10], in->s[11] ) ) );
}

// --- AABB transform ---
static void fillAABBTransform( fixOracleInput* in, uint64_t index, fixOracleRng* rng )
{
	( void )index;
	fixQuat q = oracleQuatDraw( rng );
	in->s[0] = q.v.x;
	in->s[1] = q.v.y;
	in->s[2] = q.v.z;
	in->s[3] = q.s;
	for ( int i = 4; i < 7; ++i )
	{
		uint64_t r = oracleRngNext( rng );
		in->s[i] = ( int64_t )( r % 1048577 ) - 524288; // translation
	}
	for ( int i = 7; i < 10; ++i )
	{
		uint64_t r = oracleRngNext( rng );
		in->s[i] = ( int64_t )( r % 1048577 ) - 524288; // center
	}
	for ( int i = 10; i < 13; ++i )
	{
		uint64_t r = oracleRngNext( rng );
		in->s[i] = ( int64_t )( r % 524288 ) + 1; // extents, strictly positive
	}
}
static void applyAABBTransform( const fixOracleInput* in, fixOracleOutput* out )
{
	fixTransform t;
	t.q = ( fixQuat ){ { in->s[0], in->s[1], in->s[2] }, in->s[3] };
	t.p = oracleVec( in->s[4], in->s[5], in->s[6] );
	fixVec3 center = oracleVec( in->s[7], in->s[8], in->s[9] );
	fixVec3 extent = oracleVec( in->s[10], in->s[11], in->s[12] );
	fixAABB box;
	box.lowerBound = fixVecSub( center, extent );
	box.upperBound = fixVecAdd( center, extent );
	fixAABB r = fixAABB_Transform( t, box );
	out->s[0] = r.lowerBound.x;
	out->s[1] = r.lowerBound.y;
	out->s[2] = r.lowerBound.z;
	out->s[3] = r.upperBound.x;
	out->s[4] = r.upperBound.y;
	out->s[5] = r.upperBound.z;
	out->n = 6;
}

// --- quantize boundary ---
static void fillQuantize( fixOracleInput* in, uint64_t index, fixOracleRng* rng )
{
	( void )index;
	in->s[0] = ( int64_t )oracleRngNext( rng );
	in->s[1] = ( int64_t )( oracleRngNext( rng ) % 1000000 ) + 1; // scale > 0, per the contract
}
// fixQuantize's own fill: the same draws, then the raw value pulled inside the domain
// where the conversion is defined. fixDequantize keeps fillQuantize, whose every input
// is already in its domain, so its row does not move.
static void fillQuantizeInDomain( fixOracleInput* in, uint64_t index, fixOracleRng* rng )
{
	fillQuantize( in, index, rng );
	in->s[0] = oracleQuantizeInRange( in->s[0], in->s[1] );
}
static void applyQuantize( const fixOracleInput* in, fixOracleOutput* out )
{
	double value = ( double )in->s[0] / ( double )FIX_ONE;
	int64_t scale = in->s[1] > 0 ? in->s[1] : 1;
	ORACLE_OUT1( out, fixQuantize( value, scale ) );
}
static void applyDequantize( const fixOracleInput* in, fixOracleOutput* out )
{
	int64_t scale = in->s[1] > 0 ? in->s[1] : 1;
	ORACLE_OUT1( out, oracleDoubleBits( fixDequantize( in->s[0], scale ) ) );
}

static const fixOracleOp oracleOps[] = {
	{ "fixMul", 2, 1, 0, fillScalar2, applyMul },
	{ "fixDiv_below2p47", 2, 1, 0, fillDivBelow, applyDiv },
	{ "fixDiv_above2p47", 2, 4, 0, fillDivAbove, applyDiv },
	{ "fixSqrt_small", 1, 1, 0, fillSqrtSmall, applySqrt },
	{ "fixSqrt_raw_below2p32", 1, 1, 1, fillScalar1, applySqrt },
	{ "fixISqrt128High_u64", 2, 1, 0, fillISqrt64, applyISqrt },
	{ "fixISqrt128High_above104bits", 2, 20, 0, fillISqrtWide, applyISqrt },
	{ "fixComputeCosSin", 1, 1, 2, fillScalar1, applyCosSin },
	{ "fixSin", 1, 1, 2, fillScalar1, applySin },
	{ "fixCos", 1, 1, 2, fillScalar1, applyCos },
	{ "fixAtan2", 2, 8, 0, fillScalar2, applyAtan2 },
	{ "fixLog2", 1, 8, 3, fillScalar1, applyLog2 },
	{ "fixExp2", 1, 8, 3, fillScalar1, applyExp2 },
	{ "fixPow", 2, 20, 0, fillScalar2, applyPow },
	{ "fixLerp", 3, 1, 0, fillLerp, applyLerp },
	{ "fixInt128Div", 4, 20, 0, fillInt128Div, applyInt128Div },
	{ "fixUInt256DivMod", 8, 100, 0, fillUInt256Div, applyUInt256Div },
	{ "fixDot", 6, 1, 0, fillVec3x2, applyDot },
	{ "fixCross", 6, 1, 0, fillVec3x2, applyCross },
	{ "fixLength", 3, 2, 0, fillVec3, applyLength },
	{ "fixNormalize", 3, 4, 0, fillVec3, applyNormalize },
	{ "fixMulQuat", 8, 2, 0, fillQuatx2, applyMulQuat },
	{ "fixNormalizeQuat", 4, 4, 0, fillQuat, applyNormalizeQuat },
	{ "fixRotateVector", 7, 4, 0, fillQuatVec, applyRotate },
	{ "fixInvRotateVector", 7, 4, 0, fillQuatVec, applyInvRotate },
	{ "fixNLerp", 9, 8, 0, fillNLerp, applyNLerp },
	{ "fixInvertMatrix", 9, 100, 0, fillMatrix, applyInvertMatrix },
	{ "fixSolve3", 12, 100, 0, fillSolve3, applySolve3 },
	{ "fixAABB_Transform", 13, 20, 0, fillAABBTransform, applyAABBTransform },
	{ "fixQuantize", 2, 1, 0, fillQuantizeInDomain, applyQuantize },
	{ "fixDequantize", 2, 1, 0, fillQuantize, applyDequantize },
};

#define FIX_ORACLE_OP_COUNT ( int )( sizeof oracleOps / sizeof oracleOps[0] )

// --------------------------------------------------------------------------------------
// Exhaustive ranges.
// --------------------------------------------------------------------------------------
// fixSqrt: every raw input below 2^32, as the raw Q48.16 value.
#define FIX_ORACLE_SQRT_EXHAUSTIVE_COUNT ( UINT64_C( 1 ) << 32 )
// fixSin/fixCos/fixComputeCosSin: every raw angle in [-2 pi, 2 pi] at Q48.16 resolution.
#define FIX_ORACLE_ANGLE_LOW ( -( int64_t )411775 )
#define FIX_ORACLE_ANGLE_HIGH ( ( int64_t )411775 )
#define FIX_ORACLE_ANGLE_COUNT ( uint64_t )( FIX_ORACLE_ANGLE_HIGH - FIX_ORACLE_ANGLE_LOW + 1 )
// fixLog2/fixExp2: a dense grid either side of zero plus a stride across the whole domain.
#define FIX_ORACLE_LOG_DENSE ( UINT64_C( 1 ) << 21 )
#define FIX_ORACLE_LOG_STRIDE_COUNT ( UINT64_C( 1 ) << 20 )
#define FIX_ORACLE_LOG_COUNT ( FIX_ORACLE_LOG_DENSE + FIX_ORACLE_LOG_STRIDE_COUNT )

static uint64_t oracleExhaustiveCount( const fixOracleOp* op )
{
	switch ( op->exhaustive )
	{
		case 1: return FIX_ORACLE_SQRT_EXHAUSTIVE_COUNT;
		case 2: return FIX_ORACLE_ANGLE_COUNT;
		case 3: return FIX_ORACLE_LOG_COUNT;
		default: return 0;
	}
}

// --------------------------------------------------------------------------------------
// THE STREAM. One definition of the input sequence, walked identically by the hash run
// and by the dump, so a dump describes exactly the inputs that were hashed.
// --------------------------------------------------------------------------------------
enum { ORACLE_PHASE_EDGE = 0, ORACLE_PHASE_CROSS = 1, ORACLE_PHASE_EXHAUSTIVE = 2, ORACLE_PHASE_RANDOM = 3, ORACLE_PHASE_DONE = 4 };

typedef struct fixOracleStream
{
	const fixOracleOp* op;
	uint64_t randomCount; // this operation's share, base / weight
	uint64_t limit;       // 0 = no limit
	uint64_t index;
	uint64_t total;
	int phase;
	uint64_t edgeI;
	uint64_t crossJ;
	uint64_t exU;
	fixOracleRng rng;
} fixOracleStream;

static uint64_t oracleRandomCountFor( const fixOracleOp* op, uint64_t base )
{
	uint64_t n = base / ( uint64_t )op->weight;
	return n == 0 ? 1 : n;
}

static uint64_t fixOracleInputCount( int opIndex, uint64_t baseRandom, uint64_t limit )
{
	oracleGridBuild();
	const fixOracleOp* op = &oracleOps[opIndex];
	uint64_t grid = ( uint64_t )oracleGridCount;
	uint64_t total = grid;                                        // EDGE
	if ( op->arity >= 2 ) total += grid * grid;                   // CROSS
	total += oracleExhaustiveCount( op );                         // EXHAUSTIVE
	total += oracleRandomCountFor( op, baseRandom );              // RANDOM
	if ( limit != 0 && total > limit ) total = limit;
	return total;
}

static void fixOracleStreamInit( fixOracleStream* st, int opIndex, uint64_t baseRandom, uint64_t limit )
{
	oracleGridBuild();
	st->op = &oracleOps[opIndex];
	st->randomCount = oracleRandomCountFor( st->op, baseRandom );
	st->limit = limit;
	st->index = 0;
	st->total = fixOracleInputCount( opIndex, baseRandom, limit );
	st->phase = ORACLE_PHASE_EDGE;
	st->edgeI = 0;
	st->crossJ = 0;
	st->exU = 0;
	oracleRngSeed( &st->rng );
}

// Derive the slots a pass does not name, so that an operation of any arity gets a full
// input from a one- or two-dimensional walk. Deterministic in (i, j).
static void oracleFillRest( const fixOracleOp* op, fixOracleInput* in, uint64_t i, uint64_t j )
{
	uint64_t mix = ( i * UINT64_C( 0x9E3779B97F4A7C15 ) ) ^ ( ( j + 1 ) * UINT64_C( 0xBF58476D1CE4E5B9 ) );
	fixOracleRng rng;
	rng.s[0] = mix;
	rng.s[1] = mix ^ UINT64_C( 0xD1B54A32D192ED03 );
	rng.s[2] = ~mix;
	rng.s[3] = mix + UINT64_C( 0x243F6A8885A308D3 );
	int start = op->arity >= 2 ? 2 : 1;
	for ( int slot = start; slot < op->arity; ++slot )
	{
		in->s[slot] = ( int64_t )oracleRngNext( &rng );
	}
	// Contract slots, whatever the walk produced.
	for ( int slot = 0; slot < op->arity; ++slot )
	{
		if ( op->fill == fillNLerp && slot == 8 ) in->s[slot] = oracleAlpha( ( uint64_t )in->s[slot] );
		if ( ( op->fill == fillQuantize || op->fill == fillQuantizeInDomain ) && slot == 1 )
			in->s[slot] = ( in->s[slot] % 1000000 ) + 1;
	}
	if ( op->fill == fillQuantizeInDomain ) in->s[0] = oracleQuantizeInRange( in->s[0], in->s[1] );
	if ( op->fill == fillQuat || op->fill == fillQuatx2 || op->fill == fillQuatVec || op->fill == fillNLerp ||
		 op->fill == fillAABBTransform )
	{
		// Quaternion slots must satisfy the operation's own validity contract.
		int base = 0;
		int quats = 1;
		if ( op->fill == fillQuatx2 || op->fill == fillNLerp ) quats = 2;
		for ( int k = 0; k < quats; ++k, base += 4 )
		{
			fixQuat q = oracleQuatFrom( in->s[base], in->s[base + 1], in->s[base + 2], in->s[base + 3] );
			in->s[base] = q.v.x;
			in->s[base + 1] = q.v.y;
			in->s[base + 2] = q.v.z;
			in->s[base + 3] = q.s;
		}
	}
	if ( op->fill == fillAABBTransform )
	{
		fixQuat q = oracleQuatFrom( in->s[0], in->s[1], in->s[2], in->s[3] );
		in->s[0] = q.v.x;
		in->s[1] = q.v.y;
		in->s[2] = q.v.z;
		in->s[3] = q.s;
		for ( int slot = 10; slot < 13; ++slot )
		{
			in->s[slot] = ( in->s[slot] & 0x7FFFF ) + 1;
		}
	}
	if ( op->fill == fillInt128Div )
	{
		uint64_t lo = ( uint64_t )in->s[3] | 1;
		uint64_t hi = ( uint64_t )in->s[2] % ( UINT64_C( 1 ) << 40 );
		if ( hi == 0 && lo == 1 ) lo = 3;
		in->s[2] = ( int64_t )hi;
		in->s[3] = ( int64_t )lo;
	}
	if ( op->fill == fillUInt256Div )
	{
		in->s[4] = ( int64_t )( ( uint64_t )in->s[4] % ( UINT64_C( 1 ) << 40 ) );
		in->s[7] |= 1;
	}
	if ( op->fill == fillLerp && op->arity >= 3 )
	{
		in->s[2] = oracleAlpha( ( uint64_t )in->s[2] );
	}
}

static bool fixOracleNext( fixOracleStream* st, fixOracleInput* in )
{
	if ( st->index >= st->total ) return false;
	const fixOracleOp* op = st->op;
	const uint64_t grid = ( uint64_t )oracleGridCount;

	switch ( st->phase )
	{
		case ORACLE_PHASE_EDGE:
			memset( in, 0, sizeof *in );
			in->s[0] = oracleGrid[st->edgeI];
			oracleFillRest( op, in, st->edgeI, 0 );
			if ( op->arity == 1 && op->exhaustive == 1 ) in->s[0] = ( int64_t )( st->edgeI < grid ? ( uint64_t )oracleGrid[st->edgeI] : 0 );
			++st->edgeI;
			if ( st->edgeI >= grid )
			{
				st->edgeI = 0;
				st->phase = op->arity >= 2 ? ORACLE_PHASE_CROSS : ORACLE_PHASE_EXHAUSTIVE;
			}
			break;
		case ORACLE_PHASE_CROSS:
			memset( in, 0, sizeof *in );
			in->s[0] = oracleGrid[st->edgeI];
			in->s[1] = oracleGrid[st->crossJ];
			oracleFillRest( op, in, st->edgeI, st->crossJ );
			++st->crossJ;
			if ( st->crossJ >= grid )
			{
				st->crossJ = 0;
				++st->edgeI;
				if ( st->edgeI >= grid )
				{
					st->edgeI = 0;
					st->phase = ORACLE_PHASE_EXHAUSTIVE;
				}
			}
			break;
		case ORACLE_PHASE_EXHAUSTIVE:
			// A zero-length exhaustive range still emits this one input, and that input
			// takes one slot of the random budget (the stream stops at `total`, which
			// does not count it). The frozen hashes include that input. Removing it
			// moves every exhaustive-0 row; do not, without regenerating the baseline.
			memset( in, 0, sizeof *in );
			if ( op->exhaustive == 1 )
			{
				in->s[0] = ( int64_t )st->exU; // every raw input below 2^32
			}
			else if ( op->exhaustive == 2 )
			{
				in->s[0] = FIX_ORACLE_ANGLE_LOW + ( int64_t )st->exU;
			}
			else if ( op->exhaustive == 3 )
			{
				in->s[0] = st->exU < FIX_ORACLE_LOG_DENSE
							   ? ( int64_t )st->exU - ( int64_t )( FIX_ORACLE_LOG_DENSE / 2 )
							   : ( int64_t )( ( st->exU - FIX_ORACLE_LOG_DENSE ) << 22 );
			}
			oracleFillRest( op, in, st->exU, 0 );
			++st->exU;
			if ( st->exU >= oracleExhaustiveCount( op ) ) st->phase = ORACLE_PHASE_RANDOM;
			break;
		default:
			op->fill( in, st->index, &st->rng );
			break;
	}
	++st->index;
	return true;
}

// --------------------------------------------------------------------------------------
// PUBLIC API of the engine.
// --------------------------------------------------------------------------------------
int fixOracleOpCount( void ) { return FIX_ORACLE_OP_COUNT; }

const char* fixOracleOpName( int opIndex ) { return oracleOps[opIndex].name; }

int fixOracleFindOp( const char* name )
{
	for ( int i = 0; i < FIX_ORACLE_OP_COUNT; ++i )
	{
		if ( strcmp( oracleOps[i].name, name ) == 0 ) return i;
	}
	return -1;
}

uint64_t fixOracleHashOp( int opIndex, uint64_t baseRandom, uint64_t limit, uint64_t* segmentsOut, uint64_t* countOut )
{
	fixOracleStream st;
	fixOracleStreamInit( &st, opIndex, baseRandom, limit );
	uint64_t h = oracleHashInit( oracleOps[opIndex].name );
	uint64_t nextSegment = 0;
	const uint64_t step = st.total / FIX_ORACLE_SEGMENTS;
	fixOracleInput in;
	fixOracleOutput out;
	while ( fixOracleNext( &st, &in ) )
	{
		out.n = 0;
		oracleOps[opIndex].apply( &in, &out );
		h = oracleHashWord( h, ( uint64_t )out.n );
		for ( int i = 0; i < out.n; ++i ) h = oracleHashWord( h, ( uint64_t )out.s[i] );
		if ( segmentsOut != NULL && step != 0 )
		{
			while ( nextSegment < FIX_ORACLE_SEGMENTS && st.index >= ( nextSegment + 1 ) * step )
			{
				segmentsOut[nextSegment] = h;
				++nextSegment;
			}
		}
	}
	if ( segmentsOut != NULL )
	{
		for ( uint64_t i = nextSegment; i < FIX_ORACLE_SEGMENTS; ++i ) segmentsOut[i] = h;
	}
	if ( countOut != NULL ) *countOut = st.index;
	return h;
}

// Print index, input slots and output words for [start, end) of the same stream the hash
// walks. Diffing two trees' dumps line by line names the FIRST differing input exactly.
void fixOracleDumpOp( int opIndex, uint64_t baseRandom, uint64_t limit, uint64_t start, uint64_t end, FILE* out )
{
	fixOracleStream st;
	fixOracleStreamInit( &st, opIndex, baseRandom, limit );
	fixOracleInput in;
	fixOracleOutput o;
	uint64_t i = 0;
	while ( fixOracleNext( &st, &in ) )
	{
		if ( i >= end ) break;
		if ( i >= start )
		{
			o.n = 0;
			oracleOps[opIndex].apply( &in, &o );
			fprintf( out, "%llu", ( unsigned long long )i );
			for ( int s = 0; s < oracleOps[opIndex].arity; ++s ) fprintf( out, " in%d=%lld", s, ( long long )in.s[s] );
			for ( int s = 0; s < o.n; ++s ) fprintf( out, " out%d=%llu", s, ( unsigned long long )( uint64_t )o.s[s] );
			fputc( '\n', out );
		}
		++i;
	}
}
