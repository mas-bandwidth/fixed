// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
//
// UNSIGNED NEGATION, AT THE EDGES.
//
// The library takes the magnitude of a signed value as (uint64_t)0 - (uint64_t)x, and
// negates its 128-bit unsigned values as (fixUInt128)0 - a, never as unary minus: MSVC warns
// C4146 on unary minus of an unsigned value. C defines both spellings as 2^N - x, so no
// result moves, and this file holds that at the inputs where a magnitude goes wrong first:
// INT64_MIN, INT64_MAX and their neighbours.
//
// Three checks:
//
//   1. THE MAGNITUDE against an oracle with no unsigned negation in it: for x < 0 it is
//      (uint64_t)-( x + 1 ) + 1, where the signed negation is defined at every input.
//
//   2. HAND VECTORS through every function that takes such a magnitude or negates a
//      128-bit value: fixNormalize, fixNormalizeQuat, fixNormalizeComponent30, fixInt128Div,
//      fixInt128Neg and fixUInt128Neg, each with the answer worked out by hand.
//
//   3. FROZEN HASHES over the cross product of the edge values, recorded from the unary
//      minus spelling before it was replaced. Every arm (native, emulated, every OS) must
//      reproduce them.

#include "fixed/fixed.h"
#include "fixed/fixed_math.h"
#include "fixed/fixed_vec.h"

#include <stdio.h>

static int failures = 0;

#define CHECK( cond )                                                                                                  \
	do                                                                                                                 \
	{                                                                                                                  \
		if ( !( cond ) )                                                                                               \
		{                                                                                                              \
			printf( "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond );                                                   \
			failures++;                                                                                                \
		}                                                                                                              \
	} while ( 0 )

static const int64_t edges[] = {
	INT64_MIN, INT64_MIN + 1, INT64_MIN + 2, -( INT64_C( 1 ) << 32 ), -65536, -3, -1, 0, 1, 3, 65536,
	INT64_C( 1 ) << 32, INT64_MAX - 1, INT64_MAX,
};
#define EDGE_COUNT ( (int)( sizeof( edges ) / sizeof( edges[0] ) ) )

static uint64_t hashStep( uint64_t hash, uint64_t value )
{
	for ( int i = 0; i < 8; i++ )
	{
		hash ^= ( value >> ( 8 * i ) ) & 0xff;
		hash *= UINT64_C( 0x100000001b3 );
	}
	return hash;
}

#define HASH_SEED UINT64_C( 0xcbf29ce484222325 )

static void checkHash( const char* name, uint64_t got, uint64_t expected )
{
	if ( got != expected )
	{
		printf( "FAIL %s hash: got 0x%016llx, expected 0x%016llx\n", name, (unsigned long long)got,
				(unsigned long long)expected );
		failures++;
	}
}

static uint64_t magnitudeOracle( int64_t x )
{
	return x < 0 ? (uint64_t)( -( x + 1 ) ) + 1 : (uint64_t)x;
}

static void testMagnitude( void )
{
	for ( int i = 0; i < EDGE_COUNT; i++ )
	{
		int64_t x = edges[i];
		uint64_t magnitude = x < 0 ? (uint64_t)0 - (uint64_t)x : (uint64_t)x;
		CHECK( magnitude == magnitudeOracle( x ) );
	}
	CHECK( magnitudeOracle( INT64_MIN ) == UINT64_C( 1 ) << 63 );
	CHECK( magnitudeOracle( INT64_MIN + 1 ) == ( UINT64_C( 1 ) << 63 ) - 1 );
}

static void testHandVectors( void )
{
	fixVec3 v = fixNormalize( FIX_LITERAL( fixVec3 ){ INT64_MIN, 0, 0 } );
	CHECK( v.x == -FIX_ONE && v.y == 0 && v.z == 0 );
	v = fixNormalize( FIX_LITERAL( fixVec3 ){ INT64_MAX, 0, 0 } );
	CHECK( v.x == FIX_ONE && v.y == 0 && v.z == 0 );
	v = fixNormalize( FIX_LITERAL( fixVec3 ){ 0, -1, 0 } );
	CHECK( v.x == 0 && v.y == -FIX_ONE && v.z == 0 );
	v = fixNormalize( FIX_LITERAL( fixVec3 ){ 0, 0, INT64_MIN + 1 } );
	CHECK( v.x == 0 && v.y == 0 && v.z == -FIX_ONE );

	fixQuat q = fixNormalizeQuat( FIX_LITERAL( fixQuat ){ { -1, 0, 0 }, 0 } );
	CHECK( q.v.x == -FIX_ONE && q.v.y == 0 && q.v.z == 0 && q.s == 0 );
	q = fixNormalizeQuat( FIX_LITERAL( fixQuat ){ { 0, 0, 0 }, -3 } );
	CHECK( q.v.x == 0 && q.v.y == 0 && q.v.z == 0 && q.s == -FIX_ONE );
	q = fixNormalizeQuat( FIX_LITERAL( fixQuat ){ { 0, INT64_MIN, 0 }, 0 } );
	CHECK( q.v.x == 0 && q.v.y == -FIX_ONE && q.v.z == 0 && q.s == 0 );

	uint64_t half = UINT64_C( 1 ) << 63;
	CHECK( fixNormalizeComponent30( INT64_MIN, half ) == -FIX30_ONE );
	CHECK( fixNormalizeComponent30( INT64_MAX, half ) == FIX30_ONE );
	CHECK( fixNormalizeComponent30( -1, 1 ) == -FIX30_ONE );
	CHECK( fixNormalizeComponent30( -1, 2 ) == -( FIX30_ONE / 2 ) );

	fixInt128 int128Min = fixInt128Make( half, 0 );
	CHECK( fixInt128Eq( fixInt128Neg( fixInt128FromI64( INT64_MIN ) ), fixInt128Make( 0, half ) ) );
	CHECK( fixInt128Eq( fixInt128Neg( int128Min ), int128Min ) );
	CHECK( fixInt128Eq( fixInt128Neg( fixInt128FromI64( 1 ) ), fixInt128FromI64( -1 ) ) );
	CHECK( fixUInt128Eq( fixUInt128Neg( fixUInt128FromU64( 1 ) ), fixUInt128Make( UINT64_MAX, UINT64_MAX ) ) );
	CHECK( fixUInt128Eq( fixUInt128Neg( fixUInt128FromU64( 0 ) ), fixUInt128FromU64( 0 ) ) );
	CHECK( fixUInt128Eq( fixUInt128Neg( fixUInt128Make( half, 0 ) ), fixUInt128Make( half, 0 ) ) );

	CHECK( fixInt128Eq( fixInt128Div( fixInt128FromI64( INT64_MIN ), fixInt128FromI64( -1 ) ), fixInt128Make( 0, half ) ) );
	CHECK( fixInt128Eq( fixInt128Div( int128Min, fixInt128FromI64( 1 ) ), int128Min ) );
	CHECK( fixInt128Eq( fixInt128Div( int128Min, fixInt128FromI64( 2 ) ), fixInt128Make( UINT64_C( 0xc000000000000000 ), 0 ) ) );
	CHECK( fixInt128Eq( fixInt128Div( int128Min, int128Min ), fixInt128FromI64( 1 ) ) );
	CHECK( fixInt128Eq( fixInt128Div( fixInt128FromI64( -7 ), fixInt128FromI64( 2 ) ), fixInt128FromI64( -3 ) ) );
	CHECK( fixInt128Eq( fixInt128Div( fixInt128FromI64( 7 ), fixInt128FromI64( -2 ) ), fixInt128FromI64( -3 ) ) );
}

static void testFrozenHashes( void )
{
	uint64_t hash = HASH_SEED;
	for ( int i = 0; i < EDGE_COUNT; i++ )
	{
		for ( int j = 0; j < EDGE_COUNT; j++ )
		{
			for ( int k = 0; k < EDGE_COUNT; k++ )
			{
				fixed_t length = 0;
				fixVec3 v = fixGetLengthAndNormalize( &length, FIX_LITERAL( fixVec3 ){ edges[i], edges[j], edges[k] } );
				hash = hashStep( hash, (uint64_t)v.x );
				hash = hashStep( hash, (uint64_t)v.y );
				hash = hashStep( hash, (uint64_t)v.z );
				hash = hashStep( hash, (uint64_t)length );
				v = fixNormalize( FIX_LITERAL( fixVec3 ){ edges[i], edges[j], edges[k] } );
				hash = hashStep( hash, (uint64_t)v.x );
				hash = hashStep( hash, (uint64_t)v.y );
				hash = hashStep( hash, (uint64_t)v.z );
			}
		}
	}
	checkHash( "fixNormalize", hash, UINT64_C( 0x0a86a8d48a6f8539 ) );

	hash = HASH_SEED;
	for ( int i = 0; i < EDGE_COUNT; i++ )
	{
		for ( int j = 0; j < EDGE_COUNT; j++ )
		{
			for ( int k = 0; k < EDGE_COUNT; k++ )
			{
				for ( int l = 0; l < EDGE_COUNT; l++ )
				{
					fixQuat q = fixNormalizeQuat( FIX_LITERAL( fixQuat ){ { edges[i], edges[j], edges[k] }, edges[l] } );
					hash = hashStep( hash, (uint64_t)q.v.x );
					hash = hashStep( hash, (uint64_t)q.v.y );
					hash = hashStep( hash, (uint64_t)q.v.z );
					hash = hashStep( hash, (uint64_t)q.s );
				}
			}
		}
	}
	checkHash( "fixNormalizeQuat", hash, UINT64_C( 0xa8281d2d77359cc1 ) );

	static const uint64_t lengths[] = { 1, 2, 3, 65536, UINT64_C( 1 ) << 32, UINT64_C( 1 ) << 63, UINT64_MAX };
	hash = HASH_SEED;
	for ( int i = 0; i < EDGE_COUNT; i++ )
	{
		for ( int j = 0; j < (int)( sizeof( lengths ) / sizeof( lengths[0] ) ); j++ )
		{
			hash = hashStep( hash, (uint64_t)(int64_t)fixNormalizeComponent30( edges[i], lengths[j] ) );
		}
	}
	checkHash( "fixNormalizeComponent30", hash, UINT64_C( 0x0e70e9ba5ed13e8a ) );

	// 128-bit operands: each edge as the high lane over a low lane of 0, 1 or all ones, and
	// each edge sign-extended. INT128_MIN / -1 overflows and division by zero is guarded by
	// every caller, so both are skipped.
	fixInt128 wide[EDGE_COUNT * 4];
	int wideCount = 0;
	for ( int i = 0; i < EDGE_COUNT; i++ )
	{
		wide[wideCount++] = fixInt128FromI64( edges[i] );
		wide[wideCount++] = fixInt128Make( (uint64_t)edges[i], 0 );
		wide[wideCount++] = fixInt128Make( (uint64_t)edges[i], 1 );
		wide[wideCount++] = fixInt128Make( (uint64_t)edges[i], UINT64_MAX );
	}
	fixInt128 int128Min = fixInt128Make( UINT64_C( 1 ) << 63, 0 );
	hash = HASH_SEED;
	for ( int i = 0; i < wideCount; i++ )
	{
		fixInt128 n = fixInt128Neg( wide[i] );
		hash = hashStep( hash, fixInt128Hi( n ) );
		hash = hashStep( hash, fixInt128Lo( n ) );
		fixUInt128 un = fixUInt128Neg( fixInt128ToUnsigned( wide[i] ) );
		hash = hashStep( hash, fixUInt128Hi( un ) );
		hash = hashStep( hash, fixUInt128Lo( un ) );
		for ( int j = 0; j < wideCount; j++ )
		{
			if ( fixInt128Eq( wide[j], FIX_INT128_ZERO ) )
			{
				continue;
			}
			if ( fixInt128Eq( wide[i], int128Min ) && fixInt128Eq( wide[j], fixInt128FromI64( -1 ) ) )
			{
				continue;
			}
			fixInt128 d = fixInt128Div( wide[i], wide[j] );
			hash = hashStep( hash, fixInt128Hi( d ) );
			hash = hashStep( hash, fixInt128Lo( d ) );
		}
	}
	checkHash( "fixInt128Div", hash, UINT64_C( 0x977ed5c151386789 ) );
}

int main( void )
{
	testMagnitude();
	testHandVectors();
	testFrozenHashes();
	if ( failures != 0 )
	{
		printf( "unsigned negation test FAILED: %d\n", failures );
		return 1;
	}
	printf( "unsigned negation test passed\n" );
	return 0;
}
