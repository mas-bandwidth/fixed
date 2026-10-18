/* SPDX-License-Identifier: MIT */
/* The pow and log test points of glibc's math/auto-libm-test-in, glibc 2.39
 * (the glibc-2.39 tag), rewritten as raw Q48.16 values. No text from that LGPL file is
 * copied; only the numeric arguments, rounded to the nearest raw.
 *
 * Each double argument is rounded to the nearest raw with llround(x * 65536); points whose
 * argument is outside the int64 raw range are dropped. 16 pow pairs and 16 log arguments
 * were kept.
 */
#pragma once
#include <stdint.h>

/* (base, exponent) in raw units. */
static const int64_t sweep_glibc_pow_points[][2] = {
	{ 2 * 65536, -8 * 65536 },
	{ 2 * 65536, -1074 * 65536 },
	{ 65536 / 2, 1074 * 65536 },
	{ 3 * 65536 / 2, 7 * 65536 / 2 },
	{ 3 * 65536, 3 * 65536 },
	{ 10 * 65536, 3 * 65536 },
	{ 3 * 65536, -5 * 65536 },
	{ 6554, 5 * 65536 / 2 },
	{ 73 * 65536 / 16, 11 * 65536 / 2 },
	{ 65543, 1000 * 65536 },
	{ 65536 * 65536, 65536 / 2 },
	{ 7, -65536 / 2 },
	{ 65536, 65536 },
	{ -2 * 65536, 3 * 65536 },
	{ -65536, -3 * 65536 },
	{ 0, 0 },
};
static const size_t sweep_glibc_pow_point_count = sizeof( sweep_glibc_pow_points ) / sizeof( sweep_glibc_pow_points[0] );

/* Arguments of the glibc log tests, in raw units. */
static const int64_t sweep_glibc_log_points[] = {
	2 * 65536,
	10 * 65536,
	65536,
	65536 / 2,
	3 * 65536,
	7 * 65536 / 2,
	65543,
	655361,
	100000 * 65536,
	12345,
	1,
	0,
	-65536,
	-1,
	65536 * 65536,
	3 * 65536 * 65536,
};
static const size_t sweep_glibc_log_point_count = sizeof( sweep_glibc_log_points ) / sizeof( sweep_glibc_log_points[0] );
