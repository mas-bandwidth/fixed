// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
//
// THE 256-BIT DIVISION, HELD AGAINST THE IMPLEMENTATION IT REPLACED.
//
// fixUInt256DivMod used to be shift-subtract: one iteration per bit of the dividend,
// around 200 of them, each doing 256-bit shifts, compares and subtracts. That is exact
// and it is slow, and it was slow in a place that turned out to matter -- fixed3d's
// inverse-inertia work puts a 3x3 solve of 2^40-scaled values in its per-substep joint
// path, every one of which lands on this function, and measured +503% on a joint-heavy
// scene.
//
// So it is now Knuth Algorithm D over 64-bit limbs: a handful of 128/64 divides instead
// of two hundred bit steps. Algorithm D is not hard to write and it is notoriously easy
// to write ALMOST right -- the quotient estimate, its two corrections, and the add-back
// that fires for roughly one input in 2^63 are exactly the parts that a casual test never
// visits. This file exists because of that.
//
// THE ORACLE IS THE OLD IMPLEMENTATION. It is kept here, verbatim, as
// referenceDivMod. That is the strongest oracle available for a replacement: it is
// already known-correct, it was already shipped, and it shares NO code with the thing it
// checks -- one walks bits, the other walks limbs. Every case below asserts bit equality
// of BOTH quotient and remainder against it, not merely the division identity, because
// the identity q*d + r == n with r < d is satisfied by a correct answer and by nothing
// else, but checking it alone would let a wrong q and a compensating r through if the
// multiply were wrong in the same way.
//
// AND the identity is checked too, independently, because the reference could in
// principle be wrong in a way that only a second kind of check would catch.
//
// WHAT IS SWEPT, and why each part is there:
//
//   1. HAND VECTORS at the boundaries: zero divisor, divisor 1, dividend < divisor,
//      dividend == divisor, powers of two either side of every limb boundary, and the
//      full-width extremes. These are the cases an author reasons about and therefore the
//      ones most likely to be handled by a special case that is subtly wrong.
//
//   2. LIMB-COUNT CROSS PRODUCT: every combination of significant-limb counts from 1 to 4
//      for dividend and divisor, with the top limb both large and small. Algorithm D
//      branches on the divisor's limb count (a single-limb divisor is a different path
//      entirely), and the normalization shift is driven by the top limb's leading zeros,
//      so the shift-of-0 and shift-of-63 paths need visiting deliberately.
//
//   3. THE ADD-BACK VECTORS. Constructed rather than stumbled upon: the correction step
//      fires when the two-limb quotient estimate overshoots, which needs a divisor whose
//      top limb is just above half the base and a dividend chosen to sit in the narrow
//      band above it. These are the classic Algorithm D failure vectors and they are the
//      reason this file is not just a random sweep.
//
//   4. A LARGE RANDOMIZED SWEEP from the deterministic splitmix64 stream, with operand
//      bit lengths drawn across the whole range so short/short, long/short and long/long
//      all get traffic. The hand vectors say the boundaries are right; this says the
//      middle is.
//
// NEGATIVE CONTROL: built a second time with -DFIX_INT256_NO_ADDBACK, which removes the
// add-back correction from the library. That build MUST fail (ctest WILL_FAIL), and it is
// aimed at the add-back on purpose rather than at some generic perturbation, because the
// add-back is the branch this suite was in real danger of never reaching.
//
// It was in real danger: the first version of this file carried hand-written "add-back
// vectors" that looked right and hit the branch EXACTLY ZERO TIMES. That was found by
// instrumenting the implementation and counting, not by reading the test. The vectors
// below were then searched for, and the control now keeps the coverage honest -- a sweep
// that stops reaching the branch turns the control green, and a green control is a
// failure.

#include "fixed/fixed_int256.h"

#include <stdint.h>
#include <stdio.h>

static int fails = 0;
#define CHECK( cond )                                                                                                          \
	do                                                                                                                         \
	{                                                                                                                          \
		if ( !( cond ) )                                                                                                       \
		{                                                                                                                      \
			printf( "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond );                                                           \
			fails++;                                                                                                           \
		}                                                                                                                      \
	} while ( 0 )

static void section( const char* name )
{
	printf( "%s\n", name );
}

// ================================================================================
// The oracle: the shift-subtract implementation this replaces, verbatim.
// ================================================================================

static void referenceDivMod( fixUInt256 dividend, fixUInt256 divisor, fixUInt256* quotientOut, fixUInt256* remainderOut )
{
	fixUInt256 quotient;
	quotient.hi = FIX_UINT128_ZERO;
	quotient.lo = FIX_UINT128_ZERO;
	fixUInt256 remainder = quotient;

	if ( fixUInt256IsZero( divisor ) )
	{
		*quotientOut = quotient;
		*remainderOut = remainder;
		return;
	}

	for ( int i = fixUInt256BitLength( dividend ) - 1; i >= 0; --i )
	{
		remainder = fixUInt256Shl( remainder, 1 );
		if ( fixUInt256Bit( dividend, i ) )
		{
			remainder = fixUInt256Add( remainder, fixUInt256FromU64( 1 ) );
		}
		if ( fixUInt256Ge( remainder, divisor ) )
		{
			remainder = fixUInt256Sub( remainder, divisor );
			quotient = fixUInt256Add( quotient, fixUInt256Shl( fixUInt256FromU64( 1 ), i ) );
		}
	}

	*quotientOut = quotient;
	*remainderOut = remainder;
}

// ================================================================================

static fixUInt256 make256( uint64_t l3, uint64_t l2, uint64_t l1, uint64_t l0 )
{
	fixUInt256 r;
	r.hi = fixUInt128Make( l3, l2 );
	r.lo = fixUInt128Make( l1, l0 );
	return r;
}

static fixUInt256 or256( fixUInt256 a, fixUInt256 b )
{
	fixUInt256 r;
	r.hi = fixUInt128Or( a.hi, b.hi );
	r.lo = fixUInt128Or( a.lo, b.lo );
	return r;
}

static bool eq256( fixUInt256 a, fixUInt256 b )
{
	return fixUInt128Eq( a.hi, b.hi ) && fixUInt128Eq( a.lo, b.lo );
}

static void print256( const char* label, fixUInt256 a )
{
	printf( "  %s = %016llx %016llx %016llx %016llx\n", label, (unsigned long long)fixUInt128Hi( a.hi ),
			(unsigned long long)fixUInt128Lo( a.hi ), (unsigned long long)fixUInt128Hi( a.lo ),
			(unsigned long long)fixUInt128Lo( a.lo ) );
}

// The whole of the checking, in one place: both outputs against the oracle, and the
// division identity checked independently of it.
static void checkPair( fixUInt256 n, fixUInt256 d )
{
	fixUInt256 q, r, refQ, refR;
	fixUInt256DivMod( n, d, &q, &r );
	referenceDivMod( n, d, &refQ, &refR );

	bool ok = eq256( q, refQ ) && eq256( r, refR );

	// The identity, computed without the oracle. Skipped for a zero divisor, where the
	// documented answer is zero for both and no identity applies.
	if ( ok && fixUInt256IsZero( d ) == false )
	{
		fixUInt256 product;
		product.hi = FIX_UINT128_ZERO;
		product.lo = FIX_UINT128_ZERO;

		// q * d accumulated limb by limb, wrapping is fine: a correct q * d + r cannot
		// exceed the dividend it came from.
		uint64_t ql[4] = { fixUInt128Lo( q.lo ), fixUInt128Hi( q.lo ), fixUInt128Lo( q.hi ), fixUInt128Hi( q.hi ) };
		for ( int i = 0; i < 4; i++ )
		{
			if ( ql[i] == 0 ) continue;
			fixUInt256 term = fixUInt256MulU128ByU64( d.lo, ql[i] );
			fixUInt256 termHi = fixUInt256MulU128ByU64( d.hi, ql[i] );
			term = fixUInt256Add( term, fixUInt256Shl( termHi, 128 ) );
			product = fixUInt256Add( product, fixUInt256Shl( term, 64 * i ) );
		}
		ok = ok && eq256( fixUInt256Add( product, r ), n ) && fixUInt256Lt( r, d );
	}

	if ( ok == false )
	{
		printf( "FAIL division mismatch\n" );
		print256( "n     ", n );
		print256( "d     ", d );
		print256( "q     ", q );
		print256( "q(ref)", refQ );
		print256( "r     ", r );
		print256( "r(ref)", refR );
		fails++;
	}
}

// ================================================================================
// 1. Hand vectors at the boundaries.
// ================================================================================

static void testBoundaries( void )
{
	int before = fails;

	const uint64_t MAX = UINT64_MAX;

	// A zero divisor is documented as zero quotient and zero remainder rather than a trap.
	{
		fixUInt256 q, r;
		fixUInt256DivMod( make256( 1, 2, 3, 4 ), make256( 0, 0, 0, 0 ), &q, &r );
		CHECK( fixUInt256IsZero( q ) && fixUInt256IsZero( r ) );
	}

	checkPair( make256( 0, 0, 0, 0 ), make256( 0, 0, 0, 1 ) );	// zero over one
	checkPair( make256( 0, 0, 0, 1 ), make256( 0, 0, 0, 1 ) );	// one over one
	checkPair( make256( 0, 0, 0, 5 ), make256( 0, 0, 0, 7 ) );	// dividend < divisor
	checkPair( make256( 0, 0, 0, 7 ), make256( 0, 0, 0, 7 ) );	// dividend == divisor
	checkPair( make256( MAX, MAX, MAX, MAX ), make256( 0, 0, 0, 1 ) );
	checkPair( make256( MAX, MAX, MAX, MAX ), make256( MAX, MAX, MAX, MAX ) );
	checkPair( make256( MAX, MAX, MAX, MAX ), make256( 0, 0, 0, MAX ) );
	checkPair( make256( MAX, MAX, MAX, MAX ), make256( MAX, 0, 0, 0 ) );

	// Powers of two either side of every limb boundary, over divisors of each width.
	for ( int bit = 0; bit < 256; bit++ )
	{
		fixUInt256 n = fixUInt256Shl( fixUInt256FromU64( 1 ), bit );
		checkPair( n, fixUInt256FromU64( 1 ) );
		checkPair( n, fixUInt256FromU64( 3 ) );
		checkPair( n, make256( 0, 0, 1, 0 ) );			 // 2^64
		checkPair( n, make256( 0, 1, 0, 0 ) );			 // 2^128
		checkPair( n, make256( 1, 0, 0, 0 ) );			 // 2^192
		checkPair( fixUInt256Sub( n, fixUInt256FromU64( 1 ) ), make256( 0, 0, 1, 0 ) );
		checkPair( fixUInt256Sub( n, fixUInt256FromU64( 1 ) ), make256( 0, 1, 0, 1 ) );
	}

	// A divisor whose top limb already has its top bit set needs a normalization shift of
	// zero; one with a single significant bit needs the maximum of 63. Both paths, at
	// every limb width.
	for ( int limbs = 1; limbs <= 4; limbs++ )
	{
		uint64_t l[4] = { 0, 0, 0, 0 };
		l[limbs - 1] = (uint64_t)1 << 63; // shift 0
		checkPair( make256( MAX, MAX, MAX, MAX ), make256( l[3], l[2], l[1], l[0] ) );
		l[limbs - 1] = 1; // shift 63
		checkPair( make256( MAX, MAX, MAX, MAX ), make256( l[3], l[2], l[1], l[0] ) );
		l[limbs - 1] = 3;
		checkPair( make256( MAX, MAX, MAX, MAX ), make256( l[3], l[2], l[1], l[0] ) );
	}

	if ( fails == before ) section( "boundaries: zero divisor, degenerate ratios, every limb boundary, both shift extremes" );
}

// ================================================================================
// 2. The limb-count cross product.
// ================================================================================

static void testLimbCounts( void )
{
	int before = fails;

	static const uint64_t patterns[] = { 1, 2, 0x8000000000000000ULL, UINT64_MAX, 0x0123456789ABCDEFULL, 0xFFFFFFFF00000000ULL };
	const int patternCount = (int)( sizeof( patterns ) / sizeof( patterns[0] ) );

	for ( int nLimbs = 1; nLimbs <= 4; nLimbs++ )
	{
		for ( int dLimbs = 1; dLimbs <= 4; dLimbs++ )
		{
			for ( int pn = 0; pn < patternCount; pn++ )
			{
				for ( int pd = 0; pd < patternCount; pd++ )
				{
					uint64_t nl[4] = { 0, 0, 0, 0 };
					uint64_t dl[4] = { 0, 0, 0, 0 };
					for ( int i = 0; i < nLimbs; i++ ) { nl[i] = patterns[( pn + i ) % patternCount]; }
					for ( int i = 0; i < dLimbs; i++ ) { dl[i] = patterns[( pd + i ) % patternCount]; }
					nl[nLimbs - 1] = patterns[pn]; // keep the top limb significant
					dl[dLimbs - 1] = patterns[pd];

					checkPair( make256( nl[3], nl[2], nl[1], nl[0] ), make256( dl[3], dl[2], dl[1], dl[0] ) );
				}
			}
		}
	}

	if ( fails == before ) section( "limb counts: every dividend/divisor width against every top-limb shape" );
}

// ================================================================================
// 3. The add-back vectors.
// ================================================================================

static fixUInt256 mul256ByLimb( fixUInt256 a, uint64_t b )
{
	fixUInt256 low = fixUInt256MulU128ByU64( a.lo, b );
	fixUInt256 high = fixUInt256MulU128ByU64( a.hi, b );
	return fixUInt256Add( low, fixUInt256Shl( high, 128 ) );
}

static uint64_t nextRandom( void );

static void testAddBack( void )
{
	int before = fails;

	const uint64_t MAX = UINT64_MAX;
	const uint64_t HALF = 0x8000000000000000ULL;

	// EIGHT VECTORS THAT ACTUALLY REACH THE ADD-BACK, found by instrumenting the
	// implementation and searching, not by reasoning about what ought to trigger it. An
	// earlier version of this file carried hand-written "add-back vectors" that looked
	// plausible and reached the branch exactly zero times -- which is why the search
	// happened, and why these are pinned literally rather than only regenerated.
	static const uint64_t vectors[][8] = {
		{ 0x80000000000067c1ULL, 0x7fffffffffbc1972ULL, 0x0000000000008e6eULL, 0x0000000000004868ULL,
		  0x0000000000000000ULL, 0x8000000000006814ULL, 0xffffffffffffff24ULL, 0xffffffffffffff91ULL },
		{ 0x800000000000d3c2ULL, 0x7fffffffffc39320ULL, 0xffffffffffffffcdULL, 0x0000000000000e8aULL,
		  0x0000000000000000ULL, 0x800000000000d3e6ULL, 0xffffffffffffffffULL, 0xffffffffffffffcdULL },
		{ 0x800000000000ee42ULL, 0xffffffffff7615f7ULL, 0x0000000000004ce3ULL, 0x0000000000000093ULL,
		  0x0000000000000000ULL, 0x800000000000ee8cULL, 0xffffffffffffff7aULL, 0xffffffffffffffffULL },
		{ 0x8000000000001048ULL, 0xfffffffffff0f717ULL, 0xffffffffffffffffULL, 0x00000000000000e5ULL,
		  0x0000000000000000ULL, 0x80000000000010bbULL, 0xffffffffffffffffULL, 0xffffffffffffffffULL },
		{ 0x8000000000009367ULL, 0x7fffffffff91bddeULL, 0x0000000000007dd1ULL, 0x0000000000003439ULL,
		  0x0000000000000000ULL, 0x80000000000093c6ULL, 0xffffffffffffff56ULL, 0xffffffffffffffbaULL },
		{ 0x800000000000a893ULL, 0x7fffffffff74b778ULL, 0xffffffffffffff33ULL, 0x000000000000a8f6ULL,
		  0x0000000000000000ULL, 0x800000000000a8fcULL, 0xffffffffffffffffULL, 0xffffffffffffff33ULL },
		{ 0x8000000000004e45ULL, 0xffffffffffb769b5ULL, 0x000000000000e763ULL, 0x00000000000000ebULL,
		  0x0000000000000000ULL, 0x8000000000004ebbULL, 0xffffffffffffff04ULL, 0xffffffffffffffffULL },
		{ 0x800000000000abcbULL, 0xffffffffff566727ULL, 0xffffffffffffffffULL, 0x00000000000000fbULL,
		  0x0000000000000000ULL, 0x800000000000ac49ULL, 0xffffffffffffffffULL, 0xffffffffffffffffULL },
	};

	for ( int i = 0; i < (int)( sizeof( vectors ) / sizeof( vectors[0] ) ); i++ )
	{
		checkPair( make256( vectors[i][0], vectors[i][1], vectors[i][2], vectors[i][3] ),
				   make256( vectors[i][4], vectors[i][5], vectors[i][6], vectors[i][7] ) );
	}

	// AND THE SHAPE THEY CAME FROM, generated, so the path keeps getting traffic if the
	// implementation changes which inputs land on it. The add-back needs the estimate's
	// error hidden below the two limbs the correction inspects: a divisor whose top limb
	// is barely over half the base and whose lower limbs are near the maximum, divided
	// into q*divisor + (divisor - 1) for a large q.
	for ( int trial = 0; trial < 4000; trial++ )
	{
		uint64_t v2 = HALF + ( nextRandom() & 0xFFFF );
		uint64_t v1 = ( trial & 1 ) ? MAX : MAX - ( nextRandom() & 0xFF );
		uint64_t v0 = ( trial & 2 ) ? MAX : MAX - ( nextRandom() & 0xFF );
		fixUInt256 d = make256( 0, v2, v1, v0 );

		uint64_t q = MAX - ( nextRandom() & 0xFF );
		fixUInt256 n = fixUInt256Add( mul256ByLimb( d, q ), fixUInt256Sub( d, fixUInt256FromU64( 1 ) ) );
		checkPair( n, d );
	}

	// The estimate-overshoot neighbourhood more generally: a top limb just over half the
	// base with maximal lower limbs, across the whole dividend range.
	checkPair( make256( 0, HALF, 0, 0 ), make256( 0, 0, HALF, MAX ) );
	checkPair( make256( MAX, MAX, MAX, MAX ), make256( 0, HALF, MAX, MAX ) );
	checkPair( make256( HALF, MAX, MAX, MAX ), make256( 0, HALF, 0, 1 ) );

	if ( fails == before ) section( "add-back: eight vectors that reach it, plus the shape they were found in" );
}

// ================================================================================
// 4. The randomized sweep.
// ================================================================================

static uint64_t rngState = 0x9E3779B97F4A7C15ULL;
static uint64_t nextRandom( void )
{
	rngState += 0x9E3779B97F4A7C15ULL;
	uint64_t z = rngState;
	z = ( z ^ ( z >> 30 ) ) * 0xBF58476D1CE4E5B9ULL;
	z = ( z ^ ( z >> 27 ) ) * 0x94D049BB133111EBULL;
	return z ^ ( z >> 31 );
}

// A value with roughly the requested bit length, so the sweep covers short/short,
// long/short and long/long rather than clustering at full width.
static fixUInt256 randomOfBitLength( int bits )
{
	fixUInt256 v = make256( nextRandom(), nextRandom(), nextRandom(), nextRandom() );
	if ( bits <= 0 )
	{
		return fixUInt256FromU64( 0 );
	}
	if ( bits < 256 )
	{
		v = fixUInt256Shr( v, 256 - bits );
		v = or256( v, fixUInt256Shl( fixUInt256FromU64( 1 ), bits - 1 ) );
	}
	return v;
}

static void testRandomSweep( void )
{
	int before = fails;

	for ( int i = 0; i < 20000; i++ )
	{
		int nBits = (int)( nextRandom() % 257 );
		int dBits = (int)( nextRandom() % 257 );
		fixUInt256 n = randomOfBitLength( nBits );
		fixUInt256 d = randomOfBitLength( dBits );
		checkPair( n, d );
	}

	// And a pass weighted to the shape this library actually divides: a wide numerator
	// over a divisor of similar width, which is what the matrix inverse produces.
	for ( int i = 0; i < 20000; i++ )
	{
		int dBits = 100 + (int)( nextRandom() % 120 );
		int nBits = dBits + (int)( nextRandom() % 64 );
		if ( nBits > 256 ) nBits = 256;
		checkPair( randomOfBitLength( nBits ), randomOfBitLength( dBits ) );
	}

	if ( fails == before ) section( "randomized sweep: 40,000 pairs across the whole bit-length range" );
}

// ================================================================================

int main( void )
{
	testBoundaries();
	testLimbCounts();
	testAddBack();
	testRandomSweep();

	if ( fails )
	{
		printf( "FAILED: %d check(s)\n", fails );
		return 1;
	}

	printf( "256-bit division matches the shift-subtract reference on every case\n" );
	return 0;
}
