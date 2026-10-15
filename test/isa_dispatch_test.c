// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
// IsaDispatchTest: the runtime ISA dispatch (include/fixed/fixed_isa.h), in three parts.
//
//   PARSERS    the pure functions from raw words to facts, on FAKED cpuid / XCR0 / hwcap /
//              sysctl words: a CPU flag without the OS register state is not a fact, a leaf
//              the CPU does not report is not read, and each arm64 bit lands where it should.
//   RULES      selection and refusal: an unknown name, a reserved name this build does not
//              compile, and a variant whose facts are missing are each refused by name and
//              leave the table as it was; the best variant for a set of facts.
//   VARIANTS   every variant this build compiled and this host can run, forced in turn and
//              held to scalar's bits: the frozen 1,000-input batch oracle rows, a longer
//              oracle prefix against the scalar variant's own hash, and seeded lanes at every
//              tail length and an unaligned offset against fixMul and fixDot element by
//              element. Prints VARIANT <name> RAN, or SKIP and the reason.
//
// No clock, no files, no network: the seeded lane count is the argument (default 100
// million), and detection is exercised on faked words. The VARIANTS part reads the real host's
// facts, which is the point of it; a variant the host cannot run is a SKIP, not a pass.
#include "fixed/fixed_isa.h"
#include "fixed/fixed_vec.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The oracle engine, compiled into this executable (benchmark/oracle.c).
extern int fixOracleOpCount( void );
extern const char* fixOracleOpName( int opIndex );
extern uint64_t fixOracleHashOp( int opIndex, uint64_t baseRandom, uint64_t limit, uint64_t* segmentsOut,
								 uint64_t* countOut );

// Whether this build should carry the AVX-512 variant: the same condition src/fixed_isa.c
// uses, restated here so a change to one without the other fails this test.
#if ( defined( __x86_64__ ) || defined( _M_X64 ) ) && !defined( FIX_SATURATE ) && \
	( defined( __GNUC__ ) || defined( __clang__ ) || defined( _MSC_VER ) )
	#define EXPECT_AVX512_COMPILED 1
#else
	#define EXPECT_AVX512_COMPILED 0
#endif

static int fails = 0;

#define CHECK( cond )                                                            \
	do                                                                           \
	{                                                                            \
		if ( !( cond ) )                                                         \
		{                                                                        \
			printf( "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond );             \
			++fails;                                                             \
		}                                                                        \
	} while ( 0 )

// --- faked words ---
#define ECX_OSXSAVE ( 1u << 27 )
#define ECX_AVX ( 1u << 28 )
#define EBX_AVX2 ( 1u << 5 )
#define EBX_AVX512F ( 1u << 16 )
#define EBX_AVX512DQ ( 1u << 17 )
#define EBX_AVX512IFMA ( 1u << 21 )
#define EBX_AVX512VL ( 1u << 31 )
#define XCR0_YMM UINT64_C( 0x7 )
#define XCR0_ZMM UINT64_C( 0xe7 )

static const uint32_t zen4Ebx = EBX_AVX2 | EBX_AVX512F | EBX_AVX512DQ | EBX_AVX512IFMA | EBX_AVX512VL;
static const uint32_t allX86 = FIX_ISA_FACT_AVX2 | FIX_ISA_FACT_OS_YMM | FIX_ISA_FACT_AVX512F | FIX_ISA_FACT_AVX512DQ |
							   FIX_ISA_FACT_AVX512VL | FIX_ISA_FACT_AVX512IFMA | FIX_ISA_FACT_OS_ZMM;

static void IsaParserTest( void )
{
	// A Zen 4 / Zen 5 class host with an OS that saves zmm state: every x86 fact.
	CHECK( fixIsaParseX86( 0x10, ECX_OSXSAVE | ECX_AVX, zen4Ebx, XCR0_ZMM ) == allX86 );

	// The same CPU under an OS that saves only ymm state: the AVX-512 flags are facts about
	// the CPU, but there is no OS zmm state, so the avx512 variant must not be runnable.
	uint32_t noZmm = fixIsaParseX86( 0x10, ECX_OSXSAVE | ECX_AVX, zen4Ebx, XCR0_YMM );
	CHECK( ( noZmm & FIX_ISA_FACT_OS_ZMM ) == 0 );
	CHECK( ( noZmm & FIX_ISA_FACT_OS_YMM ) != 0 );
	uint32_t missing = 0;
	CHECK( fixIsaCheck( FIX_ISA_AVX512, noZmm, &missing ) ==
		   ( EXPECT_AVX512_COMPILED ? FIX_ISA_HOST_LACKS : FIX_ISA_NOT_COMPILED ) );
	if ( EXPECT_AVX512_COMPILED ) CHECK( missing == FIX_ISA_FACT_OS_ZMM );
	CHECK( fixIsaBest( noZmm ) == FIX_ISA_SCALAR );

	// No OSXSAVE: XCR0 is not to be trusted (the caller could not have read it), whatever it says.
	CHECK( ( fixIsaParseX86( 0x10, ECX_AVX, zen4Ebx, UINT64_C( 0xff ) ) & ( FIX_ISA_FACT_OS_YMM | FIX_ISA_FACT_OS_ZMM ) ) == 0 );
	// OSXSAVE but no AVX: no OS vector state facts either.
	CHECK( ( fixIsaParseX86( 0x10, ECX_OSXSAVE, zen4Ebx, XCR0_ZMM ) & ( FIX_ISA_FACT_OS_YMM | FIX_ISA_FACT_OS_ZMM ) ) == 0 );
	// XCR0 with zmm but no opmask bit is not zmm state.
	CHECK( ( fixIsaParseX86( 0x10, ECX_OSXSAVE | ECX_AVX, zen4Ebx, UINT64_C( 0xc7 ) ) & FIX_ISA_FACT_OS_ZMM ) == 0 );

	// A CPU whose highest leaf is below 7 has no leaf 7: its word is not read.
	CHECK( ( fixIsaParseX86( 6, ECX_OSXSAVE | ECX_AVX, 0xffffffffu, XCR0_ZMM ) &
			 ( FIX_ISA_FACT_AVX2 | FIX_ISA_FACT_AVX512F | FIX_ISA_FACT_AVX512DQ ) ) == 0 );

	// Haswell / Zen 1-3 class: AVX2 with ymm state, nothing AVX-512.
	uint32_t hsw = fixIsaParseX86( 0xd, ECX_OSXSAVE | ECX_AVX, EBX_AVX2, XCR0_YMM );
	CHECK( hsw == ( FIX_ISA_FACT_AVX2 | FIX_ISA_FACT_OS_YMM ) );
	CHECK( fixIsaBest( hsw ) == FIX_ISA_SCALAR ); // avx2 is reserved, not compiled

	// F without DQ (Knights Landing): no vpmullq, so not the avx512 variant.
	uint32_t knl = fixIsaParseX86( 0xd, ECX_OSXSAVE | ECX_AVX, EBX_AVX2 | EBX_AVX512F, XCR0_ZMM );
	CHECK( ( knl & FIX_ISA_FACT_AVX512DQ ) == 0 );
	CHECK( fixIsaBest( knl ) == FIX_ISA_SCALAR );

	// Each bit alone.
	CHECK( fixIsaParseX86( 7, 0, EBX_AVX512IFMA, 0 ) == FIX_ISA_FACT_AVX512IFMA );
	CHECK( fixIsaParseX86( 7, 0, EBX_AVX512VL, 0 ) == FIX_ISA_FACT_AVX512VL );
	CHECK( fixIsaParseX86( 0, 0, 0, 0 ) == 0 );

	// Linux arm64: HWCAP_ASIMD (1), HWCAP_SVE (22); HWCAP2_SVE2 (1), HWCAP2_SME (23), HWCAP2_SME2 (37).
	CHECK( fixIsaParseLinuxArm64( UINT64_C( 1 ) << 1, 0 ) == FIX_ISA_FACT_NEON );
	CHECK( fixIsaParseLinuxArm64( UINT64_C( 1 ) << 22, 0 ) == FIX_ISA_FACT_SVE );
	CHECK( fixIsaParseLinuxArm64( 0, UINT64_C( 1 ) << 1 ) == FIX_ISA_FACT_SVE2 );
	CHECK( fixIsaParseLinuxArm64( 0, UINT64_C( 1 ) << 23 ) == FIX_ISA_FACT_SME );
	CHECK( fixIsaParseLinuxArm64( 0, UINT64_C( 1 ) << 37 ) == FIX_ISA_FACT_SME2 );
	CHECK( fixIsaParseLinuxArm64( 0, 0 ) == 0 );
	// Neighbouring bits are not facts (HWCAP_FP is bit 0; HWCAP2_SME_I16I64 is bit 24).
	CHECK( fixIsaParseLinuxArm64( UINT64_C( 1 ), UINT64_C( 1 ) << 24 ) == 0 );

	// macOS arm64 sysctls.
	CHECK( fixIsaParseDarwinArm64( 1, 0, 0 ) == FIX_ISA_FACT_NEON );
	CHECK( fixIsaParseDarwinArm64( 1, 1, 1 ) == ( FIX_ISA_FACT_NEON | FIX_ISA_FACT_SME | FIX_ISA_FACT_SME2 ) );
	CHECK( fixIsaParseDarwinArm64( 0, 0, 0 ) == 0 );
	// Arm facts select nothing compiled today: neon and sme2 are reserved names.
	CHECK( fixIsaBest( fixIsaParseDarwinArm64( 1, 1, 1 ) ) == FIX_ISA_SCALAR );

	// Fact names round-trip and cover every bit.
	for ( int i = 0; i < FIX_ISA_FACT_COUNT; ++i ) CHECK( fixIsaFactName( 1u << i ) != NULL );
	CHECK( fixIsaFactName( 0 ) == NULL );
	CHECK( fixIsaFactName( 3 ) == NULL );
}

static void IsaRulesTest( void )
{
	const char* names[] = { "scalar", "avx2", "avx512", "avx512ifma", "neon", "sme2" };
	CHECK( sizeof names / sizeof names[0] == FIX_ISA_COUNT );
	for ( int i = 0; i < FIX_ISA_COUNT; ++i )
	{
		fixIsa isa = FIX_ISA_COUNT;
		CHECK( fixIsaFromName( names[i], &isa ) == 1 );
		CHECK( isa == ( fixIsa )i );
		CHECK( strcmp( fixIsaVariantName( ( fixIsa )i ), names[i] ) == 0 );
	}
	CHECK( fixIsaVariantName( FIX_ISA_COUNT ) == NULL );
	CHECK( fixIsaFromName( "AVX512", NULL ) == 0 ); // names are exact
	CHECK( fixIsaFromName( "", NULL ) == 0 );
	CHECK( fixIsaFromName( NULL, NULL ) == 0 );

	CHECK( fixIsaCompiled( FIX_ISA_SCALAR ) );
	CHECK( fixIsaCompiled( FIX_ISA_AVX512 ) == EXPECT_AVX512_COMPILED );
	CHECK( !fixIsaCompiled( FIX_ISA_AVX2 ) );
	CHECK( !fixIsaCompiled( FIX_ISA_AVX512IFMA ) );
	CHECK( !fixIsaCompiled( FIX_ISA_NEON ) );
	CHECK( !fixIsaCompiled( FIX_ISA_SME2 ) );

	// Scalar runs anywhere; the best variant for no facts is scalar.
	CHECK( fixIsaCheck( FIX_ISA_SCALAR, 0, NULL ) == FIX_ISA_OK );
	CHECK( fixIsaBest( 0 ) == FIX_ISA_SCALAR );
	CHECK( fixIsaBest( allX86 ) == ( EXPECT_AVX512_COMPILED ? FIX_ISA_AVX512 : FIX_ISA_SCALAR ) );
	CHECK( fixIsaCheck( FIX_ISA_COUNT, allX86, NULL ) == FIX_ISA_UNKNOWN_NAME );

	// REFUSED BY NAME, NEVER SILENTLY REPLACED: each refusal leaves the table as it was.
	fixIsa before = fixIsaSelected();
	CHECK( fixForceIsa( "bogus" ) == FIX_ISA_UNKNOWN_NAME );
	CHECK( fixIsaSelected() == before );
	CHECK( fixForceIsa( NULL ) == FIX_ISA_UNKNOWN_NAME );
	CHECK( fixIsaSelected() == before );
	const char* reserved[] = { "avx2", "avx512ifma", "neon", "sme2" };
	for ( size_t i = 0; i < sizeof reserved / sizeof reserved[0]; ++i )
	{
		CHECK( fixForceIsa( reserved[i] ) == FIX_ISA_NOT_COMPILED );
		CHECK( fixIsaSelected() == before );
	}
	if ( !EXPECT_AVX512_COMPILED )
	{
		CHECK( fixForceIsa( "avx512" ) == FIX_ISA_NOT_COMPILED );
		CHECK( fixIsaSelected() == before );
	}
	else if ( fixIsaCheck( FIX_ISA_AVX512, fixIsaHostFacts(), NULL ) != FIX_ISA_OK )
	{
		CHECK( fixForceIsa( "avx512" ) == FIX_ISA_HOST_LACKS );
		CHECK( fixIsaSelected() == before );
	}

	// The default selection is the best for the detected facts (FIXED_ISA unset), and the
	// name matches the selection.
	if ( getenv( "FIXED_ISA" ) == NULL ) CHECK( fixIsaSelected() == fixIsaBest( fixIsaHostFacts() ) );
	CHECK( strcmp( fixIsaName(), fixIsaVariantName( fixIsaSelected() ) ) == 0 );
	// Forcing scalar always succeeds and is undone by forcing the default back.
	CHECK( fixForceIsa( "scalar" ) == FIX_ISA_OK );
	CHECK( fixIsaSelected() == FIX_ISA_SCALAR );
	CHECK( fixForceIsa( fixIsaVariantName( before ) ) == FIX_ISA_OK );
	CHECK( fixIsaSelected() == before );
}

// --- the variants against scalar ---

// FROZEN: test/oracle_unit_test.c's values for the batch rows (the 1,000-input prefix).
#define ORACLE_UNIT_MUL_BATCH UINT64_C( 0xf74d919d55ad89c8 )
#define ORACLE_UNIT_DOT_BATCH3 UINT64_C( 0xa967efd82587b494 )
#define ORACLE_LONG_PREFIX UINT64_C( 200000 )

static int oracleIndex( const char* name )
{
	for ( int i = 0; i < fixOracleOpCount(); ++i )
		if ( strcmp( fixOracleOpName( i ), name ) == 0 ) return i;
	return -1;
}

enum { CAP = 4096 + 1 };
static fixed_t lane[6][CAP], gotMul[CAP], gotDot[CAP];
static uint64_t rngState;

static uint64_t nextRand( void )
{
	uint64_t x = rngState;
	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	return rngState = x;
}

// n lanes starting one element past each array's start, so no pointer is vector aligned.
static int lanesMatch( size_t n )
{
	for ( size_t i = 1; i <= n; ++i )
		for ( int j = 0; j < 6; ++j ) lane[j][i] = ( fixed_t )nextRand();
	fixMulBatch( gotMul + 1, lane[0] + 1, lane[1] + 1, n );
	fixDotBatch3( gotDot + 1, lane[0] + 1, lane[1] + 1, lane[2] + 1, lane[3] + 1, lane[4] + 1, lane[5] + 1, n );
	for ( size_t i = 1; i <= n; ++i )
	{
		fixVec3 a = { lane[0][i], lane[1][i], lane[2][i] }, b = { lane[3][i], lane[4][i], lane[5][i] };
		if ( gotMul[i] != fixMul( lane[0][i], lane[1][i] ) || gotDot[i] != fixDot( a, b ) )
		{
			printf( "FAIL %s:%d: variant %s lane %zu of %zu: a=%lld b=%lld mul=%lld want %lld; dot=%lld want %lld\n",
					__FILE__, __LINE__, fixIsaName(), i - 1, n, ( long long )lane[0][i], ( long long )lane[1][i],
					( long long )gotMul[i], ( long long )fixMul( lane[0][i], lane[1][i] ), ( long long )gotDot[i],
					( long long )fixDot( a, b ) );
			return 0;
		}
	}
	return 1;
}

static void IsaVariantsTest( uint64_t lanes )
{
	const fixIsa before = fixIsaSelected();
	const uint32_t facts = fixIsaHostFacts();
	const int mulRow = oracleIndex( "fixMulBatch" ), dotRow = oracleIndex( "fixDotBatch3" );
	CHECK( mulRow >= 0 && dotRow >= 0 );
	if ( mulRow < 0 || dotRow < 0 ) return;

	printf( "host facts:" );
	for ( int i = 0; i < FIX_ISA_FACT_COUNT; ++i )
		if ( facts & ( 1u << i ) ) printf( " %s", fixIsaFactName( 1u << i ) );
	printf( "\ndefault variant: %s\n", fixIsaName() );

	// The scalar variant's own long-prefix hashes are the reference for every other variant.
	CHECK( fixForceIsa( "scalar" ) == FIX_ISA_OK );
	const uint64_t refMul = fixOracleHashOp( mulRow, 1000000, ORACLE_LONG_PREFIX, NULL, NULL );
	const uint64_t refDot = fixOracleHashOp( dotRow, 1000000, ORACLE_LONG_PREFIX, NULL, NULL );

	for ( int v = 0; v < FIX_ISA_COUNT; ++v )
	{
		const char* name = fixIsaVariantName( ( fixIsa )v );
		uint32_t missing = 0;
		fixIsaResult ok = fixIsaCheck( ( fixIsa )v, facts, &missing );
		if ( ok == FIX_ISA_NOT_COMPILED )
		{
			printf( "VARIANT %s SKIP not compiled into this build\n", name );
			continue;
		}
		if ( ok == FIX_ISA_HOST_LACKS )
		{
			printf( "VARIANT %s SKIP host lacks", name );
			for ( int i = 0; i < FIX_ISA_FACT_COUNT; ++i )
				if ( missing & ( 1u << i ) ) printf( " %s", fixIsaFactName( 1u << i ) );
			printf( "\n" );
			continue;
		}
		CHECK( fixForceIsa( name ) == FIX_ISA_OK );
		CHECK( strcmp( fixIsaName(), name ) == 0 );
		const int before_fails = fails;

#if !defined( FIX_SATURATE )
		// The frozen rows hold only for the wrapping arithmetic; a saturated build checks
		// against its own scalar variant below instead.
		CHECK( fixOracleHashOp( mulRow, 1000000, 1000, NULL, NULL ) == ORACLE_UNIT_MUL_BATCH );
		CHECK( fixOracleHashOp( dotRow, 1000000, 1000, NULL, NULL ) == ORACLE_UNIT_DOT_BATCH3 );
#endif
		CHECK( fixOracleHashOp( mulRow, 1000000, ORACLE_LONG_PREFIX, NULL, NULL ) == refMul );
		CHECK( fixOracleHashOp( dotRow, 1000000, ORACLE_LONG_PREFIX, NULL, NULL ) == refDot );

		// Every tail length around the vector widths, then full batches up to the lane count.
		rngState = UINT64_C( 0x243f6a8885a308d3 ) ^ ( uint64_t )v;
		uint64_t done = 0;
		for ( size_t n = 0; n <= 33 && fails == before_fails; ++n )
		{
			if ( !lanesMatch( n ) ) ++fails;
			done += n;
		}
		while ( done < lanes && fails == before_fails )
		{
			size_t n = lanes - done < CAP - 1 ? ( size_t )( lanes - done ) : ( size_t )( CAP - 1 );
			if ( !lanesMatch( n ) ) ++fails;
			done += n;
		}
		if ( fails == before_fails )
			printf( "VARIANT %s RAN: batch oracle rows and %llu seeded lanes bit-identical to scalar\n", name,
					( unsigned long long )done );
		else
			printf( "VARIANT %s RAN: FAILED\n", name );
	}
	CHECK( fixForceIsa( fixIsaVariantName( before ) ) == FIX_ISA_OK );
}

int main( int argc, char** argv )
{
	const uint64_t lanes = argc > 1 ? strtoull( argv[1], NULL, 10 ) : UINT64_C( 100000000 );
	printf( "SUBTEST IsaParserTest\n" );
	IsaParserTest();
	printf( "SUBTEST IsaRulesTest\n" );
	IsaRulesTest();
	printf( "SUBTEST IsaVariantsTest\n" );
	IsaVariantsTest( lanes );
	if ( fails )
	{
		printf( "IsaDispatchTest FAIL: %d check(s)\n", fails );
		return 1;
	}
	printf( "IsaDispatchTest PASS\n" );
	return 0;
}
