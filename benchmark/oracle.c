// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
//
// The fixed output oracle. For every public hot operation this computes a 64-bit hash
// of the outputs over a defined, deterministic input set: the structured edge grid
// (0, +-1, +-2^k, +-(2^k - 1) for every k, INT64_MIN/MAX, saturation values), the
// exhaustive prefixes where feasible (fixSqrt over every raw input below 2^32; the
// trig over every raw angle in [-2pi, 2pi] at fixed resolution; a dense log2/exp2
// grid), and a seeded pseudo-random stream (xoshiro256**, the seed is in this source).
//
// The hash folds outputs in input order, so a one-bit change in any output moves the
// hash. The library guarantee is bit-identity across platforms, architectures and
// compilers, native __int128 and FIX_FORCE_EMULATED_INT128 alike: the SAME hashes must
// come out of every arm. This file is the source those hashes are frozen from.
//
// It is also #included (with ORACLE_LIB defined) by oracle_check.c and by
// test/oracle_unit_test.c, which link the same hashing core without its main().
#include "fixed/fixed_vec.h"
#include "fixed/fixed_wide.h"
#include "fixed/fixed_quantize.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------
// Deterministic hashing
// ---------------------------------------------------------------------------------------------

static uint64_t oracle_mix(uint64_t x)
{
	x += UINT64_C( 0x9E3779B97F4A7C15 );
	x = ( x ^ ( x >> 30 ) ) * UINT64_C( 0xBF58476D1CE4E5B9 );
	x = ( x ^ ( x >> 27 ) ) * UINT64_C( 0x94D049BB133111EB );
	return x ^ ( x >> 31 );
}

static void oracle_feed_u64( uint64_t* h, uint64_t v ) { *h = oracle_mix( *h ^ oracle_mix( v ) ); }
static void oracle_feed_i64( uint64_t* h, int64_t v ) { oracle_feed_u64( h, (uint64_t)v ); }
static void oracle_feed_double( uint64_t* h, double d )
{
	uint64_t b;
	memcpy( &b, &d, sizeof( b ) );
	oracle_feed_u64( h, b );
}
static void oracle_feed_vec3( uint64_t* h, fixVec3 v )
{
	oracle_feed_i64( h, v.x );
	oracle_feed_i64( h, v.y );
	oracle_feed_i64( h, v.z );
}
static void oracle_feed_quat( uint64_t* h, fixQuat q )
{
	oracle_feed_vec3( h, q.v );
	oracle_feed_i64( h, q.s );
}
static void oracle_feed_mat3( uint64_t* h, fixMatrix3 m )
{
	oracle_feed_vec3( h, m.cx );
	oracle_feed_vec3( h, m.cy );
	oracle_feed_vec3( h, m.cz );
}
static void oracle_feed_aabb( uint64_t* h, fixAABB a )
{
	oracle_feed_vec3( h, a.lowerBound );
	oracle_feed_vec3( h, a.upperBound );
}

// ---------------------------------------------------------------------------------------------
// xoshiro256** — the fixed random stream. The seed is in this source.
// ---------------------------------------------------------------------------------------------

typedef struct oracle_rng
{
	uint64_t s[4];
} oracle_rng;

static uint64_t oracle_rotl( uint64_t x, int k ) { return ( x << k ) | ( x >> ( 64 - k ) ); }

static uint64_t oracle_rng_next( oracle_rng* r )
{
	const uint64_t result = oracle_rotl( r->s[1] * 5, 7 ) * 9;
	const uint64_t t = r->s[1] << 17;
	r->s[2] ^= r->s[0];
	r->s[3] ^= r->s[1];
	r->s[1] ^= r->s[2];
	r->s[0] ^= r->s[3];
	r->s[2] ^= t;
	r->s[3] = oracle_rotl( r->s[3], 45 );
	return result;
}

static oracle_rng oracle_rng_seed( void )
{
	oracle_rng r = { { UINT64_C( 0x243F6A8885A308D3 ), UINT64_C( 0x13198A2E03707344 ),
		UINT64_C( 0xA4093822299F31D0 ), UINT64_C( 0x082EFA98EC4E6C89 ) } };
	return r;
}

// ---------------------------------------------------------------------------------------------
// Structured edge grid
// ---------------------------------------------------------------------------------------------

#define ORACLE_MAX_EDGES 320

static int oracle_make_edges( int64_t* e )
{
	int n = 0;
	e[n++] = 0;
	e[n++] = 1;
	e[n++] = -1;
	// INT64_MIN is reserved (fixIsValidFixed rejects it; fixAbs/fixAtan2 are undefined on
	// it), so it is exercised only through the full-range random streams, not the grid.
	e[n++] = INT64_MAX;
	e[n++] = FIX_MIN; // negative saturation
	e[n++] = FIX_MAX; // positive saturation
	for ( int k = 0; k < 62; ++k )
	{
		int64_t p = (int64_t)1 << k;
		e[n++] = p;
		e[n++] = -p;
		e[n++] = p - 1;
		e[n++] = -( p - 1 );
	}
	return n;
}

// ---------------------------------------------------------------------------------------------
// Random input generators (each draws from a fresh xoshiro seeded from the fixed seed,
// so every op's stream is deterministic and reproducible on its own).
// ---------------------------------------------------------------------------------------------

static int64_t oracle_rnd_i64( oracle_rng* r, int mode )
{
	uint64_t v = oracle_rng_next( r );
	if ( mode == 0 ) // "ordinary" small magnitudes, ~+-2^20
	{
		return (int64_t)( v & UINT64_C( 0x1FFFFF ) ) - (int64_t)( 1 << 20 );
	}
	if ( mode == 1 ) // full int64
	{
		return (int64_t)v;
	}
	if ( mode == 2 ) // positive, below 2^48 (hits fixDiv/width fast paths and u64 sqrt)
	{
		return (int64_t)( v & ( ( UINT64_C( 1 ) << 48 ) - 1 ) );
	}
	return (int64_t)v;
}

static uint64_t oracle_rnd_u64( oracle_rng* r ) { return oracle_rng_next( r ); }

static fixVec3 oracle_rnd_vec3( oracle_rng* r )
{
	uint64_t v = oracle_rng_next( r );
	return (fixVec3){ (fixed_t)( (int64_t)( v & UINT64_C( 0x1FFFFF ) ) - ( 1 << 20 ) ),
		(fixed_t)( (int64_t)( ( v >> 21 ) & UINT64_C( 0x1FFFFF ) ) - ( 1 << 20 ) ),
		(fixed_t)( (int64_t)( ( v >> 42 ) & UINT64_C( 0x1FFFFF ) ) - ( 1 << 20 ) ) };
}

static fixQuat oracle_rnd_quat( oracle_rng* r )
{
	uint64_t v = oracle_rng_next( r );
	fixQuat q = { { (fixed_t)( (int64_t)( v & UINT64_C( 0x1FFFFF ) ) - ( 1 << 20 ) ),
		(fixed_t)( (int64_t)( ( v >> 21 ) & UINT64_C( 0x1FFFFF ) ) - ( 1 << 20 ) ),
		(fixed_t)( (int64_t)( ( v >> 42 ) & UINT64_C( 0x1FFFFF ) ) - ( 1 << 20 ) ) },
		FIX_ONE + (fixed_t)( (int64_t)( ( v >> 21 ) & UINT64_C( 0xFFFF ) ) - 32768 ) };
	return q;
}

static fixMatrix3 oracle_rnd_mat3( oracle_rng* r )
{
	fixMatrix3 m;
	m.cx = oracle_rnd_vec3( r );
	m.cy = oracle_rnd_vec3( r );
	m.cz = oracle_rnd_vec3( r );
	// Bias toward invertible, inertia-like symmetric-ish matrices by strengthening the
	// diagonal so the exact 128-bit arm (the solver's arm) is the one exercised.
	m.cx.x += 4 * FIX_ONE;
	m.cy.y += 4 * FIX_ONE;
	m.cz.z += 4 * FIX_ONE;
	return m;
}

static fixAABB oracle_rnd_aabb( oracle_rng* r )
{
	fixVec3 c = oracle_rnd_vec3( r );
	fixVec3 e = { fixAbs( oracle_rnd_i64( r, 0 ) ) + FIX_ONE, fixAbs( oracle_rnd_i64( r, 0 ) ) + FIX_ONE,
		fixAbs( oracle_rnd_i64( r, 0 ) ) + FIX_ONE };
	return (fixAABB){ fixVecSub( c, e ), fixVecAdd( c, e ) };
}

// ---------------------------------------------------------------------------------------------
// Per-operation hashing. Each operation folds the structured edge grid (crossed with
// itself for binary ops) and then `randomCount` seeded inputs.
// ---------------------------------------------------------------------------------------------

// unary/binary helpers
static uint64_t oracle_hash_seed( void )
{
	uint64_t h = UINT64_C( 0xCBF29CE484222325 ); // FNV offset basis
	return h;
}

typedef fixed_t ( *oracle_unary_t )( fixed_t );
typedef fixed_t ( *oracle_binary_t )( fixed_t, fixed_t );

static uint64_t oracle_unary( oracle_unary_t fn, uint64_t randomCount, int mode )
{
	uint64_t h = oracle_hash_seed();
	int64_t e[ORACLE_MAX_EDGES];
	int ne = oracle_make_edges( e );
	for ( int i = 0; i < ne; ++i )
		oracle_feed_i64( &h, fn( e[i] ) );
	oracle_rng r = oracle_rng_seed();
	for ( uint64_t i = 0; i < randomCount; ++i )
		oracle_feed_i64( &h, fn( oracle_rnd_i64( &r, mode ) ) );
	return h;
}

static uint64_t oracle_binary( oracle_binary_t fn, uint64_t randomCount, int modeA, int modeB )
{
	uint64_t h = oracle_hash_seed();
	int64_t e[ORACLE_MAX_EDGES];
	int ne = oracle_make_edges( e );
	for ( int i = 0; i < ne; ++i )
		for ( int j = 0; j < ne; ++j )
			oracle_feed_i64( &h, fn( e[i], e[j] ) );
	oracle_rng r = oracle_rng_seed();
	for ( uint64_t i = 0; i < randomCount; ++i )
		oracle_feed_i64( &h, fn( oracle_rnd_i64( &r, modeA ), oracle_rnd_i64( &r, modeB ) ) );
	return h;
}

// fixDiv wants a nonzero divisor; fold with the divisor guaranteed nonzero.
static uint64_t oracle_div( uint64_t randomCount, int numMode )
{
	uint64_t h = oracle_hash_seed();
	int64_t e[ORACLE_MAX_EDGES];
	int ne = oracle_make_edges( e );
	for ( int i = 0; i < ne; ++i )
		for ( int j = 0; j < ne; ++j )
			oracle_feed_i64( &h, fixDiv( e[i], e[j] == 0 ? 1 : e[j] ) );
	oracle_rng r = oracle_rng_seed();
	for ( uint64_t i = 0; i < randomCount; ++i )
	{
		int64_t den = oracle_rnd_i64( &r, 0 );
		if ( den == 0 )
			den = 1;
		oracle_feed_i64( &h, fixDiv( oracle_rnd_i64( &r, numMode ), den ) );
	}
	return h;
}

// The one operation with a fixed argument signature that doesn't match the helpers.
static uint64_t oracle_isqrt128( uint64_t randomCount )
{
	uint64_t h = oracle_hash_seed();
	int64_t e[ORACLE_MAX_EDGES];
	int ne = oracle_make_edges( e );
	for ( int i = 0; i < ne; ++i )
		oracle_feed_i64( &h, (int64_t)fixISqrt128High( (uint64_t)e[i] >> 63, (uint64_t)e[i] ) );
	oracle_rng r = oracle_rng_seed();
	for ( uint64_t i = 0; i < randomCount; ++i )
	{
		uint64_t lo = oracle_rnd_u64( &r );
		uint64_t hi;
		switch ( i % 3 )
		{
			case 0: hi = 0; break;                                    // 64-bit arm
			case 1: hi = oracle_rnd_u64( &r ) & UINT64_C( 0xFFFFFFFFFF ); break; // seed arm (< 2^40)
			default: hi = ( UINT64_C( 1 ) << 40 ) | ( oracle_rnd_u64( &r ) & ( ( UINT64_C( 1 ) << 40 ) - 1 ) ); break; // restoring arm
		}
		oracle_feed_i64( &h, (int64_t)fixISqrt128High( hi, lo ) );
	}
	return h;
}

static uint64_t oracle_cos_sin( uint64_t randomCount )
{
	uint64_t h = oracle_hash_seed();
	int64_t e[ORACLE_MAX_EDGES];
	int ne = oracle_make_edges( e );
	for ( int i = 0; i < ne; ++i )
	{
		fixCosSin cs = fixComputeCosSin( e[i] );
		oracle_feed_i64( &h, cs.cosine );
		oracle_feed_i64( &h, cs.sine );
	}
	oracle_rng r = oracle_rng_seed();
	for ( uint64_t i = 0; i < randomCount; ++i )
	{
		fixCosSin cs = fixComputeCosSin( oracle_rnd_i64( &r, 0 ) );
		oracle_feed_i64( &h, cs.cosine );
		oracle_feed_i64( &h, cs.sine );
	}
	return h;
}

// Exhaustive trig over every raw angle in [-2pi, 2pi] at the fixed resolution.
// 2pi ~ 411775 raw units, so [-2pi, 2pi] is ~823551 raw values; about 800k. A nonzero
// limit takes the first `limit` values of the sweep so the unit test can use a prefix.
static uint64_t oracle_cos_sin_exhaustive( uint64_t limit )
{
	uint64_t h = oracle_hash_seed();
	const int64_t twoPi = FIX( 6.28318530718 ) * 2;
	if ( limit == 0 )
		limit = (uint64_t)( 2 * twoPi + 1 );
	uint64_t n = 0;
	for ( int64_t a = -twoPi; a <= twoPi && n < limit; ++a, ++n )
	{
		fixCosSin cs = fixComputeCosSin( a );
		oracle_feed_i64( &h, cs.cosine );
		oracle_feed_i64( &h, cs.sine );
	}
	return h;
}

// Exhaustive fixSqrt over every raw input below 2^32. Configurable so the unit test can
// take a small prefix without changing the stream.
static uint64_t oracle_sqrt_exhaustive( uint64_t limit )
{
	uint64_t h = oracle_hash_seed();
	for ( uint64_t a = 0; a < limit; ++a )
		oracle_feed_i64( &h, fixSqrt( (fixed_t)a ) );
	return h;
}

// Dense log2/exp2 grid over seeded values; a nonzero limit takes a prefix.
static uint64_t oracle_log_exp_grid( int isExp2, uint64_t limit )
{
	uint64_t h = oracle_hash_seed();
	oracle_rng r = oracle_rng_seed();
	if ( limit == 0 )
		limit = 200000;
	for ( uint64_t i = 0; i < limit; ++i )
	{
		int64_t v = (int64_t)oracle_rng_next( &r );
		int64_t a = v % ( 47 * FIX_ONE );
		oracle_feed_i64( &h, isExp2 ? fixExp2( a ) : fixLog2( a ) );
	}
	return h;
}

// Composite: hashing helpers that keep the per-op code short.
static uint64_t oracle_hash_one_inner( const char* name, uint64_t randomCount, uint64_t exhaustiveLimit );

// Name registry. oracle_check and the unit test walk this to know the full set.
static const char* const oracle_op_names[] = {
	"fixMul", "fixDivBelow", "fixDivAbove", "fixSqrtSmall", "fixSqrtU64", "fixSqrtWide",
	"fixISqrt128High", "fixComputeCosSin", "fixSin", "fixCos", "fixAtan2", "fixLog2",
	"fixExp2", "fixPow", "fixLerp", "fixDot", "fixCross", "fixLength", "fixNormalize",
	"fixMulQuat", "fixNormalizeQuat", "fixRotateVector", "fixInvRotateVector", "fixNLerp",
	"fixInvertMatrix", "fixSolve3", "fixAABBTransform", "fixQuantize", "fixDequantize",
	"fixInt128Div", "fixUInt256DivMod",
};
#define ORACLE_OP_COUNT ( sizeof( oracle_op_names ) / sizeof( oracle_op_names[0] ) )

static uint64_t oracle_hash_one_inner( const char* name, uint64_t randomCount, uint64_t exhaustiveLimit )
{
	uint64_t h;

	if ( !strcmp( name, "fixMul" ) )
		return oracle_binary( fixMul, randomCount, 0, 0 );
	if ( !strcmp( name, "fixDivBelow" ) )
		return oracle_div( randomCount, 0 );
	if ( !strcmp( name, "fixDivAbove" ) )
		return oracle_div( randomCount, 1 );
	if ( !strcmp( name, "fixSqrtSmall" ) )
		return oracle_unary( fixSqrt, randomCount, 0 );
	if ( !strcmp( name, "fixSqrtU64" ) )
		return oracle_unary( fixSqrt, randomCount, 2 );
	if ( !strcmp( name, "fixSqrtWide" ) )
	{
		// The >104-bit arm of fixISqrt128High only; there is no fixSqrt that reaches it.
		h = oracle_hash_seed();
		oracle_rng r = oracle_rng_seed();
		for ( uint64_t i = 0; i < randomCount; ++i )
		{
			uint64_t hi = ( UINT64_C( 1 ) << 40 ) | ( oracle_rnd_u64( &r ) & ( ( UINT64_C( 1 ) << 40 ) - 1 ) );
			oracle_feed_i64( &h, (int64_t)fixISqrt128High( hi, oracle_rnd_u64( &r ) ) );
		}
		return h;
	}
	if ( !strcmp( name, "fixISqrt128High" ) )
		return oracle_isqrt128( randomCount );
	if ( !strcmp( name, "fixComputeCosSin" ) )
	{
		h = oracle_cos_sin( randomCount );
		oracle_feed_u64( &h, oracle_cos_sin_exhaustive( exhaustiveLimit ) );
		return h;
	}
	if ( !strcmp( name, "fixSin" ) )
		return oracle_unary( fixSin, randomCount, 0 );
	if ( !strcmp( name, "fixCos" ) )
		return oracle_unary( fixCos, randomCount, 0 );
	if ( !strcmp( name, "fixAtan2" ) )
		return oracle_binary( fixAtan2, randomCount, 0, 0 );
	if ( !strcmp( name, "fixLog2" ) )
	{
		h = oracle_unary( fixLog2, randomCount, 2 );
		oracle_feed_u64( &h, oracle_log_exp_grid( 0, exhaustiveLimit ) );
		return h;
	}
	if ( !strcmp( name, "fixExp2" ) )
	{
		h = oracle_unary( fixExp2, randomCount, 0 );
		oracle_feed_u64( &h, oracle_log_exp_grid( 1, exhaustiveLimit ) );
		return h;
	}
	if ( !strcmp( name, "fixPow" ) )
		return oracle_binary( fixPow, randomCount, 0, 0 );
	if ( !strcmp( name, "fixLerp" ) )
	{
		h = oracle_hash_seed();
		int64_t e[ORACLE_MAX_EDGES];
		int ne = oracle_make_edges( e );
		for ( int i = 0; i < ne; ++i )
			for ( int j = 0; j < ne; ++j )
				oracle_feed_i64( &h, fixLerp( e[i], e[j], FIX_HALF ) );
		oracle_rng r = oracle_rng_seed();
		for ( uint64_t i = 0; i < randomCount; ++i )
			oracle_feed_i64( &h,
				fixLerp( oracle_rnd_i64( &r, 0 ), oracle_rnd_i64( &r, 0 ), (fixed_t)( oracle_rnd_u64( &r ) & 0xFFFF ) ) );
		return h;
	}

	// ---- vector / quaternion / matrix / AABB composites ----
	if ( !strcmp( name, "fixDot" ) )
	{
		h = oracle_hash_seed();
		oracle_rng r = oracle_rng_seed();
		for ( uint64_t i = 0; i < randomCount; ++i )
			oracle_feed_i64( &h, fixDot( oracle_rnd_vec3( &r ), oracle_rnd_vec3( &r ) ) );
		return h;
	}
	if ( !strcmp( name, "fixCross" ) )
	{
		h = oracle_hash_seed();
		oracle_rng r = oracle_rng_seed();
		for ( uint64_t i = 0; i < randomCount; ++i )
			oracle_feed_vec3( &h, fixCross( oracle_rnd_vec3( &r ), oracle_rnd_vec3( &r ) ) );
		return h;
	}
	if ( !strcmp( name, "fixLength" ) )
	{
		h = oracle_hash_seed();
		oracle_rng r = oracle_rng_seed();
		for ( uint64_t i = 0; i < randomCount; ++i )
			oracle_feed_i64( &h, fixLength( oracle_rnd_vec3( &r ) ) );
		return h;
	}
	if ( !strcmp( name, "fixNormalize" ) )
	{
		h = oracle_hash_seed();
		oracle_rng r = oracle_rng_seed();
		for ( uint64_t i = 0; i < randomCount; ++i )
			oracle_feed_vec3( &h, fixNormalize( oracle_rnd_vec3( &r ) ) );
		return h;
	}
	if ( !strcmp( name, "fixMulQuat" ) )
	{
		h = oracle_hash_seed();
		oracle_rng r = oracle_rng_seed();
		for ( uint64_t i = 0; i < randomCount; ++i )
			oracle_feed_quat( &h, fixMulQuat( oracle_rnd_quat( &r ), oracle_rnd_quat( &r ) ) );
		return h;
	}
	if ( !strcmp( name, "fixNormalizeQuat" ) )
	{
		h = oracle_hash_seed();
		oracle_rng r = oracle_rng_seed();
		for ( uint64_t i = 0; i < randomCount; ++i )
			oracle_feed_quat( &h, fixNormalizeQuat( oracle_rnd_quat( &r ) ) );
		return h;
	}
	if ( !strcmp( name, "fixRotateVector" ) )
	{
		h = oracle_hash_seed();
		oracle_rng r = oracle_rng_seed();
		for ( uint64_t i = 0; i < randomCount; ++i )
			oracle_feed_vec3( &h, fixRotateVector( oracle_rnd_quat( &r ), oracle_rnd_vec3( &r ) ) );
		return h;
	}
	if ( !strcmp( name, "fixInvRotateVector" ) )
	{
		h = oracle_hash_seed();
		oracle_rng r = oracle_rng_seed();
		for ( uint64_t i = 0; i < randomCount; ++i )
			oracle_feed_vec3( &h, fixInvRotateVector( oracle_rnd_quat( &r ), oracle_rnd_vec3( &r ) ) );
		return h;
	}
	if ( !strcmp( name, "fixNLerp" ) )
	{
		h = oracle_hash_seed();
		oracle_rng r = oracle_rng_seed();
		for ( uint64_t i = 0; i < randomCount; ++i )
			oracle_feed_quat( &h, fixNLerp( oracle_rnd_quat( &r ), oracle_rnd_quat( &r ), FIX_HALF ) );
		return h;
	}
	if ( !strcmp( name, "fixInvertMatrix" ) )
	{
		h = oracle_hash_seed();
		oracle_rng r = oracle_rng_seed();
		for ( uint64_t i = 0; i < randomCount; ++i )
			oracle_feed_mat3( &h, fixInvertMatrix( oracle_rnd_mat3( &r ) ) );
		return h;
	}
	if ( !strcmp( name, "fixSolve3" ) )
	{
		h = oracle_hash_seed();
		oracle_rng r = oracle_rng_seed();
		for ( uint64_t i = 0; i < randomCount; ++i )
			oracle_feed_vec3( &h, fixSolve3( oracle_rnd_mat3( &r ), oracle_rnd_vec3( &r ) ) );
		return h;
	}
	if ( !strcmp( name, "fixAABBTransform" ) )
	{
		h = oracle_hash_seed();
		oracle_rng r = oracle_rng_seed();
		for ( uint64_t i = 0; i < randomCount; ++i )
		{
			fixQuat q = fixNormalizeQuat( oracle_rnd_quat( &r ) );
			fixTransform t = { oracle_rnd_vec3( &r ), q };
			oracle_feed_aabb( &h, fixAABB_Transform( t, oracle_rnd_aabb( &r ) ) );
		}
		return h;
	}
	if ( !strcmp( name, "fixQuantize" ) )
	{
		h = oracle_hash_seed();
		oracle_rng r = oracle_rng_seed();
		for ( uint64_t i = 0; i < randomCount; ++i )
		{
			double v = (double)oracle_rnd_i64( &r, 0 ) / (double)FIX_ONE;
			oracle_feed_i64( &h, fixQuantize( v, FIX_ONE ) );
		}
		return h;
	}
	if ( !strcmp( name, "fixDequantize" ) )
	{
		h = oracle_hash_seed();
		oracle_rng r = oracle_rng_seed();
		for ( uint64_t i = 0; i < randomCount; ++i )
			oracle_feed_double( &h, fixDequantize( oracle_rnd_i64( &r, 0 ), FIX_ONE ) );
		return h;
	}
	if ( !strcmp( name, "fixInt128Div" ) )
	{
		h = oracle_hash_seed();
		oracle_rng r = oracle_rng_seed();
		for ( uint64_t i = 0; i < randomCount; ++i )
		{
			fixInt128 a = fixInt128Make( oracle_rnd_u64( &r ), oracle_rnd_u64( &r ) );
			fixInt128 b = fixInt128Make( oracle_rnd_u64( &r ) & UINT64_C( 0x7FFFFFFFFFFFFFFF ),
				oracle_rnd_u64( &r ) | 1 ); // nonzero
			fixInt128 q = fixInt128Div( a, b );
			oracle_feed_i64( &h, fixInt128Hi( q ) );
			oracle_feed_i64( &h, fixInt128Lo( q ) );
		}
		return h;
	}
	if ( !strcmp( name, "fixUInt256DivMod" ) )
	{
		h = oracle_hash_seed();
		oracle_rng r = oracle_rng_seed();
		for ( uint64_t i = 0; i < randomCount; ++i )
		{
			fixUInt256 a = { fixUInt128Make( oracle_rnd_u64( &r ), oracle_rnd_u64( &r ) ),
				fixUInt128Make( oracle_rnd_u64( &r ), oracle_rnd_u64( &r ) ) };
			fixUInt256 b = { fixUInt128Make( oracle_rnd_u64( &r ) & UINT64_C( 0x7FFFFFFFFFFFFFFF ),
				oracle_rnd_u64( &r ) | 1 ), fixUInt128Make( oracle_rnd_u64( &r ), oracle_rnd_u64( &r ) ) };
			if ( fixUInt256IsZero( b ) )
			{
				b.lo = fixUInt128FromU64( 1 );
			}
			fixUInt256 q, rem;
			fixUInt256DivMod( a, b, &q, &rem );
			oracle_feed_i64( &h, (int64_t)fixUInt128Hi( q.hi ) );
			oracle_feed_i64( &h, (int64_t)fixUInt128Lo( q.hi ) );
			oracle_feed_i64( &h, (int64_t)fixUInt128Hi( q.lo ) );
			oracle_feed_i64( &h, (int64_t)fixUInt128Lo( q.lo ) );
			oracle_feed_i64( &h, (int64_t)fixUInt128Hi( rem.hi ) );
			oracle_feed_i64( &h, (int64_t)fixUInt128Lo( rem.hi ) );
			oracle_feed_i64( &h, (int64_t)fixUInt128Hi( rem.lo ) );
			oracle_feed_i64( &h, (int64_t)fixUInt128Lo( rem.lo ) );
		}
		return h;
	}

	return oracle_hash_seed();
}

// The one function every consumer uses: hash of operation `name` over the defined set.
// `exhaustiveLimit` bounds the exhaustive fixSqrt prefix (0 means the full 2^32).
uint64_t oracle_hash_one( const char* name, uint64_t randomCount, uint64_t exhaustiveLimit )
{
	uint64_t h = oracle_hash_one_inner( name, randomCount, exhaustiveLimit );
	if ( !strcmp( name, "fixSqrtU64" ) )
	{
		// The exhaustive fixSqrt sweep over every raw input below 2^32 folds here, once.
		uint64_t lim = exhaustiveLimit ? exhaustiveLimit : ( UINT64_C( 1 ) << 32 );
		oracle_feed_u64( &h, oracle_sqrt_exhaustive( lim ) );
	}
	return h;
}

// ---------------------------------------------------------------------------------------------
// Defaults
// ---------------------------------------------------------------------------------------------

uint64_t oracle_default_random_count( void ) { return UINT64_C( 50000000 ); }
uint64_t oracle_default_exhaustive_limit( void ) { return 0; } // 0 => full 2^32

#ifndef ORACLE_LIB

int main( int argc, char** argv )
{
	uint64_t randomCount = oracle_default_random_count();
	uint64_t exhaustiveLimit = oracle_default_exhaustive_limit();
	if ( argc > 1 )
		randomCount = (uint64_t)strtoull( argv[1], NULL, 10 );
	if ( argc > 2 )
		exhaustiveLimit = (uint64_t)strtoull( argv[2], NULL, 10 );

	for ( size_t i = 0; i < ORACLE_OP_COUNT; ++i )
	{
		uint64_t h = oracle_hash_one( oracle_op_names[i], randomCount, exhaustiveLimit );
		printf( "%s\t%016llx\n", oracle_op_names[i], (unsigned long long)h );
	}
	return 0;
}

#endif // ORACLE_LIB
