/* SPDX-License-Identifier: MIT */
/* The sweep engine: domain pieces, seeded draws, pthreads with contiguous blocks, the
 * not-correctly-rounded log, the per-function scoreboard and --emit-points.
 *
 * Every reference is MPFR at 256 bits, RN(f(raw * 2^-16) * 2^16) to nearest-even, with the
 * documented saturation and out-of-domain results taken as exact values. Errors are in raw
 * units (2^-16).
 */
#define _GNU_SOURCE
#include "sweep.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef FIX_SOURCE_DIR
#define FIX_SOURCE_DIR "."
#endif
#ifndef FIX_SWEEP_ARM
#define FIX_SWEEP_ARM "unknown"
#endif

#define WORST_CAP 256
#define SEED 0x9E3779B97F4A7C15ULL
#define FNV_OFFSET 0xCBF29CE484222325ULL
#define FNV_PRIME 0x00000100000001B3ULL

typedef struct
{
	uint64_t err;
	int64_t a;
	int64_t b;
	int64_t result;
	size_t idx;
} worst_t;

typedef struct
{
	uint64_t maxerr;
	uint64_t notcr;
	worst_t worst[WORST_CAP];
	size_t nworst;
} acc_t;

static uint64_t absdiff( int64_t a, int64_t b )
{
	if ( a >= b )
	{
		return (uint64_t)a - (uint64_t)b;
	}
	return (uint64_t)b - (uint64_t)a;
}

static int better( const worst_t* x, const worst_t* y )
{
	if ( x->err != y->err )
	{
		return x->err > y->err;
	}
	return x->idx < y->idx;
}

static void acc_insert( acc_t* acc, worst_t c )
{
	if ( acc->nworst < WORST_CAP )
	{
		acc->worst[acc->nworst++] = c;
		return;
	}
	size_t w = 0;
	for ( size_t i = 1; i < acc->nworst; ++i )
	{
		if ( better( &acc->worst[w], &acc->worst[i] ) )
		{
			w = i;
		}
	}
	if ( better( &c, &acc->worst[w] ) )
	{
		acc->worst[w] = c;
	}
}

static int cmp_worst( const void* pa, const void* pb )
{
	const worst_t* a = (const worst_t*)pa;
	const worst_t* b = (const worst_t*)pb;
	if ( a->err != b->err )
	{
		return a->err > b->err ? -1 : 1;
	}
	if ( a->idx != b->idx )
	{
		return a->idx < b->idx ? -1 : 1;
	}
	return 0;
}

static uint64_t fnv_add( uint64_t h, int64_t v )
{
	uint64_t u = (uint64_t)v;
	for ( int i = 0; i < 8; ++i )
	{
		h ^= ( u >> ( 8 * i ) ) & 0xffu;
		h *= FNV_PRIME;
	}
	return h;
}

static uint64_t xs_next( uint64_t* s )
{
	uint64_t x = *s;
	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	*s = x;
	return x;
}

static int64_t xs_draw( uint64_t* s, int64_t lo, int64_t hi )
{
	uint64_t span = (uint64_t)( hi - lo );
	if ( span == 0 )
	{
		return lo;
	}
	return lo + (int64_t)( xs_next( s ) % span );
}

mpfr_t* sweep_mpfr_tmp( void )
{
	static __thread mpfr_t v;
	static __thread int ready = 0;
	if ( !ready )
	{
		mpfr_init2( v, 256 );
		ready = 1;
	}
	return &v;
}

int64_t sweep_mpfr_round( mpfr_t v )
{
	mpfr_mul_2si( v, v, 16, MPFR_RNDN );
	mpfr_rint( v, v, MPFR_RNDN );
	if ( mpfr_cmp_si( v, (long)INT64_MAX ) > 0 )
	{
		return INT64_MAX;
	}
	if ( mpfr_cmp_si( v, (long)INT64_MIN ) < 0 )
	{
		return INT64_MIN;
	}
	return (int64_t)mpfr_get_si( v, MPFR_RNDZ );
}

typedef struct
{
	const sweep_fn* f;
	const int64_t* as;
	const int64_t* bs;
	size_t begin;
	size_t end;
	int64_t* res;
	unsigned char* flags;
	acc_t acc;
} job_t;

static void* worker( void* p )
{
	job_t* j = (job_t*)p;
	memset( &j->acc, 0, sizeof( j->acc ) );
	for ( size_t i = j->begin; i < j->end; ++i )
	{
		int64_t a = j->as[i];
		int64_t b = j->bs ? j->bs[i] : 0;
		int64_t r = j->f->fn( a, b );
		int64_t ref = j->f->ref( a, b );
		uint64_t e = absdiff( r, ref );
		j->res[i] = r;
		j->flags[i] = (unsigned char)( r != ref );
		if ( e > j->acc.maxerr )
		{
			j->acc.maxerr = e;
		}
		if ( r != ref )
		{
			j->acc.notcr++;
		}
		worst_t c;
		c.err = e;
		c.a = a;
		c.b = b;
		c.result = r;
		c.idx = i;
		acc_insert( &j->acc, c );
	}
	return NULL;
}

static void process_piece( const sweep_fn* f, const int64_t* as, const int64_t* bs, size_t n, const char* label, const char* mode,
						   int threads, FILE* doc, FILE* nf, acc_t* global )
{
	if ( n == 0 )
	{
		return;
	}
	int64_t* res = (int64_t*)malloc( n * sizeof( int64_t ) );
	unsigned char* flags = (unsigned char*)calloc( n, 1 );
	if ( !res || !flags )
	{
		fprintf( stderr, "sweep: out of memory for %zu inputs\n", n );
		exit( 1 );
	}
	if ( (size_t)threads > n )
	{
		threads = (int)n;
	}
	if ( threads < 1 )
	{
		threads = 1;
	}
	if ( threads > 64 )
	{
		threads = 64;
	}
	pthread_t tid[64];
	job_t jobs[64];
	size_t base = n / (size_t)threads;
	size_t rem = n % (size_t)threads;
	size_t at = 0;
	for ( int t = 0; t < threads; ++t )
	{
		size_t len = base + ( (size_t)t < rem ? 1 : 0 );
		jobs[t].f = f;
		jobs[t].as = as;
		jobs[t].bs = bs;
		jobs[t].begin = at;
		jobs[t].end = at + len;
		jobs[t].res = res;
		jobs[t].flags = flags;
		at += len;
	}
	for ( int t = 1; t < threads; ++t )
	{
		if ( pthread_create( &tid[t], NULL, worker, &jobs[t] ) != 0 )
		{
			fprintf( stderr, "sweep: pthread_create failed\n" );
			exit( 1 );
		}
	}
	worker( &jobs[0] );
	for ( int t = 1; t < threads; ++t )
	{
		pthread_join( tid[t], NULL );
	}

	acc_t pacc;
	memset( &pacc, 0, sizeof( pacc ) );
	for ( int t = 0; t < threads; ++t )
	{
		if ( jobs[t].acc.maxerr > pacc.maxerr )
		{
			pacc.maxerr = jobs[t].acc.maxerr;
		}
		pacc.notcr += jobs[t].acc.notcr;
		for ( size_t i = 0; i < jobs[t].acc.nworst; ++i )
		{
			acc_insert( &pacc, jobs[t].acc.worst[i] );
		}
	}

	uint64_t hash = FNV_OFFSET;
	for ( size_t i = 0; i < n; ++i )
	{
		hash = fnv_add( hash, res[i] );
		if ( flags[i] )
		{
			if ( f->arity == 1 )
			{
				fprintf( nf, "%" PRId64 "\n", as[i] );
			}
			else
			{
				fprintf( nf, "%" PRId64 " %" PRId64 "\n", as[i], bs[i] );
			}
		}
	}

	if ( pacc.nworst > 0 )
	{
		qsort( pacc.worst, pacc.nworst, sizeof( worst_t ), cmp_worst );
	}
	int64_t worst_raw = pacc.nworst > 0 ? pacc.worst[0].a : 0;

	printf( "sweep fn=%s piece=[%s) mode=%s inputs=%zu maxerr=%" PRIu64 " notcr=%" PRIu64 " worst=%" PRId64 " hash=%016" PRIx64 "\n",
			f->name, label, mode, n, pacc.maxerr, pacc.notcr, worst_raw, hash );
	fflush( stdout );
	if ( doc )
	{
		fprintf( doc, "| [%s) | %s | %zu | %" PRIu64 " | %" PRIu64 " | %" PRId64 " | %016" PRIx64 " |\n", label, mode, n,
				 pacc.maxerr, pacc.notcr, worst_raw, hash );
	}
	for ( size_t i = 0; i < pacc.nworst; ++i )
	{
		acc_insert( global, pacc.worst[i] );
	}

	free( res );
	free( flags );
}

static void fill_exhaustive( int64_t* as, int64_t lo, size_t n )
{
	for ( size_t i = 0; i < n; ++i )
	{
		as[i] = lo + (int64_t)i;
	}
}

static void fill_sampled( int64_t* as, int64_t* bs, int64_t lo, int64_t hi, int64_t blo, int64_t bhi, size_t n, int binary )
{
	uint64_t s = SEED;
	for ( size_t i = 0; i < n; ++i )
	{
		as[i] = xs_draw( &s, lo, hi );
		if ( binary )
		{
			bs[i] = xs_draw( &s, blo, bhi );
		}
	}
}

static void piece_label( char* out, size_t cap, int64_t lo, int64_t hi )
{
	snprintf( out, cap, "%" PRId64 ",%" PRId64, lo, hi );
}

static void run_point_list( const sweep_fn* f, const int64_t* as, const int64_t* bs, size_t n, const char* label, int threads, FILE* doc,
							FILE* nf, acc_t* global )
{
	process_piece( f, as, bs, n, label, "exhaustive", threads, doc, nf, global );
}

static void mkdir_p( const char* path )
{
	char tmp[4096];
	snprintf( tmp, sizeof( tmp ), "%s", path );
	for ( char* p = tmp + 1; *p; ++p )
	{
		if ( *p == '/' )
		{
			*p = 0;
			mkdir( tmp, 0755 );
			*p = '/';
		}
	}
	mkdir( tmp, 0755 );
}

static void exe_dir( char* out, size_t cap )
{
	ssize_t k = readlink( "/proc/self/exe", out, cap - 1 );
	if ( k <= 0 )
	{
		snprintf( out, cap, "." );
		return;
	}
	out[k] = 0;
	char* p = strrchr( out, '/' );
	if ( p )
	{
		*p = 0;
	}
}

static void sanitize( char* out, size_t cap, const char* name )
{
	size_t j = 0;
	for ( size_t i = 0; name[i] && j + 1 < cap; ++i )
	{
		char c = name[i];
		out[j++] = ( ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) ) ? c : '_';
	}
	out[j] = 0;
}

static void emit_points( const sweep_fn* f, const char* path, int64_t dlo, int64_t dhi, acc_t* global, int threads )
{
	if ( f->arity != 1 )
	{
		fprintf( stderr, "sweep: --emit-points supports unary functions only\n" );
		return;
	}
	if ( global->nworst > 0 )
	{
		qsort( global->worst, global->nworst, sizeof( worst_t ), cmp_worst );
	}
	size_t nworst = global->nworst < 256 ? global->nworst : 256;
	size_t n = f->nedges + 4096 + nworst;
	int64_t* raw = (int64_t*)malloc( n * sizeof( int64_t ) );
	int64_t* expected = (int64_t*)malloc( n * sizeof( int64_t ) );
	if ( !raw || !expected )
	{
		fprintf( stderr, "sweep: out of memory emitting points\n" );
		free( raw );
		free( expected );
		return;
	}
	size_t k = 0;
	for ( size_t i = 0; i < f->nedges; ++i )
	{
		raw[k++] = f->edges[i];
	}
	uint64_t s = SEED;
	for ( int i = 0; i < 4096; ++i )
	{
		raw[k++] = xs_draw( &s, dlo, dhi );
	}
	for ( size_t i = 0; i < nworst; ++i )
	{
		raw[k++] = global->worst[i].a;
	}
	for ( size_t i = 0; i < n; ++i )
	{
		expected[i] = f->ref( raw[i], 0 );
	}
	char id[128];
	char upper[128];
	sanitize( id, sizeof( id ), f->name );
	snprintf( upper, sizeof( upper ), "%s", id );
	for ( char* q = upper; *q; ++q )
	{
		if ( *q >= 'a' && *q <= 'z' )
		{
			*q = (char)( *q - 'a' + 'A' );
		}
	}
	char dir[4096];
	snprintf( dir, sizeof( dir ), "%s", path );
	char* slash = strrchr( dir, '/' );
	if ( slash )
	{
		*slash = 0;
		mkdir_p( dir );
	}
	FILE* fp = fopen( path, "w" );
	if ( !fp )
	{
		fprintf( stderr, "sweep: cannot write %s\n", path );
		free( raw );
		free( expected );
		return;
	}
	fprintf( fp, "/* Generated by fixed_sweep --fn %s --emit-points. Do not edit. */\n", f->name );
	fprintf( fp, "/* Every libfixmath edge, 4096 xorshift64 draws (seed 0x9E3779B97F4A7C15), and the\n" );
	fprintf( fp, " * %zu worst inputs of the sweep, with correctly rounded raw outputs. */\n", nworst );
	fprintf( fp, "#pragma once\n#include <stdint.h>\n\n" );
	fprintf( fp, "#define %s_POINTS_COUNT %zu\n\n", upper, n );
	fprintf( fp, "static const int64_t %s_points_raw[%s_POINTS_COUNT] = {\n", id, upper );
	for ( size_t i = 0; i < n; ++i )
	{
		fprintf( fp, "\t%" PRId64 ",%s", raw[i], ( i % 8 == 7 ) ? "\n" : " " );
	}
	fprintf( fp, "\n};\n\n" );
	fprintf( fp, "static const int64_t %s_points_expected[%s_POINTS_COUNT] = {\n", id, upper );
	for ( size_t i = 0; i < n; ++i )
	{
		fprintf( fp, "\t%" PRId64 ",%s", expected[i], ( i % 8 == 7 ) ? "\n" : " " );
	}
	fprintf( fp, "\n};\n" );
	fclose( fp );
	printf( "sweep emitted points fn=%s inputs=%zu file=%s\n", f->name, n, path );
	free( raw );
	free( expected );
	( void )threads;
}

static int run_fn( const sweep_fn* f, int threads, const char* emit_path )
{
	char docpath[4096];
	snprintf( docpath, sizeof( docpath ), "%s/docs/scoreboard", FIX_SOURCE_DIR );
	mkdir_p( docpath );
	char docfile[4096];
	snprintf( docfile, sizeof( docfile ), "%s/%s.md", docpath, f->name );
	FILE* doc = fopen( docfile, "w" );
	if ( !doc )
	{
		fprintf( stderr, "sweep: cannot write %s\n", docfile );
		return 1;
	}

	char dir[4096];
	exe_dir( dir, sizeof( dir ) );
	char notcrdir[4096];
	snprintf( notcrdir, sizeof( notcrdir ), "%s/sweep", dir );
	mkdir_p( notcrdir );
	char notcrfile[4096];
	snprintf( notcrfile, sizeof( notcrfile ), "%s/%s.notcr", notcrdir, f->name );
	FILE* nf = fopen( notcrfile, "w" );
	if ( !nf )
	{
		fprintf( stderr, "sweep: cannot write %s\n", notcrfile );
		fclose( doc );
		return 1;
	}
	static char nfbuf[1 << 20];
	setvbuf( nf, nfbuf, _IOFBF, sizeof( nfbuf ) );

	fprintf( doc, "# %s\n\n", f->name );
	fprintf( doc, "Arm: `%s`. Reference: MPFR 256 bits, RN(f(raw * 2^-16) * 2^16) to nearest-even,\n", FIX_SWEEP_ARM );
	fprintf( doc, "error in raw units (2^-16). Saturating and documented out-of-domain results are\ncomputed as exact values.\n\n" );
	fprintf( doc, "Command: `fixed_sweep --fn %s --threads %d`\n\n", f->name, threads );
	fprintf( doc, "| piece | mode | inputs | max error | not correctly rounded | worst input | hash |\n" );
	fprintf( doc, "|---|---|---|---|---|---|---|\n" );

	acc_t global;
	memset( &global, 0, sizeof( global ) );
	int64_t dlo = INT64_MAX;
	int64_t dhi = INT64_MIN;

	if ( f->npoints > 0 )
	{
		size_t n = f->npoints;
		int64_t* as = (int64_t*)malloc( n * sizeof( int64_t ) );
		int64_t* bs = (int64_t*)malloc( n * sizeof( int64_t ) );
		int64_t lo = INT64_MAX, hi = INT64_MIN;
		for ( size_t i = 0; i < n; ++i )
		{
			as[i] = f->points[i].a;
			bs[i] = f->points[i].b;
			if ( as[i] < lo ) lo = as[i];
			if ( as[i] > hi ) hi = as[i];
		}
		char label[128];
		piece_label( label, sizeof( label ), lo, hi );
		run_point_list( f, as, bs, n, label, threads, doc, nf, &global );
		free( as );
		free( bs );
	}

	if ( f->nedges > 0 )
	{
		size_t n = f->arity == 1 ? f->nedges : 2 * f->nedges;
		int64_t* as = (int64_t*)malloc( n * sizeof( int64_t ) );
		int64_t* bs = (int64_t*)malloc( n * sizeof( int64_t ) );
		size_t k = 0;
		for ( size_t i = 0; i < f->nedges; ++i )
		{
			as[k] = f->edges[i];
			bs[k] = 0;
			k++;
		}
		if ( f->arity == 2 )
		{
			for ( size_t i = 0; i < f->nedges; ++i )
			{
				as[k] = 65536;
				bs[k] = f->edges[i];
				k++;
			}
		}
		int64_t lo = INT64_MAX, hi = INT64_MIN;
		for ( size_t i = 0; i < n; ++i )
		{
			if ( as[i] < lo ) lo = as[i];
			if ( as[i] > hi ) hi = as[i];
		}
		char label[128];
		piece_label( label, sizeof( label ), lo, hi );
		run_point_list( f, as, bs, n, label, threads, doc, nf, &global );
		free( as );
		free( bs );
	}

	for ( size_t pi = 0; pi < f->npieces; ++pi )
	{
		const sweep_piece* p = &f->pieces[pi];
		if ( p->mode == SWEEP_EXHAUSTIVE )
		{
			size_t n = (size_t)( p->hi - p->lo );
			int64_t* as = (int64_t*)malloc( ( n ? n : 1 ) * sizeof( int64_t ) );
			fill_exhaustive( as, p->lo, n );
			char label[128];
			piece_label( label, sizeof( label ), p->lo, p->hi );
			process_piece( f, as, NULL, n, label, "exhaustive", threads, doc, nf, &global );
			free( as );
			if ( p->lo < dlo ) dlo = p->lo;
			if ( p->hi > dhi ) dhi = p->hi;
		}
		else if ( p->mode == SWEEP_SAMPLED )
		{
			size_t n = (size_t)p->count;
			int64_t* as = (int64_t*)malloc( ( n ? n : 1 ) * sizeof( int64_t ) );
			int64_t* bs = f->arity == 2 ? (int64_t*)malloc( ( n ? n : 1 ) * sizeof( int64_t ) ) : NULL;
			fill_sampled( as, bs, p->lo, p->hi, p->blo, p->bhi, n, f->arity == 2 );
			char label[128];
			piece_label( label, sizeof( label ), p->lo, p->hi );
			process_piece( f, as, bs, n, label, "sampled", threads, doc, nf, &global );
			free( as );
			free( bs );
			if ( p->lo < dlo ) dlo = p->lo;
			if ( p->hi > dhi ) dhi = p->hi;
		}
		else if ( p->mode == SWEEP_PER_BINADE )
		{
			int64_t b = p->lo;
			while ( b > 0 && b < p->hi )
			{
				int64_t e = ( b > p->hi / 2 ) ? p->hi : b * 2;
				if ( e <= b )
				{
					break;
				}
				size_t n = (size_t)p->count;
				int64_t* as = (int64_t*)malloc( ( n ? n : 1 ) * sizeof( int64_t ) );
				int64_t* bs = f->arity == 2 ? (int64_t*)malloc( ( n ? n : 1 ) * sizeof( int64_t ) ) : NULL;
				fill_sampled( as, bs, b, e, p->blo, p->bhi, n, f->arity == 2 );
				char label[128];
				piece_label( label, sizeof( label ), b, e );
				process_piece( f, as, bs, n, label, "sampled", threads, doc, nf, &global );
				free( as );
				free( bs );
				if ( b < dlo ) dlo = b;
				if ( e > dhi ) dhi = e;
				b = e;
			}
		}
	}

	if ( global.nworst > 0 )
	{
		qsort( global.worst, global.nworst, sizeof( worst_t ), cmp_worst );
	}
	size_t nshow = global.nworst < 32 ? global.nworst : 32;
	fprintf( doc, "\n## Worst %zu inputs\n\n| input | result | reference | error |\n|---|---|---|---|\n", nshow );
	for ( size_t i = 0; i < nshow; ++i )
	{
		const worst_t* w = &global.worst[i];
		int64_t ref = f->ref( w->a, w->b );
		if ( f->arity == 1 )
		{
			fprintf( doc, "| %" PRId64 " | %" PRId64 " | %" PRId64 " | %" PRIu64 " |\n", w->a, w->result, ref, w->err );
		}
		else
		{
			fprintf( doc, "| %" PRId64 ",%" PRId64 " | %" PRId64 " | %" PRId64 " | %" PRIu64 " |\n", w->a, w->b, w->result, ref,
					 w->err );
		}
	}
	fprintf( doc, "\nThe hash column is FNV-1a over every output, little-endian bytes, in input order;\n" );
	fprintf( doc, "it is thread-count independent. Regenerate with the command above; the nightly\n" );
	fprintf( doc, "workflow runs every descriptor on Linux with MPFR.\n" );
	fclose( doc );
	fclose( nf );

	if ( emit_path )
	{
		emit_points( f, emit_path, dlo, dhi, &global, threads );
	}
	return 0;
}

/* CTest before 3.29 has no test property that excludes a labelled test from the default
 * run while still selecting it with -L. So a sweep test carries --ctest-label-guard and runs
 * only when the parent ctest command line asks for the slow (or sweep) label. A direct run
 * of fixed_sweep never passes the flag and is unaffected. */
static int ctest_requests_slow( void )
{
	char path[64];
	snprintf( path, sizeof( path ), "/proc/%d/cmdline", (int)getppid() );
	FILE* fp = fopen( path, "rb" );
	if ( !fp )
	{
		return 0;
	}
	char buf[8192];
	size_t n = fread( buf, 1, sizeof( buf ) - 1, fp );
	fclose( fp );
	buf[n] = 0;
	int want = 0;
	for ( size_t i = 0; i < n; )
	{
		const char* t = buf + i;
		size_t l = strlen( t );
		if ( want )
		{
			if ( strstr( t, "slow" ) || strstr( t, "sweep" ) )
			{
				return 1;
			}
			want = 0;
		}
		else if ( strcmp( t, "-L" ) == 0 || strcmp( t, "--label-regex" ) == 0 )
		{
			want = 1;
		}
		i += l + 1;
	}
	return 0;
}

int main( int argc, char** argv )
{
	const char* only = NULL;
	const char* emit_path = NULL;
	int threads = 1;
	int label_guard = 0;
	for ( int i = 1; i < argc; ++i )
	{
		if ( strcmp( argv[i], "--fn" ) == 0 && i + 1 < argc )
		{
			only = argv[++i];
		}
		else if ( strcmp( argv[i], "--threads" ) == 0 && i + 1 < argc )
		{
			threads = atoi( argv[++i] );
		}
		else if ( strcmp( argv[i], "--ctest-label-guard" ) == 0 )
		{
			label_guard = 1;
		}
		else if ( strcmp( argv[i], "--emit-points" ) == 0 && i + 1 < argc )
		{
			emit_path = argv[++i];
		}
		else
		{
			fprintf( stderr, "usage: fixed_sweep [--fn <name>] [--threads <n>] [--emit-points <file>]\n" );
			return 2;
		}
	}
	if ( label_guard && !ctest_requests_slow() )
	{
		return 0;
	}
	if ( threads < 1 )
	{
		threads = 1;
	}
	int ran = 0;
	for ( size_t i = 0; i < sweep_nfns; ++i )
	{
		if ( only && strcmp( only, sweep_fns[i]->name ) != 0 )
		{
			continue;
		}
		if ( run_fn( sweep_fns[i], threads, only ? emit_path : NULL ) != 0 )
		{
			return 1;
		}
		ran++;
	}
	if ( only && !ran )
	{
		fprintf( stderr, "sweep: no descriptor named %s\n", only );
		return 1;
	}
	return 0;
}
