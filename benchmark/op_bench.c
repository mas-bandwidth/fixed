// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
//
// op_bench: nanoseconds per operation for every hot public operation, in two modes.
//
//   SCALAR  a dependent chain: each call's input carries the previous call's output (through
//           `x & zero`, a zero the compiler cannot see), so the loop measures LATENCY. The
//           chain costs an and, an xor and the fold of the output into one word; the row
//           chain.overhead measures exactly that with no operation in it.
//   BATCH   independent calls over arrays of 4,096 inputs: THROUGHPUT, and the baseline a
//           SIMD batch implementation has to beat.
//
// Inputs are generated at startup from --seed, outside the timed loops, in each operation's
// ordinary domain (benchmark/README.md lists them). Every output is folded into a checksum,
// so no work can be elided, and the checksum is computed from one fixed-size pass, so it is
// the same for both modes and comparable between two builds of different trees: a different
// checksum on the same seed means a different output.
//
// Output, CSV on stdout: op,mode,ns_min,ns_median,ns_max,checksum,reps
//   op_bench [--repeats N] [--min-ms M] [--seed S] [--list] [op ...]
// An op argument selects every row whose name starts with it (fixDiv selects fixDiv.lo and
// fixDiv.hi). Each sample runs at least --min-ms (default 20); --repeats samples (default 7)
// give the min, median and max.
#if !defined( _WIN32 ) && !defined( _POSIX_C_SOURCE )
#define _POSIX_C_SOURCE 200809L
#endif
#include "fixed/fixed_vec.h"
#include "fixed/fixed_quantize.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined( _MSC_VER )
#define NOINLINE __declspec( noinline )
#else
#define NOINLINE __attribute__( ( noinline ) )
#endif

enum { N = 4096 };

static volatile uint64_t zeroSource = 0; // read once per kernel: a zero the optimizer cannot see
static volatile uint64_t sink;

static uint64_t nowNs( void )
{
	struct timespec ts;
#if defined( CLOCK_MONOTONIC ) && !defined( _WIN32 )
	clock_gettime( CLOCK_MONOTONIC, &ts );
#else
	timespec_get( &ts, TIME_UTC );
#endif
	return (uint64_t)ts.tv_sec * UINT64_C( 1000000000 ) + (uint64_t)ts.tv_nsec;
}

// ---- inputs ----

static uint64_t rngState;
static uint64_t rnd( void )
{
	// xorshift64*: setup only, never timed.
	rngState ^= rngState >> 12;
	rngState ^= rngState << 25;
	rngState ^= rngState >> 27;
	return rngState * UINT64_C( 0x2545F4914F6CDD1D );
}
static int64_t rndRange( int64_t lo, int64_t hi ) { return lo + (int64_t)( rnd() % (uint64_t)( hi - lo ) ); }

static uint64_t isqrt64( uint64_t n )
{
	uint64_t r = 0, bit = UINT64_C( 1 ) << 62;
	while ( bit > n ) bit >>= 2;
	while ( bit )
	{
		if ( n >= r + bit ) { n -= r + bit; r = ( r >> 1 ) + bit; }
		else r >>= 1;
		bit >>= 2;
	}
	return r;
}

static fixQuat unitQuat( void )
{
	for ( ;; )
	{
		int64_t x = rndRange( -65536, 65537 ), y = rndRange( -65536, 65537 ), z = rndRange( -65536, 65537 );
		uint64_t ss = (uint64_t)( x * x + y * y + z * z );
		if ( ss > ( UINT64_C( 1 ) << 32 ) ) continue;
		fixQuat q = { { x, y, z }, (fixed_t)isqrt64( ( UINT64_C( 1 ) << 32 ) - ss ) };
		return q;
	}
}

#define UNITS( n ) ( (int64_t)( n ) * FIX_ONE )

static int64_t mulA[N], mulB[N];
static int64_t divLoA[N], divHiA[N], divB[N];
static int64_t sqrtSmall[N], sqrtLarge[N];
static uint64_t isqLoHi[N], isqHiHi[N], isqLo[N];
static int64_t angle[N], atanY[N], atanX[N], logIn[N], expIn[N], powBase[N], powExp[N];
static int64_t lerpA[N], lerpB[N], alpha[N];
static fixVec3 vecA[N], vecB[N];
static fixQuat quatA[N], quatB[N];
static fixMatrix3 mat[N];
static fixTransform xf[N];
static fixAABB box[N];
static double quantIn[N];
static int64_t dequantIn[N];
static fixInt128 i128A[N], i128B[N];
static fixUInt256 u256A[N], u256B[N];

static void initialize( uint64_t seed )
{
	rngState = seed ? seed : 1;
	for ( int i = 0; i < N; ++i )
	{
		mulA[i] = rndRange( -UNITS( 32768 ), UNITS( 32768 ) );
		mulB[i] = rndRange( -UNITS( 32768 ), UNITS( 32768 ) );
		divLoA[i] = rndRange( -( (int64_t)1 << 46 ), (int64_t)1 << 46 );
		divHiA[i] = rndRange( (int64_t)1 << 48, (int64_t)1 << 61 ) * ( ( rnd() & 1 ) ? 1 : -1 );
		divB[i] = rndRange( FIX_ONE / 16, UNITS( 4096 ) ) * ( ( rnd() & 1 ) ? 1 : -1 );
		sqrtSmall[i] = rndRange( 0, (int64_t)1 << 20 );
		sqrtLarge[i] = rndRange( (int64_t)1 << 40, (int64_t)1 << 62 );
		isqLoHi[i] = (uint64_t)rndRange( 1, (int64_t)1 << 40 );
		isqHiHi[i] = (uint64_t)rndRange( (int64_t)1 << 40, INT64_MAX );
		isqLo[i] = rnd();
		angle[i] = rndRange( -411775, 411776 );
		atanY[i] = rndRange( -UNITS( 1024 ), UNITS( 1024 ) );
		atanX[i] = rndRange( -UNITS( 1024 ), UNITS( 1024 ) );
		logIn[i] = rndRange( 1, (int64_t)1 << 47 );
		expIn[i] = rndRange( -UNITS( 16 ), UNITS( 47 ) );
		powBase[i] = rndRange( 1, UNITS( 16 ) );
		powExp[i] = rndRange( -UNITS( 4 ), UNITS( 4 ) );
		lerpA[i] = rndRange( -UNITS( 32768 ), UNITS( 32768 ) );
		lerpB[i] = rndRange( -UNITS( 32768 ), UNITS( 32768 ) );
		alpha[i] = rndRange( 0, FIX_ONE + 1 );
		vecA[i] = ( fixVec3 ){ rndRange( -UNITS( 256 ), UNITS( 256 ) ), rndRange( -UNITS( 256 ), UNITS( 256 ) ),
							   rndRange( -UNITS( 256 ), UNITS( 256 ) ) };
		vecB[i] = ( fixVec3 ){ rndRange( -UNITS( 256 ), UNITS( 256 ) ), rndRange( -UNITS( 256 ), UNITS( 256 ) ),
							   rndRange( -UNITS( 256 ), UNITS( 256 ) ) };
		quatA[i] = unitQuat();
		quatB[i] = unitQuat();
		fixed_t* m = &mat[i].cx.x;
		for ( int k = 0; k < 9; ++k ) m[k] = rndRange( -UNITS( 4 ), UNITS( 4 ) );
		mat[i].cx.x += UNITS( 16 );
		mat[i].cy.y += UNITS( 16 );
		mat[i].cz.z += UNITS( 16 );
		xf[i].p = vecA[i];
		xf[i].q = quatA[i];
		fixVec3 e = { rndRange( FIX_ONE, UNITS( 8 ) ), rndRange( FIX_ONE, UNITS( 8 ) ), rndRange( FIX_ONE, UNITS( 8 ) ) };
		box[i].lowerBound = fixVecSub( vecB[i], e );
		box[i].upperBound = fixVecAdd( vecB[i], e );
		quantIn[i] = (double)rndRange( -( (int64_t)1 << 40 ), (int64_t)1 << 40 ) / 1048576.0;
		dequantIn[i] = rndRange( -( (int64_t)1 << 50 ), (int64_t)1 << 50 );
		i128A[i] = fixInt128Make( rnd() >> 28, rnd() );
		i128B[i] = fixInt128Make( 0, ( rnd() >> 14 ) | 1 );
		uint64_t a4[4] = { rnd(), rnd(), rnd(), rnd() >> 8 };
		uint64_t b4[4] = { rnd(), rnd() >> 4, 0, 0 };
		b4[0] |= 1;
		u256A[i] = fixUInt256FromLimbs( a4 );
		u256B[i] = fixUInt256FromLimbs( b4 );
	}
}

// ---- kernels ----
//
// BODY reads element i, mixes `inj` into one input (zero at run time), and assigns the
// folded output to r_. The batch kernel's inj changes per repetition only, so no repetition
// can be hoisted out of the loop; the chain kernel's inj carries the previous output.

#define U( x ) ( (uint64_t)( x ) )
#define FOLD3( v ) ( U( ( v ).x ) + U( ( v ).y ) + U( ( v ).z ) )
#define FOLDQ( q ) ( FOLD3( ( q ).v ) + U( ( q ).s ) )

#define KERNELS( ID, BODY )                                                                                                \
	static NOINLINE uint64_t ID##_batch( int reps )                                                                        \
	{                                                                                                                      \
		const uint64_t m = zeroSource;                                                                                     \
		uint64_t sum = 0;                                                                                                  \
		for ( int k = 0; k < reps; ++k )                                                                                   \
		{                                                                                                                  \
			const int64_t inj = (int64_t)( (uint64_t)k & m );                                                              \
			for ( int i = 0; i < N; ++i )                                                                                  \
			{                                                                                                              \
				uint64_t r_;                                                                                               \
				BODY;                                                                                                      \
				sum += r_;                                                                                                 \
			}                                                                                                              \
		}                                                                                                                  \
		return sum;                                                                                                        \
	}                                                                                                                      \
	static NOINLINE uint64_t ID##_chain( int reps )                                                                        \
	{                                                                                                                      \
		const uint64_t m = zeroSource;                                                                                     \
		uint64_t x = 0, sum = 0;                                                                                           \
		for ( int k = 0; k < reps; ++k )                                                                                   \
		{                                                                                                                  \
			for ( int i = 0; i < N; ++i )                                                                                  \
			{                                                                                                              \
				const int64_t inj = (int64_t)( x & m );                                                                    \
				uint64_t r_;                                                                                               \
				BODY;                                                                                                      \
				x = r_;                                                                                                    \
				sum += r_;                                                                                                 \
			}                                                                                                              \
		}                                                                                                                  \
		return sum;                                                                                                        \
	}

KERNELS( overhead, r_ = U( mulA[i] ^ inj ) )
KERNELS( mul, r_ = U( fixMul( mulA[i] ^ inj, mulB[i] ) ) )
KERNELS( div_lo, r_ = U( fixDiv( divLoA[i] ^ inj, divB[i] ) ) )
KERNELS( div_hi, r_ = U( fixDiv( divHiA[i] ^ inj, divB[i] ) ) )
KERNELS( sqrt_small, r_ = U( fixSqrt( sqrtSmall[i] ^ inj ) ) )
KERNELS( sqrt_large, r_ = U( fixSqrt( sqrtLarge[i] ^ inj ) ) )
KERNELS( isqrt_le104, r_ = fixISqrt128High( isqLoHi[i], isqLo[i] ^ U( inj ) ) )
KERNELS( isqrt_gt104, r_ = fixISqrt128High( isqHiHi[i], isqLo[i] ^ U( inj ) ) )
KERNELS( cossin, fixCosSin cs = fixComputeCosSin( angle[i] ^ inj ); r_ = U( cs.cosine ) + U( cs.sine ) )
KERNELS( sin, r_ = U( fixSin( angle[i] ^ inj ) ) )
KERNELS( cos, r_ = U( fixCos( angle[i] ^ inj ) ) )
KERNELS( atan2, r_ = U( fixAtan2( atanY[i] ^ inj, atanX[i] ) ) )
KERNELS( log2, r_ = U( fixLog2( logIn[i] ^ inj ) ) )
KERNELS( exp2, r_ = U( fixExp2( expIn[i] ^ inj ) ) )
KERNELS( pow, r_ = U( fixPow( powBase[i] ^ inj, powExp[i] ) ) )
KERNELS( lerp, r_ = U( fixLerp( lerpA[i] ^ inj, lerpB[i], alpha[i] ) ) )
KERNELS( dot, fixVec3 a = vecA[i]; a.x ^= inj; r_ = U( fixDot( a, vecB[i] ) ) )
KERNELS( cross, fixVec3 a = vecA[i]; a.x ^= inj; fixVec3 c = fixCross( a, vecB[i] ); r_ = FOLD3( c ) )
KERNELS( length, fixVec3 a = vecA[i]; a.x ^= inj; r_ = U( fixLength( a ) ) )
KERNELS( normalize, fixVec3 a = vecA[i]; a.x ^= inj; fixVec3 c = fixNormalize( a ); r_ = FOLD3( c ) )
KERNELS( mulquat, fixQuat a = quatA[i]; a.s ^= inj; fixQuat c = fixMulQuat( a, quatB[i] ); r_ = FOLDQ( c ) )
KERNELS( normquat, fixQuat a = quatA[i]; a.s ^= inj; fixQuat c = fixNormalizeQuat( a ); r_ = FOLDQ( c ) )
KERNELS( rotate, fixVec3 v = vecB[i]; v.x ^= inj; fixVec3 c = fixRotateVector( quatA[i], v ); r_ = FOLD3( c ) )
KERNELS( invrotate, fixVec3 v = vecB[i]; v.x ^= inj; fixVec3 c = fixInvRotateVector( quatA[i], v ); r_ = FOLD3( c ) )
KERNELS( nlerp, fixQuat a = quatA[i]; a.s ^= inj; fixQuat c = fixNLerp( a, quatB[i], alpha[i] ); r_ = FOLDQ( c ) )
KERNELS( invert, fixMatrix3 a = mat[i]; a.cx.x ^= inj; fixMatrix3 c = fixInvertMatrix( a );
		 r_ = FOLD3( c.cx ) + FOLD3( c.cy ) + FOLD3( c.cz ) )
KERNELS( solve3, fixMatrix3 a = mat[i]; a.cx.x ^= inj; fixVec3 c = fixSolve3( a, vecB[i] ); r_ = FOLD3( c ) )
KERNELS( aabb, fixTransform t = xf[i]; t.p.x ^= inj; fixAABB c = fixAABB_Transform( t, box[i] );
		 r_ = FOLD3( c.lowerBound ) + FOLD3( c.upperBound ) )
KERNELS( quantize, r_ = U( fixQuantize( quantIn[( i ^ (int)inj ) & ( N - 1 )], 65536 ) ) )
KERNELS( dequantize, double d = fixDequantize( dequantIn[i] ^ inj, 65536 ); uint64_t b_; memcpy( &b_, &d, 8 ); r_ = b_ )
KERNELS( int128div, fixInt128 a = fixInt128Make( fixInt128Hi( i128A[i] ), fixInt128Lo( i128A[i] ) ^ U( inj ) );
		 fixInt128 q = fixInt128Div( a, i128B[i] ); r_ = fixInt128Lo( q ) + fixInt128Hi( q ) )
// One declaration per statement: a comma at the top level of BODY would split the macro argument.
KERNELS( uint256div, uint64_t al[4]; fixUInt256ToLimbs( u256A[i], al ); al[0] ^= U( inj ); fixUInt256 q; fixUInt256 r;
		 fixUInt256DivMod( fixUInt256FromLimbs( al ), u256B[i], &q, &r ); uint64_t ql[4]; uint64_t rl[4];
		 fixUInt256ToLimbs( q, ql ); fixUInt256ToLimbs( r, rl ); r_ = ql[0] + ql[1] + ql[2] + ql[3] + rl[0] + rl[1] )

typedef struct BenchOp
{
	const char* name;
	uint64_t ( *batch )( int );
	uint64_t ( *chain )( int );
} BenchOp;

#define OP( NAME, ID ) { NAME, ID##_batch, ID##_chain }
static const BenchOp ops[] = {
	OP( "chain.overhead", overhead ),
	OP( "fixMul", mul ),
	OP( "fixDiv.lo", div_lo ),
	OP( "fixDiv.hi", div_hi ),
	OP( "fixSqrt.small", sqrt_small ),
	OP( "fixSqrt.large", sqrt_large ),
	OP( "fixISqrt128High.le104", isqrt_le104 ),
	OP( "fixISqrt128High.gt104", isqrt_gt104 ),
	OP( "fixComputeCosSin", cossin ),
	OP( "fixSin", sin ),
	OP( "fixCos", cos ),
	OP( "fixAtan2", atan2 ),
	OP( "fixLog2", log2 ),
	OP( "fixExp2", exp2 ),
	OP( "fixPow", pow ),
	OP( "fixLerp", lerp ),
	OP( "fixDot", dot ),
	OP( "fixCross", cross ),
	OP( "fixLength", length ),
	OP( "fixNormalize", normalize ),
	OP( "fixMulQuat", mulquat ),
	OP( "fixNormalizeQuat", normquat ),
	OP( "fixRotateVector", rotate ),
	OP( "fixInvRotateVector", invrotate ),
	OP( "fixNLerp", nlerp ),
	OP( "fixInvertMatrix", invert ),
	OP( "fixSolve3", solve3 ),
	OP( "fixAABB_Transform", aabb ),
	OP( "fixQuantize", quantize ),
	OP( "fixDequantize", dequantize ),
	OP( "fixInt128Div", int128div ),
	OP( "fixUInt256DivMod", uint256div ),
};
enum { OP_COUNT = (int)( sizeof( ops ) / sizeof( ops[0] ) ) };

static int cmpDouble( const void* a, const void* b )
{
	double x = *(const double*)a, y = *(const double*)b;
	return x < y ? -1 : x > y ? 1 : 0;
}

static void measure( const char* name, const char* mode, uint64_t ( *kernel )( int ), int repeats, uint64_t minNs )
{
	uint64_t checksum = kernel( 1 );
	sink = checksum;
	// Calibrate: double the repetitions until one sample takes at least minNs.
	int reps = 1;
	for ( ;; )
	{
		uint64_t t0 = nowNs();
		sink = kernel( reps );
		uint64_t dt = nowNs() - t0;
		if ( dt >= minNs || reps >= ( 1 << 24 ) ) break;
		reps *= 2;
	}
	double samples[64];
	if ( repeats > 64 ) repeats = 64;
	for ( int s = 0; s < repeats; ++s )
	{
		uint64_t t0 = nowNs();
		sink = kernel( reps );
		uint64_t dt = nowNs() - t0;
		samples[s] = (double)dt / ( (double)reps * N );
	}
	qsort( samples, (size_t)repeats, sizeof( double ), cmpDouble );
	printf( "%s,%s,%.4f,%.4f,%.4f,%llu,%d\n", name, mode, samples[0], samples[repeats / 2], samples[repeats - 1],
			(unsigned long long)checksum, reps );
	fflush( stdout );
}

static int wanted( const char* name, int argc, char** argv, int first )
{
	if ( first >= argc ) return 1;
	for ( int i = first; i < argc; ++i )
		if ( strncmp( name, argv[i], strlen( argv[i] ) ) == 0 ) return 1;
	return 0;
}

int main( int argc, char** argv )
{
	int repeats = 7;
	uint64_t minNs = 20 * UINT64_C( 1000000 );
	uint64_t seed = 42;
	int i = 1;
	for ( ; i < argc && strncmp( argv[i], "--", 2 ) == 0; ++i )
	{
		if ( strcmp( argv[i], "--repeats" ) == 0 && i + 1 < argc ) repeats = atoi( argv[++i] );
		else if ( strcmp( argv[i], "--min-ms" ) == 0 && i + 1 < argc ) minNs = strtoull( argv[++i], NULL, 10 ) * UINT64_C( 1000000 );
		else if ( strcmp( argv[i], "--seed" ) == 0 && i + 1 < argc ) seed = strtoull( argv[++i], NULL, 10 );
		else if ( strcmp( argv[i], "--list" ) == 0 )
		{
			for ( int k = 0; k < OP_COUNT; ++k ) printf( "%s\n", ops[k].name );
			return 0;
		}
		else
		{
			fprintf( stderr, "op_bench: unknown argument %s\n", argv[i] );
			return 2;
		}
	}
	if ( repeats < 1 ) repeats = 1;
	initialize( seed );
	printf( "op,mode,ns_min,ns_median,ns_max,checksum,reps\n" );
	for ( int k = 0; k < OP_COUNT; ++k )
	{
		if ( !wanted( ops[k].name, argc, argv, i ) ) continue;
		measure( ops[k].name, "scalar", ops[k].chain, repeats, minNs );
		measure( ops[k].name, "batch", ops[k].batch, repeats, minNs );
	}
	return 0;
}
