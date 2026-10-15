// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
// BatchMulLaneEquivalenceTest: all SIMD lanes, tails, unaligned data and large seeded
// coverage of fixMulBatch and fixDotBatch3 against the scalar public operations, through
// whichever variant the runtime dispatch selected on this host (printed on the PASS line).
// Adopted from benchmark/experiments/spike-batch-mul-avx2-avx512. test/isa_dispatch_test.c
// runs every variant the host can run; this one runs the default selection, on both 128-bit
// arms and in the FIX_SATURATE build. The seeded lane count is the optional argument.
#include "fixed/fixed_isa.h"
#include "fixed/fixed_vec.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum { CAP = 4096 };
static fixed_t aa[CAP + 1], bb[CAP + 1], cc[CAP + 1];
static fixed_t dd[CAP + 1], ee[CAP + 1], ff[CAP + 1], gotMul[CAP + 1], gotDot[CAP + 1];
static fixed_t edges[280];
static size_t edgeCount;
static uint64_t rngState = UINT64_C( 0x243f6a8885a308d3 );

static uint64_t nextRand( void )
{
	uint64_t x = rngState;
	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	return rngState = x;
}

static void addEdge( fixed_t value )
{
	for ( size_t i = 0; i < edgeCount; ++i ) if ( edges[i] == value ) return;
	edges[edgeCount++] = value;
}

static int check( size_t n, uint64_t base )
{
	// +1 makes all eight input and output pointers 8-byte aligned but unaligned
	// relative to either vector register width. The zero-length call is valid.
	fixed_t *a = aa + 1, *b = bb + 1, *c = cc + 1;
	fixed_t *d = dd + 1, *e = ee + 1, *f = ff + 1;
	gotMul[1] = INT64_C( 0x1020304050607080 );
	gotDot[1] = INT64_C( 0x1020304050607080 );
	fixMulBatch( gotMul + 1, a, b, n );
	fixDotBatch3( gotDot + 1, a, b, c, d, e, f, n );
	if ( n == 0 && ( gotMul[1] != INT64_C( 0x1020304050607080 ) || gotDot[1] != INT64_C( 0x1020304050607080 ) ) ) return 1;
	for ( size_t i = 0; i < n; ++i )
	{
		fixed_t mul = fixMul( a[i], b[i] );
		fixVec3 v = { a[i], b[i], c[i] }, w = { d[i], e[i], f[i] };
		fixed_t dot = fixDot( v, w );
		if ( gotMul[i + 1] != mul || gotDot[i + 1] != dot )
		{
			fprintf( stderr, "BatchMulLaneEquivalenceTest FAIL at %llu: a=%lld b=%lld mul=%lld expected=%lld dot=%lld expected=%lld\n",
				(unsigned long long)( base + i ), (long long)a[i], (long long)b[i],
				(long long)gotMul[i + 1], (long long)mul, (long long)gotDot[i + 1], (long long)dot );
			return 1;
		}
	}
	return 0;
}

int main( int argc, char** argv )
{
	const uint64_t lanes = argc > 1 ? strtoull( argv[1], NULL, 10 ) : UINT64_C( 100000000 );
	addEdge( 0 ); addEdge( 1 ); addEdge( -1 );
	addEdge( INT64_MIN ); addEdge( INT64_MAX ); addEdge( FIX_MIN ); addEdge( FIX_MAX );
	for ( unsigned k = 1; k < 63; ++k )
	{
		int64_t p = INT64_C( 1 ) << k;
		addEdge( p ); addEdge( -p ); addEdge( p - 1 ); addEdge( -( p - 1 ) );
	}
	const size_t tails[] = { 0, 1, 3, 4, 5, 7, 8, 9, 4095 };
	for ( size_t t = 0; t < sizeof tails / sizeof tails[0]; ++t )
	{
		for ( size_t i = 0; i < tails[t]; ++i )
		{
			aa[i + 1] = edges[i % edgeCount]; bb[i + 1] = edges[( i * 19 ) % edgeCount];
			cc[i + 1] = edges[( i * 29 ) % edgeCount]; dd[i + 1] = edges[( i * 37 ) % edgeCount];
			ee[i + 1] = edges[( i * 43 ) % edgeCount]; ff[i + 1] = edges[( i * 59 ) % edgeCount];
		}
		if ( check( tails[t], 0 ) ) return 1;
	}
	uint64_t index = 0;
	for ( size_t i = 0; i < edgeCount; ++i )
	{
		for ( size_t j = 0; j < edgeCount; ++j )
		{
			size_t slot = (size_t)( index % 4095 );
			aa[slot + 1] = edges[i]; bb[slot + 1] = edges[j];
			cc[slot + 1] = edges[( i + j ) % edgeCount];
			dd[slot + 1] = edges[j]; ee[slot + 1] = edges[i];
			ff[slot + 1] = edges[( i * 7 + j * 11 ) % edgeCount];
			++index;
			if ( slot == 4094 && check( 4095, index - 4095 ) ) return 1;
		}
	}
	if ( index % 4095 && check( (size_t)( index % 4095 ), index - index % 4095 ) ) return 1;
	for ( uint64_t base = 0; base < lanes; base += 4095 )
	{
		size_t n = (size_t)( lanes - base );
		if ( n > 4095 ) n = 4095;
		for ( size_t i = 0; i < n; ++i )
		{
			aa[i + 1] = (fixed_t)nextRand(); bb[i + 1] = (fixed_t)nextRand();
			cc[i + 1] = (fixed_t)nextRand(); dd[i + 1] = (fixed_t)nextRand();
			ee[i + 1] = (fixed_t)nextRand(); ff[i + 1] = (fixed_t)nextRand();
		}
		if ( check( n, base ) ) return 1;
	}
	printf( "BatchMulLaneEquivalenceTest PASS: variant %s; %zu structured edges crossed; %llu seeded lanes; mul and dot\n",
		fixIsaName(), edgeCount, (unsigned long long)lanes );
	return 0;
}
