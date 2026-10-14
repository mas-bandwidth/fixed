// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
//
// THE BIT-EXACT OUTPUT ORACLE.
//
// For every hot public operation this computes one 64-bit hash over the operation's outputs
// on a defined input set. A change that is meant to be faster and nothing else must leave
// every hash where it is; benchmark/oracle-baseline.txt freezes them from the tree they were
// captured on, and benchmark/oracle_check.c diffs a build against that file.
//
// The input set of each operation is a fixed sequence, in this order:
//   (b) the structured edge grid: 0, +-1, +-2^k, +-(2^k - 1) for every k, INT64_MIN,
//       INT64_MAX and the saturation value -INT64_MAX (FIX_MIN), crossed with itself for
//       binary operations; the 128- and 256-bit seam use the same grid at their own width;
//   (a) an exhaustive or dense sweep where one is feasible (fixSqrt for every raw input below
//       2^32, the sine and cosine family for every raw angle in [-2 pi, 2 pi], fixLog2 and
//       fixExp2 over dense grids);
//   (c) pseudo-random inputs from xoshiro256**, seeded from ORACLE_SEED below and the
//       operation's index, log-uniform in magnitude so every bit length is visited;
//   (d) for the composite operations (vectors, quaternions, matrices, AABBs), seeded random
//       structures in the domain each operation documents.
// The edge grid comes first on purpose: the unit tier (test/oracle_unit_test.c) hashes only
// a 1,000-input prefix of each sequence, and that prefix should be the hard cases.
//
// No floating point is used to make or judge an input; the only doubles are the ones the
// quantize boundary takes and returns, built from integers by exact conversions.
//
// This file is included by benchmark/oracle_check.c and test/oracle_unit_test.c rather
// than linked, so each build compiles it against the library tree it is testing.

#include "fixed/fixed_vec.h"
#include "fixed/fixed_quantize.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/// The seed of every random stream in the oracle. Changing it moves every frozen hash.
#define ORACLE_SEED UINT64_C( 0x6F7261636C653031 )

/// Default count of random inputs per scalar operation, (c); composites use a tenth, (d).
#define ORACLE_DEFAULT_RANDOM UINT64_C( 50000000 )

// ---------------------------------------------------------------------------------------------
// The running state of one operation's pass.
// ---------------------------------------------------------------------------------------------

typedef struct fixOracle fixOracle;
typedef void ( *fixOracleCheckpointFn )( fixOracle* o, uint64_t index, uint64_t hash );

struct fixOracle
{
	uint64_t hash;		  // over every output word so far
	uint64_t index;		  // inputs consumed so far
	uint64_t limit;		  // stop once this many inputs are consumed (a prefix); UINT64_MAX is all
	uint64_t randomCount; // (c) per scalar operation; (d) is a tenth of it
	int op;				  // index of the running operation, part of its stream's seed
	// Optional: called with the prefix hash at every checkpoint index (see oracleIsCheckpoint).
	fixOracleCheckpointFn checkpoint;
	void* user;
	// Optional: print every input in [dumpFrom, dumpTo) with its outputs.
	FILE* dump;
	uint64_t dumpFrom, dumpTo;
};

/// Checkpoints are where a prefix hash is recorded: every 2^16 inputs below 2^20, then every
/// 2^22. They locate the first differing chunk of a failing operation from the baseline file.
static int oracleIsCheckpoint( uint64_t index )
{
	if ( index == 0 ) return 0;
	if ( index <= ( UINT64_C( 1 ) << 20 ) ) return ( index & 0xFFFF ) == 0;
	return ( index & 0x3FFFFF ) == 0;
}

static inline uint64_t oracleMix( uint64_t h, uint64_t v )
{
	h ^= v;
	h *= UINT64_C( 0xFF51AFD7ED558CCD );
	h ^= h >> 32;
	return h;
}

/// Consume one input: hash its outputs, dump it if asked. Returns 0 once the limit is reached.
static int oracleEmit( fixOracle* o, const uint64_t* in, int nin, const uint64_t* out, int nout )
{
	if ( o->index >= o->limit ) return 0;
	for ( int i = 0; i < nout; ++i ) o->hash = oracleMix( o->hash, out[i] );
	if ( o->dump && o->index >= o->dumpFrom && o->index < o->dumpTo )
	{
		fprintf( o->dump, "%llu in", (unsigned long long)o->index );
		for ( int i = 0; i < nin; ++i ) fprintf( o->dump, " %016llx", (unsigned long long)in[i] );
		fprintf( o->dump, " out" );
		for ( int i = 0; i < nout; ++i ) fprintf( o->dump, " %016llx", (unsigned long long)out[i] );
		fprintf( o->dump, "\n" );
	}
	o->index += 1;
	if ( o->checkpoint && oracleIsCheckpoint( o->index ) ) o->checkpoint( o, o->index, o->hash );
	return o->index < o->limit;
}

#define ORACLE_EMIT( IN, OUT )                                                                                             \
	do                                                                                                                     \
	{                                                                                                                      \
		const uint64_t in_[] = IN;                                                                                         \
		const uint64_t out_[] = OUT;                                                                                       \
		if ( !oracleEmit( o, in_, (int)( sizeof( in_ ) / 8 ), out_, (int)( sizeof( out_ ) / 8 ) ) ) return;           \
	} while ( 0 )
#define ORACLE_ARGS( ... ) { __VA_ARGS__ }
#define U( x ) ( (uint64_t)( x ) )

// ---------------------------------------------------------------------------------------------
// xoshiro256** (Blackman and Vigna), seeded through splitmix64.
// ---------------------------------------------------------------------------------------------

typedef struct oracleRng
{
	uint64_t s[4];
} oracleRng;

static uint64_t oracleSplitMix( uint64_t* x )
{
	uint64_t z = ( *x += UINT64_C( 0x9E3779B97F4A7C15 ) );
	z = ( z ^ ( z >> 30 ) ) * UINT64_C( 0xBF58476D1CE4E5B9 );
	z = ( z ^ ( z >> 27 ) ) * UINT64_C( 0x94D049BB133111EB );
	return z ^ ( z >> 31 );
}

static oracleRng oracleSeed( int op, uint64_t salt )
{
	uint64_t x = ORACLE_SEED ^ ( (uint64_t)op * UINT64_C( 0xD1B54A32D192ED03 ) ) ^ salt;
	oracleRng r;
	for ( int i = 0; i < 4; ++i ) r.s[i] = oracleSplitMix( &x );
	return r;
}

static inline uint64_t oracleRotl( uint64_t x, int k ) { return ( x << k ) | ( x >> ( 64 - k ) ); }

static inline uint64_t oracleNext( oracleRng* r )
{
	uint64_t* s = r->s;
	uint64_t result = oracleRotl( s[1] * 5, 7 ) * 9;
	uint64_t t = s[1] << 17;
	s[2] ^= s[0];
	s[3] ^= s[1];
	s[1] ^= s[2];
	s[0] ^= s[3];
	s[2] ^= t;
	s[3] = oracleRotl( s[3], 45 );
	return result;
}

/// A signed value whose magnitude is log-uniform: a random word shifted right (arithmetic,
/// spelled portably) by a random 0..63, so every bit length is equally likely.
static inline int64_t oracleAsr( uint64_t v, int s )
{
	uint64_t sign = ( v >> 63 ) ? ~UINT64_C( 0 ) : 0;
	if ( s == 0 ) return (int64_t)v;
	return (int64_t)( ( v >> s ) | ( sign << ( 64 - s ) ) );
}

static inline int64_t oracleShaped( oracleRng* r )
{
	uint64_t v = oracleNext( r );
	return oracleAsr( v, (int)( oracleNext( r ) & 63 ) );
}

/// A shaped value of at most `bits` magnitude bits (below 2^bits in absolute value).
static inline int64_t oracleShapedBits( oracleRng* r, int bits )
{
	uint64_t v = oracleNext( r );
	int s = 64 - bits + (int)( oracleNext( r ) % (uint64_t)bits );
	return oracleAsr( v, s );
}

// ---------------------------------------------------------------------------------------------
// The edge grids.
// ---------------------------------------------------------------------------------------------

enum
{
	ORACLE_EDGE_MAX = 260,
	ORACLE_EDGE128_MAX = 520,
	ORACLE_EDGE256_MAX = 520
};

/// Pass as maxMagnitude: the whole grid, or the whole grid without INT64_MIN.
#define ORACLE_EDGE_ALL ( (int64_t)-1 )
#define ORACLE_EDGE_NO_INT64_MIN ( (int64_t)-2 )

/// 0, +-1, +-2^k and +-(2^k - 1) for k = 1..62, INT64_MAX, INT64_MIN, -INT64_MAX; or the
/// part of it at most maxMagnitude in absolute value (which never includes INT64_MIN).
static int oracleEdge( int64_t* e, int64_t maxMagnitude )
{
	int n = 0;
	e[n++] = 0;
	e[n++] = 1;
	e[n++] = -1;
	for ( int k = 1; k <= 62; ++k )
	{
		int64_t p = (int64_t)1 << k;
		e[n++] = p;
		e[n++] = -p;
		e[n++] = p - 1;
		e[n++] = -( p - 1 );
	}
	e[n++] = INT64_MAX;
	e[n++] = INT64_MIN;
	e[n++] = -INT64_MAX;
	// The restricted grids (for operations with a documented domain) keep the order.
	int m = 0;
	for ( int i = 0; i < n; ++i )
	{
		int keep = maxMagnitude == ORACLE_EDGE_ALL ||
				   ( maxMagnitude == ORACLE_EDGE_NO_INT64_MIN && e[i] != INT64_MIN ) ||
				   ( maxMagnitude >= 0 && e[i] != INT64_MIN && ( e[i] < 0 ? -e[i] : e[i] ) <= maxMagnitude );
		if ( keep ) e[m++] = e[i];
	}
	return m;
}

/// Unsigned: 0, 2^k for k = 0..63, 2^k - 1 for k = 1..64.
static int oracleEdgeUnsigned( uint64_t* e )
{
	int n = 0;
	e[n++] = 0;
	for ( int k = 0; k <= 63; ++k ) e[n++] = UINT64_C( 1 ) << k;
	for ( int k = 1; k <= 64; ++k ) e[n++] = k == 64 ? ~UINT64_C( 0 ) : ( UINT64_C( 1 ) << k ) - 1;
	return n;
}

typedef struct oracleU128
{
	uint64_t hi, lo;
} oracleU128;

static oracleU128 oracleNeg128( oracleU128 a )
{
	oracleU128 r;
	r.lo = ~a.lo + 1;
	r.hi = ~a.hi + ( a.lo == 0 ? 1 : 0 );
	return r;
}

static oracleU128 oraclePow2m( int k, int minusOne )
{
	oracleU128 r = { 0, 0 };
	if ( k < 64 ) r.lo = UINT64_C( 1 ) << k;
	else if ( k < 128 ) r.hi = UINT64_C( 1 ) << ( k - 64 );
	if ( minusOne )
	{
		if ( r.lo == 0 ) { r.lo = ~UINT64_C( 0 ); r.hi -= 1; }
		else r.lo -= 1;
	}
	return r;
}

/// Signed 128: 0, +-2^k (k = 0..126), +-(2^k - 1) (k = 1..127), and -2^127.
static int oracleEdge128( oracleU128* e )
{
	int n = 0;
	e[n++] = ( oracleU128 ){ 0, 0 };
	for ( int k = 0; k <= 126; ++k )
	{
		e[n++] = oraclePow2m( k, 0 );
		e[n++] = oracleNeg128( oraclePow2m( k, 0 ) );
	}
	for ( int k = 1; k <= 127; ++k )
	{
		e[n++] = oraclePow2m( k, 1 );
		e[n++] = oracleNeg128( oraclePow2m( k, 1 ) );
	}
	e[n++] = ( oracleU128 ){ UINT64_C( 0x8000000000000000 ), 0 };
	return n;
}

typedef struct oracleU256
{
	uint64_t l[4];
} oracleU256;

/// Unsigned 256: 0, 2^k (k = 0..255), 2^k - 1 (k = 1..256).
static int oracleEdge256( oracleU256* e )
{
	int n = 0;
	memset( &e[n++], 0, sizeof( oracleU256 ) );
	for ( int k = 0; k <= 255; ++k )
	{
		memset( &e[n], 0, sizeof( oracleU256 ) );
		e[n++].l[k / 64] = UINT64_C( 1 ) << ( k % 64 );
	}
	for ( int k = 1; k <= 256; ++k )
	{
		memset( &e[n], 0, sizeof( oracleU256 ) );
		for ( int i = 0; i < k; ++i ) e[n].l[i / 64] |= UINT64_C( 1 ) << ( i % 64 );
		n++;
	}
	return n;
}

static int oracleIsZero256( const oracleU256* a ) { return ( a->l[0] | a->l[1] | a->l[2] | a->l[3] ) == 0; }

// ---------------------------------------------------------------------------------------------
// Composite generators, (d). Integer-only, and independent of the library under test, so a
// change to the library can never change an input.
// ---------------------------------------------------------------------------------------------

static uint64_t oracleIsqrt64( uint64_t n )
{
	uint64_t r = 0, bit = UINT64_C( 1 ) << 62;
	while ( bit > n ) bit >>= 2;
	while ( bit )
	{
		if ( n >= r + bit ) { n -= r + bit; r = ( r >> 1 ) + bit; }
		else r >>= 1;
		bit >>= 2;
	}
	return r;
}

/// A vector with every component below 2^38 raw in magnitude, never all zero.
static fixVec3 oracleVec( oracleRng* r )
{
	fixVec3 v = { oracleShapedBits( r, 38 ), oracleShapedBits( r, 38 ), oracleShapedBits( r, 38 ) };
	if ( v.x == 0 && v.y == 0 && v.z == 0 ) v.x = 1;
	return v;
}

/// A quaternion that passes fixIsNormalizedQuat, made with integers: three components inside
/// the unit ball and the fourth their exact integer complement.
static fixQuat oracleUnitQuat( oracleRng* r )
{
	for ( ;; )
	{
		int64_t x = (int64_t)( oracleNext( r ) % 131073 ) - 65536;
		int64_t y = (int64_t)( oracleNext( r ) % 131073 ) - 65536;
		int64_t z = (int64_t)( oracleNext( r ) % 131073 ) - 65536;
		uint64_t ss = (uint64_t)( x * x ) + (uint64_t)( y * y ) + (uint64_t)( z * z );
		if ( ss > ( UINT64_C( 1 ) << 32 ) ) continue;
		int64_t s = (int64_t)oracleIsqrt64( ( UINT64_C( 1 ) << 32 ) - ss );
		if ( oracleNext( r ) & 1 ) s = -s;
		fixQuat q = { { x, y, z }, s };
		return q;
	}
}

/// A quaternion of arbitrary scale (components below 2^40 raw), never all zero.
static fixQuat oracleAnyQuat( oracleRng* r )
{
	fixQuat q = { { oracleShapedBits( r, 40 ), oracleShapedBits( r, 40 ), oracleShapedBits( r, 40 ) },
				  oracleShapedBits( r, 40 ) };
	if ( q.v.x == 0 && q.v.y == 0 && q.v.z == 0 && q.s == 0 ) q.s = 1;
	return q;
}

/// A matrix: half general (components below 2^32 raw, log-uniform), half diagonally dominant.
static fixMatrix3 oracleMatrix( oracleRng* r )
{
	fixMatrix3 m;
	fixed_t* c = &m.cx.x;
	for ( int i = 0; i < 9; ++i ) c[i] = oracleShapedBits( r, 32 );
	if ( oracleNext( r ) & 1 )
	{
		m.cx.x = ( m.cx.x < 0 ? -m.cx.x : m.cx.x ) + ( (int64_t)1 << 33 );
		m.cy.y = ( m.cy.y < 0 ? -m.cy.y : m.cy.y ) + ( (int64_t)1 << 33 );
		m.cz.z = ( m.cz.z < 0 ? -m.cz.z : m.cz.z ) + ( (int64_t)1 << 33 );
	}
	return m;
}

static const int64_t oracleScales[] = { 1, 2, 3, 10, 100, 1000, 65536, (int64_t)1 << 20, (int64_t)1 << 30 };
enum { ORACLE_SCALE_COUNT = (int)( sizeof( oracleScales ) / sizeof( oracleScales[0] ) ) };

static uint64_t oracleDoubleBits( double d )
{
	uint64_t u;
	memcpy( &u, &d, 8 );
	return u;
}

/// An exact double: an integer below 2^30 in magnitude over a power of two.
static double oracleExactDouble( int64_t integer, int pow2 )
{
	double d = (double)integer;
	for ( int i = 0; i < pow2; ++i ) d *= 0.5;
	return d;
}

// ---------------------------------------------------------------------------------------------
// The operations.
// ---------------------------------------------------------------------------------------------

#define ORACLE_SCALAR_UNARY( FN, CALL, DENSE )                                                                              \
	static void FN( fixOracle* o )                                                                                         \
	{                                                                                                                      \
		int64_t e[ORACLE_EDGE_MAX];                                                                                        \
		int n = oracleEdge( e, ORACLE_EDGE_ALL );                                                                                       \
		for ( int i = 0; i < n; ++i ) { int64_t a = e[i]; ORACLE_EMIT( ORACLE_ARGS( U( a ) ), ORACLE_ARGS( U( CALL ) ) ); } \
		DENSE                                                                                                              \
		oracleRng r = oracleSeed( o->op, 0 );                                                                              \
		for ( uint64_t j = 0; j < o->randomCount; ++j )                                                                    \
		{                                                                                                                  \
			int64_t a = oracleShaped( &r );                                                                                \
			ORACLE_EMIT( ORACLE_ARGS( U( a ) ), ORACLE_ARGS( U( CALL ) ) );                                                \
		}                                                                                                                  \
	}

#define ORACLE_SCALAR_BINARY( FN, CALL, EDGE )                                                                                    \
	static void FN( fixOracle* o )                                                                                         \
	{                                                                                                                      \
		int64_t e[ORACLE_EDGE_MAX];                                                                                        \
		int n = oracleEdge( e, EDGE );                                                                                       \
		for ( int i = 0; i < n; ++i )                                                                                      \
			for ( int k = 0; k < n; ++k )                                                                                  \
			{                                                                                                              \
				int64_t a = e[i], b = e[k];                                                                                \
				ORACLE_EMIT( ORACLE_ARGS( U( a ), U( b ) ), ORACLE_ARGS( U( CALL ) ) );                                    \
			}                                                                                                              \
		oracleRng r = oracleSeed( o->op, 0 );                                                                              \
		for ( uint64_t j = 0; j < o->randomCount; ++j )                                                                    \
		{                                                                                                                  \
			int64_t a = oracleShaped( &r ), b = oracleShaped( &r );                                                        \
			ORACLE_EMIT( ORACLE_ARGS( U( a ), U( b ) ), ORACLE_ARGS( U( CALL ) ) );                                        \
		}                                                                                                                  \
	}

ORACLE_SCALAR_BINARY( oracleMul, fixMul( a, b ), ORACLE_EDGE_ALL )
ORACLE_SCALAR_BINARY( oracleDiv, fixDiv( a, b ), ORACLE_EDGE_ALL )
// INT64_MIN is not a fixed_t value (FIX_MIN is -INT64_MAX), and fixAtan2 is not defined on it:
// on x86-64 with native 128-bit arithmetic fixAtan2( 0, INT64_MIN ) raises SIGFPE in the
// tree this baseline was captured from, while the emulated arm returns. So the grid here
// leaves it out; the random stream below can still produce it (one draw in 2^64).
ORACLE_SCALAR_BINARY( oracleAtan2, fixAtan2( a, b ), ORACLE_EDGE_NO_INT64_MIN )
ORACLE_SCALAR_BINARY( oraclePow, fixPow( a, b ), ORACLE_EDGE_ALL )

// (a) fixSqrt: every raw input below 2^32.
ORACLE_SCALAR_UNARY( oracleSqrt, fixSqrt( a ),
	for ( int64_t a = 0; a < ( (int64_t)1 << 32 ); ++a ) { ORACLE_EMIT( ORACLE_ARGS( U( a ) ), ORACLE_ARGS( U( fixSqrt( a ) ) ) ); } )

// (a) the sine/cosine family: every raw angle in [-2 pi, 2 pi].
#define ORACLE_TWO_PI_RAW ( (int64_t)411775 )
static uint64_t oracleCosSinWord( fixCosSin cs ) { return oracleMix( U( cs.cosine ), U( cs.sine ) ); }
ORACLE_SCALAR_UNARY( oracleCosSin, oracleCosSinWord( fixComputeCosSin( a ) ),
	for ( int64_t a = -ORACLE_TWO_PI_RAW; a <= ORACLE_TWO_PI_RAW; ++a ) { fixCosSin cs = fixComputeCosSin( a );
		ORACLE_EMIT( ORACLE_ARGS( U( a ) ), ORACLE_ARGS( U( cs.cosine ), U( cs.sine ) ) ); } )
ORACLE_SCALAR_UNARY( oracleSin, fixSin( a ),
	for ( int64_t a = -ORACLE_TWO_PI_RAW; a <= ORACLE_TWO_PI_RAW; ++a ) { ORACLE_EMIT( ORACLE_ARGS( U( a ) ), ORACLE_ARGS( U( fixSin( a ) ) ) ); } )
ORACLE_SCALAR_UNARY( oracleCos, fixCos( a ),
	for ( int64_t a = -ORACLE_TWO_PI_RAW; a <= ORACLE_TWO_PI_RAW; ++a ) { ORACLE_EMIT( ORACLE_ARGS( U( a ) ), ORACLE_ARGS( U( fixCos( a ) ) ) ); } )

// (a) fixLog2: every raw input in [1, 2^22], then 4,096 evenly spaced inputs in every octave
// [2^k, 2^(k+1)) for k = 22..62.
ORACLE_SCALAR_UNARY( oracleLog2, fixLog2( a ),
	for ( int64_t a = 1; a <= ( (int64_t)1 << 22 ); ++a ) { ORACLE_EMIT( ORACLE_ARGS( U( a ) ), ORACLE_ARGS( U( fixLog2( a ) ) ) ); }
	for ( int k = 22; k <= 62; ++k ) for ( int64_t j = 0; j < 4096; ++j ) {
		int64_t a = ( (int64_t)1 << k ) + j * ( ( (int64_t)1 << k ) >> 12 );
		ORACLE_EMIT( ORACLE_ARGS( U( a ) ), ORACLE_ARGS( U( fixLog2( a ) ) ) ); } )

// (a) fixExp2: every raw input in [-17, 48] whole units, which spans from the underflow to
// zero through the saturation at 2^47.
ORACLE_SCALAR_UNARY( oracleExp2, fixExp2( a ),
	for ( int64_t a = -17 * FIX_ONE; a <= 48 * FIX_ONE; ++a ) { ORACLE_EMIT( ORACLE_ARGS( U( a ) ), ORACLE_ARGS( U( fixExp2( a ) ) ) ); } )

// fixISqrt128High: the unsigned edge grid crossed with itself as (hi, lo), then random words
// with a log-uniform high half (so both the <= 104-bit seeded arm and the restoring arm run).
static void oracleISqrt128High( fixOracle* o )
{
	uint64_t e[ORACLE_EDGE_MAX];
	int n = oracleEdgeUnsigned( e );
	for ( int i = 0; i < n; ++i )
		for ( int k = 0; k < n; ++k )
		{
			uint64_t hi = e[i], lo = e[k];
			ORACLE_EMIT( ORACLE_ARGS( hi, lo ), ORACLE_ARGS( fixISqrt128High( hi, lo ) ) );
		}
	oracleRng r = oracleSeed( o->op, 0 );
	for ( uint64_t j = 0; j < o->randomCount; ++j )
	{
		uint64_t hi = oracleNext( &r ) >> ( oracleNext( &r ) & 63 );
		if ( ( oracleNext( &r ) & 7 ) == 0 ) hi = 0;
		uint64_t lo = oracleNext( &r );
		ORACLE_EMIT( ORACLE_ARGS( hi, lo ), ORACLE_ARGS( fixISqrt128High( hi, lo ) ) );
	}
}

// fixLerp: the edge grid inside the domain the sum stays representable on (|a|, |b| <= 2^46,
// |alpha| <= 2^20 raw), crossed three ways, then random inputs in the same domain.
static void oracleLerp( fixOracle* o )
{
	int64_t e[ORACLE_EDGE_MAX], ea[ORACLE_EDGE_MAX];
	int n = oracleEdge( e, (int64_t)1 << 46 );
	int na = oracleEdge( ea, (int64_t)1 << 20 );
	for ( int i = 0; i < n; ++i )
		for ( int k = 0; k < n; ++k )
			for ( int t = 0; t < na; ++t )
			{
				int64_t a = e[i], b = e[k], alpha = ea[t];
				ORACLE_EMIT( ORACLE_ARGS( U( a ), U( b ), U( alpha ) ), ORACLE_ARGS( U( fixLerp( a, b, alpha ) ) ) );
			}
	oracleRng r = oracleSeed( o->op, 0 );
	for ( uint64_t j = 0; j < o->randomCount; ++j )
	{
		int64_t a = oracleShapedBits( &r, 46 ), b = oracleShapedBits( &r, 46 ), alpha = oracleShapedBits( &r, 20 );
		ORACLE_EMIT( ORACLE_ARGS( U( a ), U( b ), U( alpha ) ), ORACLE_ARGS( U( fixLerp( a, b, alpha ) ) ) );
	}
}

// The 128-bit seam: fixInt128Div over the signed 128-bit edge grid crossed with itself
// (without a zero divisor or the one overflowing quotient), then random operands of
// log-uniform bit length.
static oracleU128 oracleRandom128( oracleRng* r )
{
	oracleU128 v = { oracleNext( r ), oracleNext( r ) };
	int s = (int)( oracleNext( r ) & 127 );
	uint64_t sign = ( v.hi >> 63 ) ? ~UINT64_C( 0 ) : 0;
	if ( s >= 64 ) { v.lo = (uint64_t)oracleAsr( v.hi, s - 64 ); v.hi = sign; }
	else if ( s > 0 ) { v.lo = ( v.lo >> s ) | ( v.hi << ( 64 - s ) ); v.hi = (uint64_t)oracleAsr( v.hi, s ); }
	return v;
}

static int oracleDiv128Defined( oracleU128 a, oracleU128 b )
{
	if ( b.hi == 0 && b.lo == 0 ) return 0;
	if ( a.hi == UINT64_C( 0x8000000000000000 ) && a.lo == 0 && b.hi == ~UINT64_C( 0 ) && b.lo == ~UINT64_C( 0 ) ) return 0;
	return 1;
}

static void oracleInt128Div( fixOracle* o )
{
	oracleU128 e[ORACLE_EDGE128_MAX];
	int n = oracleEdge128( e );
	for ( int i = 0; i < n; ++i )
		for ( int k = 0; k < n; ++k )
		{
			oracleU128 a = e[i], b = e[k];
			if ( !oracleDiv128Defined( a, b ) ) continue;
			fixInt128 q = fixInt128Div( fixInt128Make( a.hi, a.lo ), fixInt128Make( b.hi, b.lo ) );
			ORACLE_EMIT( ORACLE_ARGS( a.hi, a.lo, b.hi, b.lo ), ORACLE_ARGS( fixInt128Hi( q ), fixInt128Lo( q ) ) );
		}
	oracleRng r = oracleSeed( o->op, 0 );
	for ( uint64_t j = 0; j < o->randomCount; ++j )
	{
		oracleU128 a = oracleRandom128( &r ), b = oracleRandom128( &r );
		if ( !oracleDiv128Defined( a, b ) ) b.lo |= 2;
		fixInt128 q = fixInt128Div( fixInt128Make( a.hi, a.lo ), fixInt128Make( b.hi, b.lo ) );
		ORACLE_EMIT( ORACLE_ARGS( a.hi, a.lo, b.hi, b.lo ), ORACLE_ARGS( fixInt128Hi( q ), fixInt128Lo( q ) ) );
	}
}

// The 256-bit divide: fixUInt256DivMod over the unsigned 256-bit edge grid crossed with
// itself (no zero divisor), then random operands of log-uniform bit length.
static oracleU256 oracleRandom256( oracleRng* r )
{
	oracleU256 v;
	for ( int i = 0; i < 4; ++i ) v.l[i] = oracleNext( r );
	int s = (int)( oracleNext( r ) & 255 );
	int limbs = s / 64, bits = s % 64;
	for ( int i = 0; i < 4; ++i )
	{
		uint64_t lo = i + limbs < 4 ? v.l[i + limbs] : 0;
		uint64_t hi = i + limbs + 1 < 4 ? v.l[i + limbs + 1] : 0;
		v.l[i] = bits == 0 ? lo : ( lo >> bits ) | ( hi << ( 64 - bits ) );
	}
	return v;
}

#define ORACLE_DIV256( A, B )                                                                                              \
	do                                                                                                                     \
	{                                                                                                                      \
		fixUInt256 q_, r_;                                                                                                 \
		uint64_t ql_[4], rl_[4];                                                                                           \
		fixUInt256DivMod( fixUInt256FromLimbs( ( A ).l ), fixUInt256FromLimbs( ( B ).l ), &q_, &r_ );                     \
		fixUInt256ToLimbs( q_, ql_ );                                                                                      \
		fixUInt256ToLimbs( r_, rl_ );                                                                                      \
		ORACLE_EMIT( ORACLE_ARGS( ( A ).l[0], ( A ).l[1], ( A ).l[2], ( A ).l[3], ( B ).l[0], ( B ).l[1], ( B ).l[2], ( B ).l[3] ), \
					 ORACLE_ARGS( ql_[0], ql_[1], ql_[2], ql_[3], rl_[0], rl_[1], rl_[2], rl_[3] ) );                     \
	} while ( 0 )

static void oracleUInt256DivMod( fixOracle* o )
{
	static oracleU256 e[ORACLE_EDGE256_MAX];
	int n = oracleEdge256( e );
	for ( int i = 0; i < n; ++i )
		for ( int k = 0; k < n; ++k )
		{
			if ( oracleIsZero256( &e[k] ) ) continue;
			ORACLE_DIV256( e[i], e[k] );
		}
	oracleRng r = oracleSeed( o->op, 0 );
	for ( uint64_t j = 0; j < o->randomCount; ++j )
	{
		oracleU256 a = oracleRandom256( &r ), b = oracleRandom256( &r );
		if ( oracleIsZero256( &b ) ) b.l[0] = 1;
		ORACLE_DIV256( a, b );
	}
}

// Quantize boundary: integers below 2^30 over a power of two up to 2^23, at each scale.
static void oracleQuantize( fixOracle* o )
{
	int64_t e[ORACLE_EDGE_MAX];
	int n = oracleEdge( e, (int64_t)1 << 30 );
	for ( int i = 0; i < n; ++i )
		for ( int s = 0; s < ORACLE_SCALE_COUNT; ++s )
		{
			double v = (double)e[i];
			ORACLE_EMIT( ORACLE_ARGS( oracleDoubleBits( v ), U( oracleScales[s] ) ), ORACLE_ARGS( U( fixQuantize( v, oracleScales[s] ) ) ) );
		}
	oracleRng r = oracleSeed( o->op, 0 );
	for ( uint64_t j = 0; j < o->randomCount; ++j )
	{
		double v = oracleExactDouble( oracleShapedBits( &r, 30 ), (int)( oracleNext( &r ) % 24 ) );
		int64_t scale = oracleScales[oracleNext( &r ) % ORACLE_SCALE_COUNT];
		ORACLE_EMIT( ORACLE_ARGS( oracleDoubleBits( v ), U( scale ) ), ORACLE_ARGS( U( fixQuantize( v, scale ) ) ) );
	}
}

static void oracleDequantize( fixOracle* o )
{
	int64_t e[ORACLE_EDGE_MAX];
	int n = oracleEdge( e, ORACLE_EDGE_ALL );
	for ( int i = 0; i < n; ++i )
		for ( int s = 0; s < ORACLE_SCALE_COUNT; ++s )
		{
			int64_t a = e[i];
			ORACLE_EMIT( ORACLE_ARGS( U( a ), U( oracleScales[s] ) ), ORACLE_ARGS( oracleDoubleBits( fixDequantize( a, oracleScales[s] ) ) ) );
		}
	oracleRng r = oracleSeed( o->op, 0 );
	for ( uint64_t j = 0; j < o->randomCount; ++j )
	{
		int64_t a = oracleShaped( &r );
		int64_t scale = oracleScales[oracleNext( &r ) % ORACLE_SCALE_COUNT];
		ORACLE_EMIT( ORACLE_ARGS( U( a ), U( scale ) ), ORACLE_ARGS( oracleDoubleBits( fixDequantize( a, scale ) ) ) );
	}
}

// ---- composites, (d) ----

#define V3( v ) U( ( v ).x ), U( ( v ).y ), U( ( v ).z )
#define Q4( q ) V3( ( q ).v ), U( ( q ).s )
#define M9( m ) V3( ( m ).cx ), V3( ( m ).cy ), V3( ( m ).cz )

#define ORACLE_COMPOSITE( FN, SETUP, IN, OUT )                                                                              \
	static void FN( fixOracle* o )                                                                                         \
	{                                                                                                                      \
		oracleRng r = oracleSeed( o->op, 0 );                                                                              \
		uint64_t count = o->randomCount / 10;                                                                              \
		for ( uint64_t j = 0; j < count; ++j )                                                                             \
		{                                                                                                                  \
			SETUP;                                                                                                         \
			ORACLE_EMIT( ORACLE_ARGS IN, ORACLE_ARGS OUT );                                                                                        \
		}                                                                                                                  \
	}

ORACLE_COMPOSITE( oracleDot, fixVec3 a = oracleVec( &r ); fixVec3 b = oracleVec( &r ); fixed_t d = fixDot( a, b ),
				  ( V3( a ), V3( b ) ), ( U( d ) ) )
ORACLE_COMPOSITE( oracleCross, fixVec3 a = oracleVec( &r ); fixVec3 b = oracleVec( &r ); fixVec3 c = fixCross( a, b ),
				  ( V3( a ), V3( b ) ), ( V3( c ) ) )
ORACLE_COMPOSITE( oracleLength, fixVec3 a = oracleVec( &r ); fixed_t l = fixLength( a ),
				  ( V3( a ) ), ( U( l ) ) )
ORACLE_COMPOSITE( oracleNormalize, fixVec3 a = oracleVec( &r ); fixVec3 c = fixNormalize( a ),
				  ( V3( a ) ), ( V3( c ) ) )
ORACLE_COMPOSITE( oracleMulQuat,
				  fixQuat a = ( j & 1 ) ? oracleUnitQuat( &r ) : oracleAnyQuat( &r );
				  fixQuat b = ( j & 1 ) ? oracleUnitQuat( &r ) : oracleAnyQuat( &r );
				  if ( !( j & 1 ) ) { a.v.x >>= 16; a.v.y >>= 16; a.v.z >>= 16; a.s >>= 16; b.v.x >>= 16; b.v.y >>= 16; b.v.z >>= 16; b.s >>= 16; }
				  fixQuat c = fixMulQuat( a, b ),
				  ( Q4( a ), Q4( b ) ), ( Q4( c ) ) )
ORACLE_COMPOSITE( oracleNormalizeQuat, fixQuat a = ( j & 1 ) ? oracleUnitQuat( &r ) : oracleAnyQuat( &r );
				  fixQuat c = fixNormalizeQuat( a ),
				  ( Q4( a ) ), ( Q4( c ) ) )
ORACLE_COMPOSITE( oracleRotateVector, fixQuat q = oracleUnitQuat( &r ); fixVec3 v = oracleVec( &r );
				  v.x >>= 4; v.y >>= 4; v.z >>= 4; fixVec3 c = fixRotateVector( q, v ),
				  ( Q4( q ), V3( v ) ), ( V3( c ) ) )
ORACLE_COMPOSITE( oracleInvRotateVector, fixQuat q = oracleUnitQuat( &r ); fixVec3 v = oracleVec( &r );
				  v.x >>= 4; v.y >>= 4; v.z >>= 4; fixVec3 c = fixInvRotateVector( q, v ),
				  ( Q4( q ), V3( v ) ), ( V3( c ) ) )
ORACLE_COMPOSITE( oracleNLerp, fixQuat a = oracleUnitQuat( &r ); fixQuat b = oracleUnitQuat( &r );
				  fixed_t alpha = (fixed_t)( oracleNext( &r ) % ( FIX_ONE + 1 ) ); fixQuat c = fixNLerp( a, b, alpha ),
				  ( Q4( a ), Q4( b ), U( alpha ) ), ( Q4( c ) ) )
ORACLE_COMPOSITE( oracleInvertMatrix, fixMatrix3 m = oracleMatrix( &r ); fixMatrix3 c = fixInvertMatrix( m ),
				  ( M9( m ) ), ( M9( c ) ) )
ORACLE_COMPOSITE( oracleSolve3, fixMatrix3 m = oracleMatrix( &r ); fixVec3 a = oracleVec( &r ); fixVec3 c = fixSolve3( m, a ),
				  ( M9( m ), V3( a ) ), ( V3( c ) ) )
ORACLE_COMPOSITE( oracleAABBTransform,
				  fixTransform t; t.p = oracleVec( &r ); t.q = oracleUnitQuat( &r );
				  fixVec3 center = oracleVec( &r ); center.x >>= 2; center.y >>= 2; center.z >>= 2;
				  fixVec3 ext = oracleVec( &r ); ext.x = ( ext.x < 0 ? -ext.x : ext.x ) >> 2; ext.y = ( ext.y < 0 ? -ext.y : ext.y ) >> 2;
				  ext.z = ( ext.z < 0 ? -ext.z : ext.z ) >> 2;
				  fixAABB box; box.lowerBound = fixVecSub( center, ext ); box.upperBound = fixVecAdd( center, ext );
				  fixAABB c = fixAABB_Transform( t, box ),
				  ( V3( t.p ), Q4( t.q ), V3( box.lowerBound ), V3( box.upperBound ) ),
				  ( V3( c.lowerBound ), V3( c.upperBound ) ) )

// ---------------------------------------------------------------------------------------------
// The table. The order and the names are part of the baseline file; append, never reorder.
// ---------------------------------------------------------------------------------------------

typedef struct fixOracleOp
{
	const char* name;
	void ( *run )( fixOracle* o );
} fixOracleOp;

static const fixOracleOp fixOracleOps[] = {
	{ "fixMul", oracleMul },
	{ "fixDiv", oracleDiv },
	{ "fixSqrt", oracleSqrt },
	{ "fixISqrt128High", oracleISqrt128High },
	{ "fixComputeCosSin", oracleCosSin },
	{ "fixSin", oracleSin },
	{ "fixCos", oracleCos },
	{ "fixAtan2", oracleAtan2 },
	{ "fixLog2", oracleLog2 },
	{ "fixExp2", oracleExp2 },
	{ "fixPow", oraclePow },
	{ "fixLerp", oracleLerp },
	{ "fixDot", oracleDot },
	{ "fixCross", oracleCross },
	{ "fixLength", oracleLength },
	{ "fixNormalize", oracleNormalize },
	{ "fixMulQuat", oracleMulQuat },
	{ "fixNormalizeQuat", oracleNormalizeQuat },
	{ "fixRotateVector", oracleRotateVector },
	{ "fixInvRotateVector", oracleInvRotateVector },
	{ "fixNLerp", oracleNLerp },
	{ "fixInvertMatrix", oracleInvertMatrix },
	{ "fixSolve3", oracleSolve3 },
	{ "fixAABB_Transform", oracleAABBTransform },
	{ "fixQuantize", oracleQuantize },
	{ "fixDequantize", oracleDequantize },
	{ "fixInt128Div", oracleInt128Div },
	{ "fixUInt256DivMod", oracleUInt256DivMod },
};

enum { FIX_ORACLE_OP_COUNT = (int)( sizeof( fixOracleOps ) / sizeof( fixOracleOps[0] ) ) };

static int fixOracleFind( const char* name )
{
	for ( int i = 0; i < FIX_ORACLE_OP_COUNT; ++i )
		if ( strcmp( fixOracleOps[i].name, name ) == 0 ) return i;
	return -1;
}

/// Run one operation's whole sequence (or its first `limit` inputs) and return its hash.
/// o->checkpoint, o->dump and o->randomCount are the caller's; the rest is reset here.
static uint64_t fixOracleRun( int op, fixOracle* o, uint64_t limit )
{
	o->hash = UINT64_C( 0xCBF29CE484222325 );
	o->index = 0;
	o->limit = limit;
	o->op = op;
	fixOracleOps[op].run( o );
	return o->hash;
}
