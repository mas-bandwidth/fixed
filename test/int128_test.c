// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
//
// The emulated 128-bit pair, held to the compiler's native __int128 operation by
// operation, and pinned by a frozen hash on the compilers that have no native to
// compare against.
//
// WHY BOTH HALVES ARE NEEDED. The differential sweeps below are the real proof, and
// they only exist on gcc, clang and clang-cl -- the compilers that already had 128-bit
// integers and therefore never needed the emulation. Plain MSVC, the compiler the
// emulation exists FOR, has nothing to differentiate against. So every structured sweep
// also feeds a frozen FNV hash: the compilers with native prove the hash is the value
// native produces, and plain MSVC proves it still produces that value. Neither half
// alone would be worth much.
//
// NEGATIVE CONTROLS, ALWAYS ON, in the manner of port_equality_test.c. Every sweep runs
// once clean and once with the emulated result perturbed by one, and the perturbed arm
// must find at least one mismatch per random sample. A comparison that only ever runs
// its passing arm cannot tell you it compared anything.
//
// THE NATIVE REFERENCES ARE FORCED OUT OF LINE. Both sides of this comparison are
// FIX_ALWAYS_INLINE header code operating on values the optimizer can track, which is
// exactly the shape that let a 200-million-comparison sweep fold to nothing and pass in
// 0.00s on pull request #9. REF_NOINLINE keeps the native side opaque, and main() prints
// the elapsed time of the whole suite so a suspiciously instant run is visible.
//
// WHAT IS DELIBERATELY NOT COMPARED, because native has no answer to compare to:
//   - division and modulo by zero (undefined for native __int128; arm64 and x86-64 do
//     not agree with each other)
//   - shift counts outside [0, 127] (undefined for native shifts)
// The emulated pair defines both cases anyway so it cannot trap, and those definitions
// are checked against frozen expectations at the end of main rather than against native.

#include "fixed/fixed.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifndef INT128_SWEEP_SCALE
	#define INT128_SWEEP_SCALE 1
#endif
#define SAMPLES( n ) ( (uint64_t)( n ) * (uint64_t)INT128_SWEEP_SCALE )

#if defined( _MSC_VER ) && !defined( __clang__ )
	#define REF_NOINLINE __declspec( noinline )
#else
	#define REF_NOINLINE __attribute__( ( noinline ) )
#endif

// The differential half only exists where the compiler has __int128. Note this is NOT
// FIX_INT128_EMULATED: a build forced onto the emulated path with
// FIX_FORCE_EMULATED_INT128 still has native __int128 available to compare against, and
// that is the build where this test matters most.
#if defined( __SIZEOF_INT128__ )
	#define HAS_NATIVE 1
__extension__ typedef __int128 nativeInt128;
__extension__ typedef unsigned __int128 nativeUInt128;
#else
	#define HAS_NATIVE 0
#endif

static int fails = 0;
static int printCounts = 0;

static uint64_t fnv = 0xCBF29CE484222325ULL;
static void mix64( uint64_t v )
{
	const uint8_t* b = (const uint8_t*)&v;
	for ( int i = 0; i < 8; i++ ) { fnv ^= b[i]; fnv *= 0x00000100000001B3ULL; }
}
static void mixU( fixEmuUInt128 v ) { mix64( v.lo ); mix64( v.hi ); }
static void mixS( fixEmuInt128 v ) { mix64( v.lo ); mix64( v.hi ); }

// FROZEN 2026-08-24, captured from the emulated pair and confirmed equal to the value
// the native operations produce on arm64 (see the differential sweeps). Every compiler
// and operating system must reproduce it. If a legitimate change alters it, re-capture
// deliberately and say why in the pull request; never loosen it to make a mismatch go
// away.
#ifndef EXPECTED_INT128_HASH
	#define EXPECTED_INT128_HASH 0x348cfaf5be984407ULL
#endif

// Deterministic sampling. splitmix64: pure integer, identical on every platform.
static uint64_t rngState;
static uint64_t nextRandom( void )
{
	rngState += 0x9E3779B97F4A7C15ULL;
	uint64_t z = rngState;
	z = ( z ^ ( z >> 30 ) ) * 0xBF58476D1CE4E5B9ULL;
	z = ( z ^ ( z >> 27 ) ) * 0x94D049BB133111EBULL;
	return z ^ ( z >> 31 );
}

// The structured domain: lane values chosen to hit every boundary a 128-bit
// implementation can get wrong -- zero, one, both saturation ends, the sign bit, the
// all-ones lane, and a value with bits in every nibble.
static const uint64_t laneValues[] = {
	0,
	1,
	2,
	0xFFFFFFFFFFFFFFFFULL,
	0xFFFFFFFFFFFFFFFEULL,
	0x8000000000000000ULL,
	0x7FFFFFFFFFFFFFFFULL,
	0x00000000FFFFFFFFULL,
	0xFFFFFFFF00000000ULL,
	0x0123456789ABCDEFULL,
};
#define LANE_COUNT ( (int)( sizeof( laneValues ) / sizeof( laneValues[0] ) ) )

// One quantum on the low lane, with the carry into the high lane, so the perturbation
// is a real +1 on the 128-bit value rather than a lane-local wrap that could coincide.
static fixEmuUInt128 bumpU( fixEmuUInt128 v, int perturb )
{
	if ( perturb == 0 ) return v;
	return fixEmuUInt128Add( v, fixEmuUInt128FromU64( (uint64_t)perturb ) );
}
static fixEmuInt128 bumpS( fixEmuInt128 v, int perturb )
{
	if ( perturb == 0 ) return v;
	return fixEmuInt128Add( v, fixEmuInt128FromI64( perturb ) );
}
static uint64_t bump64( uint64_t v, int perturb )
{
	return v + (uint64_t)perturb;
}

#if HAS_NATIVE

// ================================================================================
// The native references, forced out of line. See the REF_NOINLINE note in the header
// comment: an oracle the compiler can collapse into the thing it is checking is not an
// oracle.
// ================================================================================

static REF_NOINLINE nativeUInt128 nativeMakeU( uint64_t hi, uint64_t lo )
{
	return ( (nativeUInt128)hi << 64 ) | lo;
}
static REF_NOINLINE nativeInt128 nativeMakeS( uint64_t hi, uint64_t lo )
{
	return (nativeInt128)( ( (nativeUInt128)hi << 64 ) | lo );
}
static REF_NOINLINE nativeUInt128 refUAdd( nativeUInt128 a, nativeUInt128 b ) { return a + b; }
static REF_NOINLINE nativeUInt128 refUSub( nativeUInt128 a, nativeUInt128 b ) { return a - b; }
static REF_NOINLINE nativeUInt128 refUMul( nativeUInt128 a, nativeUInt128 b ) { return a * b; }
static REF_NOINLINE nativeUInt128 refUNeg( nativeUInt128 a ) { return -a; }
static REF_NOINLINE nativeUInt128 refUMulU64( uint64_t a, uint64_t b ) { return (nativeUInt128)a * b; }
static REF_NOINLINE nativeUInt128 refUShl( nativeUInt128 a, int s ) { return a << s; }
static REF_NOINLINE nativeUInt128 refUShr( nativeUInt128 a, int s ) { return a >> s; }
static REF_NOINLINE nativeUInt128 refUAnd( nativeUInt128 a, nativeUInt128 b ) { return a & b; }
static REF_NOINLINE nativeUInt128 refUOr( nativeUInt128 a, nativeUInt128 b ) { return a | b; }
static REF_NOINLINE nativeUInt128 refUXor( nativeUInt128 a, nativeUInt128 b ) { return a ^ b; }
static REF_NOINLINE nativeUInt128 refUNot( nativeUInt128 a ) { return ~a; }
// DIVISION IS THE ONE REFERENCE THAT CANNOT ALWAYS BE THE COMPILER'S OPERATOR.
//
// ClangCL compiles __int128 but does not link compiler-rt, so __udivti3 and friends are
// undefined symbols at link time -- the same constraint fixInt128Div already documents
// and works around. Where the operator links it is used, because it is the strongest
// possible oracle; where it does not, the reference is shift-subtract long division
// written on the NATIVE 128-bit type, which still shares nothing with the emulated pair's
// lane arithmetic and so still catches a carry, borrow or half-boundary mistake.
#if defined( _WIN32 )
static REF_NOINLINE nativeUInt128 refUDivMod( nativeUInt128 a, nativeUInt128 b, bool wantRemainder )
{
	nativeUInt128 q = 0;
	nativeUInt128 r = 0;
	for ( int i = 127; i >= 0; i-- )
	{
		r = ( r << 1 ) | ( ( a >> i ) & 1 );
		if ( r >= b )
		{
			r -= b;
			q |= (nativeUInt128)1 << i;
		}
	}
	return wantRemainder ? r : q;
}
static REF_NOINLINE nativeUInt128 refUDiv( nativeUInt128 a, nativeUInt128 b ) { return refUDivMod( a, b, false ); }
static REF_NOINLINE nativeUInt128 refUMod( nativeUInt128 a, nativeUInt128 b ) { return refUDivMod( a, b, true ); }
#else
static REF_NOINLINE nativeUInt128 refUDiv( nativeUInt128 a, nativeUInt128 b ) { return a / b; }
static REF_NOINLINE nativeUInt128 refUMod( nativeUInt128 a, nativeUInt128 b ) { return a % b; }
#endif
static REF_NOINLINE int refUCmp( nativeUInt128 a, nativeUInt128 b )
{
	return ( a == b ? 1 : 0 ) | ( a < b ? 2 : 0 ) | ( a > b ? 4 : 0 ) | ( a <= b ? 8 : 0 ) | ( a >= b ? 16 : 0 );
}

// The signed arithmetic references go through the unsigned type. The emulated pair wraps
// on overflow because two's complement lanes wrap, which is what native hardware does --
// but writing `a + b` on a signed __int128 and letting it overflow is UNDEFINED behavior,
// and this suite runs under UBSan. Casting through unsigned is the same bit pattern with
// defined semantics, the same idiom fixShiftLeft uses in the library itself. The
// structured domain deliberately includes both saturation ends, so these DO overflow.
static REF_NOINLINE nativeInt128 refSAdd( nativeInt128 a, nativeInt128 b )
{
	return (nativeInt128)( (nativeUInt128)a + (nativeUInt128)b );
}
static REF_NOINLINE nativeInt128 refSSub( nativeInt128 a, nativeInt128 b )
{
	return (nativeInt128)( (nativeUInt128)a - (nativeUInt128)b );
}
static REF_NOINLINE nativeInt128 refSMul( nativeInt128 a, nativeInt128 b )
{
	return (nativeInt128)( (nativeUInt128)a * (nativeUInt128)b );
}
static REF_NOINLINE nativeInt128 refSNeg( nativeInt128 a ) { return (nativeInt128)( -(nativeUInt128)a ); }
static REF_NOINLINE nativeInt128 refSMulI64( int64_t a, int64_t b ) { return (nativeInt128)a * b; }
static REF_NOINLINE nativeInt128 refSShl( nativeInt128 a, int s ) { return (nativeInt128)( (nativeUInt128)a << s ); }
static REF_NOINLINE nativeInt128 refSShr( nativeInt128 a, int s ) { return a >> s; }
#if defined( _WIN32 )
// Sign extraction, unsigned division on the magnitudes, sign application: C semantics,
// truncation toward zero with the remainder's sign following the dividend.
static REF_NOINLINE nativeInt128 refSDivMod( nativeInt128 a, nativeInt128 b, bool wantRemainder )
{
	bool negativeA = a < 0;
	bool negativeB = b < 0;
	nativeUInt128 ua = negativeA ? -(nativeUInt128)a : (nativeUInt128)a;
	nativeUInt128 ub = negativeB ? -(nativeUInt128)b : (nativeUInt128)b;
	if ( wantRemainder )
	{
		nativeUInt128 r = refUDivMod( ua, ub, true );
		return (nativeInt128)( negativeA ? -r : r );
	}
	nativeUInt128 q = refUDivMod( ua, ub, false );
	return (nativeInt128)( ( negativeA != negativeB ) ? -q : q );
}
static REF_NOINLINE nativeInt128 refSDiv( nativeInt128 a, nativeInt128 b ) { return refSDivMod( a, b, false ); }
static REF_NOINLINE nativeInt128 refSMod( nativeInt128 a, nativeInt128 b ) { return refSDivMod( a, b, true ); }
#else
static REF_NOINLINE nativeInt128 refSDiv( nativeInt128 a, nativeInt128 b ) { return a / b; }
static REF_NOINLINE nativeInt128 refSMod( nativeInt128 a, nativeInt128 b ) { return a % b; }
#endif
static REF_NOINLINE int64_t refSToI64( nativeInt128 a ) { return (int64_t)a; }
static REF_NOINLINE int refSCmp( nativeInt128 a, nativeInt128 b )
{
	return ( a == b ? 1 : 0 ) | ( a < b ? 2 : 0 ) | ( a > b ? 4 : 0 ) | ( a <= b ? 8 : 0 ) | ( a >= b ? 16 : 0 );
}

static bool sameU( fixEmuUInt128 emu, nativeUInt128 nat )
{
	return emu.lo == (uint64_t)nat && emu.hi == (uint64_t)( nat >> 64 );
}
static bool sameS( fixEmuInt128 emu, nativeInt128 nat )
{
	return emu.lo == (uint64_t)(nativeUInt128)nat && emu.hi == (uint64_t)( (nativeUInt128)nat >> 64 );
}

#endif // HAS_NATIVE

// A comparison against native, or nothing where there is no native. The clean arm still
// walks the whole domain and still feeds the hash on platforms without __int128, so the
// sweep is never an empty loop -- it is the ORACLE that is missing there, not the work.
#if HAS_NATIVE
	#define CHECK_U( emuValue, nativeValue ) ( sameU( ( emuValue ), ( nativeValue ) ) ? 0 : 1 )
	#define CHECK_S( emuValue, nativeValue ) ( sameS( ( emuValue ), ( nativeValue ) ) ? 0 : 1 )
	#define CHECK_64( emuValue, nativeValue ) ( ( emuValue ) == ( nativeValue ) ? 0 : 1 )
#else
	#define CHECK_U( emuValue, nativeValue ) ( 0 )
	#define CHECK_S( emuValue, nativeValue ) ( 0 )
	#define CHECK_64( emuValue, nativeValue ) ( 0 )
#endif

// ================================================================================
// The sweeps. Each walks the structured lane domain, then a deterministic random
// sample, comparing the emulated operation to the native one.
// ================================================================================

static uint64_t sweepUnsignedArith( int perturb, uint64_t samples )
{
	uint64_t bad = 0;

	for ( int ah = 0; ah < LANE_COUNT; ah++ )
	{
		for ( int al = 0; al < LANE_COUNT; al++ )
		{
			for ( int bh = 0; bh < LANE_COUNT; bh++ )
			{
				for ( int bl = 0; bl < LANE_COUNT; bl++ )
				{
					fixEmuUInt128 a = fixEmuUInt128Make( laneValues[ah], laneValues[al] );
					fixEmuUInt128 b = fixEmuUInt128Make( laneValues[bh], laneValues[bl] );

					fixEmuUInt128 sum = bumpU( fixEmuUInt128Add( a, b ), perturb );
					fixEmuUInt128 difference = bumpU( fixEmuUInt128Sub( a, b ), perturb );
					fixEmuUInt128 product = bumpU( fixEmuUInt128Mul( a, b ), perturb );
					fixEmuUInt128 negated = bumpU( fixEmuUInt128Neg( a ), perturb );

#if HAS_NATIVE
					nativeUInt128 na = nativeMakeU( laneValues[ah], laneValues[al] );
					nativeUInt128 nb = nativeMakeU( laneValues[bh], laneValues[bl] );
					bad += CHECK_U( sum, refUAdd( na, nb ) );
					bad += CHECK_U( difference, refUSub( na, nb ) );
					bad += CHECK_U( product, refUMul( na, nb ) );
					bad += CHECK_U( negated, refUNeg( na ) );
#endif
					if ( !perturb ) { mixU( sum ); mixU( difference ); mixU( product ); mixU( negated ); }
				}
			}
		}
	}

	rngState = 0xA1000000ULL + (uint64_t)perturb;
	for ( uint64_t i = 0; i < samples; i++ )
	{
		uint64_t ah = nextRandom(), al = nextRandom(), bh = nextRandom(), bl = nextRandom();
		fixEmuUInt128 a = fixEmuUInt128Make( ah, al );
		fixEmuUInt128 b = fixEmuUInt128Make( bh, bl );

		fixEmuUInt128 sum = bumpU( fixEmuUInt128Add( a, b ), perturb );
		fixEmuUInt128 product = bumpU( fixEmuUInt128Mul( a, b ), perturb );

#if HAS_NATIVE
		nativeUInt128 na = nativeMakeU( ah, al );
		nativeUInt128 nb = nativeMakeU( bh, bl );
		bad += CHECK_U( sum, refUAdd( na, nb ) );
		bad += CHECK_U( product, refUMul( na, nb ) );
		bad += CHECK_U( bumpU( fixEmuUInt128Sub( a, b ), perturb ), refUSub( na, nb ) );
#endif
	}

	return bad;
}

static uint64_t sweepUnsignedMul64( int perturb, uint64_t samples )
{
	uint64_t bad = 0;

	for ( int ai = 0; ai < LANE_COUNT; ai++ )
	{
		for ( int bi = 0; bi < LANE_COUNT; bi++ )
		{
			fixEmuUInt128 product = bumpU( fixEmuUInt128MulU64( laneValues[ai], laneValues[bi] ), perturb );

			// The intrinsic-accelerated multiply against the portable schoolbook. On a
			// compiler with no intrinsic these are the same function and this comparison
			// is a tautology; on MSVC it is the check that the intrinsic path is right.
			fixEmuUInt128 schoolbook = fixEmuUInt128MulU64Schoolbook( laneValues[ai], laneValues[bi] );
			if ( !fixEmuUInt128Eq( bumpU( schoolbook, perturb ), product ) ) bad++;

			bad += CHECK_U( product, refUMulU64( laneValues[ai], laneValues[bi] ) );
			if ( !perturb ) mixU( product );
		}
	}

	rngState = 0xA2000000ULL + (uint64_t)perturb;
	for ( uint64_t i = 0; i < samples; i++ )
	{
		uint64_t a = nextRandom(), b = nextRandom();
		fixEmuUInt128 product = bumpU( fixEmuUInt128MulU64( a, b ), perturb );
		if ( !fixEmuUInt128Eq( bumpU( fixEmuUInt128MulU64Schoolbook( a, b ), perturb ), product ) ) bad++;
		bad += CHECK_U( product, refUMulU64( a, b ) );
	}

	return bad;
}

static uint64_t sweepUnsignedShifts( int perturb, uint64_t samples )
{
	uint64_t bad = 0;

	// Every shift count in the defined range against every structured lane pair. 128
	// counts is small enough to walk exhaustively, and the boundary at 64 -- where a
	// lane-local shift would be undefined -- is the one that breaks implementations.
	for ( int ah = 0; ah < LANE_COUNT; ah++ )
	{
		for ( int al = 0; al < LANE_COUNT; al++ )
		{
			fixEmuUInt128 a = fixEmuUInt128Make( laneValues[ah], laneValues[al] );
			for ( int shift = 0; shift < 128; shift++ )
			{
				fixEmuUInt128 left = bumpU( fixEmuUInt128Shl( a, shift ), perturb );
				fixEmuUInt128 right = bumpU( fixEmuUInt128Shr( a, shift ), perturb );

#if HAS_NATIVE
				nativeUInt128 na = nativeMakeU( laneValues[ah], laneValues[al] );
				bad += CHECK_U( left, refUShl( na, shift ) );
				bad += CHECK_U( right, refUShr( na, shift ) );
#endif
				if ( !perturb ) { mixU( left ); mixU( right ); }
			}
		}
	}

	rngState = 0xA3000000ULL + (uint64_t)perturb;
	for ( uint64_t i = 0; i < samples; i++ )
	{
		uint64_t ah = nextRandom(), al = nextRandom();
		int shift = (int)( nextRandom() % 128 );
		fixEmuUInt128 a = fixEmuUInt128Make( ah, al );

		fixEmuUInt128 left = bumpU( fixEmuUInt128Shl( a, shift ), perturb );
		fixEmuUInt128 right = bumpU( fixEmuUInt128Shr( a, shift ), perturb );
#if HAS_NATIVE
		nativeUInt128 na = nativeMakeU( ah, al );
		bad += CHECK_U( left, refUShl( na, shift ) );
		bad += CHECK_U( right, refUShr( na, shift ) );
#endif
	}

	return bad;
}

static uint64_t sweepUnsignedBitwise( int perturb, uint64_t samples )
{
	uint64_t bad = 0;

	for ( int ah = 0; ah < LANE_COUNT; ah++ )
	{
		for ( int al = 0; al < LANE_COUNT; al++ )
		{
			for ( int bh = 0; bh < LANE_COUNT; bh++ )
			{
				for ( int bl = 0; bl < LANE_COUNT; bl++ )
				{
					fixEmuUInt128 a = fixEmuUInt128Make( laneValues[ah], laneValues[al] );
					fixEmuUInt128 b = fixEmuUInt128Make( laneValues[bh], laneValues[bl] );

					fixEmuUInt128 conjunction = bumpU( fixEmuUInt128And( a, b ), perturb );
					fixEmuUInt128 disjunction = bumpU( fixEmuUInt128Or( a, b ), perturb );
					fixEmuUInt128 exclusive = bumpU( fixEmuUInt128Xor( a, b ), perturb );
					fixEmuUInt128 complement = bumpU( fixEmuUInt128Not( a ), perturb );

#if HAS_NATIVE
					nativeUInt128 na = nativeMakeU( laneValues[ah], laneValues[al] );
					nativeUInt128 nb = nativeMakeU( laneValues[bh], laneValues[bl] );
					bad += CHECK_U( conjunction, refUAnd( na, nb ) );
					bad += CHECK_U( disjunction, refUOr( na, nb ) );
					bad += CHECK_U( exclusive, refUXor( na, nb ) );
					bad += CHECK_U( complement, refUNot( na ) );
#endif
					if ( !perturb )
					{
						mixU( conjunction ); mixU( disjunction ); mixU( exclusive ); mixU( complement );
					}
				}
			}
		}
	}

	rngState = 0xA4000000ULL + (uint64_t)perturb;
	for ( uint64_t i = 0; i < samples; i++ )
	{
		uint64_t ah = nextRandom(), al = nextRandom(), bh = nextRandom(), bl = nextRandom();
		fixEmuUInt128 a = fixEmuUInt128Make( ah, al );
		fixEmuUInt128 b = fixEmuUInt128Make( bh, bl );

		fixEmuUInt128 exclusive = bumpU( fixEmuUInt128Xor( a, b ), perturb );
		fixEmuUInt128 complement = bumpU( fixEmuUInt128Not( a ), perturb );
#if HAS_NATIVE
		nativeUInt128 na = nativeMakeU( ah, al );
		nativeUInt128 nb = nativeMakeU( bh, bl );
		bad += CHECK_U( exclusive, refUXor( na, nb ) );
		bad += CHECK_U( complement, refUNot( na ) );
		bad += CHECK_U( bumpU( fixEmuUInt128And( a, b ), perturb ), refUAnd( na, nb ) );
		bad += CHECK_U( bumpU( fixEmuUInt128Or( a, b ), perturb ), refUOr( na, nb ) );
#endif
	}

	return bad;
}

// The comparison sweeps perturb the OPERAND rather than the result: a boolean has no
// quantum to add, and flipping the answer directly would not prove the sweep reaches the
// comparison. Shifting one operand by one changes the answer at every boundary, and the
// random domain is dense enough that some sample near equality moves on every arm.
static uint64_t sweepUnsignedCompare( int perturb, uint64_t samples )
{
	uint64_t bad = 0;

	for ( int ah = 0; ah < LANE_COUNT; ah++ )
	{
		for ( int al = 0; al < LANE_COUNT; al++ )
		{
			for ( int bh = 0; bh < LANE_COUNT; bh++ )
			{
				for ( int bl = 0; bl < LANE_COUNT; bl++ )
				{
					fixEmuUInt128 a = fixEmuUInt128Make( laneValues[ah], laneValues[al] );
					fixEmuUInt128 b = bumpU( fixEmuUInt128Make( laneValues[bh], laneValues[bl] ), perturb );

					int emu = ( fixEmuUInt128Eq( a, b ) ? 1 : 0 ) | ( fixEmuUInt128Lt( a, b ) ? 2 : 0 ) |
							  ( fixEmuUInt128Gt( a, b ) ? 4 : 0 ) | ( fixEmuUInt128Le( a, b ) ? 8 : 0 ) |
							  ( fixEmuUInt128Ge( a, b ) ? 16 : 0 );

#if HAS_NATIVE
					nativeUInt128 na = nativeMakeU( laneValues[ah], laneValues[al] );
					nativeUInt128 nb = nativeMakeU( laneValues[bh], laneValues[bl] );
					bad += CHECK_64( emu, refUCmp( na, nb ) );
#endif
					if ( !perturb ) mix64( (uint64_t)emu );
				}
			}
		}
	}

	// THE RANDOM SAMPLES SIT ON THE ORDERING BOUNDARY ON PURPOSE, and the negative control
	// is the reason. A comparison has no result to perturb -- flipping the answer bit
	// would prove nothing about whether the operands ever reached the comparison -- so the
	// control perturbs the OPERAND the emulated side sees while native keeps the original.
	// That only changes an answer when the two values are adjacent, so every random sample
	// uses b == a or b == a - 1, the two arrangements a +1 on b always moves. (b == a + 1
	// would NOT move: a < a + 1 and a < a + 2 read the same.) Adjacency is also where
	// comparison bugs actually live: the high lanes tie and the low lanes decide.
	rngState = 0xA5000000ULL + (uint64_t)perturb;
	for ( uint64_t i = 0; i < samples; i++ )
	{
		fixEmuUInt128 a = fixEmuUInt128Make( nextRandom(), nextRandom() );
		fixEmuUInt128 b = ( i & 1 ) ? fixEmuUInt128Sub( a, fixEmuUInt128FromU64( 1 ) ) : a;
		fixEmuUInt128 seen = bumpU( b, perturb );

		int emu = ( fixEmuUInt128Eq( a, seen ) ? 1 : 0 ) | ( fixEmuUInt128Lt( a, seen ) ? 2 : 0 ) |
				  ( fixEmuUInt128Gt( a, seen ) ? 4 : 0 ) | ( fixEmuUInt128Le( a, seen ) ? 8 : 0 ) |
				  ( fixEmuUInt128Ge( a, seen ) ? 16 : 0 );
#if HAS_NATIVE
		bad += CHECK_64( emu, refUCmp( nativeMakeU( a.hi, a.lo ), nativeMakeU( b.hi, b.lo ) ) );
#endif
	}

	return bad;
}

static uint64_t sweepUnsignedDivMod( int perturb, uint64_t samples )
{
	uint64_t bad = 0;

	for ( int ah = 0; ah < LANE_COUNT; ah++ )
	{
		for ( int al = 0; al < LANE_COUNT; al++ )
		{
			for ( int bh = 0; bh < LANE_COUNT; bh++ )
			{
				for ( int bl = 0; bl < LANE_COUNT; bl++ )
				{
					if ( laneValues[bh] == 0 && laneValues[bl] == 0 ) continue; // undefined for native

					fixEmuUInt128 a = fixEmuUInt128Make( laneValues[ah], laneValues[al] );
					fixEmuUInt128 b = fixEmuUInt128Make( laneValues[bh], laneValues[bl] );

					fixEmuUInt128 quotient, remainder;
					fixEmuUInt128DivMod( a, b, &quotient, &remainder );
					quotient = bumpU( quotient, perturb );
					remainder = bumpU( remainder, perturb );

					// The single-result forms must agree with the pair form.
					if ( !fixEmuUInt128Eq( bumpU( fixEmuUInt128Div( a, b ), perturb ), quotient ) ) bad++;
					if ( !fixEmuUInt128Eq( bumpU( fixEmuUInt128Mod( a, b ), perturb ), remainder ) ) bad++;

#if HAS_NATIVE
					nativeUInt128 na = nativeMakeU( laneValues[ah], laneValues[al] );
					nativeUInt128 nb = nativeMakeU( laneValues[bh], laneValues[bl] );
					bad += CHECK_U( quotient, refUDiv( na, nb ) );
					bad += CHECK_U( remainder, refUMod( na, nb ) );
#endif
					if ( !perturb ) { mixU( quotient ); mixU( remainder ); }
				}
			}
		}
	}

	rngState = 0xA6000000ULL + (uint64_t)perturb;
	for ( uint64_t i = 0; i < samples; i++ )
	{
		uint64_t ah = nextRandom(), al = nextRandom();
		uint64_t bh = nextRandom(), bl = nextRandom();
		// A third of the divisors fit in 64 bits and a third in 32, so the fast path and
		// the long-division path both get real traffic instead of the divisor almost
		// always being enormous.
		if ( ( i % 3 ) == 1 ) bh = 0;
		if ( ( i % 3 ) == 2 ) { bh = 0; bl &= 0xFFFFFFFFULL; }
		if ( bh == 0 && bl == 0 ) bl = 1;

		fixEmuUInt128 a = fixEmuUInt128Make( ah, al );
		fixEmuUInt128 b = fixEmuUInt128Make( bh, bl );
		fixEmuUInt128 quotient = bumpU( fixEmuUInt128Div( a, b ), perturb );
		fixEmuUInt128 remainder = bumpU( fixEmuUInt128Mod( a, b ), perturb );

#if HAS_NATIVE
		nativeUInt128 na = nativeMakeU( ah, al );
		nativeUInt128 nb = nativeMakeU( bh, bl );
		bad += CHECK_U( quotient, refUDiv( na, nb ) );
		bad += CHECK_U( remainder, refUMod( na, nb ) );
#endif
		// The reconstruction identity, checked everywhere because it needs no oracle:
		// quotient * divisor + remainder must be the dividend, and the remainder must be
		// below the divisor. This is what still catches a broken divide on plain MSVC.
		fixEmuUInt128 rebuilt =
			fixEmuUInt128Add( fixEmuUInt128Mul( fixEmuUInt128Div( a, b ), b ), fixEmuUInt128Mod( a, b ) );
		if ( !fixEmuUInt128Eq( rebuilt, a ) || !fixEmuUInt128Lt( fixEmuUInt128Mod( a, b ), b ) ) bad++;
	}

	return bad;
}

static uint64_t sweepSignedArith( int perturb, uint64_t samples )
{
	uint64_t bad = 0;

	for ( int ah = 0; ah < LANE_COUNT; ah++ )
	{
		for ( int al = 0; al < LANE_COUNT; al++ )
		{
			for ( int bh = 0; bh < LANE_COUNT; bh++ )
			{
				for ( int bl = 0; bl < LANE_COUNT; bl++ )
				{
					fixEmuInt128 a = fixEmuInt128Make( laneValues[ah], laneValues[al] );
					fixEmuInt128 b = fixEmuInt128Make( laneValues[bh], laneValues[bl] );

					fixEmuInt128 sum = bumpS( fixEmuInt128Add( a, b ), perturb );
					fixEmuInt128 difference = bumpS( fixEmuInt128Sub( a, b ), perturb );
					fixEmuInt128 product = bumpS( fixEmuInt128Mul( a, b ), perturb );
					fixEmuInt128 negated = bumpS( fixEmuInt128Neg( a ), perturb );

#if HAS_NATIVE
					nativeInt128 na = nativeMakeS( laneValues[ah], laneValues[al] );
					nativeInt128 nb = nativeMakeS( laneValues[bh], laneValues[bl] );
					bad += CHECK_S( sum, refSAdd( na, nb ) );
					bad += CHECK_S( difference, refSSub( na, nb ) );
					bad += CHECK_S( product, refSMul( na, nb ) );
					bad += CHECK_S( negated, refSNeg( na ) );
#endif
					if ( !perturb ) { mixS( sum ); mixS( difference ); mixS( product ); mixS( negated ); }
				}
			}
		}
	}

	rngState = 0xB1000000ULL + (uint64_t)perturb;
	for ( uint64_t i = 0; i < samples; i++ )
	{
		uint64_t ah = nextRandom(), al = nextRandom(), bh = nextRandom(), bl = nextRandom();
		fixEmuInt128 a = fixEmuInt128Make( ah, al );
		fixEmuInt128 b = fixEmuInt128Make( bh, bl );

		fixEmuInt128 sum = bumpS( fixEmuInt128Add( a, b ), perturb );
		fixEmuInt128 product = bumpS( fixEmuInt128Mul( a, b ), perturb );
#if HAS_NATIVE
		nativeInt128 na = nativeMakeS( ah, al );
		nativeInt128 nb = nativeMakeS( bh, bl );
		bad += CHECK_S( sum, refSAdd( na, nb ) );
		bad += CHECK_S( product, refSMul( na, nb ) );
		bad += CHECK_S( bumpS( fixEmuInt128Sub( a, b ), perturb ), refSSub( na, nb ) );
		bad += CHECK_S( bumpS( fixEmuInt128Neg( a ), perturb ), refSNeg( na ) );
#endif
	}

	return bad;
}

// The widening signed multiply: the single hottest emulated operation, since it is the
// multiply inside every fixMul, every dot product and every cofactor.
static uint64_t sweepSignedMul64( int perturb, uint64_t samples )
{
	uint64_t bad = 0;

	static const int64_t signedLanes[] = {
		0, 1, -1, 2, -2, INT64_MAX, INT64_MIN, INT64_MIN + 1, INT32_MAX, INT32_MIN, 0x0123456789ABCDEFLL, -0x0123456789ABCDEFLL,
	};
	const int signedLaneCount = (int)( sizeof( signedLanes ) / sizeof( signedLanes[0] ) );

	for ( int ai = 0; ai < signedLaneCount; ai++ )
	{
		for ( int bi = 0; bi < signedLaneCount; bi++ )
		{
			fixEmuInt128 product = bumpS( fixEmuInt128MulI64( signedLanes[ai], signedLanes[bi] ), perturb );
			bad += CHECK_S( product, refSMulI64( signedLanes[ai], signedLanes[bi] ) );
			if ( !perturb ) mixS( product );
		}
	}

	rngState = 0xB2000000ULL + (uint64_t)perturb;
	for ( uint64_t i = 0; i < samples; i++ )
	{
		int64_t a = (int64_t)nextRandom();
		int64_t b = (int64_t)nextRandom();
		// Half the samples are the small magnitudes real fixed-point traffic is made of,
		// where a sign-extension bug hides behind lanes that are almost all zero or all
		// ones; the other half are full-width.
		if ( ( i & 1 ) == 0 ) { a >>= 40; b >>= 40; }

		fixEmuInt128 product = bumpS( fixEmuInt128MulI64( a, b ), perturb );
		bad += CHECK_S( product, refSMulI64( a, b ) );
		// Sign identity, checked on every platform because it needs no oracle: (-a)*b
		// must be -(a*b) for every input whose negation exists. This is the check that
		// still has teeth on the compiler with no native to compare against.
		if ( a != INT64_MIN )
		{
			fixEmuInt128 mirrored = fixEmuInt128MulI64( -a, b );
			fixEmuInt128 expected = fixEmuInt128Neg( fixEmuInt128MulI64( a, b ) );
			if ( !fixEmuInt128Eq( mirrored, expected ) ) bad++;
		}
	}

	return bad;
}

static uint64_t sweepSignedShifts( int perturb, uint64_t samples )
{
	uint64_t bad = 0;

	for ( int ah = 0; ah < LANE_COUNT; ah++ )
	{
		for ( int al = 0; al < LANE_COUNT; al++ )
		{
			fixEmuInt128 a = fixEmuInt128Make( laneValues[ah], laneValues[al] );
			for ( int shift = 0; shift < 128; shift++ )
			{
				fixEmuInt128 left = bumpS( fixEmuInt128Shl( a, shift ), perturb );
				fixEmuInt128 right = bumpS( fixEmuInt128Shr( a, shift ), perturb );

#if HAS_NATIVE
				nativeInt128 na = nativeMakeS( laneValues[ah], laneValues[al] );
				bad += CHECK_S( left, refSShl( na, shift ) );
				bad += CHECK_S( right, refSShr( na, shift ) );
#endif
				if ( !perturb ) { mixS( left ); mixS( right ); }
			}
		}
	}

	rngState = 0xB3000000ULL + (uint64_t)perturb;
	for ( uint64_t i = 0; i < samples; i++ )
	{
		uint64_t ah = nextRandom(), al = nextRandom();
		int shift = (int)( nextRandom() % 128 );
		fixEmuInt128 a = fixEmuInt128Make( ah, al );

		fixEmuInt128 left = bumpS( fixEmuInt128Shl( a, shift ), perturb );
		fixEmuInt128 right = bumpS( fixEmuInt128Shr( a, shift ), perturb );
#if HAS_NATIVE
		nativeInt128 na = nativeMakeS( ah, al );
		bad += CHECK_S( left, refSShl( na, shift ) );
		bad += CHECK_S( right, refSShr( na, shift ) );
#endif
		// Oracle-free and true on every platform: an arithmetic right shift can never
		// turn a negative value non-negative, at any count.
		if ( fixEmuInt128IsNegative( a ) && !fixEmuInt128IsNegative( fixEmuInt128Shr( a, shift ) ) ) bad++;
	}

	return bad;
}

static uint64_t sweepSignedCompare( int perturb, uint64_t samples )
{
	uint64_t bad = 0;

	for ( int ah = 0; ah < LANE_COUNT; ah++ )
	{
		for ( int al = 0; al < LANE_COUNT; al++ )
		{
			for ( int bh = 0; bh < LANE_COUNT; bh++ )
			{
				for ( int bl = 0; bl < LANE_COUNT; bl++ )
				{
					fixEmuInt128 a = fixEmuInt128Make( laneValues[ah], laneValues[al] );
					fixEmuInt128 b = bumpS( fixEmuInt128Make( laneValues[bh], laneValues[bl] ), perturb );

					int emu = ( fixEmuInt128Eq( a, b ) ? 1 : 0 ) | ( fixEmuInt128Lt( a, b ) ? 2 : 0 ) |
							  ( fixEmuInt128Gt( a, b ) ? 4 : 0 ) | ( fixEmuInt128Le( a, b ) ? 8 : 0 ) |
							  ( fixEmuInt128Ge( a, b ) ? 16 : 0 );

#if HAS_NATIVE
					nativeInt128 na = nativeMakeS( laneValues[ah], laneValues[al] );
					nativeInt128 nb = nativeMakeS( laneValues[bh], laneValues[bl] );
					bad += CHECK_64( emu, refSCmp( na, nb ) );
#endif
					if ( !perturb ) mix64( (uint64_t)emu );
				}
			}
		}
	}

	// Adjacent operands, for the reason spelled out in sweepUnsignedCompare above.
	rngState = 0xB4000000ULL + (uint64_t)perturb;
	for ( uint64_t i = 0; i < samples; i++ )
	{
		fixEmuInt128 a = fixEmuInt128Make( nextRandom(), nextRandom() );
		fixEmuInt128 b = ( i & 1 ) ? fixEmuInt128Sub( a, fixEmuInt128FromI64( 1 ) ) : a;
		fixEmuInt128 seen = bumpS( b, perturb );

		int emu = ( fixEmuInt128Eq( a, seen ) ? 1 : 0 ) | ( fixEmuInt128Lt( a, seen ) ? 2 : 0 ) |
				  ( fixEmuInt128Gt( a, seen ) ? 4 : 0 ) | ( fixEmuInt128Le( a, seen ) ? 8 : 0 ) |
				  ( fixEmuInt128Ge( a, seen ) ? 16 : 0 );
#if HAS_NATIVE
		bad += CHECK_64( emu, refSCmp( nativeMakeS( a.hi, a.lo ), nativeMakeS( b.hi, b.lo ) ) );
#endif
	}

	return bad;
}

static uint64_t sweepSignedDivMod( int perturb, uint64_t samples )
{
	uint64_t bad = 0;

	for ( int ah = 0; ah < LANE_COUNT; ah++ )
	{
		for ( int al = 0; al < LANE_COUNT; al++ )
		{
			for ( int bh = 0; bh < LANE_COUNT; bh++ )
			{
				for ( int bl = 0; bl < LANE_COUNT; bl++ )
				{
					if ( laneValues[bh] == 0 && laneValues[bl] == 0 ) continue; // undefined for native

					// INT128_MIN / -1 overflows. The emulated pair documents the wrap to
					// INT128_MIN, which is what two's complement hardware produces, but
					// native's behaviour there is undefined and on x86-64 it TRAPS -- so
					// the differential arm must not evaluate it.
					bool overflowing = laneValues[ah] == 0x8000000000000000ULL && laneValues[al] == 0 &&
									   laneValues[bh] == 0xFFFFFFFFFFFFFFFFULL && laneValues[bl] == 0xFFFFFFFFFFFFFFFFULL;

					fixEmuInt128 a = fixEmuInt128Make( laneValues[ah], laneValues[al] );
					fixEmuInt128 b = fixEmuInt128Make( laneValues[bh], laneValues[bl] );

					fixEmuInt128 quotient, remainder;
					fixEmuInt128DivMod( a, b, &quotient, &remainder );
					quotient = bumpS( quotient, perturb );
					remainder = bumpS( remainder, perturb );

					if ( !fixEmuInt128Eq( bumpS( fixEmuInt128Div( a, b ), perturb ), quotient ) ) bad++;
					if ( !fixEmuInt128Eq( bumpS( fixEmuInt128Mod( a, b ), perturb ), remainder ) ) bad++;

#if HAS_NATIVE
					if ( !overflowing )
					{
						nativeInt128 na = nativeMakeS( laneValues[ah], laneValues[al] );
						nativeInt128 nb = nativeMakeS( laneValues[bh], laneValues[bl] );
						bad += CHECK_S( quotient, refSDiv( na, nb ) );
						bad += CHECK_S( remainder, refSMod( na, nb ) );
					}
#else
					(void)overflowing;
#endif
					if ( !perturb ) { mixS( quotient ); mixS( remainder ); }
				}
			}
		}
	}

	rngState = 0xB5000000ULL + (uint64_t)perturb;
	for ( uint64_t i = 0; i < samples; i++ )
	{
		uint64_t ah = nextRandom(), al = nextRandom();
		uint64_t bh = nextRandom(), bl = nextRandom();
		if ( ( i % 3 ) == 1 ) bh = ( bl & 1 ) ? 0xFFFFFFFFFFFFFFFFULL : 0;
		if ( ( i % 3 ) == 2 ) { bh = 0; bl &= 0xFFFFFFFFULL; }
		if ( bh == 0 && bl == 0 ) bl = 1;

		fixEmuInt128 a = fixEmuInt128Make( ah, al );
		fixEmuInt128 b = fixEmuInt128Make( bh, bl );
		fixEmuInt128 quotient = bumpS( fixEmuInt128Div( a, b ), perturb );
		fixEmuInt128 remainder = bumpS( fixEmuInt128Mod( a, b ), perturb );

#if HAS_NATIVE
		nativeInt128 na = nativeMakeS( ah, al );
		nativeInt128 nb = nativeMakeS( bh, bl );
		bad += CHECK_S( quotient, refSDiv( na, nb ) );
		bad += CHECK_S( remainder, refSMod( na, nb ) );
#endif
		// Reconstruction, oracle-free: quotient * divisor + remainder is the dividend.
		fixEmuInt128 rebuilt =
			fixEmuInt128Add( fixEmuInt128Mul( fixEmuInt128Div( a, b ), b ), fixEmuInt128Mod( a, b ) );
		if ( !fixEmuInt128Eq( rebuilt, a ) ) bad++;
	}

	return bad;
}

static uint64_t sweepConversions( int perturb, uint64_t samples )
{
	uint64_t bad = 0;

	static const int64_t signedLanes[] = { 0, 1, -1, INT64_MAX, INT64_MIN, INT64_MIN + 1, INT32_MAX, INT32_MIN };
	const int signedLaneCount = (int)( sizeof( signedLanes ) / sizeof( signedLanes[0] ) );

	for ( int i = 0; i < signedLaneCount; i++ )
	{
		fixEmuInt128 fromSigned = bumpS( fixEmuInt128FromI64( signedLanes[i] ), perturb );
		fixEmuUInt128 fromSignedU = bumpU( fixEmuUInt128FromI64( signedLanes[i] ), perturb );
		fixEmuInt128 fromUnsigned = bumpS( fixEmuInt128FromU64( (uint64_t)signedLanes[i] ), perturb );
		uint64_t narrowed = bump64( (uint64_t)fixEmuInt128ToI64( fixEmuInt128FromI64( signedLanes[i] ) ), perturb );

#if HAS_NATIVE
		bad += CHECK_S( fromSigned, (nativeInt128)signedLanes[i] );
		bad += CHECK_U( fromSignedU, (nativeUInt128)(nativeInt128)signedLanes[i] );
		bad += CHECK_S( fromUnsigned, (nativeInt128)(uint64_t)signedLanes[i] );
		bad += CHECK_64( narrowed, (uint64_t)refSToI64( (nativeInt128)signedLanes[i] ) );
#endif
		if ( !perturb ) { mixS( fromSigned ); mixU( fromSignedU ); mixS( fromUnsigned ); mix64( narrowed ); }
	}

	for ( int hi = 0; hi < LANE_COUNT; hi++ )
	{
		for ( int lo = 0; lo < LANE_COUNT; lo++ )
		{
			fixEmuInt128 s = fixEmuInt128Make( laneValues[hi], laneValues[lo] );
			fixEmuUInt128 u = fixEmuToUnsigned( s );
			fixEmuInt128 roundTrip = bumpS( fixEmuToSigned( u ), perturb );

			// The lane accessors and the sign predicate, checked against the bits they
			// were built from rather than against native: there is nothing to compare.
			if ( fixEmuInt128Lo( s ) != laneValues[lo] || fixEmuInt128Hi( s ) != laneValues[hi] ) bad++;
			if ( fixEmuUInt128Lo( u ) != laneValues[lo] || fixEmuUInt128Hi( u ) != laneValues[hi] ) bad++;
			if ( fixEmuInt128IsNegative( s ) != ( ( laneValues[hi] >> 63 ) != 0 ) ) bad++;
			if ( !fixEmuInt128Eq( roundTrip, bumpS( s, perturb ) ) ) bad++;

			if ( !perturb ) { mixS( roundTrip ); }
		}
	}

	rngState = 0xC1000000ULL + (uint64_t)perturb;
	for ( uint64_t i = 0; i < samples; i++ )
	{
		int64_t v = (int64_t)nextRandom();
		if ( ( i & 1 ) == 0 ) v >>= 40;

		fixEmuInt128 widened = bumpS( fixEmuInt128FromI64( v ), perturb );
		bad += CHECK_S( widened, (nativeInt128)v );
		// Round trip, oracle-free.
		if ( fixEmuInt128ToI64( fixEmuInt128FromI64( v ) ) != v ) bad++;
	}

	return bad;
}

// ================================================================================

typedef uint64_t ( *SweepFcn )( int perturb, uint64_t samples );

// Run one sweep clean, then once per negative-control arm. Clean must find nothing and
// every control arm must find at least one mismatch per random sample -- accepting any
// non-zero count would pass a sweep whose random loop never ran.
static void run( const char* name, SweepFcn sweep, uint64_t samples, int controlArms )
{
	uint64_t clean = sweep( 0, samples );
	if ( clean != 0 )
	{
		printf( "FAIL %s: %llu mismatches\n", name, (unsigned long long)clean );
		fails++;
		return;
	}

#if HAS_NATIVE
	for ( int arm = 1; arm <= controlArms; arm++ )
	{
		uint64_t dirty = sweep( arm, samples );
		if ( dirty < samples )
		{
			printf( "FAIL %s: negative control arm %d found %llu mismatches, expected at least %llu -- "
					"part of the sweep is blind\n",
					name, arm, (unsigned long long)dirty, (unsigned long long)samples );
			fails++;
			return;
		}
	}
#else
	// No native operation to differ from, so a perturbed emulated result has nothing to
	// disagree with and the control cannot fire. On this compiler the frozen hash below
	// is what holds these sweeps, and it is the value the compilers WITH native proved
	// their own operations produce.
	(void)controlArms;
#endif

	printf( "%s\n", name );
	if ( printCounts )
	{
		printf( "        samples=%llu structured+random, control arms=%d\n", (unsigned long long)samples, controlArms );
	}
}

// The documented behaviour where native has none, checked against the values the header
// promises rather than against a compiler. These are the cases the differential sweeps
// deliberately skip, so if they were not pinned here they would not be pinned anywhere.
static void checkDocumentedEdges( void )
{
	int before = fails;

	// Shift counts outside [0, 127]: zero for the left shifts and the unsigned right
	// shift, all sign bits for the signed arithmetic right shift.
	fixEmuUInt128 u = fixEmuUInt128Make( 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL );
	fixEmuInt128 negative = fixEmuInt128Make( 0x8000000000000000ULL, 1 );
	fixEmuInt128 positive = fixEmuInt128Make( 0x7FFFFFFFFFFFFFFFULL, 1 );

	if ( !fixEmuUInt128Eq( fixEmuUInt128Shl( u, 128 ), fixEmuUInt128Make( 0, 0 ) ) ) fails++;
	if ( !fixEmuUInt128Eq( fixEmuUInt128Shl( u, -1 ), fixEmuUInt128Make( 0, 0 ) ) ) fails++;
	if ( !fixEmuUInt128Eq( fixEmuUInt128Shr( u, 128 ), fixEmuUInt128Make( 0, 0 ) ) ) fails++;
	if ( !fixEmuUInt128Eq( fixEmuUInt128Shr( u, -1 ), fixEmuUInt128Make( 0, 0 ) ) ) fails++;
	if ( !fixEmuInt128Eq( fixEmuInt128Shl( negative, 200 ), fixEmuInt128FromI64( 0 ) ) ) fails++;
	if ( !fixEmuInt128Eq( fixEmuInt128Shr( negative, 200 ), fixEmuInt128FromI64( -1 ) ) ) fails++;
	if ( !fixEmuInt128Eq( fixEmuInt128Shr( positive, 200 ), fixEmuInt128FromI64( 0 ) ) ) fails++;
	if ( !fixEmuInt128Eq( fixEmuInt128Shr( negative, -1 ), fixEmuInt128FromI64( -1 ) ) ) fails++;

	// INT128_MIN / -1 wraps to INT128_MIN with a zero remainder, the bit pattern two's
	// complement hardware produces. Native is undefined here and x86-64 traps.
	fixEmuInt128 minimum = fixEmuInt128Make( 0x8000000000000000ULL, 0 );
	fixEmuInt128 minusOne = fixEmuInt128FromI64( -1 );
	if ( !fixEmuInt128Eq( fixEmuInt128Div( minimum, minusOne ), minimum ) ) fails++;
	if ( !fixEmuInt128Eq( fixEmuInt128Mod( minimum, minusOne ), fixEmuInt128FromI64( 0 ) ) ) fails++;
	if ( !fixEmuInt128Eq( fixEmuInt128Neg( minimum ), minimum ) ) fails++;

	// Division by zero is UNDEFINED and callers may not rely on this. What is guaranteed
	// is only that it is total -- it returns rather than trapping, so a caller's mistake
	// cannot kill a process. Checked so that "total" stays true, not as a contract.
	fixEmuUInt128 zero = fixEmuUInt128Make( 0, 0 );
	if ( !fixEmuUInt128Eq( fixEmuUInt128Div( u, zero ), zero ) ) fails++;
	if ( !fixEmuUInt128Eq( fixEmuUInt128Mod( u, zero ), zero ) ) fails++;
	if ( !fixEmuInt128Eq( fixEmuInt128Div( negative, fixEmuInt128FromI64( 0 ) ), fixEmuInt128FromI64( 0 ) ) ) fails++;

	if ( fails != before )
	{
		printf( "FAIL documented edge behaviour: %d case(s) differ from the header's promise\n", fails - before );
		return;
	}

	printf( "documented edges hold: out-of-range shifts, INT128_MIN over -1, and totality on a zero divisor\n" );
}

int main( int argc, char** argv )
{
	for ( int i = 1; i < argc; i++ )
	{
		if ( strcmp( argv[i], "--counts" ) == 0 ) printCounts = 1;
	}

	clock_t started = clock();

	run( "emulated unsigned add, subtract, multiply and negate match native", sweepUnsignedArith, SAMPLES( 200000 ), 1 );
	run( "emulated unsigned widening 64x64 multiply matches native and the schoolbook form", sweepUnsignedMul64,
		 SAMPLES( 300000 ), 1 );
	run( "emulated unsigned shifts match native at every count in [0, 127]", sweepUnsignedShifts, SAMPLES( 200000 ), 1 );
	run( "emulated unsigned bitwise operations match native", sweepUnsignedBitwise, SAMPLES( 200000 ), 1 );
	run( "emulated unsigned comparisons match native", sweepUnsignedCompare, SAMPLES( 200000 ), 1 );
	run( "emulated unsigned divide and modulo match native", sweepUnsignedDivMod, SAMPLES( 20000 ), 1 );
	run( "emulated signed add, subtract, multiply and negate match native", sweepSignedArith, SAMPLES( 200000 ), 1 );
	run( "emulated signed widening 64x64 multiply matches native", sweepSignedMul64, SAMPLES( 300000 ), 1 );
	run( "emulated signed shifts match native at every count in [0, 127]", sweepSignedShifts, SAMPLES( 200000 ), 1 );
	run( "emulated signed comparisons match native", sweepSignedCompare, SAMPLES( 200000 ), 1 );
	run( "emulated signed divide and modulo match native", sweepSignedDivMod, SAMPLES( 20000 ), 1 );
	run( "emulated conversions and lane accessors match native", sweepConversions, SAMPLES( 200000 ), 1 );

	checkDocumentedEdges();

	if ( EXPECTED_INT128_HASH == 0ULL )
	{
		printf( "emulated structured sweep hash = 0x%016llx (capture mode: not yet frozen)\n", (unsigned long long)fnv );
	}
	else if ( fnv != (uint64_t)EXPECTED_INT128_HASH )
	{
		printf( "FAIL emulated structured sweep hash = 0x%016llx, expected 0x%016llx\n", (unsigned long long)fnv,
				(unsigned long long)EXPECTED_INT128_HASH );
		fails++;
	}
	else
	{
		printf( "emulated structured sweep hash is unchanged across platforms\n" );
	}

	// The elapsed time is printed on purpose. These sweeps compare header-inline code
	// against header-inline code, which is the shape an optimizer can delete outright;
	// a run that reports success in no time at all has not run.
	double elapsed = (double)( clock() - started ) / (double)CLOCKS_PER_SEC;
	printf( "int128 sweeps ran in %.2fs\n", elapsed );

	if ( fails )
	{
		printf( "FAILED: %d check(s)\n", fails );
		return 1;
	}

	return 0;
}
