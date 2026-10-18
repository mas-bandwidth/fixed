// SPDX-License-Identifier: MIT
// Exact axis-angle quaternion against the CORE-MATH correctly-rounded oracle.
#include "fixed/fixed_vec.h"
#include <fenv.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

double cr_sin( double x );
double cr_cos( double x );

#define main sincos_core_math_main
#include "sincos_core_math_test.c"
#undef main

#define QUAT_BOUND 2
#define SAMPLE_COUNT ( 1u << 18 )

static uint64_t axisangle_rng = UINT64_C( 0x9E3779B97F4A7C15 );
static uint64_t axisangle_next( void )
{
	uint64_t x = axisangle_rng;
	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	axisangle_rng = x;
	return x;
}

static int64_t round_even_ld( long double x )
{
	long double f = floorl( x );
	long double d = x - f;
	if ( d > 0.5L || ( d == 0.5L && fmodl( f, 2.0L ) != 0.0L ) ) f += 1.0L;
	return (int64_t)f;
}

static int64_t diff_i64( int64_t a, int64_t b ) { return a > b ? a - b : b - a; }

typedef struct sample_row
{
	uint64_t count;
	int64_t maxulp;
	fixVec3 axis;
	fixed_t radians;
	int64_t got[4], want[4];
} sample_row;

static int64_t round_q62_axis( int64_t sineQ62, fixed_t axisRaw )
{
	fixInt128 p = fixInt128MulI64( sineQ62, axisRaw );
	bool neg = fixInt128IsNegative( p );
	fixUInt128 u = fixInt128ToUnsigned( neg ? fixInt128Neg( p ) : p );
	fixUInt128 q = fixUInt128Shr( u, 62 );
	uint64_t rem = fixUInt128Lo( u ) & ((UINT64_C(1) << 62) - 1);
	uint64_t half = UINT64_C(1) << 61;
	if ( rem > half || ( rem == half && ( fixUInt128Lo( q ) & 1 ) ) ) q = fixUInt128Add( q, fixUInt128FromU64( 1 ) );
	fixInt128 signedQ = fixInt128FromUnsigned( q );
	if ( neg ) signedQ = fixInt128Neg( signedQ );
	return fixInt128ToI64( signedQ );
}

static fixQuat make( fixVec3 axis, fixed_t radians )
{
#ifdef QUAT_AXIS_ANGLE_EXACT_NEGATIVE_CONTROL
	return fixMakeQuatFromAxisAngle( axis, radians );
#else
	return fixMakeQuatFromAxisAngleExact( axis, radians );
#endif
}

static void check_one( sample_row* row, fixVec3 axis, fixed_t radians )
{
	fixed_t oracleRaw = exact_raw( radians );
	double half = (double)oracleRaw / 131072.0;
	long double sr = (long double)cr_sin( half );
	long double cr = (long double)cr_cos( half );
	int64_t want[4] = { round_even_ld( sr * axis.x ), round_even_ld( sr * axis.y ),
					  round_even_ld( sr * axis.z ), round_even_ld( cr * 65536.0L ) };
	radians = (fixed_t)oracleRaw;
	fixQuat q = make( axis, radians );
	int64_t got[4] = { q.v.x, q.v.y, q.v.z, q.s };
	int64_t ulp = 0;
	for ( int i = 0; i < 4; i++ ) if ( diff_i64( got[i], want[i] ) > ulp ) ulp = diff_i64( got[i], want[i] );
	row->count++;
	if ( ulp > row->maxulp )
	{
		row->maxulp = ulp; row->axis = axis; row->radians = radians;
		memcpy( row->got, got, sizeof got ); memcpy( row->want, want, sizeof want );
	}
}

static fixVec3 random_unit_axis( void )
{
	const fixed_t range = (fixed_t)64 << 16;
	fixVec3 v = { (fixed_t)(axisangle_next() % (uint64_t)(2 * range + 1)) - range,
				  (fixed_t)(axisangle_next() % (uint64_t)(2 * range + 1)) - range,
				  (fixed_t)(axisangle_next() % (uint64_t)(2 * range + 1)) - range };
	if ( v.x == 0 && v.y == 0 && v.z == 0 ) v.x = 1;
	return fixNormalize( v );
}

static fixed_t random_uniform_angle( void )
{
	const fixed_t range = (fixed_t)64 << 16;
	return (fixed_t)( axisangle_next() % (uint64_t)( 2 * range + 1 ) ) - range;
}

static void print_axisangle_row( const char* piece, const sample_row* r )
{
	printf( "core-math row=axisangleexact[%s] inputs=%llu maxulp=%lld bound=%d worst=<%lld,%lld,%lld,%lld>\n",
			piece, (unsigned long long)r->count, (long long)r->maxulp, QUAT_BOUND,
			(long long)r->axis.x, (long long)r->axis.y, (long long)r->axis.z, (long long)r->radians );
	if ( r->maxulp > QUAT_BOUND )
	{
		printf( "axisangleexact FAIL got=<%lld,%lld,%lld,%lld> want=<%lld,%lld,%lld,%lld>\n",
				(long long)r->got[0], (long long)r->got[1], (long long)r->got[2], (long long)r->got[3],
				(long long)r->want[0], (long long)r->want[1], (long long)r->want[2], (long long)r->want[3] );
	}
}

static int run( void )
{
	int failed = 0;
	fesetround( FE_TONEAREST );
	// Uniform angles use 2^18 independently generated, normalized axes.
	{
		sample_row r = { 0 };
		for ( uint32_t i = 0; i < SAMPLE_COUNT; i++ ) check_one( &r, random_unit_axis(), random_uniform_angle() );
		print_axisangle_row( "uniform", &r ); failed |= r.maxulp > QUAT_BOUND;
	}
	// Raw neighborhoods of every k*pi, |k| <= 16, with reproducible axes.
	{
		sample_row r = { 0 };
		const int64_t piRaw = INT64_C(205887);
		for ( int k = -16; k <= 16; k++ ) for ( int d = -64; d <= 64; d++ )
			check_one( &r, random_unit_axis(), (fixed_t)( k * piRaw + d ) );
		print_axisangle_row( "pi-boundaries", &r ); failed |= r.maxulp > QUAT_BOUND;
	}
	// One random raw angle of each sign at every octave, each with a random unit axis.
	{
		sample_row r = { 0 };
		for ( int e = 5; e <= 46; e++ )
		{
			uint64_t lo = UINT64_C(1) << ( e + 16 );
			uint64_t hi = e < 46 ? ( UINT64_C(1) << ( e + 17 ) ) - 1 : (uint64_t)FIX_MAX;
			uint64_t raw = lo + axisangle_next() % ( hi - lo + 1 );
			fixed_t a = (fixed_t)raw;
			check_one( &r, random_unit_axis(), a ); check_one( &r, random_unit_axis(), -a );
		}
		print_axisangle_row( "octaves", &r ); failed |= r.maxulp > QUAT_BOUND;
	}
	// Coordinate axes at the origin, pi turns, and largest exactly representable doubles.
	{
		sample_row r = { 0 };
		fixVec3 axes[3] = { fixVec3_axisX, fixVec3_axisY, fixVec3_axisZ };
		fixed_t angles[] = { 0, 205887, -205887, 411775, -411775,
			(fixed_t)( (uint64_t)FIX_MAX & ~(uint64_t)1023 ),
			(fixed_t)( -(int64_t)( (uint64_t)FIX_MAX & ~(uint64_t)1023 ) ) };
		for ( int a = 0; a < 3; a++ ) for ( size_t i = 0; i < sizeof angles / sizeof angles[0]; i++ ) check_one( &r, axes[a], angles[i] );
		print_axisangle_row( "coordinate-and-edge", &r ); failed |= r.maxulp > QUAT_BOUND;
	}
	if ( failed ) return 1;
	puts( "axis angle exact test passed" );
	return 0;
}

int main( void ) { return run(); }
