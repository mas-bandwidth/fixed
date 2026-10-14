// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
//
// Runs the fixed output oracle and diffs it against benchmark/oracle-baseline.txt.
// Exits non-zero on the first differing operation, printing its name, the frozen hash,
// the recomputed hash, and the first inputs that feed it. The SAME hashes must come out
// of the native and the emulated (FIX_FORCE_EMULATED_INT128) arms, so this program is
// built once against each and both must pass against the one baseline.
#define ORACLE_LIB
#include "oracle.c"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main( int argc, char** argv )
{
	const char* baseline = argc > 1 ? argv[1] : "benchmark/oracle-baseline.txt";
	uint64_t randomCount = argc > 2 ? (uint64_t)strtoull( argv[2], NULL, 10 ) : oracle_default_random_count();
	uint64_t exhaustiveLimit = argc > 3 ? (uint64_t)strtoull( argv[3], NULL, 10 ) : oracle_default_exhaustive_limit();

	FILE* f = fopen( baseline, "r" );
	if ( !f )
	{
		fprintf( stderr, "oracle_check: cannot open %s\n", baseline );
		return 2;
	}

	int mismatches = 0;
	char line[256];
	int lineNo = 0;
	while ( fgets( line, sizeof( line ), f ) )
	{
		lineNo++;
		// strip trailing newline / spaces
		size_t len = strlen( line );
		while ( len && ( line[len - 1] == '\n' || line[len - 1] == '\r' || line[len - 1] == ' ' ) )
			line[--len] = '\0';
		if ( len == 0 || line[0] == '#' )
			continue;

		char name[64];
		uint64_t expected;
		if ( sscanf( line, "%63s\t%16" SCNx64, name, &expected ) != 2 )
		{
			// tolerate a single-space separator too
			if ( sscanf( line, "%63s %16" SCNx64, name, &expected ) != 2 )
			{
				fprintf( stderr, "oracle_check: malformed baseline line %d: %s\n", lineNo, line );
				fclose( f );
				return 2;
			}
		}

		uint64_t got = oracle_hash_one( name, randomCount, exhaustiveLimit );
		if ( got != expected )
		{
			if ( mismatches == 0 )
			{
				fprintf( stderr, "DIFF %s: expected %016" PRIx64 " got %016" PRIx64 "\n", name, expected, got );
			}
			else
			{
				fprintf( stderr, "DIFF %s: expected %016" PRIx64 " got %016" PRIx64 "\n", name, expected, got );
			}
			mismatches++;
		}
	}
	fclose( f );

	if ( mismatches )
	{
		fprintf( stderr, "oracle_check: %d operation(s) differ\n", mismatches );
		return 1;
	}

	printf( "oracle_check: %d operation(s) match baseline\n", lineNo );
	return 0;
}
