// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
// oracle_check: run the output oracle and diff it against benchmark/oracle-baseline.txt.
// Exit non-zero on ANY difference, printing the first differing operation, the input
// index range the difference was narrowed to, and the dump command that names the first
// differing input exactly.
//
// This program is built TWICE by CMake: once against the library and once against
// fixed_emulated (FIX_FORCE_EMULATED_INT128). The SAME baseline file must match both, and
// that agreement is itself the cross-check of the emulated 128-bit pair.
//
//   fixed_oracle_check                     check against the committed baseline
//   fixed_oracle_check --write             regenerate the baseline (then inspect the diff)
//   fixed_oracle_check --op fixMul         one operation
//   fixed_oracle_check --dump fixMul 0 64  index/input/output triples for an index range
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The engine lives in benchmark/oracle.c and is linked into this executable. Its API is
// declared here rather than in a new header, because this card adds no header files.
extern int fixOracleOpCount( void );
extern const char* fixOracleOpName( int opIndex );
extern int fixOracleFindOp( const char* name );
extern uint64_t fixOracleHashOp( int opIndex, uint64_t baseRandom, uint64_t limit, uint64_t* segmentsOut,
								 uint64_t* countOut );
extern void fixOracleDumpOp( int opIndex, uint64_t baseRandom, uint64_t limit, uint64_t start, uint64_t end,
							 FILE* out );

#define ORACLE_SEGMENTS 16
// 50 million seeded draws per operation, before the weight divisor. -n overrides this.
// Otherwise the baseline file's base_random line wins, and this constant is only the
// fallback when that line is absent (and the value --write records in that case).
#define ORACLE_DEFAULT_BASE_RANDOM UINT64_C( 50000000 )

typedef struct OracleRow
{
	char name[64];
	uint64_t count;
	uint64_t hash;
	uint64_t segments[ORACLE_SEGMENTS];
	int bool_valid;
} OracleRow;

int main( int argc, char** argv )
{
	const char* baselinePath = "benchmark/oracle-baseline.txt";
	const char* onlyOp = NULL;
	uint64_t baseRandom = 0; // 0 = take it from the baseline file
	uint64_t limit = 0;
	bool write = false;
	int dump = -1;
	uint64_t dumpStart = 0;
	uint64_t dumpEnd = 64;

	for ( int i = 1; i < argc; ++i )
	{
		if ( strcmp( argv[i], "--write" ) == 0 ) write = true;
		else if ( strcmp( argv[i], "--baseline" ) == 0 && i + 1 < argc ) baselinePath = argv[++i];
		else if ( strcmp( argv[i], "--op" ) == 0 && i + 1 < argc ) onlyOp = argv[++i];
		else if ( strcmp( argv[i], "-n" ) == 0 && i + 1 < argc ) baseRandom = strtoull( argv[++i], NULL, 10 );
		else if ( strcmp( argv[i], "--limit" ) == 0 && i + 1 < argc ) limit = strtoull( argv[++i], NULL, 10 );
		else if ( strcmp( argv[i], "--dump" ) == 0 && i + 1 < argc )
		{
			dump = fixOracleFindOp( argv[++i] );
			if ( i + 2 < argc )
			{
				dumpStart = strtoull( argv[++i], NULL, 10 );
				dumpEnd = strtoull( argv[++i], NULL, 10 );
			}
		}
		else
		{
			fprintf( stderr, "usage: %s [--baseline FILE] [--op NAME] [-n BASE] [--limit N] [--write] "
							 "[--dump NAME START END]\n",
					 argv[0] );
			return 2;
		}
	}

	if ( onlyOp != NULL && fixOracleFindOp( onlyOp ) < 0 )
	{
		fprintf( stderr, "ORACLE: no such operation: %s\n", onlyOp );
		fprintf( stderr, "valid operations: " );
		for ( int i = 0; i < fixOracleOpCount(); ++i )
		{
			if ( i != 0 ) fprintf( stderr, ", " );
			fprintf( stderr, "%s", fixOracleOpName( i ) );
		}
		fprintf( stderr, "\n" );
		return 2;
	}

	// --- read the baseline (also for --dump, so a dump walks the same base_random the hash did) ---
	uint64_t fileBase = ORACLE_DEFAULT_BASE_RANDOM;
	static OracleRow rows[64];
	int rowCount = 0;
	FILE* f = fopen( baselinePath, "r" );
	if ( f == NULL && !write && dump < 0 )
	{
		fprintf( stderr, "ORACLE: cannot open baseline %s\n", baselinePath );
		return 2;
	}
	if ( f != NULL )
	{
		char line[4096];
		while ( fgets( line, sizeof line, f ) != NULL )
		{
			if ( line[0] == '#' )
			{
				const char* key = strstr( line, "base_random:" );
				if ( key != NULL ) fileBase = strtoull( key + strlen( "base_random:" ), NULL, 10 );
				continue;
			}
			if ( line[0] == '\n' || line[0] == '\r' || line[0] == '\0' ) continue;
			if ( rowCount >= 64 ) break;
			OracleRow* r = &rows[rowCount];
			memset( r, 0, sizeof *r );
			char* p = line;
			char* tab = strchr( p, '\t' );
			if ( tab == NULL ) continue;
			*tab = '\0';
			size_t nameLen = strlen( p );
			if ( nameLen >= sizeof r->name ) nameLen = sizeof r->name - 1;
			memcpy( r->name, p, nameLen );
			r->name[nameLen] = '\0';
			p = tab + 1;
			r->count = strtoull( p, &p, 10 );
			if ( *p == '\t' ) ++p;
			r->hash = strtoull( p, &p, 16 );
			for ( int s = 0; s < ORACLE_SEGMENTS; ++s )
			{
				if ( *p != '\t' ) break;
				++p;
				r->segments[s] = strtoull( p, &p, 16 );
			}
			r->bool_valid = true;
			++rowCount;
		}
		fclose( f );
	}
	if ( baseRandom == 0 ) baseRandom = fileBase;

	if ( dump >= 0 )
	{
		fixOracleDumpOp( dump, baseRandom, limit, dumpStart, dumpEnd, stdout );
		return 0;
	}

	const int ops = fixOracleOpCount();
	FILE* out = write ? fopen( baselinePath, "w" ) : stdout;
	if ( write )
	{
		if ( out == NULL )
		{
			fprintf( stderr, "ORACLE: cannot write %s\n", baselinePath );
			return 2;
		}
		fprintf( out, "# fixed output oracle baseline. Regenerate with: fixed_oracle_check --write\n" );
		fprintf( out, "# Every value is a hash of OUTPUT BITS over the input sets defined in benchmark/oracle.c.\n" );
		fprintf( out, "# The same file must match the native __int128 build and the FIX_FORCE_EMULATED_INT128 build.\n" );
		fprintf( out, "# base_random: %llu\n", ( unsigned long long )baseRandom );
		fprintf( out, "# columns: operation <TAB> input_count <TAB> hash <TAB> %d segment hashes\n", ORACLE_SEGMENTS );
	}

	int failures = 0;
	for ( int i = 0; i < ops; ++i )
	{
		const char* name = fixOracleOpName( i );
		if ( onlyOp != NULL && strcmp( onlyOp, name ) != 0 ) continue;
		uint64_t segments[ORACLE_SEGMENTS];
		uint64_t count = 0;
		uint64_t hash = fixOracleHashOp( i, baseRandom, limit, segments, &count );

		if ( write )
		{
			fprintf( out, "%s\t%llu\t%016llx", name, ( unsigned long long )count, ( unsigned long long )hash );
			for ( int s = 0; s < ORACLE_SEGMENTS; ++s ) fprintf( out, "\t%016llx", ( unsigned long long )segments[s] );
			fputc( '\n', out );
			continue;
		}

		const OracleRow* base = NULL;
		for ( int r = 0; r < rowCount; ++r )
		{
			if ( strcmp( rows[r].name, name ) == 0 )
			{
				base = &rows[r];
				break;
			}
		}
		if ( base == NULL || !base->bool_valid )
		{
			printf( "ORACLE FAIL %s: no baseline row (a new operation needs its hashes frozen in %s)\n", name,
					baselinePath );
			++failures;
			continue;
		}
		if ( base->count != count )
		{
			printf( "ORACLE FAIL %s: input count %llu, baseline %llu (the input set changed shape)\n", name,
					( unsigned long long )count, ( unsigned long long )base->count );
			++failures;
			continue;
		}
		if ( base->hash == hash )
		{
			printf( "ORACLE OK   %s  count=%llu  hash=%016llx\n", name, ( unsigned long long )count,
					( unsigned long long )hash );
			continue;
		}

		++failures;
		// Narrow to the segment whose running hash first diverges, then name the range.
		int seg = ORACLE_SEGMENTS - 1;
		for ( int s = 0; s < ORACLE_SEGMENTS; ++s )
		{
			if ( base->segments[s] != segments[s] )
			{
				seg = s;
				break;
			}
		}
		uint64_t step = count / ORACLE_SEGMENTS;
		uint64_t lo = ( uint64_t )seg * step;
		uint64_t hi = ( seg == ORACLE_SEGMENTS - 1 ) ? count : ( uint64_t )( seg + 1 ) * step;
		printf( "ORACLE FAIL %s: hash %016llx, baseline %016llx\n", name, ( unsigned long long )hash,
				( unsigned long long )base->hash );
		printf( "  segments" );
		for ( int s = 0; s < ORACLE_SEGMENTS; ++s ) printf( " %016llx", ( unsigned long long )segments[s] );
		printf( "\n" );
		printf( "  first differing input is in [%llu, %llu) of %llu inputs (segment %d)\n", ( unsigned long long )lo,
				( unsigned long long )hi, ( unsigned long long )count, seg );
		printf( "  name it exactly with: %s --dump %s %llu %llu   (diff two trees' dumps line by line)\n", argv[0], name,
				( unsigned long long )lo, ( unsigned long long )hi );
		if ( failures == 1 )
		{
			// Print the input at the head of the narrowed range: the first candidate.
			fflush( stdout );
			fixOracleDumpOp( i, baseRandom, limit, lo, lo + 1, stdout );
		}
	}

	if ( write )
	{
		fclose( out );
		printf( "ORACLE: wrote %s\n", baselinePath );
		return 0;
	}
	if ( failures != 0 )
	{
		printf( "ORACLE: %d operation(s) differ from %s\n", failures, baselinePath );
		return 1;
	}
	printf( "ORACLE: %s match %s (base_random=%llu)\n", onlyOp != NULL ? onlyOp : "all operations", baselinePath,
			( unsigned long long )baseRandom );
	return 0;
}
