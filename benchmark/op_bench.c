// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
// THE OPERATION BENCHMARK: nanoseconds per operation, for every public hot operation, in
// two modes.
//
//   SCALAR  a DEPENDENT CHAIN: each call's first output word becomes the next call's
//           first input word. Nothing can overlap, so this measures LATENCY, which is
//           what a solver's inner loop sees when one result feeds the next.
//   BATCH   INDEPENDENT operations over an array of 4096 prepared inputs. Calls do not
//           depend on each other, so this measures THROUGHPUT, and it is the baseline the
//           SIMD batch spikes in this stream have to beat.
//
// Inputs are prepared outside the timed loop from the same seeded fills the output oracle
// uses (benchmark/oracle.c is #included so that the two halves of this card cannot drift
// apart on what an input is). Every output word is consumed into a checksum so no work can
// be elided.
//
// WHAT A ROW COSTS THAT IS NOT THE OPERATION. Each row dispatches through the operation's
// apply function and stores its output words, and both modes pay that identically. It is a
// constant added to every row (about a nanosecond on the machines measured here), so an
// absolute number is an UPPER BOUND on the operation's own cost. The A/B numbers ab.sh
// prints are unaffected: both trees pay the same overhead, and a difference between them is
// a difference in the operation.
//
// Usage:
//   fixed_op_bench [repeats] [iterations] [operation]
// Output CSV: op,mode,ns_per_op_min,ns_per_op_median,ns_per_op_max,checksum
#include "oracle.c"

#include <stdlib.h>
#include <time.h>

#if defined( _MSC_VER )
	#define OP_BENCH_NOINLINE __declspec( noinline )
#else
	#define OP_BENCH_NOINLINE __attribute__( noinline )
#endif

enum { OP_BENCH_BATCH = 4096 };

static fixOracleInput opBenchInputs[OP_BENCH_BATCH];
static volatile uint64_t opBenchSink;

// The slot the dependent chain feeds back through. Slot 0 for every operation except the
// AABB transform, whose quaternion slots carry a validity contract the chain would break;
// its translation is chained instead, which is equally dependent and leaves the rotation
// alone.
static int opBenchChainSlot( int opIndex )
{
	return strcmp( oracleOps[opIndex].name, "fixAABB_Transform" ) == 0 ? 4 : 0;
}

static void opBenchPrepare( int opIndex )
{
	fixOracleRng rng;
	oracleRngSeed( &rng );
	// Advance the stream past the values the oracle's own passes would use, so the timing
	// inputs are not the correctness inputs.
	for ( int i = 0; i < 64; ++i ) ( void )oracleRngNext( &rng );
	for ( int i = 0; i < OP_BENCH_BATCH; ++i )
	{
		memset( &opBenchInputs[i], 0, sizeof opBenchInputs[i] );
		oracleOps[opIndex].fill( &opBenchInputs[i], ( uint64_t )i, &rng );
	}
}

static uint64_t opBenchScalar( int opIndex, uint64_t iterations )
{
	fixOracleInput chain = opBenchInputs[0];
	const int slot = opBenchChainSlot( opIndex );
	fixOracleApply apply = oracleOps[opIndex].apply;
	uint64_t sum = 0;
	for ( uint64_t j = 0; j < iterations; ++j )
	{
		fixOracleOutput out;
		out.n = 0;
		apply( &chain, &out );
		sum += ( uint64_t )out.s[0];
		chain.s[slot] = out.s[0]; // the dependency: latency, not throughput
	}
	return sum;
}

static uint64_t opBenchBatch( int opIndex, uint64_t passes )
{
	fixOracleApply apply = oracleOps[opIndex].apply;
	uint64_t sum = 0;
	for ( uint64_t p = 0; p < passes; ++p )
	{
		for ( int i = 0; i < OP_BENCH_BATCH; ++i )
		{
			fixOracleOutput out;
			out.n = 0;
			apply( &opBenchInputs[i], &out );
			for ( int k = 0; k < out.n; ++k ) sum += ( uint64_t )out.s[k];
		}
	}
	return sum;
}

static double opBenchNowNs( void )
{
	struct timespec ts;
	clock_gettime( CLOCK_MONOTONIC, &ts );
	return ( double )ts.tv_sec * 1e9 + ( double )ts.tv_nsec;
}

static int opBenchCompare( const void* a, const void* b )
{
	double x = *( const double* )a;
	double y = *( const double* )b;
	return ( x > y ) - ( x < y );
}

int main( int argc, char** argv )
{
	int repeats = argc > 1 ? atoi( argv[1] ) : 7;
	uint64_t iterations = argc > 2 ? strtoull( argv[2], NULL, 10 ) : 200000;
	const char* only = argc > 3 ? argv[3] : NULL;
	if ( repeats < 1 ) repeats = 1;
	if ( iterations < 1 ) iterations = 1;
	const uint64_t passes = ( iterations + OP_BENCH_BATCH - 1 ) / OP_BENCH_BATCH;
	const int ops = fixOracleOpCount();

	printf( "op,mode,ns_per_op_min,ns_per_op_median,ns_per_op_max,checksum\n" );
	for ( int i = 0; i < ops; ++i )
	{
		const char* name = oracleOps[i].name;
		if ( only != NULL && strcmp( only, name ) != 0 ) continue;
		opBenchPrepare( i );

		double scalar[64];
		double batch[64];
		uint64_t scalarChecksum = 0;
		uint64_t batchChecksum = 0;
		int n = repeats < 64 ? repeats : 64;

		// Warm up outside the timed region: page faults and branch predictors are not the
		// thing being measured.
		opBenchSink = opBenchScalar( i, iterations / 10 + 1 );
		opBenchSink = opBenchBatch( i, passes / 10 + 1 );

		for ( int r = 0; r < n; ++r )
		{
			double t0 = opBenchNowNs();
			uint64_t cs = opBenchScalar( i, iterations );
			double t1 = opBenchNowNs();
			scalar[r] = ( t1 - t0 ) / ( double )iterations;
			scalarChecksum = cs;

			t0 = opBenchNowNs();
			cs = opBenchBatch( i, passes );
			t1 = opBenchNowNs();
			batch[r] = ( t1 - t0 ) / ( double )( passes * ( uint64_t )OP_BENCH_BATCH );
			batchChecksum = cs;
		}
		opBenchSink = scalarChecksum ^ batchChecksum;

		qsort( scalar, ( size_t )n, sizeof scalar[0], opBenchCompare );
		qsort( batch, ( size_t )n, sizeof batch[0], opBenchCompare );
		printf( "%s,scalar,%.4f,%.4f,%.4f,%llu\n", name, scalar[0], scalar[n / 2], scalar[n - 1],
				( unsigned long long )scalarChecksum );
		printf( "%s,batch,%.4f,%.4f,%.4f,%llu\n", name, batch[0], batch[n / 2], batch[n - 1],
				( unsigned long long )batchChecksum );
	}
	return 0;
}
