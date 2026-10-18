// SPDX-License-Identifier: MIT
// Exact range and error checks for fixUnwindAngle; independent Q80 two-pi reference.
#include "fixed/fixed_math.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

// round(2*pi*2^80), computed independently with Machin pi at 100 decimal digits.
static const unsigned __int128 TWO_PI_Q80 = ((unsigned __int128)0x6487E << 64) | UINT64_C(0xD5110B4611A62633);

static uint64_t rng = UINT64_C(0x9E3779B97F4A7C15);
static uint64_t digest = UINT64_C(0xCBF29CE484222325);
static uint64_t next_rng(void)
{
	rng ^= rng << 13;
	rng ^= rng >> 7;
	rng ^= rng << 17;
	return rng;
}

#ifdef UNWIND_ANGLE_NEGATIVE_CONTROL
#define UNWIND(v) fixUnwindAngle(v)
#else
#define UNWIND(v) fixUnwindAngleExact(v)
#endif

typedef struct row
{
	const char* lo;
	const char* hi;
	uint64_t inputs;
	uint64_t maxerr;
	uint64_t outside;
	int failed;
} row;

static uint64_t absdiff(int64_t a, int64_t b)
{
	return a >= b ? (uint64_t)a - (uint64_t)b : (uint64_t)b - (uint64_t)a;
}

static int64_t reference(int64_t raw)
{
	uint64_t mag = raw < 0 ? -(uint64_t)raw : (uint64_t)raw;
	unsigned __int128 numerator = (unsigned __int128)mag << 64;
	unsigned __int128 turns = numerator / TWO_PI_Q80;
	unsigned __int128 rem = numerator - turns * TWO_PI_Q80;
	bool negative = raw < 0;
	if (rem * 2 >= TWO_PI_Q80)
	{
		rem = TWO_PI_Q80 - rem;
		negative = !negative;
	}
	uint64_t remMag = (uint64_t)((rem + ((unsigned __int128)1 << 63)) >> 64);
	int64_t out = negative ? -(int64_t)remMag : (int64_t)remMag;
	if (out > 205887) out = 205887;
	if (out < -205887) out = -205887;
	return out;
}

static void check(row* r, int64_t input)
{
	int64_t got = UNWIND(input);
	int64_t want = reference(input);
	uint64_t err = absdiff(got, want);
	uint64_t bits = (uint64_t)got;
	for (int i = 0; i < 8; ++i)
	{
		digest = (digest ^ (uint8_t)(bits >> (i * 8))) * UINT64_C(0x100000001B3);
	}
	r->inputs++;
	if (err > r->maxerr) r->maxerr = err;
	if (got < -205887 || got > 205887) r->outside++;
	if (err > 1 || got < -205887 || got > 205887)
	{
		if (!r->failed) printf("unwind fail input=%" PRId64 " got=%" PRId64 " reference=%" PRId64 "\n", input, got, want);
		r->failed = 1;
	}
}

static void print_row(row* r)
{
	printf("unwind row=%s,%s inputs=%" PRIu64 " maxerr=%" PRIu64 " bound=1 outside=%" PRIu64 "\n",
		r->lo, r->hi, r->inputs, r->maxerr, r->outside);
}

static int run_piece(row* r)
{
	print_row(r);
	return r->failed ? 1 : 0;
}

int main(void)
{
	int failures = 0;
	const int64_t core = 823549;
	row coreRow = { "-4pi", "4pi", 0, 0, 0, 0 };
	for (int64_t x = -core; x <= core; ++x) check(&coreRow, x);
	failures += run_piece(&coreRow);

#ifdef UNWIND_ANGLE_NEGATIVE_CONTROL
	const int firstOctave = 46, lastOctave = 46;
	const uint32_t samplesPerOctave = 1u << 14;
#else
	const int firstOctave = 5, lastOctave = 46;
	const uint32_t samplesPerOctave = 1u << 18;
#endif
	for (int e = firstOctave; e <= lastOctave; ++e)
	{
		int64_t lo = (int64_t)1 << (e + 16);
		int64_t hi = e < 46 ? ((int64_t)1 << (e + 17)) - 1 : INT64_MAX;
		uint64_t span = (uint64_t)(hi - lo) + 1;
		row r = { "", "", 0, 0, 0, 0 };
		char loText[32], hiText[32];
		snprintf(loText, sizeof loText, "2^%d", e);
		snprintf(hiText, sizeof hiText, "2^%d", e + 1);
		r.lo = loText; r.hi = hiText;
		for (uint32_t i = 0; i < samplesPerOctave; ++i)
		{
			uint64_t u = next_rng();
			int64_t x = lo + (int64_t)(u % span);
			if (u >> 63) x = -x;
			check(&r, x);
		}
		failures += run_piece(&r);
	}

	row edge = { "edges", "special", 0, 0, 0, 0 };
	const int64_t cases[] = { INT64_MIN, INT64_MAX, -205888, -205887, -205886, 205886, 205887, 205888,
		-1, 0, 1, -411774, 411774, -823548, 823548 };
	for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i) check(&edge, cases[i]);
	for (int64_t k = -32; k <= 32; ++k) check(&edge, k * 411775);
	failures += run_piece(&edge);
	printf("unwind digest=%016" PRIx64 "\n", digest);
	if (failures)	return 1;
	puts("unwind angle test passed");
	return 0;
}
