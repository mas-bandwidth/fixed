// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
//
// THE SCALE ENVELOPE OF THE 3x3 INVERSE AND SOLVE.
//
// Every other suite in this repository asks whether a function is right. This one asks
// over what range of inputs it is right, and what it does past that range -- because the
// inverse is the one operation here whose correctness is a function of magnitude, and
// because the way it used to fail past that range was to return a number of arbitrary
// sign without saying anything.
//
// WHY THIS FILE EXISTS. A physics consumer inverts an inertia tensor, and inertia grows
// as the fifth power of size. Test corpora are built at human scale, so a suite can cover
// the inverse thoroughly at side 1 and never once visit side 250 -- which is a factor of
// 250 in the input and a factor of 10^12 in the cofactors. That is what happened: this
// library's inverse was wrong for every large body and no test noticed, because no test
// was large. A tolerance cannot catch that and a hash cannot catch that. Only walking the
// magnitudes catches it, so this file walks the magnitudes.
//
// THE ORACLE IS EXACT AND INDEPENDENT. For a diagonal matrix diag(d,d,d) the inverse is
// diag(1/d,1/d,1/d), and in raw Q48.16 that is exactly 2^32 / d_raw, truncated toward
// zero. That expression shares nothing with the implementation -- no cofactors, no
// determinant, no 128- or 256-bit intermediate -- so it cannot be blind to the same
// mistake. It is also an EQUALITY, not a tolerance: both arms of the inverse compute the
// same rational and truncate it the same way, so the right answer is one specific
// integer at every size, including the sizes where that integer is zero.
//
// THE THREE CONTRACTS, in the order they matter:
//
//   1. EXACT WHERE REPRESENTABLE. Below the representability line the inverse equals the
//      oracle bit for bit.
//
//   2. HONEST ZERO WHERE NOT. Q48.16 holds no value between zero and 1/65536, so the
//      inverse of a body whose inertia exceeds 65,536 units is not a small number in this
//      format -- it is no number at all. The answer is zero, and zero is the exact
//      truncation of the true ratio rather than a failure to compute it. A uniform solid
//      cube of density 1 crosses that line at about 13.1 units on a side.
//
//   3. NEVER WRONG IN SIGN. This is the contract the old wide arm broke and the reason
//      the file exists. A positive-definite tensor has a positive-definite inverse; an
//      implementation that reduces its way into an int64 cast can return a NEGATIVE
//      inverse inertia, which a solver reads as a body that accelerates against its own
//      torque. Measured before the fix, at side 250, this was not a rounding error: the
//      inverse came back negative with 1.13e9 times the true magnitude.
//
// NEGATIVE CONTROL: built a second time with -DINVERSE_NEGATIVE_CONTROL, which restores
// the reduction the old wide arm used -- a 16-bit cofactor shift accumulated through an
// int64 cast. That build MUST fail (ctest WILL_FAIL). The control is the historical bug
// itself rather than an invented perturbation, so what this suite proves is precisely
// that it would have caught the thing it was written for.

#include "fixed/fixed.h"
#include "fixed/fixed_vec.h"

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

#ifdef INVERSE_NEGATIVE_CONTROL
// The historical wide arm, restored verbatim as the control: cofactors reduced by 16
// fraction bits and accumulated into the determinant through an int64 cast. Both steps
// are silent past their range, which is the whole point -- this is what the suite has to
// be able to see.
static fixMatrix3 historicalWideInverse( fixMatrix3 m )
{
	fixInt128 c00 = fixCofactor128( m.cy.y, m.cz.z, m.cy.z, m.cz.y );
	fixInt128 c01 = fixCofactor128( m.cy.z, m.cz.x, m.cy.x, m.cz.z );
	fixInt128 c02 = fixCofactor128( m.cy.x, m.cz.y, m.cy.y, m.cz.x );
	fixInt128 c10 = fixCofactor128( m.cz.y, m.cx.z, m.cz.z, m.cx.y );
	fixInt128 c11 = fixCofactor128( m.cz.z, m.cx.x, m.cz.x, m.cx.z );
	fixInt128 c12 = fixCofactor128( m.cz.x, m.cx.y, m.cz.y, m.cx.x );
	fixInt128 c20 = fixCofactor128( m.cx.y, m.cy.z, m.cx.z, m.cy.y );
	fixInt128 c21 = fixCofactor128( m.cx.z, m.cy.x, m.cx.x, m.cy.z );
	fixInt128 c22 = fixCofactor128( m.cx.x, m.cy.y, m.cx.y, m.cy.x );

	fixInt128 limit = fixInt128ShiftLeft( fixInt128FromI64( 1 ), 62 );
	if ( fixInt128Lt( fixInt128Neg( limit ), c00 ) && fixInt128Lt( c00, limit ) && fixInt128Lt( fixInt128Neg( limit ), c10 ) &&
		 fixInt128Lt( c10, limit ) && fixInt128Lt( fixInt128Neg( limit ), c20 ) && fixInt128Lt( c20, limit ) )
	{
		return fixInvertMatrix( m );
	}

	fixInt128 det = fixInt128Add( fixInt128Add( fixInt128MulI64( m.cx.x, fixInt128ToI64( fixInt128Shr( c00, 16 ) ) ),
												fixInt128MulI64( m.cy.x, fixInt128ToI64( fixInt128Shr( c10, 16 ) ) ) ),
								  fixInt128MulI64( m.cz.x, fixInt128ToI64( fixInt128Shr( c20, 16 ) ) ) );
	if ( fixInt128Eq( det, FIX_INT128_ZERO ) )
	{
		return fixMat3_zero;
	}

	fixMatrix3 out;
	out.cx = FIX_LITERAL( fixVec3 ){ (fixed_t)fixInt128ToI64( fixInt128Div( fixInt128ShiftLeft( c00, 16 ), det ) ),
									 (fixed_t)fixInt128ToI64( fixInt128Div( fixInt128ShiftLeft( c10, 16 ), det ) ),
									 (fixed_t)fixInt128ToI64( fixInt128Div( fixInt128ShiftLeft( c20, 16 ), det ) ) };
	out.cy = FIX_LITERAL( fixVec3 ){ (fixed_t)fixInt128ToI64( fixInt128Div( fixInt128ShiftLeft( c01, 16 ), det ) ),
									 (fixed_t)fixInt128ToI64( fixInt128Div( fixInt128ShiftLeft( c11, 16 ), det ) ),
									 (fixed_t)fixInt128ToI64( fixInt128Div( fixInt128ShiftLeft( c21, 16 ), det ) ) };
	out.cz = FIX_LITERAL( fixVec3 ){ (fixed_t)fixInt128ToI64( fixInt128Div( fixInt128ShiftLeft( c02, 16 ), det ) ),
									 (fixed_t)fixInt128ToI64( fixInt128Div( fixInt128ShiftLeft( c12, 16 ), det ) ),
									 (fixed_t)fixInt128ToI64( fixInt128Div( fixInt128ShiftLeft( c22, 16 ), det ) ) };
	return out;
}
	#define fixInvertMatrix( m ) historicalWideInverse( m )
#endif

// ================================================================================
// The oracle.
// ================================================================================

// The exact inverse of a raw Q48.16 diagonal entry, truncated toward zero, computed
// without touching the code under test. 2^32 fits in int64 and so does every d this file
// builds, so this needs no wide arithmetic of its own -- which is the point. An oracle
// that needed the same 256-bit layer as the implementation would be testing that layer
// against itself.
static fixed_t exactInverseOfDiagonal( fixed_t d )
{
	return ( (int64_t)1 << 32 ) / d;
}

// The exact solve of a diagonal system, likewise: x = v / d, so raw x = v_raw * 2^16 /
// d_raw truncated toward zero. Callers keep v small enough that the numerator stays in
// int64, which every simulation value does by a wide margin.
static fixed_t exactSolveOfDiagonal( fixed_t v, fixed_t d )
{
	return ( v * ( (int64_t)1 << 16 ) ) / d;
}

static fixMatrix3 diagonal( fixed_t d )
{
	fixMatrix3 m = fixMat3_zero;
	m.cx.x = d;
	m.cy.y = d;
	m.cz.z = d;
	return m;
}

// The inertia of a uniform solid cube of side s at density 1, as a raw Q48.16 value:
// m = s^3 and I = m*s^2/6, so I = s^5/6. Built in integers scaled by 2^16 rather than
// through a double, so the input to the test is exact and the same on every platform.
// s is given in 1/2 units (so s2 = 27 means side 13.5).
static fixed_t cubeInertiaRaw( int64_t s2 )
{
	// I = s^5 / 6 with s = s2/2, so I = s2^5 / (32 * 6) = s2^5 / 192.
	// Raw = I * 2^16 = s2^5 * 65536 / 192 = s2^5 * 1024 / 3.
	fixInt128 fifth = fixInt128MulI64( s2, s2 );
	fifth = fixInt128MulI64( fixInt128ToI64( fifth ), s2 );
	// s2^3 fits in int64 for every size here; go wide only for the last two factors.
	int64_t cube = fixInt128ToI64( fifth );
	fixInt128 raw = fixInt128MulI64( cube, s2 );
	raw = fixInt128Div( fixInt128MulI64( fixInt128ToI64( raw ), s2 * 1024 ), fixInt128FromI64( 3 ) );
	return (fixed_t)fixInt128ToI64( raw );
}

// ================================================================================
// The band. One row per size, walking from well inside the representable range to well
// past it, with the two documented cliffs bracketed on both sides.
// ================================================================================

typedef struct BandCase
{
	const char* name;
	int64_t halfSides; // side * 2, so half units are exact
} BandCase;

static void testCubeBand( void )
{
	int before = fails;

	static const BandCase cases[] = {
		{ "side 1", 2 },		  { "side 5", 10 },	   { "side 11", 22 },  { "side 13", 26 },
		{ "side 13.5", 27 },	  { "side 20", 40 },   { "side 37.5", 75 }, { "side 44", 88 },
		{ "side 80", 160 },		  { "side 120", 240 }, { "side 250", 500 }, { "side 500", 1000 },
	};

	for ( int i = 0; i < (int)( sizeof( cases ) / sizeof( cases[0] ) ); i++ )
	{
		fixed_t d = cubeInertiaRaw( cases[i].halfSides );
		fixed_t expected = exactInverseOfDiagonal( d );
		fixMatrix3 inverse = fixInvertMatrix( diagonal( d ) );

		// Contract 1 and 2 together: one integer is right at every size, and past the
		// representability line that integer is zero.
		CHECK( inverse.cx.x == expected );
		CHECK( inverse.cy.y == expected );
		CHECK( inverse.cz.z == expected );

		// Contract 3: a positive-definite tensor never inverts to a negative entry, and
		// the off-diagonal of a diagonal matrix stays exactly zero.
		CHECK( inverse.cx.x >= 0 );
		CHECK( inverse.cy.y >= 0 );
		CHECK( inverse.cz.z >= 0 );
		CHECK( inverse.cx.y == 0 && inverse.cx.z == 0 );
		CHECK( inverse.cy.x == 0 && inverse.cy.z == 0 );
		CHECK( inverse.cz.x == 0 && inverse.cz.y == 0 );
	}

	// The representability line itself, stated as the library's own boundary rather than
	// as a size: the largest inertia whose inverse is a nonzero Q48.16 value is 65,536
	// units, and one quantum past it the inverse is zero. A consumer that needs to tell
	// "zero because too large" from "zero because static" tests the input against this.
	{
		fixed_t atLine = FIX( 65536.0f );
		CHECK( fixInvertMatrix( diagonal( atLine ) ).cx.x == 1 );
		CHECK( fixInvertMatrix( diagonal( atLine + 1 ) ).cx.x == 0 );
	}

	if ( fails == before ) section( "the cube band: exact where representable, zero past it, never negative" );
}

// ================================================================================
// Ship-scale boxes. The sizes a space game actually contains, and the reason the band
// above is not an abstract exercise -- these are two orders of magnitude past the line,
// where the old reduction returned entries of both signs.
// ================================================================================

static void testShipScaleBoxes( void )
{
	int before = fails;

	// A uniform solid box a x b x c at density 1: m = abc, Ix = m(b^2+c^2)/12. Computed
	// in integers, in units, then scaled -- exact for every dimension used here.
	struct
	{
		const char* name;
		int64_t a, b, c;
	} boxes[] = {
		{ "Destroyer 200x100x400", 200, 100, 400 },
		{ "Carrier 1000x250x500", 1000, 250, 500 },
	};

	for ( int i = 0; i < (int)( sizeof( boxes ) / sizeof( boxes[0] ) ); i++ )
	{
		int64_t a = boxes[i].a, b = boxes[i].b, c = boxes[i].c;
		int64_t mass = a * b * c;

		int64_t axis[3] = { b * b + c * c, a * a + c * c, a * a + b * b };
		for ( int k = 0; k < 3; k++ )
		{
			// raw = mass * axis / 12 * 65536, kept wide because mass * axis * 65536
			// leaves int64 for the carrier.
			fixInt128 raw = fixInt128MulI64( mass, axis[k] );
			raw = fixInt128MulI64( fixInt128ToI64( fixInt128Div( raw, fixInt128FromI64( 12 ) ) ), 65536 );
			fixed_t d = (fixed_t)fixInt128ToI64( raw );

			fixMatrix3 inverse = fixInvertMatrix( diagonal( d ) );
			CHECK( inverse.cx.x == exactInverseOfDiagonal( d ) );
			CHECK( inverse.cx.x >= 0 );
			// Every one of these is far past the line, so the honest answer is zero.
			CHECK( inverse.cx.x == 0 );
		}
	}

	if ( fails == before ) section( "ship-scale boxes: two orders past the line, and they say so" );
}

// ================================================================================
// The general case. A diagonal matrix exercises the determinant and one cofactor; these
// exercise all nine, including the off-column cofactors the exact arm's range test used
// to ignore.
// ================================================================================

static void testGeneralHugeMatrices( void )
{
	int before = fails;

	// Scaling a matrix by an integer k scales its inverse by 1/k exactly, which gives a
	// reference for non-diagonal inputs without a second implementation: invert the small
	// one, scale the input up, and the entries must fall the same way. Checked as an
	// inequality on magnitude, because the scaled inverse rounds.
	{
		fixMatrix3 small = { { FIX( 3.0f ), FIX( 1.0f ), FIX( 0.5f ) },
							 { FIX( 1.0f ), FIX( 4.0f ), FIX( 2.0f ) },
							 { FIX( 0.5f ), FIX( 2.0f ), FIX( 5.0f ) } };
		fixMatrix3 previous = fixInvertMatrix( small );
		CHECK( previous.cx.x > 0 );

		for ( int k = 1; k <= 12; k++ )
		{
			fixMatrix3 scaled = fixMulSM( FIX( 4.0f ), small );
			for ( int j = 1; j < k; j++ ) { scaled = fixMulSM( FIX( 4.0f ), scaled ); }

			fixMatrix3 inverse = fixInvertMatrix( scaled );

			// Monotone decreasing in magnitude, never increasing and never changing sign.
			// A wrapped cast breaks both at once, which is exactly how it was found.
			CHECK( inverse.cx.x >= 0 );
			CHECK( inverse.cy.y >= 0 );
			CHECK( inverse.cz.z >= 0 );
			CHECK( inverse.cx.x <= previous.cx.x );
			CHECK( inverse.cy.y <= previous.cy.y );
			CHECK( inverse.cz.z <= previous.cz.z );
			CHECK( fixAbs( inverse.cx.y ) <= fixAbs( previous.cx.y ) );
			CHECK( fixAbs( inverse.cy.z ) <= fixAbs( previous.cy.z ) );

			previous = inverse;
		}

		// Twelve factors of four past a matrix of order 5 is far past the line, so the
		// walk has to have arrived at zero rather than merely stayed small.
		CHECK( previous.cx.x == 0 && previous.cy.y == 0 && previous.cz.z == 0 );
	}

	// A singular matrix is zero at every scale, including scales that reach the wide arm.
	{
		fixMatrix3 singular = { { FIX( 1.0f ), FIX( 2.0f ), FIX( 3.0f ) },
								{ FIX( 2.0f ), FIX( 4.0f ), FIX( 6.0f ) },
								{ FIX( 3.0f ), FIX( 6.0f ), FIX( 9.0f ) } };
		CHECK( fixInvertMatrix( singular ).cx.x == 0 );

		fixMatrix3 hugeSingular = { { FIX( 1e8f ), FIX( 2e8f ), FIX( 3e8f ) },
									{ FIX( 2e8f ), FIX( 4e8f ), FIX( 6e8f ) },
									{ FIX( 3e8f ), FIX( 6e8f ), FIX( 9e8f ) } };
		fixMatrix3 inverse = fixInvertMatrix( hugeSingular );
		CHECK( inverse.cx.x == 0 && inverse.cy.y == 0 && inverse.cz.z == 0 );
		CHECK( inverse.cx.y == 0 && inverse.cy.z == 0 && inverse.cz.x == 0 );
	}

	// An off-column cofactor past the limit with the first-column cofactors below it.
	// This is the shape the exact arm's three-cofactor range test admitted and then
	// overflowed on: c00, c10 and c20 involve only rows y and z, so a large entry placed
	// in the first column alone leaves them small while c11, c12, c21 and c22 grow.
	{
		fixMatrix3 skewed = { { FIX( 4e9f ), FIX( 4e9f ), FIX( 4e9f ) },
							  { FIX( 1.0f ), FIX( 2.0f ), FIX( 0.5f ) },
							  { FIX( 0.5f ), FIX( 1.0f ), FIX( 3.0f ) } };
		fixMatrix3 inverse = fixInvertMatrix( skewed );
		CHECK( fixIsValidMatrix3( inverse ) );
		// The true inverse of this matrix has entries of order 1e-10 to 1e-1. Nothing in
		// it reaches the magnitude a wrapped cast produces, so a bound is enough to
		// separate the two regimes.
		CHECK( fixAbs( inverse.cx.x ) < FIX( 1.0f ) );
		CHECK( fixAbs( inverse.cy.y ) < FIX( 4.0f ) );
		CHECK( fixAbs( inverse.cz.z ) < FIX( 4.0f ) );
	}

	if ( fails == before ) section( "general matrices: monotone to zero, singular at every scale, off-column cofactors" );
}

// ================================================================================
// Solve. fixSolve3 carries the same cofactor and determinant arithmetic as the inverse
// and runs per substep in a consumer's gyroscopic step, so its envelope is the one that
// turns a bad cofactor into an angular velocity.
// ================================================================================

static void testSolveBand( void )
{
	int before = fails;

	fixVec3 target = { FIX( 1.0f ), FIX( -2.0f ), FIX( 0.5f ) };

	static const BandCase cases[] = {
		{ "side 1", 2 },	 { "side 5", 10 },	  { "side 11", 22 },  { "side 13", 26 },   { "side 13.5", 27 },
		{ "side 20", 40 },	 { "side 37.5", 75 }, { "side 44", 88 },  { "side 80", 160 },  { "side 120", 240 },
		{ "side 250", 500 }, { "side 500", 1000 },
	};

	for ( int i = 0; i < (int)( sizeof( cases ) / sizeof( cases[0] ) ); i++ )
	{
		fixed_t d = cubeInertiaRaw( cases[i].halfSides );
		fixVec3 solution = fixSolve3( diagonal( d ), target );

		// Solving a diagonal system is a componentwise divide, and the exact truncated
		// answer is one integer per component at every size. The sign of each component
		// follows the sign of the target for a positive-definite matrix, which is the
		// property a wrapped cofactor destroys.
		CHECK( solution.x == exactSolveOfDiagonal( target.x, d ) );
		CHECK( solution.y == exactSolveOfDiagonal( target.y, d ) );
		CHECK( solution.z == exactSolveOfDiagonal( target.z, d ) );
		CHECK( solution.x >= 0 );
		CHECK( solution.y <= 0 );
		CHECK( solution.z >= 0 );
	}

	// The solve of a singular system is the zero vector at every scale.
	{
		fixMatrix3 singular = { { FIX( 1.0f ), FIX( 2.0f ), FIX( 3.0f ) },
								{ FIX( 2.0f ), FIX( 4.0f ), FIX( 6.0f ) },
								{ FIX( 3.0f ), FIX( 6.0f ), FIX( 9.0f ) } };
		fixVec3 solution = fixSolve3( singular, target );
		CHECK( solution.x == 0 && solution.y == 0 && solution.z == 0 );
	}

	// Solve agrees with invert-then-multiply wherever the inverse is representable. Not
	// bit-identical -- solving divides once and multiplying rounds twice -- so this is a
	// bound, and it is here to catch an arm of one diverging from the other in kind
	// rather than in ulps.
	{
		fixMatrix3 m = { { FIX( 3.0f ), FIX( 1.0f ), FIX( 0.5f ) },
						 { FIX( 1.0f ), FIX( 4.0f ), FIX( 2.0f ) },
						 { FIX( 0.5f ), FIX( 2.0f ), FIX( 5.0f ) } };
		fixVec3 direct = fixSolve3( m, target );
		fixVec3 viaInverse = fixMulMV( fixInvertMatrix( m ), target );
		CHECK( fixAbs( direct.x - viaInverse.x ) <= 8 );
		CHECK( fixAbs( direct.y - viaInverse.y ) <= 8 );
		CHECK( fixAbs( direct.z - viaInverse.z ) <= 8 );
	}

	if ( fails == before ) section( "solve: exact componentwise divide across the band, sign preserved" );
}

// ================================================================================

int main( void )
{
	testCubeBand();
	testShipScaleBoxes();
	testGeneralHugeMatrices();
	testSolveBand();

	if ( fails )
	{
		printf( "FAILED: %d check(s)\n", fails );
		return 1;
	}

	printf( "inverse scale envelope holds from side 1 to side 500\n" );
	return 0;
}
