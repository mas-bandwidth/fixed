// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
//
// OracleHashStableTest: the unit tier of the bit-exact output oracle (benchmark/oracle.c).
//
// The full oracle (benchmark/oracle_check) hashes billions of inputs and belongs to the
// bench tier. This runs the SAME oracle code on the first 1,000 inputs of every operation's
// sequence -- the edge grid comes first in each, so the prefix is the hard cases -- and
// compares against hashes baked in below. No files, no clock, no randomness beyond the
// oracle's fixed seed.
//
// CMakeLists.txt builds it twice, against the native library and against the library forced
// onto the emulated 128-bit path (FIX_FORCE_EMULATED_INT128), and both must produce the
// values below: the emulation is held to bit-identity with native here too.
//
// A moved hash IS the failure. If a change is meant to move one, that is a semantic change,
// and the baseline (benchmark/oracle-baseline.txt) moves with it in the same pull request.
#include "../benchmark/oracle.c"

enum { PREFIX = 1000 };

typedef struct Expected
{
	const char* name;
	uint64_t hash;
} Expected;

// FROZEN 2026-10-14 from the tree at eb059e6 (sprint/fixed-perf), native and emulated alike.
static const Expected expected[] = {
	{ "fixMul", UINT64_C( 0x3668d88c9a4817e7 ) },
	{ "fixDiv", UINT64_C( 0xe2c8243bc7e9b016 ) },
	{ "fixSqrt", UINT64_C( 0x7115eb6d0ce217f0 ) },
	{ "fixISqrt128High", UINT64_C( 0x0bd8df11cb75ac02 ) },
	{ "fixComputeCosSin", UINT64_C( 0x36c086c3b8747e49 ) },
	{ "fixSin", UINT64_C( 0xf9b473a7efb19e8d ) },
	{ "fixCos", UINT64_C( 0xc24e0845b836c113 ) },
	{ "fixAtan2", UINT64_C( 0x4fb1693757c32b8d ) },
	{ "fixLog2", UINT64_C( 0x4eaa6344653da8ef ) },
	{ "fixExp2", UINT64_C( 0xd70f2c0ed68c0aaf ) },
	{ "fixPow", UINT64_C( 0x643913421d9e7e90 ) },
	{ "fixLerp", UINT64_C( 0x12c3073cc7eb1df3 ) },
	{ "fixDot", UINT64_C( 0xc0e6ae417b51e7e7 ) },
	{ "fixCross", UINT64_C( 0x66be276432fadece ) },
	{ "fixLength", UINT64_C( 0xdc9555d34758215b ) },
	{ "fixNormalize", UINT64_C( 0xd390eafa978846bf ) },
	{ "fixMulQuat", UINT64_C( 0x46529f80a213dd4c ) },
	{ "fixNormalizeQuat", UINT64_C( 0xbc25e01a79fdd63a ) },
	{ "fixRotateVector", UINT64_C( 0xe206759158d8de9d ) },
	{ "fixInvRotateVector", UINT64_C( 0x6b55339c3a3bb878 ) },
	{ "fixNLerp", UINT64_C( 0x5cb6d57f1fba7b3b ) },
	{ "fixInvertMatrix", UINT64_C( 0xf0bf14a8ba411a21 ) },
	{ "fixSolve3", UINT64_C( 0xefe724e6154948b4 ) },
	{ "fixAABB_Transform", UINT64_C( 0x98e11654f5602f53 ) },
	{ "fixQuantize", UINT64_C( 0xfd23848f39974afa ) },
	{ "fixDequantize", UINT64_C( 0x036804ee5feb0e47 ) },
	{ "fixInt128Div", UINT64_C( 0x537a4b6e78013bb3 ) },
	{ "fixUInt256DivMod", UINT64_C( 0x6239fb9a2ca7d7f8 ) },
};

int main( void )
{
	int failures = 0;
	printf( "OracleHashStableTest (%s arm): %d operations, prefix %d\n", FIX_INT128_EMULATED ? "emulated" : "native",
			FIX_ORACLE_OP_COUNT, PREFIX );
	for ( int op = 0; op < FIX_ORACLE_OP_COUNT; ++op )
	{
		fixOracle o;
		memset( &o, 0, sizeof( o ) );
		o.randomCount = ORACLE_DEFAULT_RANDOM;
		uint64_t got = fixOracleRun( op, &o, PREFIX );
		const Expected* want = NULL;
		for ( size_t i = 0; i < sizeof( expected ) / sizeof( expected[0] ); ++i )
			if ( strcmp( expected[i].name, fixOracleOps[op].name ) == 0 ) want = &expected[i];
		if ( o.index != PREFIX )
		{
			printf( "FAIL %s: the sequence has %llu inputs, fewer than the prefix\n", fixOracleOps[op].name,
					(unsigned long long)o.index );
			failures++;
		}
		else if ( !want || want->hash != got )
		{
			printf( "FAIL %s: prefix hash %016llx, expected %016llx\n", fixOracleOps[op].name, (unsigned long long)got,
					want ? (unsigned long long)want->hash : 0ULL );
			printf( "\t{ \"%s\", UINT64_C( 0x%016llx ) },\n", fixOracleOps[op].name, (unsigned long long)got );
			failures++;
		}
	}
	if ( failures )
	{
		printf( "OracleHashStableTest FAILED: %d of %d operations\n", failures, FIX_ORACLE_OP_COUNT );
		return 1;
	}
	printf( "OracleHashStableTest passed\n" );
	return 0;
}
