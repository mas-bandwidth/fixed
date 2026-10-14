// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
//
// oracle_check: run the bit-exact output oracle (benchmark/oracle.c) against the library
// this binary was built with, and diff every operation's hash against the frozen baseline.
// Exits non-zero on any difference, naming the first differing operation and the first
// differing input it can locate.
//
//   oracle_check                         check against the baseline file
//   oracle_check --baseline <file>       ... against another file
//   oracle_check --ops fixMul,fixDiv     ... only these operations
//   oracle_check --against <binary>      on a difference, run <binary> (an oracle_check built
//                                        from the baseline tree) to find the exact first input
//   oracle_check --write <file> [--note <text>]   capture a baseline from this build
//   oracle_check --dump <op> <from> <to> print inputs [from, to) of <op> with their outputs
//   oracle_check --random <n>            random inputs per scalar operation (default 50M)
//   oracle_check --list                  the operation names
//
// The baseline locates a difference to a chunk by its checkpoint hashes (every 2^16 inputs
// below 2^20, every 2^22 after); --against narrows it to the exact input by dumping the
// chunk from both builds and comparing line by line.
#if !defined( _WIN32 ) && !defined( _POSIX_C_SOURCE )
#define _POSIX_C_SOURCE 200809L
#endif
#include "oracle.c"

#include <stdlib.h>

#if defined( _WIN32 )
#define ORACLE_POPEN _popen
#define ORACLE_PCLOSE _pclose
#else
#define ORACLE_POPEN popen
#define ORACLE_PCLOSE pclose
#endif

#ifndef FIX_ORACLE_BASELINE
#define FIX_ORACLE_BASELINE "benchmark/oracle-baseline.txt"
#endif

typedef struct Checkpoint
{
	uint64_t index, hash;
} Checkpoint;

typedef struct OpResult
{
	char name[128];
	uint64_t count, hash;
	Checkpoint* chk;
	int nchk, capchk;
	int present;
} OpResult;

static void pushCheckpoint( OpResult* r, uint64_t index, uint64_t hash )
{
	if ( r->nchk == r->capchk )
	{
		r->capchk = r->capchk ? 2 * r->capchk : 64;
		r->chk = (Checkpoint*)realloc( r->chk, (size_t)r->capchk * sizeof( Checkpoint ) );
		if ( !r->chk ) { fprintf( stderr, "oracle_check: out of memory\n" ); exit( 2 ); }
	}
	r->chk[r->nchk].index = index;
	r->chk[r->nchk].hash = hash;
	r->nchk++;
}

static void onCheckpoint( fixOracle* o, uint64_t index, uint64_t hash ) { pushCheckpoint( (OpResult*)o->user, index, hash ); }

static const char* armName( void )
{
#if FIX_INT128_EMULATED
	return "emulated-int128";
#else
	return "native-int128";
#endif
}

static void computeOp( int op, uint64_t randomCount, OpResult* r )
{
	fixOracle o;
	memset( &o, 0, sizeof( o ) );
	memset( r, 0, sizeof( *r ) );
	snprintf( r->name, sizeof( r->name ), "%s", fixOracleOps[op].name );
	o.randomCount = randomCount;
	o.checkpoint = onCheckpoint;
	o.user = r;
	r->hash = fixOracleRun( op, &o, UINT64_MAX );
	r->count = o.index;
	r->present = 1;
}

static void dumpOp( int op, uint64_t randomCount, uint64_t from, uint64_t to, FILE* out )
{
	fixOracle o;
	memset( &o, 0, sizeof( o ) );
	o.randomCount = randomCount;
	o.dump = out;
	o.dumpFrom = from;
	o.dumpTo = to;
	fixOracleRun( op, &o, to );
}

// ---- the baseline file ----

static OpResult baseline[FIX_ORACLE_OP_COUNT];
static uint64_t baselineRandom;

static int readBaseline( const char* path )
{
	FILE* f = fopen( path, "r" );
	if ( !f ) { fprintf( stderr, "oracle_check: cannot read baseline %s\n", path ); return 0; }
	char line[512], name[128];
	unsigned long long a, b;
	while ( fgets( line, sizeof( line ), f ) )
	{
		if ( line[0] == '#' || line[0] == '\n' ) continue;
		if ( sscanf( line, "random %llu", &a ) == 1 ) { baselineRandom = a; continue; }
		if ( sscanf( line, "op %127s count %llu hash %llx", name, &a, &b ) == 3 )
		{
			int op = fixOracleFind( name );
			if ( op < 0 ) { fprintf( stderr, "oracle_check: baseline names unknown operation %s\n", name ); fclose( f ); return 0; }
			snprintf( baseline[op].name, sizeof( baseline[op].name ), "%s", name );
			baseline[op].count = a;
			baseline[op].hash = b;
			baseline[op].present = 1;
			continue;
		}
		if ( sscanf( line, "chk %127s %llu %llx", name, &a, &b ) == 3 )
		{
			int op = fixOracleFind( name );
			if ( op < 0 ) { fprintf( stderr, "oracle_check: baseline names unknown operation %s\n", name ); fclose( f ); return 0; }
			pushCheckpoint( &baseline[op], a, b );
			continue;
		}
		fprintf( stderr, "oracle_check: unreadable baseline line: %s", line );
		fclose( f );
		return 0;
	}
	fclose( f );
	return 1;
}

static void writeOp( FILE* f, const OpResult* r )
{
	fprintf( f, "op %s count %llu hash %016llx\n", r->name, (unsigned long long)r->count, (unsigned long long)r->hash );
	for ( int i = 0; i < r->nchk; ++i )
		fprintf( f, "chk %s %llu %016llx\n", r->name, (unsigned long long)r->chk[i].index, (unsigned long long)r->chk[i].hash );
}

// ---- locating a difference ----

/// The chunk [lo, hi) holding the first differing input, from the checkpoints.
static void firstDifferingChunk( const OpResult* got, const OpResult* want, uint64_t* lo, uint64_t* hi )
{
	*lo = 0;
	*hi = got->count < want->count ? got->count : want->count;
	int n = got->nchk < want->nchk ? got->nchk : want->nchk;
	for ( int i = 0; i < n; ++i )
	{
		if ( got->chk[i].index != want->chk[i].index ) { *hi = got->chk[i].index; return; }
		if ( got->chk[i].hash != want->chk[i].hash ) { *hi = got->chk[i].index; return; }
		*lo = got->chk[i].index;
	}
	if ( got->count != want->count && *hi <= *lo ) *hi = *lo + 1;
}

static int readLine( FILE* f, char* buf, size_t size )
{
	if ( !fgets( buf, (int)size, f ) ) return 0;
	size_t n = strlen( buf );
	if ( n && buf[n - 1] == '\n' ) buf[n - 1] = 0;
	return 1;
}

/// Dump [lo, hi) of op from this build and from `against`, print the first line that differs.
static void exactDifference( int op, uint64_t randomCount, uint64_t lo, uint64_t hi, const char* against )
{
	FILE* mine = tmpfile();
	if ( !mine ) { printf( "  exact input: cannot open a temporary file\n" ); return; }
	dumpOp( op, randomCount, lo, hi, mine );
	rewind( mine );
	char cmd[1024];
	snprintf( cmd, sizeof( cmd ), "\"%s\" --random %llu --dump %s %llu %llu", against, (unsigned long long)randomCount,
			  fixOracleOps[op].name, (unsigned long long)lo, (unsigned long long)hi );
	FILE* theirs = ORACLE_POPEN( cmd, "r" );
	if ( !theirs ) { printf( "  exact input: cannot run %s\n", cmd ); fclose( mine ); return; }
	char a[1024], b[1024];
	int found = 0;
	for ( ;; )
	{
		int ha = readLine( mine, a, sizeof( a ) ), hb = readLine( theirs, b, sizeof( b ) );
		if ( !ha && !hb ) break;
		if ( !ha || !hb || strcmp( a, b ) != 0 )
		{
			printf( "  first differing input (this build): %s\n", ha ? a : "(none)" );
			printf( "  first differing input (baseline):   %s\n", hb ? b : "(none)" );
			found = 1;
			break;
		}
	}
	if ( !found ) printf( "  exact input: the two dumps of [%llu, %llu) agree; the difference is in the input count\n",
						  (unsigned long long)lo, (unsigned long long)hi );
	ORACLE_PCLOSE( theirs );
	fclose( mine );
}

static int selected( const char* list, const char* name )
{
	if ( !list ) return 1;
	size_t n = strlen( name );
	for ( const char* p = list; *p; )
	{
		const char* comma = strchr( p, ',' );
		size_t len = comma ? (size_t)( comma - p ) : strlen( p );
		if ( len == n && strncmp( p, name, n ) == 0 ) return 1;
		if ( !comma ) break;
		p = comma + 1;
	}
	return 0;
}

int main( int argc, char** argv )
{
	const char* baselinePath = FIX_ORACLE_BASELINE;
	const char* writePath = NULL;
	const char* ops = NULL;
	const char* against = NULL;
	const char* note = "";
	uint64_t randomCount = ORACLE_DEFAULT_RANDOM;
	int randomGiven = 0;

	for ( int i = 1; i < argc; ++i )
	{
		if ( strcmp( argv[i], "--baseline" ) == 0 && i + 1 < argc ) baselinePath = argv[++i];
		else if ( strcmp( argv[i], "--write" ) == 0 && i + 1 < argc ) writePath = argv[++i];
		else if ( strcmp( argv[i], "--ops" ) == 0 && i + 1 < argc ) ops = argv[++i];
		else if ( strcmp( argv[i], "--against" ) == 0 && i + 1 < argc ) against = argv[++i];
		else if ( strcmp( argv[i], "--note" ) == 0 && i + 1 < argc ) note = argv[++i];
		else if ( strcmp( argv[i], "--random" ) == 0 && i + 1 < argc ) { randomCount = strtoull( argv[++i], NULL, 10 ); randomGiven = 1; }
		else if ( strcmp( argv[i], "--list" ) == 0 )
		{
			for ( int op = 0; op < FIX_ORACLE_OP_COUNT; ++op ) printf( "%s\n", fixOracleOps[op].name );
			return 0;
		}
		else if ( strcmp( argv[i], "--dump" ) == 0 && i + 3 < argc )
		{
			int op = fixOracleFind( argv[i + 1] );
			if ( op < 0 ) { fprintf( stderr, "oracle_check: unknown operation %s\n", argv[i + 1] ); return 2; }
			uint64_t from = strtoull( argv[i + 2], NULL, 10 ), to = strtoull( argv[i + 3], NULL, 10 );
			for ( int k = i + 4; k + 1 < argc; ++k )
				if ( strcmp( argv[k], "--random" ) == 0 ) randomCount = strtoull( argv[k + 1], NULL, 10 );
			dumpOp( op, randomCount, from, to, stdout );
			return 0;
		}
		else
		{
			fprintf( stderr, "oracle_check: unknown argument %s (see the comment at the top of oracle_check.c)\n", argv[i] );
			return 2;
		}
	}

	if ( writePath )
	{
		FILE* f = fopen( writePath, "w" );
		if ( !f ) { fprintf( stderr, "oracle_check: cannot write %s\n", writePath ); return 2; }
		fprintf( f, "# The bit-exact output oracle's frozen hashes (benchmark/oracle.c, benchmark/README.md).\n" );
		fprintf( f, "# Captured on the %s arm. %s\n", armName(), note );
		fprintf( f, "# op <name> count <inputs> hash <hash of every output>; chk <name> <index> <prefix hash>.\n" );
		fprintf( f, "random %llu\n", (unsigned long long)randomCount );
		for ( int op = 0; op < FIX_ORACLE_OP_COUNT; ++op )
		{
			if ( !selected( ops, fixOracleOps[op].name ) ) continue;
			OpResult r;
			computeOp( op, randomCount, &r );
			writeOp( f, &r );
			printf( "captured %s count=%llu hash=%016llx\n", r.name, (unsigned long long)r.count, (unsigned long long)r.hash );
			fflush( stdout );
			free( r.chk );
		}
		fclose( f );
		return 0;
	}

	if ( !readBaseline( baselinePath ) ) return 2;
	if ( randomGiven && randomCount != baselineRandom )
	{
		fprintf( stderr, "oracle_check: --random %llu differs from the baseline's %llu\n", (unsigned long long)randomCount,
				 (unsigned long long)baselineRandom );
		return 2;
	}
	randomCount = baselineRandom;
	printf( "oracle_check arm=%s baseline=%s random=%llu\n", armName(), baselinePath, (unsigned long long)randomCount );

	int failures = 0, checked = 0;
	for ( int op = 0; op < FIX_ORACLE_OP_COUNT; ++op )
	{
		if ( !selected( ops, fixOracleOps[op].name ) ) continue;
		if ( !baseline[op].present )
		{
			printf( "FAIL %s: not in the baseline file\n", fixOracleOps[op].name );
			failures++;
			continue;
		}
		OpResult r;
		computeOp( op, randomCount, &r );
		checked++;
		if ( r.count == baseline[op].count && r.hash == baseline[op].hash )
		{
			printf( "ok   %s count=%llu hash=%016llx\n", r.name, (unsigned long long)r.count, (unsigned long long)r.hash );
		}
		else
		{
			uint64_t lo, hi;
			firstDifferingChunk( &r, &baseline[op], &lo, &hi );
			printf( "FAIL %s count=%llu hash=%016llx, baseline count=%llu hash=%016llx\n", r.name, (unsigned long long)r.count,
					(unsigned long long)r.hash, (unsigned long long)baseline[op].count, (unsigned long long)baseline[op].hash );
			printf( "  first difference among inputs [%llu, %llu); the first of them here:\n", (unsigned long long)lo,
					(unsigned long long)hi );
			printf( "  " );
			fflush( stdout );
			dumpOp( op, randomCount, lo, lo + 1, stdout );
			if ( against && failures == 0 ) exactDifference( op, randomCount, lo, hi, against );
			else if ( failures == 0 )
				printf( "  for the exact input: oracle_check --against <oracle_check built from the baseline tree> --ops %s\n",
						r.name );
			failures++;
		}
		fflush( stdout );
		free( r.chk );
	}
	if ( failures )
	{
		printf( "ORACLE FAILED: %d of %d operations differ from %s\n", failures, checked, baselinePath );
		return 1;
	}
	printf( "ORACLE OK: %d operations bit-identical to %s\n", checked, baselinePath );
	return 0;
}
