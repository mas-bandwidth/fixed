// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
// Runtime ISA dispatch: detection, the kernel table, the override, and the scalar kernels.
// The design and the contract are in include/fixed/fixed_isa.h; why there is no state
// machine to model is in USAGE.md, "Runtime ISA dispatch".
//
// THIS FILE IS BASELINE CODE. It is compiled with the library's ordinary flags and must
// never contain an instruction the baseline CPU lacks: the variant kernels live in their own
// translation units (src/fixed_isa_avx512.c) behind per-function target attributes, and are
// reached from here only through the table, after detection has said the host can run them.
#include "fixed/fixed_isa.h"
#include "fixed/fixed_vec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if ( defined( __x86_64__ ) || defined( _M_X64 ) ) && !defined( FIX_SATURATE ) && \
	( defined( __GNUC__ ) || defined( __clang__ ) || defined( _MSC_VER ) )
	#define FIX_ISA_HAVE_AVX512 1
#else
	#define FIX_ISA_HAVE_AVX512 0
#endif

#if defined( __x86_64__ ) || defined( _M_X64 ) || defined( __i386__ ) || defined( _M_IX86 )
	#define FIX_ISA_X86 1
	#if defined( _MSC_VER ) && !defined( __clang__ )
		#include <intrin.h>
	#else
		#include <cpuid.h>
	#endif
#else
	#define FIX_ISA_X86 0
#endif

#if ( defined( __aarch64__ ) || defined( _M_ARM64 ) ) && defined( __linux__ )
	#include <sys/auxv.h>
	#define FIX_ISA_LINUX_ARM64 1
#else
	#define FIX_ISA_LINUX_ARM64 0
#endif

#if ( defined( __aarch64__ ) || defined( __arm64__ ) ) && defined( __APPLE__ )
	#include <sys/sysctl.h>
	#define FIX_ISA_DARWIN_ARM64 1
#else
	#define FIX_ISA_DARWIN_ARM64 0
#endif

typedef void ( *fixMulBatchFn )( fixed_t*, const fixed_t*, const fixed_t*, size_t );
typedef void ( *fixDotBatch3Fn )( fixed_t*, const fixed_t*, const fixed_t*, const fixed_t*, const fixed_t*,
								  const fixed_t*, const fixed_t*, size_t );

// The scalar kernels. Not static: the variant TUs call them for their tails, so a tail is
// the scalar definition by construction rather than a copy of it.
void fixIsaMulBatchScalar( fixed_t* out, const fixed_t* a, const fixed_t* b, size_t n );
void fixIsaDotBatch3Scalar( fixed_t* out, const fixed_t* ax, const fixed_t* ay, const fixed_t* az,
							const fixed_t* bx, const fixed_t* by, const fixed_t* bz, size_t n );

void fixIsaMulBatchScalar( fixed_t* out, const fixed_t* a, const fixed_t* b, size_t n )
{
	for ( size_t i = 0; i < n; ++i )
		out[i] = fixMul( a[i], b[i] );
}

void fixIsaDotBatch3Scalar( fixed_t* out, const fixed_t* ax, const fixed_t* ay, const fixed_t* az,
							const fixed_t* bx, const fixed_t* by, const fixed_t* bz, size_t n )
{
	for ( size_t i = 0; i < n; ++i )
	{
		fixVec3 a = { ax[i], ay[i], az[i] };
		fixVec3 b = { bx[i], by[i], bz[i] };
		out[i] = fixDot( a, b );
	}
}

#if FIX_ISA_HAVE_AVX512
// src/fixed_isa_avx512.c
void fixIsaMulBatchAvx512( fixed_t* out, const fixed_t* a, const fixed_t* b, size_t n );
void fixIsaDotBatch3Avx512( fixed_t* out, const fixed_t* ax, const fixed_t* ay, const fixed_t* az,
							const fixed_t* bx, const fixed_t* by, const fixed_t* bz, size_t n );
#endif

// THE KERNEL TABLE: one pointer per batch operation. It starts on scalar, which every host
// can run, so a batch call made before init (from another static constructor, say) is
// still correct; it is rewritten only by the init and by fixForceIsa.
typedef struct fixIsaKernels
{
	fixMulBatchFn mulBatch;
	fixDotBatch3Fn dotBatch3;
} fixIsaKernels;

static fixIsaKernels fixIsaTable = { fixIsaMulBatchScalar, fixIsaDotBatch3Scalar };
static fixIsa fixIsaCurrent = FIX_ISA_SCALAR;
static uint32_t fixIsaFacts = 0;

// --------------------------------------------------------------------------------------
// The pure layer.
// --------------------------------------------------------------------------------------

static const char* const fixIsaNames[FIX_ISA_COUNT] = { "scalar", "avx2", "avx512", "avx512ifma", "neon", "sme2" };

static const char* const fixIsaFactNames[FIX_ISA_FACT_COUNT] = {
	"avx2", "os-ymm-state", "avx512f", "avx512dq", "avx512vl", "avx512ifma",
	"os-zmm-state", "neon", "sve", "sve2", "sme", "sme2" };

const char* fixIsaVariantName( fixIsa isa )
{
	return ( unsigned )isa < FIX_ISA_COUNT ? fixIsaNames[isa] : NULL;
}

const char* fixIsaFactName( uint32_t fact )
{
	for ( int i = 0; i < FIX_ISA_FACT_COUNT; ++i )
		if ( fact == ( 1u << i ) ) return fixIsaFactNames[i];
	return NULL;
}

int fixIsaFromName( const char* name, fixIsa* out )
{
	if ( name == NULL ) return 0;
	for ( int i = 0; i < FIX_ISA_COUNT; ++i )
	{
		if ( strcmp( name, fixIsaNames[i] ) == 0 )
		{
			if ( out != NULL ) *out = ( fixIsa )i;
			return 1;
		}
	}
	return 0;
}

int fixIsaCompiled( fixIsa isa )
{
	switch ( isa )
	{
		case FIX_ISA_SCALAR: return 1;
		case FIX_ISA_AVX512: return FIX_ISA_HAVE_AVX512;
		default: return 0;
	}
}

uint32_t fixIsaRequiredFacts( fixIsa isa )
{
	switch ( isa )
	{
		case FIX_ISA_SCALAR: return 0;
		case FIX_ISA_AVX2: return FIX_ISA_FACT_AVX2 | FIX_ISA_FACT_OS_YMM;
		// The kernels are 512-bit only (no VL forms): F for the lanes, DQ for vpmullq.
		case FIX_ISA_AVX512: return FIX_ISA_FACT_AVX512F | FIX_ISA_FACT_AVX512DQ | FIX_ISA_FACT_OS_ZMM;
		case FIX_ISA_AVX512IFMA:
			return FIX_ISA_FACT_AVX512F | FIX_ISA_FACT_AVX512DQ | FIX_ISA_FACT_AVX512IFMA | FIX_ISA_FACT_OS_ZMM;
		case FIX_ISA_NEON: return FIX_ISA_FACT_NEON;
		case FIX_ISA_SME2: return FIX_ISA_FACT_SME | FIX_ISA_FACT_SME2;
		default: return 0xffffffffu;
	}
}

fixIsaResult fixIsaCheck( fixIsa isa, uint32_t facts, uint32_t* missing )
{
	if ( ( unsigned )isa >= FIX_ISA_COUNT ) return FIX_ISA_UNKNOWN_NAME;
	if ( !fixIsaCompiled( isa ) ) return FIX_ISA_NOT_COMPILED;
	uint32_t lack = fixIsaRequiredFacts( isa ) & ~facts;
	if ( missing != NULL ) *missing = lack;
	return lack == 0 ? FIX_ISA_OK : FIX_ISA_HOST_LACKS;
}

fixIsa fixIsaBest( uint32_t facts )
{
	// Highest index first: within an architecture the enum is in order of preference.
	for ( int i = FIX_ISA_COUNT - 1; i > 0; --i )
		if ( fixIsaCheck( ( fixIsa )i, facts, NULL ) == FIX_ISA_OK ) return ( fixIsa )i;
	return FIX_ISA_SCALAR;
}

uint32_t fixIsaParseX86( uint32_t maxLeaf, uint32_t leaf1Ecx, uint32_t leaf7Ebx, uint64_t xcr0 )
{
	uint32_t facts = 0;
	const int osxsave = ( leaf1Ecx >> 27 ) & 1;
	const int avx = ( leaf1Ecx >> 28 ) & 1;
	if ( osxsave && avx && ( xcr0 & 0x6 ) == 0x6 ) facts |= FIX_ISA_FACT_OS_YMM; // SSE + AVX state
	if ( osxsave && avx && ( xcr0 & 0xe6 ) == 0xe6 ) facts |= FIX_ISA_FACT_OS_ZMM; // + opmask, ZMM_Hi256, Hi16_ZMM
	if ( maxLeaf >= 7 )
	{
		if ( ( leaf7Ebx >> 5 ) & 1 ) facts |= FIX_ISA_FACT_AVX2;
		if ( ( leaf7Ebx >> 16 ) & 1 ) facts |= FIX_ISA_FACT_AVX512F;
		if ( ( leaf7Ebx >> 17 ) & 1 ) facts |= FIX_ISA_FACT_AVX512DQ;
		if ( ( leaf7Ebx >> 21 ) & 1 ) facts |= FIX_ISA_FACT_AVX512IFMA;
		if ( ( leaf7Ebx >> 31 ) & 1 ) facts |= FIX_ISA_FACT_AVX512VL;
	}
	return facts;
}

uint32_t fixIsaParseLinuxArm64( uint64_t hwcap, uint64_t hwcap2 )
{
	uint32_t facts = 0;
	if ( ( hwcap >> 1 ) & 1 ) facts |= FIX_ISA_FACT_NEON;   // HWCAP_ASIMD
	if ( ( hwcap >> 22 ) & 1 ) facts |= FIX_ISA_FACT_SVE;   // HWCAP_SVE
	if ( ( hwcap2 >> 1 ) & 1 ) facts |= FIX_ISA_FACT_SVE2;  // HWCAP2_SVE2
	if ( ( hwcap2 >> 23 ) & 1 ) facts |= FIX_ISA_FACT_SME;  // HWCAP2_SME
	if ( ( hwcap2 >> 37 ) & 1 ) facts |= FIX_ISA_FACT_SME2; // HWCAP2_SME2
	return facts;
}

uint32_t fixIsaParseDarwinArm64( int advSimd, int featSme, int featSme2 )
{
	uint32_t facts = 0;
	if ( advSimd ) facts |= FIX_ISA_FACT_NEON;
	if ( featSme ) facts |= FIX_ISA_FACT_SME;
	if ( featSme2 ) facts |= FIX_ISA_FACT_SME2;
	return facts;
}

// --------------------------------------------------------------------------------------
// Reading the host. The only code here that touches the machine; everything it reads goes
// straight into a parser above.
// --------------------------------------------------------------------------------------

#if FIX_ISA_X86
static void fixIsaCpuid( uint32_t leaf, uint32_t sub, uint32_t r[4] )
{
	#if defined( _MSC_VER ) && !defined( __clang__ )
	int v[4];
	__cpuidex( v, ( int )leaf, ( int )sub );
	r[0] = ( uint32_t )v[0], r[1] = ( uint32_t )v[1], r[2] = ( uint32_t )v[2], r[3] = ( uint32_t )v[3];
	#else
	unsigned a = 0, b = 0, c = 0, d = 0;
	__cpuid_count( leaf, sub, a, b, c, d );
	r[0] = a, r[1] = b, r[2] = c, r[3] = d;
	#endif
}

static uint64_t fixIsaXgetbv0( void )
{
	#if defined( _MSC_VER ) && !defined( __clang__ )
	return ( uint64_t )_xgetbv( 0 );
	#else
	// The raw encoding of xgetbv, so no -mxsave flag is needed for this baseline TU.
	uint32_t lo = 0, hi = 0;
	__asm__ volatile( ".byte 0x0f, 0x01, 0xd0" : "=a"( lo ), "=d"( hi ) : "c"( 0 ) );
	return ( ( uint64_t )hi << 32 ) | lo;
	#endif
}
#endif

static uint32_t fixIsaDetect( void )
{
#if FIX_ISA_X86
	uint32_t r0[4], r1[4] = { 0, 0, 0, 0 }, r7[4] = { 0, 0, 0, 0 };
	fixIsaCpuid( 0, 0, r0 );
	if ( r0[0] >= 1 ) fixIsaCpuid( 1, 0, r1 );
	if ( r0[0] >= 7 ) fixIsaCpuid( 7, 0, r7 );
	uint64_t xcr0 = ( ( r1[2] >> 27 ) & 1 ) ? fixIsaXgetbv0() : 0;
	return fixIsaParseX86( r0[0], r1[2], r7[1], xcr0 );
#elif FIX_ISA_LINUX_ARM64
	return fixIsaParseLinuxArm64( ( uint64_t )getauxval( AT_HWCAP ), ( uint64_t )getauxval( AT_HWCAP2 ) );
#elif FIX_ISA_DARWIN_ARM64
	// The AdvSIMD sysctl has had three names across macOS releases (measured on an M5 Pro:
	// only hw.optional.arm.AdvSIMD exists there). The first that answers is the fact.
	const char* names[5] = { "hw.optional.arm.AdvSIMD", "hw.optional.AdvSIMD", "hw.optional.neon",
							 "hw.optional.arm.FEAT_SME", "hw.optional.arm.FEAT_SME2" };
	int v[5] = { 0, 0, 0, 0, 0 }, found[5] = { 0, 0, 0, 0, 0 };
	for ( int i = 0; i < 5; ++i )
	{
		int value = 0;
		size_t size = sizeof value;
		if ( sysctlbyname( names[i], &value, &size, NULL, 0 ) == 0 ) v[i] = value, found[i] = 1;
	}
	int advSimd = found[0] ? v[0] : found[1] ? v[1] : v[2];
	return fixIsaParseDarwinArm64( advSimd, v[3], v[4] );
#else
	return 0;
#endif
}

// --------------------------------------------------------------------------------------
// The table.
// --------------------------------------------------------------------------------------

static void fixIsaInstall( fixIsa isa )
{
	switch ( isa )
	{
#if FIX_ISA_HAVE_AVX512
		case FIX_ISA_AVX512:
			fixIsaTable.mulBatch = fixIsaMulBatchAvx512;
			fixIsaTable.dotBatch3 = fixIsaDotBatch3Avx512;
			break;
#endif
		default:
			fixIsaTable.mulBatch = fixIsaMulBatchScalar;
			fixIsaTable.dotBatch3 = fixIsaDotBatch3Scalar;
			isa = FIX_ISA_SCALAR;
			break;
	}
	fixIsaCurrent = isa;
}

static fixIsaResult fixIsaForceFacts( const char* name, uint32_t facts )
{
	fixIsa isa;
	if ( !fixIsaFromName( name, &isa ) ) return FIX_ISA_UNKNOWN_NAME;
	fixIsaResult result = fixIsaCheck( isa, facts, NULL );
	if ( result == FIX_ISA_OK ) fixIsaInstall( isa );
	return result;
}

static void fixIsaInit( void )
{
	fixIsaFacts = fixIsaDetect();
	fixIsaInstall( fixIsaBest( fixIsaFacts ) );

	// getenv is fine here: this runs single-threaded before main. MSVC deprecates it in
	// favour of _dupenv_s, which allocates; the warning is silenced, not the semantics.
#if defined( _MSC_VER )
	#pragma warning( push )
	#pragma warning( disable : 4996 )
#endif
	const char* name = getenv( "FIXED_ISA" );
#if defined( _MSC_VER )
	#pragma warning( pop )
#endif
	if ( name == NULL || name[0] == '\0' ) return;
	fixIsaResult result = fixIsaForceFacts( name, fixIsaFacts );
	if ( result == FIX_ISA_OK ) return;

	// REFUSED BY NAME, never silently replaced.
	fixIsa isa = FIX_ISA_SCALAR;
	uint32_t missing = 0;
	if ( fixIsaFromName( name, &isa ) ) fixIsaCheck( isa, fixIsaFacts, &missing );
	fprintf( stderr, "fixed: FIXED_ISA=%s refused: ", name );
	if ( result == FIX_ISA_UNKNOWN_NAME )
		fprintf( stderr, "not a variant name (scalar, avx2, avx512, avx512ifma, neon, sme2)\n" );
	else if ( result == FIX_ISA_NOT_COMPILED )
		fprintf( stderr, "not compiled into this build\n" );
	else
	{
		fprintf( stderr, "host lacks" );
		for ( int i = 0; i < FIX_ISA_FACT_COUNT; ++i )
			if ( missing & ( 1u << i ) ) fprintf( stderr, " %s", fixIsaFactNames[i] );
		fprintf( stderr, "\n" );
	}
	abort();
}

// Run the init once, single-threaded, before main.
#if defined( _MSC_VER ) && !defined( __clang__ )
	#pragma section( ".CRT$XCU", read )
static void __cdecl fixIsaInitCrt( void ) { fixIsaInit(); }
__declspec( allocate( ".CRT$XCU" ) ) void( __cdecl* fixIsaInitCrtEntry )( void ) = fixIsaInitCrt;
	#if defined( _M_X64 ) || defined( _M_ARM64 )
		#pragma comment( linker, "/include:fixIsaInitCrtEntry" )
	#else
		#pragma comment( linker, "/include:_fixIsaInitCrtEntry" )
	#endif
#else
__attribute__( ( constructor ) ) static void fixIsaInitCtor( void ) { fixIsaInit(); }
#endif

// --------------------------------------------------------------------------------------
// The public surface.
// --------------------------------------------------------------------------------------

const char* fixIsaName( void ) { return fixIsaNames[fixIsaCurrent]; }
fixIsa fixIsaSelected( void ) { return fixIsaCurrent; }
uint32_t fixIsaHostFacts( void ) { return fixIsaFacts; }
fixIsaResult fixForceIsa( const char* name ) { return fixIsaForceFacts( name, fixIsaFacts ); }

void fixMulBatch( fixed_t* out, const fixed_t* a, const fixed_t* b, size_t n )
{
	fixIsaTable.mulBatch( out, a, b, n );
}

void fixDotBatch3( fixed_t* out, const fixed_t* ax, const fixed_t* ay, const fixed_t* az, const fixed_t* bx,
				   const fixed_t* by, const fixed_t* bz, size_t n )
{
	fixIsaTable.dotBatch3( out, ax, ay, az, bx, by, bz, n );
}
