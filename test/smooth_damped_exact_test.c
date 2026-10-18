// SPDX-License-Identifier: MIT
// fixSmoothCriticallyDampedExact / fixSmoothCriticallyDampedUpDownExact against the exact
// formula with the true pi:
//
//   omega     = 2 pi / smoothTime
//   velocity' = ( velocity - omega^2 dt ( current - target ) ) / ( 1 + omega dt )^2
//   result    = current + velocity' dt
//
// THE REFERENCE is that formula evaluated in long double on the exact raw inputs. It has
// at least a 53-bit significand (MSVC's long double is double), at most about sixteen
// operations, and every reference raw in these pieces is below 2^38 in magnitude, so the
// reference error is below 2^38 * 16 * 2^-53 = 2^-11 raw, well under the 1/16 raw the card
// allows a piece. No libm, no CORE-MATH, no clock, no files: everything is arguments and a
// seeded xorshift64.
//
// THE BOUND is 2 raw units on both outputs for the core and wide pieces, and zero for the
// early returns and the one-raw times. SMOOTH_DAMPED_NEGATIVE_CONTROL replaces the new
// functions with the shipped fixSmoothCriticallyDamped pair; that build must fail, and its
// core velocity row shows the error the new function removes.
#include "fixed/fixed_math.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef SMOOTH_DAMPED_NEGATIVE_CONTROL
	#define COMPUTE_EXACT fixSmoothCriticallyDamped
	#define COMPUTE_UPDOWN fixSmoothCriticallyDampedUpDown
#else
	#define COMPUTE_EXACT fixSmoothCriticallyDampedExact
	#define COMPUTE_UPDOWN fixSmoothCriticallyDampedUpDownExact
#endif

#define CORE_BOUND 2
#define WIDE_BOUND 2
#define EXACT_BOUND 0

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

static int64_t rng_signed( int64_t magnitude )
{
	uint64_t span = (uint64_t)( 2 * magnitude + 1 );
	return (int64_t)( rng_next() % span ) - magnitude;
}

static int64_t diff64( int64_t a, int64_t b )
{
	uint64_t d;
	if ( a >= b )
	{
		d = (uint64_t)a - (uint64_t)b;
	}
	else
	{
		d = (uint64_t)b - (uint64_t)a;
	}
	return d > (uint64_t)INT64_MAX ? INT64_MAX : (int64_t)d;
}

// Round the long double reference to nearest raw, halves away from zero, and saturate at
// the fixed_t range. The clamping keeps the long double -> int64 conversion in range.
static int64_t reference_raw( long double x )
{
	const long double limit = (long double)FIX_MAX;
	if ( x >= limit + 0.5L )
	{
		return FIX_MAX;
	}
	if ( x <= -limit - 0.5L )
	{
		return FIX_MIN;
	}
	long double r = x >= 0.0L ? x + 0.5L : x - 0.5L;
	if ( r > limit )
	{
		r = limit;
	}
	if ( r < -limit )
	{
		r = -limit;
	}
	return (int64_t)r;
}

static void reference( fixed_t current, fixed_t target, fixed_t velocity, fixed_t smoothTime, fixed_t deltaTime,
					   long double* positionOut, long double* velocityOut )
{
	if ( smoothTime <= 0 )
	{
		*positionOut = (long double)target;
		*velocityOut = (long double)velocity;
		return;
	}

	if ( deltaTime <= 0 )
	{
		*positionOut = (long double)current;
		*velocityOut = (long double)velocity;
		return;
	}

	const long double pi = 3.14159265358979323846264338327950288L;
	long double s = 1.0L / 65536.0L;
	long double T = (long double)smoothTime * s;
	long double dt = (long double)deltaTime * s;
	long double c = (long double)current * s;
	long double t = (long double)target * s;
	long double v = (long double)velocity * s;

	long double omega = 2.0L * pi / T;
	long double onePlus = 1.0L + omega * dt;
	long double vp = ( v - omega * omega * dt * ( c - t ) ) / ( onePlus * onePlus );
	long double position = c + vp * dt;

	*positionOut = position * 65536.0L;
	*velocityOut = vp * 65536.0L;
}

typedef struct row
{
	const char* piece;
	const char* output;
	int bound;
	int64_t inputs;
	int64_t maxulp;
	int failed;
	int64_t worst[5];
	int64_t worstGot;
	int64_t worstRef;
} row;

static void check( row* r, const int64_t in[5], int64_t got, long double refLong )
{
	int64_t ref = reference_raw( refLong );
	int64_t d = diff64( got, ref );
	r->inputs++;
	if ( d > r->maxulp )
	{
		r->maxulp = d;
		memcpy( r->worst, in, sizeof( r->worst ) );
		r->worstGot = got;
		r->worstRef = ref;
	}
	if ( d > r->bound )
	{
		r->failed = 1;
	}
}

static void print_row( const row* r )
{
	printf( "row=smoothexact[%s].%s inputs=%lld maxulp=%lld bound=%d worst=%lld,%lld,%lld,%lld,%lld\n", r->piece,
			r->output, (long long)r->inputs, (long long)r->maxulp, r->bound, (long long)r->worst[0],
			(long long)r->worst[1], (long long)r->worst[2], (long long)r->worst[3], (long long)r->worst[4] );
}

static void run_case( row* pos, row* vel, int64_t current, int64_t target, int64_t velocity, int64_t smoothTime,
					  int64_t deltaTime )
{
	int64_t in[5] = { current, target, velocity, smoothTime, deltaTime };
	long double refPos, refVel;
	reference( current, target, velocity, smoothTime, deltaTime, &refPos, &refVel );

	fixed_t gotVelocity = velocity;
	fixed_t got = COMPUTE_EXACT( current, target, &gotVelocity, smoothTime, deltaTime );

	check( pos, in, got, refPos );
	check( vel, in, gotVelocity, refVel );
}

static void run_updown_case( row* pos, row* vel, int64_t current, int64_t target, int64_t velocity, int64_t up,
							 int64_t down, int64_t deltaTime )
{
	int64_t in[5] = { current, target, velocity, up, deltaTime };
	int64_t selected = fixAbs( target ) >= fixAbs( current ) ? up : down;
	long double refPos, refVel;
	reference( current, target, velocity, selected, deltaTime, &refPos, &refVel );

	fixed_t gotVelocity = velocity;
	fixed_t got = COMPUTE_UPDOWN( current, target, &gotVelocity, up, down, deltaTime );

	check( pos, in, got, refPos );
	check( vel, in, gotVelocity, refVel );
}

int main( void )
{
	row corePos = { "a", "pos", CORE_BOUND, 0, 0, 0, { 0, 0, 0, 0, 0 }, 0, 0 };
	row coreVel = { "a", "vel", CORE_BOUND, 0, 0, 0, { 0, 0, 0, 0, 0 }, 0, 0 };
	row widePos = { "b", "pos", WIDE_BOUND, 0, 0, 0, { 0, 0, 0, 0, 0 }, 0, 0 };
	row wideVel = { "b", "vel", WIDE_BOUND, 0, 0, 0, { 0, 0, 0, 0, 0 }, 0, 0 };
	row edgePos = { "c", "pos", EXACT_BOUND, 0, 0, 0, { 0, 0, 0, 0, 0 }, 0, 0 };
	row edgeVel = { "c", "vel", EXACT_BOUND, 0, 0, 0, { 0, 0, 0, 0, 0 }, 0, 0 };

	// (a) the core: +-64 rad, smoothTime in [1/16, 4], deltaTime in [1/240, 1/10].
	{
		const int64_t valueLimit = 64 * 65536;
		const uint64_t timeLow = 4096;								// 1/16 rad
		const uint64_t timeSpan = ( 4 * 65536 ) - timeLow + 1;		// through 4 rad
		const uint64_t deltaLow = 274;								// ceil(1/240 * 2^16)
		const uint64_t deltaSpan = 6553 - deltaLow + 1;				// floor(1/10 * 2^16)
		for ( int64_t i = 0; i < ( (int64_t)1 << 18 ); i++ )
		{
			int64_t current = rng_signed( valueLimit );
			int64_t target = rng_signed( valueLimit );
			int64_t velocity = rng_signed( valueLimit );
			int64_t smoothTime = (int64_t)( timeLow + rng_next() % timeSpan );
			int64_t deltaTime = (int64_t)( deltaLow + rng_next() % deltaSpan );
			run_case( &corePos, &coreVel, current, target, velocity, smoothTime, deltaTime );
		}
	}

	// (b) the task's wide range: +-2^20 rad, smoothTime in [1/16, 1024], deltaTime in (0, 1].
	// Four structured probes past the stated magnitudes then drive one output past the
	// fixed_t range, where the reference rounds to the saturation edge and the function must
	// return that same edge rather than wrap. The probes are far past the edge, so the long
	// double reference's small error cannot change which side they are on.
	{
		const int64_t valueLimit = (int64_t)1 << 36;
		const uint64_t timeLow = 4096;
		const uint64_t timeSpan = ( (uint64_t)1024 << 16 ) - timeLow + 1;
		for ( int64_t i = 0; i < ( (int64_t)1 << 16 ); i++ )
		{
			int64_t current = rng_signed( valueLimit );
			int64_t target = rng_signed( valueLimit );
			int64_t velocity = rng_signed( valueLimit );
			int64_t smoothTime = (int64_t)( timeLow + rng_next() % timeSpan );
			int64_t deltaTime = (int64_t)( 1 + rng_next() % 65536 );
			run_case( &widePos, &wideVel, current, target, velocity, smoothTime, deltaTime );
		}

		run_case( &widePos, &wideVel, FIX_MAX, FIX_MAX, FIX_MAX, 1, 1 );
		run_case( &widePos, &wideVel, -FIX_MAX, -FIX_MAX, -FIX_MAX, 1, 1 );
		run_case( &widePos, &wideVel, FIX_MAX, 0, FIX_MAX, 1, 1 );
		run_case( &widePos, &wideVel, -FIX_MAX, 0, -FIX_MAX, 1, 1 );
	}

	// (c) both early returns, and smoothTime / deltaTime of one raw. The pair with separate
	// up/down times runs here too, so its selection and both arms are covered.
	{
		static const int64_t triples[][3] = {
			{ 0, 0, 0 },
			{ FIX_ONE, 0, 0 },
			{ 0, FIX_ONE, 0 },
			{ FIX_ONE, -FIX_ONE, FIX_ONE / 2 },
			{ -FIX_ONE, FIX_ONE, -FIX_ONE / 2 },
			{ 12345, -6789, 1011 },
			{ -12345, 6789, -1011 },
		};
		static const int64_t times[] = { 0, -1, -FIX_ONE, 1, 2, 3, FIX_ONE / 16 };
		static const int64_t deltas[] = { 0, -1, 1, 2, 3 };

		for ( unsigned t = 0; t < sizeof( triples ) / sizeof( triples[0] ); t++ )
		{
			for ( unsigned s = 0; s < sizeof( times ) / sizeof( times[0] ); s++ )
			{
				for ( unsigned d = 0; d < sizeof( deltas ) / sizeof( deltas[0] ); d++ )
				{
					run_case( &edgePos, &edgeVel, triples[t][0], triples[t][1], triples[t][2], times[s], deltas[d] );

					int64_t down = times[s] > 0 ? times[s] + 1 : 1;
					run_updown_case( &edgePos, &edgeVel, triples[t][0], triples[t][1], triples[t][2], times[s], down,
									 deltas[d] );
				}
			}
		}
	}

	print_row( &corePos );
	print_row( &coreVel );
	print_row( &widePos );
	print_row( &wideVel );
	print_row( &edgePos );
	print_row( &edgeVel );

	row* rows[] = { &corePos, &coreVel, &widePos, &wideVel, &edgePos, &edgeVel };
	int failed = 0;
	for ( unsigned i = 0; i < sizeof( rows ) / sizeof( rows[0] ); i++ )
	{
		if ( rows[i]->failed )
		{
			failed++;
			printf( "smoothexact FAIL piece=%s output=%s inputs=%lld,%lld,%lld,%lld,%lld got=%lld ref=%lld "
					"maxulp=%lld bound=%d\n",
					rows[i]->piece, rows[i]->output, (long long)rows[i]->worst[0], (long long)rows[i]->worst[1],
					(long long)rows[i]->worst[2], (long long)rows[i]->worst[3], (long long)rows[i]->worst[4],
					(long long)rows[i]->worstGot, (long long)rows[i]->worstRef, (long long)rows[i]->maxulp,
					rows[i]->bound );
		}
	}

	if ( failed != 0 )
	{
		return 1;
	}

	printf( "smooth damped exact test passed\n" );
	return 0;
}
