// SPDX-License-Identifier: MIT
// fixAtan2Exact and the exact quaternion angles against CORE-MATH cr_atan2.
//
// fixAtan2Exact must be within 1 raw of the correctly rounded atan2 and monotone; the four exact
// quaternion angles (fixGetQuatAngleExact, fixGetAxisAngleExact, fixGetTwistAngleExact,
// fixGetSwingAngleExact) within 2 raw of the correctly rounded 2 * atan2 of their exact inputs.
//
// THE REFERENCE. cr_atan2 of the raws as doubles (the ratio is scale-free, so the raws need no
// scaling), times 65536 (exact), rounded to nearest-even by llrint in the default rounding mode.
// A raw above 2^53 in magnitude has its low bits cleared to 53 significant bits first, so it
// converts exactly; the library sees the same cleared raw, except in piece (c), where the library
// gets INT64_MIN and +-INT64_MAX themselves and the reference their policy values cleared (a
// relative change under 2^-52, under 2^-36 raw of angle). INT64_MIN is taken as -INT64_MAX.
// For the quaternion angles the lengths are sqrt( exact sum of squares ), the sums taken in
// unsigned 128-bit integers and converted once. For unit quaternions (components within 2^17 raw)
// the sums are below 2^36, so they convert exactly; the double sqrt and cr_atan2 are each within
// half an ulp of 2^-52 relative, and 2 * atan2 * 2^16 < 2^19, so the double reference is within
// 2^19 * 2^-51 = 2^-32 raw of the exact value: well under 1/16 raw.
//
// PIECES. (a) atan2 on every pair of raws in [-1024, 1024)^2, with monotonicity reversals counted
// along every row and column; (b) 2^20 seeded pairs in raw +-2^20 and 2^18 in raw +-2^62; (c) the
// axes, the diagonals and +-1 raw around each at a ladder of magnitudes, and INT64_MIN and
// +-INT64_MAX in either argument; (d) 2^18 seeded unit quaternions (four random raws in +-65536,
// normalized by fixNormalizeQuat) for each angle function, plus the identity, its negation and
// quaternions within 16 raw of a half turn.
//
// A reversal is a step along a row or column where the reference strictly rises and the library
// strictly falls, or the reverse; the branch cut at y = 0, x < 0 jumps the same way in both.
//
//   default                                  the exact functions; must pass
//   QUAT_ANGLES_EXACT_NEGATIVE_CONTROL       fixAtan2 and the four frozen angle functions; must fail
//
// The output hash (FNV-1a over every library result, its own accumulator, never mixed into another
// test's) is frozen below, so the native and emulated 128-bit arms and gcc and clang are held to
// the same bits. Seeded inputs use xorshift64 from 0x9E3779B97F4A7C15. No clock, no network, no
// file.
#include "fixed/fixed_vec.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

double cr_atan2( double y, double x );

#if defined( QUAT_ANGLES_EXACT_NEGATIVE_CONTROL )
	#define LIB_ATAN2( y, x ) fixAtan2( y, x )
	#define LIB_QUAT_ANGLE( q ) fixGetQuatAngle( q )
	#define LIB_AXIS_ANGLE( r, q ) fixGetAxisAngle( r, q )
	#define LIB_TWIST_ANGLE( q ) fixGetTwistAngle( q )
	#define LIB_SWING_ANGLE( q ) fixGetSwingAngle( q )
	#define SET_NAME "frozen fixAtan2 and angles (negative control)"
#else
	#define LIB_ATAN2( y, x ) fixAtan2Exact( y, x )
	#define LIB_QUAT_ANGLE( q ) fixGetQuatAngleExact( q )
	#define LIB_AXIS_ANGLE( r, q ) fixGetAxisAngleExact( r, q )
	#define LIB_TWIST_ANGLE( q ) fixGetTwistAngleExact( q )
	#define LIB_SWING_ANGLE( q ) fixGetSwingAngleExact( q )
	#define SET_NAME "exact"
#endif

#define ATAN2_BOUND 1
#define ANGLE_BOUND 2

static uint64_t rng_state = 0x9E3779B97F4A7C15ULL;

static uint64_t rng_next( void )
{
	uint64_t x = rng_state;
	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	rng_state = x;
	return x;
}

static uint64_t fnv = 0xCBF29CE484222325ULL;

static void mix( int64_t v )
{
	for ( int i = 0; i < 8; i++ )
	{
		fnv ^= (uint64_t)( v >> ( 8 * i ) ) & 0xFF;
		fnv *= 0x00000100000001B3ULL;
	}
}

// INT64_MIN policy: taken as -INT64_MAX.
static int64_t policy( int64_t raw )
{
	return raw == INT64_MIN ? -INT64_MAX : raw;
}

// Leave at most 53 significant bits, so the raw converts to binary64 exactly. raw != INT64_MIN.
static int64_t exact_raw( int64_t raw )
{
	if ( raw == 0 )
	{
		return 0;
	}
	uint64_t mag = raw < 0 ? (uint64_t)( -raw ) : (uint64_t)raw;
	int bits = 64 - __builtin_clzll( mag );
	if ( bits > 53 )
	{
		mag &= ~( ( (uint64_t)1 << ( bits - 53 ) ) - 1 );
	}
	return raw < 0 ? -(int64_t)mag : (int64_t)mag;
}

static int64_t round_ref( double radians )
{
	return (int64_t)llrint( radians * 65536.0 );
}

typedef struct row
{
	int64_t inputs;
	int64_t maxulp;
	int64_t reversals;
	int64_t lib;
	int64_t ref;
	int have;
	int isQuat;
	int64_t in[4];
} row;

static int fails = 0;

// The inputs are kept as numbers and printed only for the worst one, so the sweep stays cheap.
static void row_add( row* r, int64_t lib, int64_t ref, int isQuat, int64_t a, int64_t b, int64_t c, int64_t d4 )
{
	mix( lib );
	r->inputs++;
	int64_t d = lib - ref;
	if ( d < 0 )
	{
		d = -d;
	}
	if ( d > r->maxulp || !r->have )
	{
		r->maxulp = d;
		r->lib = lib;
		r->ref = ref;
		r->have = 1;
		r->isQuat = isQuat;
		r->in[0] = a;
		r->in[1] = b;
		r->in[2] = c;
		r->in[3] = d4;
	}
}

static void row_worst( const row* r, char* out, size_t n )
{
	if ( r->isQuat )
	{
		snprintf( out, n, "q=(%lld,%lld,%lld,%lld)", (long long)r->in[0], (long long)r->in[1], (long long)r->in[2],
				  (long long)r->in[3] );
	}
	else
	{
		snprintf( out, n, "(y=%lld,x=%lld)", (long long)r->in[0], (long long)r->in[1] );
	}
}

static void row_print( const char* name, const char* piece, const row* r, int bound )
{
	char worst[160];
	row_worst( r, worst, sizeof worst );
	printf( "core-math row=%s[%s] inputs=%lld maxulp=%lld bound=%d reversals=%lld worst=%s\n", name, piece,
			(long long)r->inputs, (long long)r->maxulp, bound, (long long)r->reversals, worst );
	if ( r->maxulp > bound || r->reversals != 0 )
	{
		printf( "quat angles exact FAIL row=%s[%s] input=%s lib=%lld ref=%lld maxulp=%lld bound=%d reversals=%lld\n", name,
				piece, worst, (long long)r->lib, (long long)r->ref, (long long)r->maxulp, bound,
				(long long)r->reversals );
		fails++;
	}
}

// atan2 on a pair the library sees as (y, x); the reference sees the policy values, cleared.
static void check_atan2( row* r, int64_t y, int64_t x, int64_t* libOut, double* refOut )
{
	int64_t lib = LIB_ATAN2( y, x );
	double ref = cr_atan2( (double)exact_raw( policy( y ) ), (double)exact_raw( policy( x ) ) );
	row_add( r, lib, round_ref( ref ), 0, y, x, 0, 0 );
	if ( libOut )
	{
		*libOut = lib;
		*refOut = ref;
	}
}

static int opposite( double refStep, int64_t libStep )
{
	return ( refStep > 0 && libStep < 0 ) || ( refStep < 0 && libStep > 0 );
}

static double length_of( unsigned __int128 sumOfSquares )
{
	return sqrt( (double)sumOfSquares );
}

static unsigned __int128 sq( int64_t v )
{
	return (unsigned __int128)( (__int128)v * v );
}

static row quatRow, axisRow, twistRow, swingRow;
static int axisMismatch = 0;

static void check_quat( fixQuat q )
{
	double vlen = length_of( sq( q.v.x ) + sq( q.v.y ) + sq( q.v.z ) );
	int64_t angleRef = round_ref( 2.0 * cr_atan2( vlen, (double)q.s ) );
	row_add( &quatRow, LIB_QUAT_ANGLE( q ), angleRef, 1, q.v.x, q.v.y, q.v.z, q.s );

	fixed_t radians = 0;
	fixVec3 axis = LIB_AXIS_ANGLE( &radians, q );
	row_add( &axisRow, radians, angleRef, 1, q.v.x, q.v.y, q.v.z, q.s );
	fixed_t frozenRadians = 0;
	fixVec3 frozenAxis = fixGetAxisAngle( &frozenRadians, q );
	if ( axis.x != frozenAxis.x || axis.y != frozenAxis.y || axis.z != frozenAxis.z )
	{
		axisMismatch++;
	}
	mix( axis.x );
	mix( axis.y );
	mix( axis.z );

	int64_t z = q.v.z, s = q.s;
	if ( s < 0 )
	{
		z = -z;
		s = -s;
	}
	row_add( &twistRow, LIB_TWIST_ANGLE( q ), round_ref( 2.0 * cr_atan2( (double)z, (double)s ) ), 1, q.v.x, q.v.y, q.v.z, q.s );

	double y = length_of( sq( q.v.x ) + sq( q.v.y ) );
	double x = length_of( sq( q.v.z ) + sq( q.s ) );
	row_add( &swingRow, LIB_SWING_ANGLE( q ), round_ref( 2.0 * cr_atan2( y, x ) ), 1, q.v.x, q.v.y, q.v.z, q.s );
}

int main( void )
{
	printf( "functions: %s\n", SET_NAME );

	// (a) every pair of raws in [-1024, 1024)^2, reversals along rows (x varies) and columns (y varies).
	{
		enum
		{
			N = 2048
		};
		static int64_t prevLib[N];
		static double prevRef[N];
		row r = { 0 };
		for ( int iy = 0; iy < N; iy++ )
		{
			int64_t y = iy - 1024;
			int64_t leftLib = 0;
			double leftRef = 0;
			for ( int ix = 0; ix < N; ix++ )
			{
				int64_t x = ix - 1024;
				int64_t lib;
				double ref;
				check_atan2( &r, y, x, &lib, &ref );
				if ( ix > 0 && opposite( ref - leftRef, lib - leftLib ) )
				{
					r.reversals++;
				}
				if ( iy > 0 && opposite( ref - prevRef[ix], lib - prevLib[ix] ) )
				{
					r.reversals++;
				}
				leftLib = lib;
				leftRef = ref;
				prevLib[ix] = lib;
				prevRef[ix] = ref;
			}
		}
		row_print( "atan2", "a", &r, ATAN2_BOUND );
	}

	// (b) seeded pairs: 2^20 in raw [-2^20, 2^20), 2^18 in raw [-2^62, 2^62) (cleared to 53 bits).
	{
		row r = { 0 };
		for ( int i = 0; i < ( 1 << 20 ); i++ )
		{
			int64_t y = (int64_t)rng_next() >> 43;
			int64_t x = (int64_t)rng_next() >> 43;
			check_atan2( &r, y, x, NULL, NULL );
		}
		row_print( "atan2", "b20", &r, ATAN2_BOUND );

		row w = { 0 };
		for ( int i = 0; i < ( 1 << 18 ); i++ )
		{
			int64_t y = exact_raw( (int64_t)rng_next() >> 1 );
			int64_t x = exact_raw( (int64_t)rng_next() >> 1 );
			check_atan2( &w, y, x, NULL, NULL );
		}
		row_print( "atan2", "b62", &w, ATAN2_BOUND );
	}

	// (c) the axes, the diagonals and +-1 raw around each at a ladder of magnitudes; then INT64_MIN
	// and +-INT64_MAX in either argument against a set of partners.
	{
		row r = { 0 };
		const int64_t ladder[] = { 1,
								   2,
								   3,
								   7,
								   1000,
								   65535,
								   65536,
								   65537,
								   (int64_t)1 << 31,
								   ( (int64_t)1 << 47 ) - 1,
								   (int64_t)1 << 52,
								   ( (int64_t)1 << 53 ) + 1,
								   (int64_t)1 << 62,
								   INT64_MAX - 1,
								   INT64_MAX };
		const int nl = (int)( sizeof ladder / sizeof ladder[0] );
		for ( int i = 0; i < nl; i++ )
		{
			int64_t m = ladder[i];
			for ( int sy = -1; sy <= 1; sy += 2 )
			{
				for ( int sx = -1; sx <= 1; sx += 2 )
				{
					for ( int d = -1; d <= 1; d++ )
					{
						// the axes and +-1 raw off them
						check_atan2( &r, d, sx * m, NULL, NULL );
						check_atan2( &r, sy * m, d, NULL, NULL );
						// the diagonals and +-1 raw off them (m + 1 stays in range: m < INT64_MAX there)
						if ( d <= 0 || m < INT64_MAX )
						{
							check_atan2( &r, sy * m, sx * ( m + d ), NULL, NULL );
							check_atan2( &r, sy * ( m + d ), sx * m, NULL, NULL );
						}
					}
				}
			}
		}
		const int64_t edges[] = { INT64_MIN, -INT64_MAX, INT64_MAX, INT64_MAX - 1, INT64_MIN + 1, 0, 1, -1, 65536, -65536, (int64_t)1 << 40, -( (int64_t)1 << 40 ) };
		const int ne = (int)( sizeof edges / sizeof edges[0] );
		for ( int i = 0; i < ne; i++ )
		{
			for ( int j = 0; j < ne; j++ )
			{
				check_atan2( &r, edges[i], edges[j], NULL, NULL );
			}
		}
		row_print( "atan2", "c", &r, ATAN2_BOUND );
	}

	// (d) unit quaternions for each angle function, then the identity, its negation and
	// quaternions within 16 raw of a half turn.
	{
		for ( int i = 0; i < ( 1 << 18 ); i++ )
		{
			fixQuat q;
			q.v.x = (int64_t)rng_next() >> 47;
			q.v.y = (int64_t)rng_next() >> 47;
			q.v.z = (int64_t)rng_next() >> 47;
			q.s = (int64_t)rng_next() >> 47;
			check_quat( fixNormalizeQuat( q ) );
		}
		row_print( "quatangle", "d", &quatRow, ANGLE_BOUND );
		row_print( "axisangle", "d", &axisRow, ANGLE_BOUND );
		row_print( "twistangle", "d", &twistRow, ANGLE_BOUND );
		row_print( "swingangle", "d", &swingRow, ANGLE_BOUND );

		memset( &quatRow, 0, sizeof quatRow );
		memset( &axisRow, 0, sizeof axisRow );
		memset( &twistRow, 0, sizeof twistRow );
		memset( &swingRow, 0, sizeof swingRow );
		check_quat( fixQuat_identity );
		check_quat( fixNegateQuat( fixQuat_identity ) );
		const fixVec3 axes[] = { { FIX_ONE, 0, 0 }, { 0, FIX_ONE, 0 }, { 0, 0, FIX_ONE }, { 46341, 46341, 0 }, { 37837, 37837, 37837 } };
		for ( int a = 0; a < 5; a++ )
		{
			for ( int ds = -16; ds <= 16; ds++ )
			{
				for ( int dv = -16; dv <= 16; dv += 8 )
				{
					fixQuat q = { { axes[a].x + ( axes[a].x != 0 ? dv : 0 ), axes[a].y, axes[a].z + ( axes[a].z != 0 ? -dv : 0 ) }, ds };
					check_quat( q );
					check_quat( fixNegateQuat( q ) );
				}
			}
		}
		row_print( "quatangle", "d-edges", &quatRow, ANGLE_BOUND );
		row_print( "axisangle", "d-edges", &axisRow, ANGLE_BOUND );
		row_print( "twistangle", "d-edges", &twistRow, ANGLE_BOUND );
		row_print( "swingangle", "d-edges", &swingRow, ANGLE_BOUND );
		if ( axisMismatch != 0 )
		{
			printf( "quat angles exact FAIL axis differs from fixGetAxisAngle on %d quaternions\n", axisMismatch );
			fails++;
		}
	}

	printf( "quat angles exact hash=0x%016llx\n", (unsigned long long)fnv );
	if ( fails != 0 )
	{
		return 1;
	}
#if !defined( QUAT_ANGLES_EXACT_NEGATIVE_CONTROL )
	// Checked after the bounds, so an accuracy failure reports its worst input first.
	// FROZEN 2026-10-18 (fixed-quat-angles-exact): identical from gcc and clang, native and
	// emulated 128-bit, on x86-64 Linux. A different value is a determinism finding, not a re-freeze.
	if ( fnv != 0x9326db644ed827a3ULL )
	{
		printf( "quat angles exact FAIL determinism hash 0x%016llx, frozen 0x9326db644ed827a3\n", (unsigned long long)fnv );
		return 1;
	}
#endif
	printf( "quat angles exact test passed\n" );
	return 0;
}
