// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
// ns/op for every public hot operation, in two modes:
//   SCALAR — a dependent chain (latency): each result feeds the next call.
//   BATCH  — independent operations over arrays of 4096 inputs (throughput), the
//            baseline the SIMD batch spikes beat.
// Inputs are prepared outside the timed loops; every result is folded into a checksum
// so the work is not elided. CSV output: op, mode, ns_per_op min/median/max, checksum.
#define _POSIX_C_SOURCE 199309L
#include "fixed/fixed_vec.h"
#include "fixed/fixed_wide.h"
#include "fixed/fixed_quantize.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined( _MSC_VER )
#define NOINLINE __declspec( noinline )
#else
#define NOINLINE __attribute__( ( noinline ) )
#endif

enum { COUNT = 4096 };

static fixed_t a_in[COUNT], b_in[COUNT], c_in[COUNT];
static fixed_t num_lo[COUNT], num_hi[COUNT], pos_small[COUNT], pos_u64[COUNT], den[COUNT];
static uint64_t u64_in[COUNT], u64_in2[COUNT], hi_wide[COUNT];
static fixVec3 va[COUNT], vb[COUNT];
static fixQuat qa[COUNT], qb[COUNT];
static fixMatrix3 ma[COUNT];
static fixAABB ab[COUNT];
static double d_in[COUNT];
static fixInt128 i128a[COUNT], i128b[COUNT];
static fixUInt256 u256a[COUNT], u256b[COUNT];

static volatile uint64_t sink;
static uint64_t state;

static uint64_t rnd( void )
{
	state ^= state << 13;
	state ^= state >> 7;
	state ^= state << 17;
	return state;
}

static fixed_t smallSigned( uint64_t v ) { return (fixed_t)( (int64_t)( v & UINT64_C( 0x1FFFFF ) ) - ( 1 << 20 ) ); }
static fixed_t smallPos( uint64_t v ) { return (fixed_t)( v & UINT64_C( 0xFFFFF ) ); }

static void initialize( uint64_t seed )
{
	state = seed;
	for ( int i = 0; i < COUNT; ++i )
	{
		uint64_t x = rnd();
		a_in[i] = smallSigned( x );
		b_in[i] = smallSigned( rnd() );
		c_in[i] = smallPos( rnd() );
		num_lo[i] = smallSigned( rnd() ); // below 2^47
		num_hi[i] = (fixed_t)( (int64_t)( ( rnd() & ( ( UINT64_C( 1 ) << 40 ) - 1 ) ) | ( UINT64_C( 1 ) << 47 ) ) ); // above 2^47
		pos_small[i] = smallPos( rnd() );
		pos_u64[i] = (fixed_t)( rnd() & ( ( UINT64_C( 1 ) << 48 ) - 1 ) );
		den[i] = 1 + (fixed_t)( rnd() & 7 );
		u64_in[i] = rnd();
		u64_in2[i] = rnd();
		hi_wide[i] = ( UINT64_C( 1 ) << 40 ) | ( rnd() & ( ( UINT64_C( 1 ) << 40 ) - 1 ) );
		va[i] = (fixVec3){ smallSigned( rnd() ), smallSigned( rnd() ), smallSigned( rnd() ) };
		vb[i] = (fixVec3){ smallSigned( rnd() ), smallSigned( rnd() ), smallSigned( rnd() ) };
		fixVec3 qv = { smallSigned( rnd() ) >> 4, smallSigned( rnd() ) >> 4, smallSigned( rnd() ) >> 4 };
		qa[i] = fixNormalizeQuat( (fixQuat){ qv, FIX_ONE } );
		qb[i] = fixNormalizeQuat( (fixQuat){ (fixVec3){ smallSigned( rnd() ) >> 4, smallSigned( rnd() ) >> 4, smallSigned( rnd() ) >> 4 }, FIX_ONE } );
		fixMatrix3 m = { { smallSigned( rnd() ), smallSigned( rnd() ), smallSigned( rnd() ) },
			{ smallSigned( rnd() ), smallSigned( rnd() ), smallSigned( rnd() ) },
			{ smallSigned( rnd() ), smallSigned( rnd() ), smallSigned( rnd() ) } };
		m.cx.x += 4 * FIX_ONE;
		m.cy.y += 4 * FIX_ONE;
		m.cz.z += 4 * FIX_ONE;
		ma[i] = m;
		fixVec3 e = { smallPos( rnd() ) + FIX_ONE, smallPos( rnd() ) + FIX_ONE, smallPos( rnd() ) + FIX_ONE };
		ab[i] = (fixAABB){ fixVecSub( va[i], e ), fixVecAdd( va[i], e ) };
		d_in[i] = (double)smallSigned( rnd() ) / (double)FIX_ONE;
		i128a[i] = fixInt128Make( rnd(), rnd() );
		i128b[i] = fixInt128Make( rnd() & UINT64_C( 0x7FFFFFFFFFFFFFFF ), rnd() | 1 );
		u256a[i] = (fixUInt256){ fixUInt128Make( rnd(), rnd() ), fixUInt128Make( rnd(), rnd() ) };
		u256b[i] = (fixUInt256){ fixUInt128Make( rnd() & UINT64_C( 0x7FFFFFFFFFFFFFFF ), rnd() | 1 ),
			fixUInt128Make( rnd(), rnd() ) };
		if ( fixUInt256IsZero( u256b[i] ) )
			u256b[i].lo = fixUInt128FromU64( 1 );
	}
}

// ---------------------------------------------------------------------------------------------
// BATCH kernels: independent ops over the precomputed arrays.
// ---------------------------------------------------------------------------------------------

#define BATCH( name, ... )                                                                                               \
	static NOINLINE uint64_t batch_##name( int n )                                                                       \
	{                                                                                                                    \
		uint64_t s = 0;                                                                                                  \
		for ( int j = 0; j < n; ++j )                                                                                    \
		{                                                                                                                \
			int i = j & ( COUNT - 1 );                                                                                   \
			__VA_ARGS__;                                                                                                 \
		}                                                                                                                \
		return s;                                                                                                        \
	}

BATCH( fixMul, s += (uint64_t)fixMul( a_in[i], b_in[i] ) )
BATCH( fixDivBelow, s += (uint64_t)fixDiv( num_lo[i], den[i] ) )
BATCH( fixDivAbove, s += (uint64_t)fixDiv( num_hi[i], den[i] ) )
BATCH( fixSqrtSmall, s += (uint64_t)fixSqrt( pos_small[i] ) )
BATCH( fixSqrtU64, s += (uint64_t)fixSqrt( pos_u64[i] ) )
BATCH( fixSqrtWide, s += (uint64_t)fixISqrt128High( hi_wide[i], u64_in[i] ) )
BATCH( fixISqrt128High, s += (uint64_t)fixISqrt128High( u64_in2[i], u64_in[i] ) )
BATCH( fixComputeCosSin, fixCosSin cs = fixComputeCosSin( a_in[i] ); s += (uint64_t)cs.cosine + (uint64_t)cs.sine )
BATCH( fixSin, s += (uint64_t)fixSin( a_in[i] ) )
BATCH( fixCos, s += (uint64_t)fixCos( a_in[i] ) )
BATCH( fixAtan2, s += (uint64_t)fixAtan2( a_in[i], b_in[i] ) )
BATCH( fixLog2, s += (uint64_t)fixLog2( pos_u64[i] ) )
BATCH( fixExp2, s += (uint64_t)fixExp2( c_in[i] ) )
BATCH( fixPow, s += (uint64_t)fixPow( pos_small[i] + FIX_ONE, c_in[i] ) )
BATCH( fixLerp, s += (uint64_t)fixLerp( a_in[i], b_in[i], c_in[i] ) )
BATCH( fixDot, s += (uint64_t)fixDot( va[i], vb[i] ) )
BATCH( fixCross, fixVec3 r = fixCross( va[i], vb[i] ); s += (uint64_t)r.x + (uint64_t)r.y + (uint64_t)r.z )
BATCH( fixLength, s += (uint64_t)fixLength( va[i] ) )
BATCH( fixNormalize, fixVec3 r = fixNormalize( va[i] ); s += (uint64_t)r.x + (uint64_t)r.y + (uint64_t)r.z )
BATCH( fixMulQuat, fixQuat r = fixMulQuat( qa[i], qb[i] ); s += (uint64_t)r.v.x + (uint64_t)r.v.y + (uint64_t)r.v.z + (uint64_t)r.s )
BATCH( fixNormalizeQuat, fixQuat r = fixNormalizeQuat( qa[i] ); s += (uint64_t)r.v.x + (uint64_t)r.v.y + (uint64_t)r.v.z + (uint64_t)r.s )
BATCH( fixRotateVector, fixVec3 r = fixRotateVector( qa[i], vb[i] ); s += (uint64_t)r.x + (uint64_t)r.y + (uint64_t)r.z )
BATCH( fixInvRotateVector, fixVec3 r = fixInvRotateVector( qa[i], vb[i] ); s += (uint64_t)r.x + (uint64_t)r.y + (uint64_t)r.z )
BATCH( fixNLerp, fixQuat r = fixNLerp( qa[i], qb[i], FIX_HALF ); s += (uint64_t)r.v.x + (uint64_t)r.v.y + (uint64_t)r.v.z + (uint64_t)r.s )
BATCH( fixInvertMatrix, fixMatrix3 r = fixInvertMatrix( ma[i] ); s += (uint64_t)r.cx.x + (uint64_t)r.cy.y + (uint64_t)r.cz.z )
BATCH( fixSolve3, fixVec3 r = fixSolve3( ma[i], vb[i] ); s += (uint64_t)r.x + (uint64_t)r.y + (uint64_t)r.z )
BATCH( fixAABBTransform, fixTransform t = { va[i], qa[i] }; fixAABB r = fixAABB_Transform( t, ab[i] );
	s += (uint64_t)r.lowerBound.x + (uint64_t)r.upperBound.z )
BATCH( fixQuantize, s += (uint64_t)fixQuantize( d_in[i], FIX_ONE ) )
BATCH( fixDequantize, s += (uint64_t)fixDequantize( a_in[i], FIX_ONE ) )
BATCH( fixInt128Div, fixInt128 q = fixInt128Div( i128a[i], i128b[i] );
	s += (uint64_t)fixInt128Hi( q ) + (uint64_t)fixInt128Lo( q ) )
BATCH( fixUInt256DivMod, fixUInt256 q, rem; fixUInt256DivMod( u256a[i], u256b[i], &q, &rem );
	s += (uint64_t)fixUInt128Lo( q.lo ) + (uint64_t)fixUInt128Lo( q.hi ) + (uint64_t)fixUInt128Lo( rem.lo ) )

// ---------------------------------------------------------------------------------------------
// SCALAR kernels: a dependent chain. `x` is the running value; each op feeds the next,
// so the timing is the latency of one operation.
// ---------------------------------------------------------------------------------------------

static fixVec3 vecFromX( fixed_t x ) { return (fixVec3){ x, x >> 2, x >> 4 }; }
static fixQuat quatFromX( fixed_t x )
{
	fixQuat q = { { x >> 3, x >> 5, x >> 7 }, FIX_ONE + ( x >> 9 ) };
	return q;
}
static fixMatrix3 matFromX( fixed_t x )
{
	return (fixMatrix3){ { x, x >> 2, x >> 4 }, { x >> 2, x, x >> 4 }, { x >> 4, x >> 2, x } };
}
static fixAABB aabbFromX( fixed_t x )
{
	fixVec3 e = { ( x & 0xFFFF ) + 1, ( x & 0xFFFF ) + 1, ( x & 0xFFFF ) + 1 };
	return (fixAABB){ fixVecSub( (fixVec3){ x, x, x }, e ), fixVecAdd( (fixVec3){ x, x, x }, e ) };
}

#define SCALAR( name, init, ... )                                                                                        \
	static NOINLINE uint64_t scalar_##name( int n )                                                                      \
	{                                                                                                                    \
		fixed_t x = init;                                                                                                \
		uint64_t s = 0;                                                                                                  \
		for ( int j = 0; j < n; ++j )                                                                                    \
		{                                                                                                                \
			int i = j & ( COUNT - 1 );                                                                                   \
			__VA_ARGS__;                                                                                                 \
			s += (uint64_t)x;                                                                                            \
		}                                                                                                                \
		return s;                                                                                                        \
	}

SCALAR( fixMul, a_in[0], x = fixMul( x, b_in[i] ) )
SCALAR( fixDivBelow, num_lo[0], x = fixDiv( x, den[i] ) )
SCALAR( fixDivAbove, num_hi[0], x = fixDiv( x, den[i] ) )
SCALAR( fixSqrtSmall, pos_small[0], x = fixSqrt( ( x & UINT64_C( 0xFFFFF ) ) | 0x1000 ) )
SCALAR( fixSqrtU64, pos_u64[0], x = fixSqrt( ( x & UINT64_C( 0xFFFFFFFFFFFF ) ) | 0x1000 ) )
SCALAR( fixSqrtWide, (fixed_t)u64_in[0], x = (fixed_t)fixISqrt128High( hi_wide[i], (uint64_t)x ) )
SCALAR( fixISqrt128High, (fixed_t)u64_in[0], x = (fixed_t)fixISqrt128High( u64_in2[i] & UINT64_C( 0xFFFFFFFFFF ), (uint64_t)x ) )
SCALAR( fixComputeCosSin, a_in[0], fixCosSin cs = fixComputeCosSin( ( x & UINT64_C( 0xFFFFF ) ) - ( 1 << 20 ) ); x = cs.cosine; s += (uint64_t)cs.sine )
SCALAR( fixSin, a_in[0], x = fixSin( x ) )
SCALAR( fixCos, a_in[0], x = fixCos( x ) )
SCALAR( fixAtan2, a_in[0], x = fixAtan2( x, b_in[i] ) )
SCALAR( fixLog2, pos_u64[0], x = fixLog2( ( x & UINT64_C( 0x7FFFFFFFFFFFFFFF ) ) | 0x10000 ) )
SCALAR( fixExp2, c_in[0], x = fixExp2( x & 0x2F ) )
SCALAR( fixPow, pos_small[0] + FIX_ONE, x = fixPow( ( x & UINT64_C( 0xFFFFF ) ) + 1, c_in[i] ) )
SCALAR( fixLerp, a_in[0], x = fixLerp( x, b_in[i], c_in[i] ) )
SCALAR( fixDot, a_in[0], x = fixDot( vecFromX( x ), vb[i] ) )
SCALAR( fixCross, a_in[0], fixVec3 r = fixCross( vecFromX( x ), vb[i] ); x = r.x; s += (uint64_t)r.y + (uint64_t)r.z )
SCALAR( fixLength, a_in[0], x = fixLength( vecFromX( x ) ) )
SCALAR( fixNormalize, a_in[0], fixVec3 r = fixNormalize( vecFromX( x ) ); x = r.x; s += (uint64_t)r.y + (uint64_t)r.z )
SCALAR( fixMulQuat, a_in[0], fixQuat r = fixMulQuat( quatFromX( x ), qb[i] ); x = r.v.x; s += (uint64_t)r.s )
SCALAR( fixNormalizeQuat, a_in[0], fixQuat r = fixNormalizeQuat( quatFromX( x ) ); x = r.v.x; s += (uint64_t)r.s )
SCALAR( fixRotateVector, a_in[0], fixVec3 r = fixRotateVector( qa[i], vecFromX( x ) ); x = r.x; s += (uint64_t)r.y + (uint64_t)r.z )
SCALAR( fixInvRotateVector, a_in[0], fixVec3 r = fixInvRotateVector( qa[i], vecFromX( x ) ); x = r.x; s += (uint64_t)r.y + (uint64_t)r.z )
SCALAR( fixNLerp, a_in[0], fixQuat r = fixNLerp( quatFromX( x ), qb[i], FIX_HALF ); x = r.v.x; s += (uint64_t)r.s )
SCALAR( fixInvertMatrix, a_in[0], fixMatrix3 r = fixInvertMatrix( matFromX( x ) ); x = r.cx.x; s += (uint64_t)r.cy.y + (uint64_t)r.cz.z )
SCALAR( fixSolve3, a_in[0], fixVec3 r = fixSolve3( matFromX( x ), vecFromX( x ) ); x = r.x; s += (uint64_t)r.y + (uint64_t)r.z )
SCALAR( fixAABBTransform, a_in[0], fixTransform t = { vecFromX( x ), qa[i] }; fixAABB r = fixAABB_Transform( t, aabbFromX( x ) );
	x = r.lowerBound.x; s += (uint64_t)r.upperBound.z )
SCALAR( fixQuantize, a_in[0], x = (fixed_t)fixQuantize( (double)( ( x & UINT64_C( 0xFFFFF ) ) - ( 1 << 20 ) ) / (double)FIX_ONE, FIX_ONE ) )
SCALAR( fixDequantize, a_in[0], x = (fixed_t)( fixDequantize( x, FIX_ONE ) * (double)FIX_ONE ) )

// The 128/256-bit seams keep their own running state, not a fixed_t.
static NOINLINE uint64_t scalar_fixInt128Div( int n )
{
	fixInt128 q = i128a[0];
	uint64_t s = 0;
	for ( int j = 0; j < n; ++j )
	{
		int i = j & ( COUNT - 1 );
		q = fixInt128Div( q, fixInt128Make( 0, 1 + (uint64_t)( j & 3 ) ) );
		s += (uint64_t)fixInt128Hi( q ) + (uint64_t)fixInt128Lo( q );
	}
	return s;
}

static NOINLINE uint64_t scalar_fixUInt256DivMod( int n )
{
	fixUInt256 q = u256a[0];
	uint64_t s = 0;
	for ( int j = 0; j < n; ++j )
	{
		fixUInt256 d = { FIX_UINT128_ZERO, fixUInt128FromU64( 1 + (uint64_t)( j & 3 ) ) };
		fixUInt256 rem;
		fixUInt256DivMod( q, d, &q, &rem );
		s += (uint64_t)fixUInt128Lo( q.lo ) + (uint64_t)fixUInt128Lo( rem.lo );
	}
	return s;
}

// ---------------------------------------------------------------------------------------------
// Driver
// ---------------------------------------------------------------------------------------------

typedef struct
{
	const char* name;
	uint64_t ( *scalar )( int );
	uint64_t ( *batch )( int );
} op_bench_entry;

static const op_bench_entry ops[] = {
	{ "fixMul", scalar_fixMul, batch_fixMul },
	{ "fixDivBelow", scalar_fixDivBelow, batch_fixDivBelow },
	{ "fixDivAbove", scalar_fixDivAbove, batch_fixDivAbove },
	{ "fixSqrtSmall", scalar_fixSqrtSmall, batch_fixSqrtSmall },
	{ "fixSqrtU64", scalar_fixSqrtU64, batch_fixSqrtU64 },
	{ "fixSqrtWide", scalar_fixSqrtWide, batch_fixSqrtWide },
	{ "fixISqrt128High", scalar_fixISqrt128High, batch_fixISqrt128High },
	{ "fixComputeCosSin", scalar_fixComputeCosSin, batch_fixComputeCosSin },
	{ "fixSin", scalar_fixSin, batch_fixSin },
	{ "fixCos", scalar_fixCos, batch_fixCos },
	{ "fixAtan2", scalar_fixAtan2, batch_fixAtan2 },
	{ "fixLog2", scalar_fixLog2, batch_fixLog2 },
	{ "fixExp2", scalar_fixExp2, batch_fixExp2 },
	{ "fixPow", scalar_fixPow, batch_fixPow },
	{ "fixLerp", scalar_fixLerp, batch_fixLerp },
	{ "fixDot", scalar_fixDot, batch_fixDot },
	{ "fixCross", scalar_fixCross, batch_fixCross },
	{ "fixLength", scalar_fixLength, batch_fixLength },
	{ "fixNormalize", scalar_fixNormalize, batch_fixNormalize },
	{ "fixMulQuat", scalar_fixMulQuat, batch_fixMulQuat },
	{ "fixNormalizeQuat", scalar_fixNormalizeQuat, batch_fixNormalizeQuat },
	{ "fixRotateVector", scalar_fixRotateVector, batch_fixRotateVector },
	{ "fixInvRotateVector", scalar_fixInvRotateVector, batch_fixInvRotateVector },
	{ "fixNLerp", scalar_fixNLerp, batch_fixNLerp },
	{ "fixInvertMatrix", scalar_fixInvertMatrix, batch_fixInvertMatrix },
	{ "fixSolve3", scalar_fixSolve3, batch_fixSolve3 },
	{ "fixAABBTransform", scalar_fixAABBTransform, batch_fixAABBTransform },
	{ "fixQuantize", scalar_fixQuantize, batch_fixQuantize },
	{ "fixDequantize", scalar_fixDequantize, batch_fixDequantize },
	{ "fixInt128Div", scalar_fixInt128Div, batch_fixInt128Div },
	{ "fixUInt256DivMod", scalar_fixUInt256DivMod, batch_fixUInt256DivMod },
};
#define OP_COUNT ( sizeof( ops ) / sizeof( ops[0] ) )

static int cmp_double( const void* a, const void* b )
{
	double x = *( const double* )a, y = *( const double* )b;
	return ( x > y ) - ( x < y );
}

static double now_ns( void )
{
	struct timespec ts;
	clock_gettime( CLOCK_MONOTONIC, &ts );
	return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

int main( int argc, char** argv )
{
	int n = argc > 1 ? atoi( argv[1] ) : 5000000;
	int repeats = argc > 3 ? atoi( argv[3] ) : 7;
	if ( n < 1 )
		return 1;
	initialize( argc > 2 ? (uint64_t)strtoull( argv[2], NULL, 10 ) : 42 );

	printf( "op,mode,ns_min,ns_median,ns_max,checksum\n" );
	for ( size_t oi = 0; oi < OP_COUNT; ++oi )
	{
		if ( argc > 4 && strcmp( argv[4], ops[oi].name ) != 0 )
			continue;
		for ( int mode = 0; mode < 2; ++mode )
		{
			uint64_t ( *fn )( int ) = mode == 0 ? ops[oi].scalar : ops[oi].batch;
			sink = fn( n / 10 + 1 ); // warm up
			double times[16];
			uint64_t checksum = 0;
			for ( int r = 0; r < repeats && r < 16; ++r )
			{
				double t0 = now_ns();
				checksum = fn( n );
				double t1 = now_ns();
				times[r] = ( t1 - t0 ) / n;
			}
			sink = checksum;
			qsort( times, repeats, sizeof( times[0] ), cmp_double );
			printf( "%s,%s,%.3f,%.3f,%.3f,%llu\n", ops[oi].name, mode == 0 ? "SCALAR" : "BATCH",
				times[0], times[repeats / 2], times[repeats - 1], (unsigned long long)checksum );
		}
	}
	return 0;
}
