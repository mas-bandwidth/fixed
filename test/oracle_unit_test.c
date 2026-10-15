// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
// OracleHashStableTest: the output oracle's hashes, baked into the test.
//
// This is the unit tier of the oracle. It runs benchmark/oracle.c over a 1,000-input PREFIX
// of every operation and compares against hashes baked in below. NO FILES and NO CLOCK: the
// prefix is what makes that possible, because the full input set (exhaustive square roots,
// 800k angles, millions of seeded draws) is far too large to freeze inside a test. The full
// set is what benchmark/oracle-baseline.txt freezes, and oracle_check is its runner.
//
// The same hashes are asserted on the EMULATED 128-bit arm: CMake builds this file a second
// time against fixed_emulated (FIX_FORCE_EMULATED_INT128) and the baked values are the same
// numbers, so the emulation is held to bit-identity with native __int128 by this test on
// every platform, not only on the one that lacks a native 128-bit type.
//
// Set FIX_ORACLE_PRINT_HASHES=1 to print the values a build actually produced, which is how
// the table below was filled in and how a moved hash is diagnosed. A moved hash IS the
// failure: do not update this table to make a change pass.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The engine lives in benchmark/oracle.c and is compiled into this executable.
extern int fixOracleOpCount( void );
extern const char* fixOracleOpName( int opIndex );
extern uint64_t fixOracleHashOp( int opIndex, uint64_t baseRandom, uint64_t limit, uint64_t* segmentsOut,
								 uint64_t* countOut );

// The prefix length. Every operation is hashed over exactly this many inputs, taken from
// the head of the same stream oracle_check walks.
#define ORACLE_UNIT_PREFIX UINT64_C( 1000 )

// The base random count is irrelevant at this prefix length (the prefix ends inside the
// structured passes for every operation), and is named so the call is explicit.
#define ORACLE_UNIT_BASE_RANDOM UINT64_C( 1000000 )

// FROZEN. Regenerating these is a change to the library's output bits and is the failure
// this test exists to report, not a maintenance step.
static const struct
{
	const char* name;
	uint64_t hash;
} oracleUnitExpected[] = {
	{ "fixMul", UINT64_C( 0x4215fad262c9d25c ) },
	{ "fixDiv_below2p47", UINT64_C( 0x87c44c24dcc09897 ) },
	{ "fixDiv_above2p47", UINT64_C( 0xed6034e68686a223 ) },
	{ "fixSqrt_small", UINT64_C( 0xacd9bb5067a51d63 ) },
	{ "fixSqrt_raw_below2p32", UINT64_C( 0x3f17e041a57b76b6 ) },
	{ "fixISqrt128High_u64", UINT64_C( 0xf421984066d8f206 ) },
	{ "fixISqrt128High_above104bits", UINT64_C( 0x40fcfc2d5c540eed ) },
	{ "fixComputeCosSin", UINT64_C( 0x9376522a58b5da8a ) },
	{ "fixSin", UINT64_C( 0x58bb706c902a5487 ) },
	{ "fixCos", UINT64_C( 0x6215ad9e72313a1e ) },
	{ "fixAtan2", UINT64_C( 0xc43e91b9f73c8a29 ) },
	{ "fixLog2", UINT64_C( 0x1e859de9247dacc3 ) },
	{ "fixExp2", UINT64_C( 0x75c143947c4df5a5 ) },
	{ "fixPow", UINT64_C( 0x0c1d8b7c123f574f ) },
	{ "fixLerp", UINT64_C( 0xd626f02c33447cf2 ) },
	{ "fixInt128Div", UINT64_C( 0xe6d559e99b32a54f ) },
	{ "fixUInt256DivMod", UINT64_C( 0x846059e03fd1e63f ) },
	{ "fixDot", UINT64_C( 0x3d8abd7754df6b11 ) },
	{ "fixCross", UINT64_C( 0x6be11318ed8c21d4 ) },
	{ "fixLength", UINT64_C( 0xdce5836bd1a68803 ) },
	{ "fixNormalize", UINT64_C( 0x4da171956d0d01d7 ) },
	{ "fixMulQuat", UINT64_C( 0x0e44f5712fb7def5 ) },
	{ "fixNormalizeQuat", UINT64_C( 0x63818c49cf0dcf27 ) },
	{ "fixRotateVector", UINT64_C( 0xe4e0938b12d4dde9 ) },
	{ "fixInvRotateVector", UINT64_C( 0x1d5c0f5e3515cc7e ) },
	{ "fixNLerp", UINT64_C( 0x0eccb9ee34fab9fe ) },
	{ "fixInvertMatrix", UINT64_C( 0xc8d6712e40b722f5 ) },
	{ "fixSolve3", UINT64_C( 0x7b53c2a526c7fa4e ) },
	{ "fixAABB_Transform", UINT64_C( 0xe49b93d186894fd4 ) },
	{ "fixQuantize", UINT64_C( 0x22e364f7b025cf17 ) },
	{ "fixDequantize", UINT64_C( 0x99e81db2278ac2fd ) },
	{ "fixMulBatch", UINT64_C( 0xf74d919d55ad89c8 ) },
	{ "fixDotBatch3", UINT64_C( 0xa967efd82587b494 ) },
};

static int fails = 0;

// A subtest in this suite, registered the way the suites in this tree register their checks:
// a named function whose failures are counted, printed with the line that failed.
#define RUN_SUBTEST( fn )                                                       \
	do                                                                          \
	{                                                                           \
		printf( "SUBTEST %s\n", #fn );                                          \
		fn();                                                                   \
	} while ( 0 )

static void OracleHashStableTest( void )
{
	const int print = getenv( "FIX_ORACLE_PRINT_HASHES" ) != NULL;
	const int expected = ( int )( sizeof oracleUnitExpected / sizeof oracleUnitExpected[0] );
	const int ops = fixOracleOpCount();

	if ( ops != expected )
	{
		printf( "FAIL %s:%d: the oracle has %d operations, this test freezes %d\n", __FILE__, __LINE__, ops, expected );
		++fails;
	}

	for ( int i = 0; i < ops; ++i )
	{
		const char* name = fixOracleOpName( i );
		uint64_t count = 0;
		uint64_t hash = fixOracleHashOp( i, ORACLE_UNIT_BASE_RANDOM, ORACLE_UNIT_PREFIX, NULL, &count );
		if ( print )
		{
			printf( "HASH %s 0x%016llxULL count=%llu\n", name, ( unsigned long long )hash, ( unsigned long long )count );
		}
		if ( count != ORACLE_UNIT_PREFIX )
		{
			printf( "FAIL %s:%d: %s hashed %llu inputs, expected %llu\n", __FILE__, __LINE__, name,
					( unsigned long long )count, ( unsigned long long )ORACLE_UNIT_PREFIX );
			++fails;
			continue;
		}
		uint64_t want = 0;
		int found = 0;
		for ( int e = 0; e < expected; ++e )
		{
			if ( strcmp( oracleUnitExpected[e].name, name ) == 0 )
			{
				want = oracleUnitExpected[e].hash;
				found = 1;
				break;
			}
		}
		if ( !found )
		{
			printf( "FAIL %s:%d: %s has no frozen hash in this test\n", __FILE__, __LINE__, name );
			++fails;
			continue;
		}
		if ( hash != want )
		{
			printf( "FAIL %s:%d: OracleHashStableTest %s = 0x%016llx, frozen 0x%016llx "
					"(prefix %llu, emulated arm and native arm must agree)\n",
					__FILE__, __LINE__, name, ( unsigned long long )hash, ( unsigned long long )want,
					( unsigned long long )ORACLE_UNIT_PREFIX );
			++fails;
		}
	}
}

int main( void )
{
	RUN_SUBTEST( OracleHashStableTest );
	if ( fails == 0 )
	{
		printf( "oracle_unit: %d operations, prefix %llu, hashes stable\n", fixOracleOpCount(),
				( unsigned long long )ORACLE_UNIT_PREFIX );
	}
	return fails == 0 ? 0 : 1;
}
