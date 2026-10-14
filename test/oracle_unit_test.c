// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
// OracleHashStableTest: runs the output oracle on a 1,000-input prefix of every public
// hot operation and compares against hashes baked into this file (no files, no clock).
// Built twice — once against `fixed`, once against `fixed_emulated` — so the emulated
// 128-bit arm is asserted to produce the SAME values as the native arm, on every
// platform, by both builds agreeing with one frozen table.
#define ORACLE_LIB
#include "../benchmark/oracle.c"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define ORACLE_UNIT_COUNT 1000u

// The frozen 1,000-input-prefix hashes, one per operation in oracle_op_names order.
// Regenerate with: ./build/fixed_oracle 1000 1000  (after copying the hex here).
static const struct
{
	const char* name;
	uint64_t hash;
} oracle_unit_hashes[] = {
	{ "fixMul", UINT64_C( 0x309bdde8ca10f2ce ) },
	{ "fixDivBelow", UINT64_C( 0xd023355681dafd56 ) },
	{ "fixDivAbove", UINT64_C( 0x874e5caecbd00997 ) },
	{ "fixSqrtSmall", UINT64_C( 0xac57944b61b9935a ) },
	{ "fixSqrtU64", UINT64_C( 0x7d18f34cb0add3cc ) },
	{ "fixSqrtWide", UINT64_C( 0x0a8a2d9d48f01d12 ) },
	{ "fixISqrt128High", UINT64_C( 0x70c7ed30bf58258f ) },
	{ "fixComputeCosSin", UINT64_C( 0xc527273f343a0c83 ) },
	{ "fixSin", UINT64_C( 0x59c90d1fbdb69079 ) },
	{ "fixCos", UINT64_C( 0x40e8a554f3842c7e ) },
	{ "fixAtan2", UINT64_C( 0x21ea2f808ea1b49f ) },
	{ "fixLog2", UINT64_C( 0x047cd24656f9b412 ) },
	{ "fixExp2", UINT64_C( 0x5856f8e0ce3e1e90 ) },
	{ "fixPow", UINT64_C( 0x2e03bcc41e0104ca ) },
	{ "fixLerp", UINT64_C( 0xdfad904fa95ca2f6 ) },
	{ "fixDot", UINT64_C( 0x83d356435f979031 ) },
	{ "fixCross", UINT64_C( 0x890d92e25ccc24e5 ) },
	{ "fixLength", UINT64_C( 0x011d1e1c26e9f0c6 ) },
	{ "fixNormalize", UINT64_C( 0xb23085c0fc487aff ) },
	{ "fixMulQuat", UINT64_C( 0xd18ae46bb45bdb11 ) },
	{ "fixNormalizeQuat", UINT64_C( 0x22b7cb8a11770bc6 ) },
	{ "fixRotateVector", UINT64_C( 0x1560bf3437bcb858 ) },
	{ "fixInvRotateVector", UINT64_C( 0x9f49eac17af1822c ) },
	{ "fixNLerp", UINT64_C( 0x0867b9f5782624e1 ) },
	{ "fixInvertMatrix", UINT64_C( 0x06d61b65959b1a87 ) },
	{ "fixSolve3", UINT64_C( 0xfc27cc2541d57480 ) },
	{ "fixAABBTransform", UINT64_C( 0x2978a5002b63be3c ) },
	{ "fixQuantize", UINT64_C( 0xd4a4d9c45c1cd0d9 ) },
	{ "fixDequantize", UINT64_C( 0xb103a67242ae5d36 ) },
	{ "fixInt128Div", UINT64_C( 0x50184429b86d10c7 ) },
	{ "fixUInt256DivMod", UINT64_C( 0x5b65f49f7df434f0 ) },
};

static int failures;

#define CHECK( c )                                                                                                       \
	do                                                                                                                   \
	{                                                                                                                    \
		if ( !( c ) )                                                                                                    \
		{                                                                                                                \
			if ( failures < 30 )                                                                                         \
				printf( "FAIL line %d: %s\n", __LINE__, #c );                                                            \
			failures++;                                                                                                  \
		}                                                                                                                \
	} while ( 0 )

static void oracle_hash_stable( void )
{
	// The baked table must cover every operation, in order.
	CHECK( sizeof( oracle_unit_hashes ) / sizeof( oracle_unit_hashes[0] ) == ORACLE_OP_COUNT );
	for ( size_t i = 0; i < ORACLE_OP_COUNT; ++i )
	{
		CHECK( strcmp( oracle_unit_hashes[i].name, oracle_op_names[i] ) == 0 );
		uint64_t got = oracle_hash_one( oracle_op_names[i], ORACLE_UNIT_COUNT, ORACLE_UNIT_COUNT );
		if ( got != oracle_unit_hashes[i].hash )
		{
			if ( failures < 30 )
				printf( "FAIL %s: expected %016" PRIx64 " got %016" PRIx64 "\n", oracle_op_names[i],
					oracle_unit_hashes[i].hash, got );
			failures++;
		}
	}
}

int main( int argc, char** argv )
{
	struct
	{
		const char* name;
		void ( *run )( void );
	} tests[] = {
		{ "OracleHashStable", oracle_hash_stable },
	};
	for ( size_t i = 0; i < sizeof( tests ) / sizeof( tests[0] ); ++i )
	{
		if ( argc > 1 && strcmp( argv[1], tests[i].name ) != 0 )
			continue;
		int before = failures;
		tests[i].run();
		printf( "%s: %d failures\n", tests[i].name, failures - before );
	}
	return failures ? 1 : 0;
}
