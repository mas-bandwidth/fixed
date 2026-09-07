// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
// Optimized public-API microbenchmarks. Inputs are prepared outside the timed loops.
// Run identical binaries' workloads repeatedly; compare medians, not a single sample.
#include "fixed/fixed_vec.h"
#include "fixed/fixed_wide.h"
#include "fixed/fixed_time.h"
#include "fixed/fixed_quantize.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_MSC_VER)
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE __attribute__((noinline))
#endif

enum { COUNT = 1024 };
static fixVec3 vectors[COUNT];
static fixQuat quats[COUNT];
static fixAABB boxes[COUNT];
static double values[COUNT];
static uint64_t rootInputs[COUNT];
static volatile uint64_t sink;
static uint64_t state;
static uint64_t randomBits(void)
{
	state ^= state << 13; state ^= state >> 7; state ^= state << 17;
	return state;
}
static void initialize(uint64_t seed)
{
	const fixQuat rotations[] = {
		{ { 0, 0, 46341 }, 46341 }, { { 32768, 0, 0 }, 56756 },
		{ { 0, 23170, 23170 }, 56756 }, { { 0, 0, 0 }, FIX_ONE }
	};
	state = seed;
	for (int i = 0; i < COUNT; ++i)
	{
		vectors[i] = (fixVec3){ (fixed_t)(randomBits() % 1048577) - 524288,
			(fixed_t)(randomBits() % 1048577) - 524288, (fixed_t)(randomBits() % 1048577) - 524288 };
		quats[i] = rotations[randomBits() % 4];
		fixVec3 e = { FIX_ONE + (fixed_t)(randomBits() % 524288),
			FIX_ONE + (fixed_t)(randomBits() % 524288), FIX_ONE + (fixed_t)(randomBits() % 524288) };
		boxes[i] = (fixAABB){ fixVecSub(vectors[i], e), fixVecAdd(vectors[i], e) };
		values[i] = (double)vectors[i].x / FIX_ONE;
	}
	// Separate setup keeps every existing workload's input stream unchanged.
	for (int i = 0; i < COUNT; ++i) rootInputs[i] = randomBits();
}

#define KERNEL(name, ...) \
static NOINLINE uint64_t name(int n) { \
	uint64_t sum = 0; \
	for (int j = 0; j < n; ++j) { int i = j & (COUNT - 1); __VA_ARGS__; } \
	return sum; \
}
KERNEL(multiply, sum += (uint64_t)fixMul(vectors[i].x, vectors[i].y))
KERNEL(normalize, fixVec3 r = fixNormalize(vectors[i]); sum += (uint64_t)r.x + (uint64_t)r.y + (uint64_t)r.z)
KERNEL(length_normalize, fixed_t length; fixVec3 r = fixGetLengthAndNormalize(&length, vectors[i]);
	sum += (uint64_t)r.x + (uint64_t)r.y + (uint64_t)r.z + (uint64_t)length)
KERNEL(quaternion_normalize, fixQuat r = fixNormalizeQuat(quats[i]);
	sum += (uint64_t)r.v.x + (uint64_t)r.v.y + (uint64_t)r.v.z + (uint64_t)r.s)
KERNEL(angles, sum += (uint64_t)fixGetQuatAngle(quats[i]) + (uint64_t)fixGetSwingAngle(quats[i]))
KERNEL(validity, sum += (uint64_t)fixIsValidQuat(quats[i]))
KERNEL(aabb_transform, fixTransform t = { vectors[i], quats[i] }; fixAABB r = fixAABB_Transform(t, boxes[i]);
	sum += (uint64_t)r.lowerBound.x + (uint64_t)r.upperBound.y + (uint64_t)r.upperBound.z)
KERNEL(aabb_center_extents, fixVec3 c = fixAABB_Center(boxes[i]); fixVec3 e = fixAABB_Extents(boxes[i]);
	sum += (uint64_t)c.x + (uint64_t)c.y + (uint64_t)c.z + (uint64_t)e.x + (uint64_t)e.y + (uint64_t)e.z)
KERNEL(quantize_clamped, sum += (uint64_t)fixQuantizeClamped(values[i], FIX_ONE, -4 * FIX_ONE, 4 * FIX_ONE))
KERNEL(time_narrow, sum += (uint64_t)fixTimeToFixed(fixShiftLeft(vectors[i].x, 24)))
KERNEL(scalar_sqrt, sum += (uint64_t)fixSqrt(fixAbs(vectors[i].x)))
KERNEL(vector_length, sum += (uint64_t)fixLength(vectors[i]))
KERNEL(isqrt_u64, sum += fixISqrt128High(0, rootInputs[i]))

int main(int argc, char** argv)
{
	int n = argc > 1 ? atoi(argv[1]) : 1000000;
	if (n < 1) return 1;
	initialize(argc > 2 ? (uint64_t)strtoull(argv[2], NULL, 10) : 42);
	struct { const char* name; uint64_t (*run)(int); } cases[] = {
		{ "multiply", multiply }, { "normalize", normalize }, { "length_normalize", length_normalize },
		{ "quaternion_normalize", quaternion_normalize }, { "angles", angles }, { "validity", validity },
		{ "aabb_transform", aabb_transform }, { "aabb_center_extents", aabb_center_extents },
		{ "quantize_clamped", quantize_clamped }, { "time_narrow", time_narrow },
		{ "scalar_sqrt", scalar_sqrt }, { "vector_length", vector_length }, { "isqrt_u64", isqrt_u64 }
	};
	printf("operation,ns_per_call,checksum\n");
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
	{
		if (argc > 3 && strcmp(argv[3], cases[i].name) != 0) continue;
		sink = cases[i].run(n / 10 + 1);
		clock_t start = clock();
		uint64_t checksum = cases[i].run(n);
		clock_t end = clock();
		sink = checksum;
		printf("%s,%.3f,%llu\n", cases[i].name, 1e9 * (double)(end - start) / CLOCKS_PER_SEC / n,
			(unsigned long long)checksum);
	}
	return 0;
}
