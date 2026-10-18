/* SPDX-License-Identifier: MIT */
/* The MPFR sweep: descriptor interface.
 *
 * A function is one sweep/fns/<name>.c defining `const sweep_fn sweep_fn_<name>`. The
 * registry is generated at configure time from the glob, so adding a function is one file
 * and nothing else. See sweep/README.md.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <mpfr.h>

#define SWEEP_SCALE 65536

typedef struct sweep_point
{
	int64_t a;
	int64_t b;
} sweep_point;

enum
{
	SWEEP_EXHAUSTIVE = 0,
	SWEEP_SAMPLED = 1,
	SWEEP_PER_BINADE = 2
};

/* One input domain piece. Bounds are half-open: [lo, hi). For a binary function the second
 * argument is drawn from [blo, bhi). count is the number of seeded draws for SAMPLED, and
 * the number of draws per binade for PER_BINADE (lo must be a power of two there). */
typedef struct sweep_piece
{
	int64_t lo;
	int64_t hi;
	int64_t blo;
	int64_t bhi;
	uint64_t count;
	int mode;
} sweep_piece;

typedef struct sweep_fn
{
	const char* name;
	int arity;
	int64_t ( *fn )( int64_t a, int64_t b );
	int64_t ( *ref )( int64_t a, int64_t b );
	const sweep_piece* pieces;
	size_t npieces;
	const sweep_point* points;
	size_t npoints;
	const int64_t* edges;
	size_t nedges;
} sweep_fn;

extern const sweep_fn* const sweep_fns[];
extern const size_t sweep_nfns;

/* A 256-bit MPFR scratch value per thread, and RN(raw value * 2^16) with saturation to the
 * int64 range. Shared by every descriptor reference. */
mpfr_t* sweep_mpfr_tmp( void );
int64_t sweep_mpfr_round( mpfr_t v );
