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
//   fixed_op_bench [repeats] [iterations] [operation ...]
// Every name after the iteration count is kept. An unknown name fails, rather than
// being dropped. Passing no names runs the whole table.
// Output CSV: op,mode,ns_per_op_min,ns_per_op_median,ns_per_op_max,checksum
// preceded by one comment line, "# isa: <variant> (facts: ...)", naming the batch-kernel
// variant the runtime dispatch selected (FIXED_ISA forces one; include/fixed/fixed_isa.h).
//
// THE BATCH API ROWS (fixMulBatch, fixDotBatch3). Batch mode is one fixMulBatch or
// fixDotBatch3 call over the 4096 prepared lanes per pass, so it measures the dispatched
// kernel, one indirect call per 4096 elements. Scalar mode is the same dependent chain
// through fixMul or fixDot, so it is a control that no variant should move.
#include "oracle.c"
#include "fixed/fixed_isa.h"

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
static fixed_t opBatchA[6][OP_BENCH_BATCH];
static fixed_t opBatchOut[OP_BENCH_BATCH];

enum { OP_BENCH_PLAIN = 0, OP_BENCH_MUL_BATCH, OP_BENCH_DOT_BATCH3 };
static int opBenchKind( int opIndex )
{
	if ( strcmp( oracleOps[opIndex].name, "fixMulBatch" ) == 0 ) return OP_BENCH_MUL_BATCH;
	if ( strcmp( oracleOps[opIndex].name, "fixDotBatch3" ) == 0 ) return OP_BENCH_DOT_BATCH3;
	return OP_BENCH_PLAIN;
}

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
		for ( int j = 0; j < 6; ++j ) opBatchA[j][i] = opBenchInputs[i].s[j];
	}
}

static uint64_t opBenchScalar( int opIndex, uint64_t iterations )
{
	fixOracleInput chain = opBenchInputs[0];
	const int slot = opBenchChainSlot( opIndex );
	fixOracleApply apply = oracleOps[opIndex].apply;
	const int kind = opBenchKind( opIndex );
	uint64_t sum = 0;
	for ( uint64_t j = 0; j < iterations; ++j )
	{
		fixOracleOutput out;
		out.n = 0;
		if ( kind == OP_BENCH_MUL_BATCH ) ORACLE_OUT1( &out, fixMul( chain.s[0], chain.s[1] ) );
		else if ( kind == OP_BENCH_DOT_BATCH3 )
			ORACLE_OUT1( &out, fixDot( oracleVec( chain.s[0], chain.s[1], chain.s[2] ),
									   oracleVec( chain.s[3], chain.s[4], chain.s[5] ) ) );
		else apply( &chain, &out );
		sum += ( uint64_t )out.s[0];
		chain.s[slot] = out.s[0]; // the dependency: latency, not throughput
	}
	return sum;
}

static uint64_t opBenchBatch( int opIndex, uint64_t passes )
{
	fixOracleApply apply = oracleOps[opIndex].apply;
	const int kind = opBenchKind( opIndex );
	uint64_t sum = 0;
	if ( kind != OP_BENCH_PLAIN )
	{
		for ( uint64_t p = 0; p < passes; ++p )
		{
			if ( kind == OP_BENCH_MUL_BATCH ) fixMulBatch( opBatchOut, opBatchA[0], opBatchA[1], OP_BENCH_BATCH );
			else
				fixDotBatch3( opBatchOut, opBatchA[0], opBatchA[1], opBatchA[2], opBatchA[3], opBatchA[4], opBatchA[5],
							  OP_BENCH_BATCH );
			for ( int i = 0; i < OP_BENCH_BATCH; ++i ) sum += ( uint64_t )opBatchOut[i];
		}
		return sum;
	}
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

// True when this operation is in the caller's set. No names means every operation.
// Names that are not operations are rejected in main, so a set cannot silently shrink.
static int opBenchSelected( const char* name, int argc, char** argv )
{
	if ( argc <= 3 ) return 1;
	for ( int a = 3; a < argc; ++a )
	{
		if ( strcmp( argv[a], name ) == 0 ) return 1;
	}
	return 0;
}

int main( int argc, char** argv )
{
	int repeats = argc > 1 ? atoi( argv[1] ) : 7;
	uint64_t iterations = argc > 2 ? strtoull( argv[2], NULL, 10 ) : 200000;
	if ( repeats < 1 ) repeats = 1;
	if ( iterations < 1 ) iterations = 1;
	const uint64_t passes = ( iterations + OP_BENCH_BATCH - 1 ) / OP_BENCH_BATCH;
	const int ops = fixOracleOpCount();

	for ( int a = 3; a < argc; ++a )
	{
		if ( fixOracleFindOp( argv[a] ) < 0 )
		{
			fprintf( stderr, "fixed_op_bench: unknown operation \"%s\"\n", argv[a] );
			return 2;
		}
	}

	printf( "# isa: %s (facts:", fixIsaName() );
	for ( int f = 0; f < FIX_ISA_FACT_COUNT; ++f )
		if ( fixIsaHostFacts() & ( 1u << f ) ) printf( " %s", fixIsaFactName( 1u << f ) );
	printf( ")\n" );
	printf( "op,mode,ns_per_op_min,ns_per_op_median,ns_per_op_max,checksum\n" );
	for ( int i = 0; i < ops; ++i )
	{
		const char* name = oracleOps[i].name;
		if ( !opBenchSelected( name, argc, argv ) ) continue;
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
