// SPDX-FileCopyrightText: 2026 Más Bandwidth LLC
// SPDX-License-Identifier: MIT
// Numerical contracts independent of historical port hashes. Also built with
// saturation enabled: validity checks must not overflow under either policy.
#include "fixed/fixed_vec.h"
#include "fixed/fixed_wide.h"
#include "fixed/fixed_time.h"
#include "fixed/fixed_quantize.h"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { if (failures < 30) printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)
static bool equalVec(fixVec3 a, fixVec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
static double normVec(fixVec3 a)
{
	double x = fixToDouble(a.x), y = fixToDouble(a.y), z = fixToDouble(a.z);
	return sqrt(x*x + y*y + z*z);
}
static double normQuat(fixQuat q)
{
	double v = normVec(q.v), s = fixToDouble(q.s);
	return sqrt(v*v + s*s);
}

static void normalization(void)
{
	// Include both sides of the precision-lift and single-divide fast paths.
	const fixed_t scales[] = { 1, 2, 91, 362, FIX_HALF-1, FIX_HALF, FIX_HALF+1, FIX_ONE,
		(fixed_t)1 << 46, ((fixed_t)1 << 47)-1, (fixed_t)1 << 47, ((fixed_t)1 << 47)+1,
		(fixed_t)1 << 62, FIX_MAX };
	for (size_t s = 0; s < sizeof(scales)/sizeof(scales[0]); ++s)
		for (int x = -1; x <= 1; ++x) for (int y = -1; y <= 1; ++y) for (int z = -1; z <= 1; ++z)
		{
			if (x == 0 && y == 0 && z == 0) continue;
			fixVec3 v = { x * scales[s], y * scales[s], z * scales[s] };
			fixVec3 n = fixNormalize(v);
			CHECK(fabs(normVec(n) - 1.0) < 0.0001);
			CHECK((n.x > 0) == (x > 0) && (n.x < 0) == (x < 0));
			CHECK((n.y > 0) == (y > 0) && (n.y < 0) == (y < 0));
			CHECK((n.z > 0) == (z > 0) && (n.z < 0) == (z < 0));
			fixed_t length;
			fixVec3 combined = fixGetLengthAndNormalize(&length, v);
			CHECK(fabs(normVec(combined) - 1.0) < 0.0001);
			CHECK(fabs(fixToDouble(combined.x) - fixToDouble(n.x)) < 0.0001);
			CHECK(fabs(fixToDouble(combined.y) - fixToDouble(n.y)) < 0.0001);
			CHECK(fabs(fixToDouble(combined.z) - fixToDouble(n.z)) < 0.0001);
			CHECK(length > 0);
			fixQuat q = { v, scales[s] };
			fixQuat qn = fixNormalizeQuat(q);
			CHECK(fabs(normQuat(qn) - 1.0) < 0.0001);
			CHECK(qn.s > 0);
		}
	CHECK(equalVec(fixNormalize(fixVec3_zero), fixVec3_zero));
	CHECK(fixNormalizeQuat((fixQuat){ {0,0,0}, 0 }).s == FIX_ONE);
	CHECK(fixLength((fixVec3){ FIX_MAX, FIX_MAX, FIX_MAX }) == FIX_MAX);
}

static void angles(void)
{
	for (int i = 1; i <= 512; ++i)
	{
		fixQuat q = { { i, i / 3, 0 }, FIX_ONE };
		q = fixNormalizeQuat(q);
		double expected = 2 * atan2(normVec(q.v), fixToDouble(q.s));
		fixed_t angle;
		fixVec3 axis = fixGetAxisAngle(&angle, q);
		CHECK(angle > 0);
		CHECK(fabs(fixToDouble(angle) - expected) < 0.0001);
		CHECK(fabs(normVec(axis) - 1.0) < 0.0001);
		CHECK(fixGetQuatAngle(q) == angle);
		CHECK(fabs(fixToDouble(fixGetSwingAngle(q)) - expected) < 0.0001);
	}
}

static void validators(void)
{
	const fixed_t bad[] = { INT64_MIN, FIX_MIN, FIX_MAX, (fixed_t)1 << 40, -((fixed_t)1 << 40), 2 * FIX_ONE };
	for (size_t i = 0; i < sizeof(bad)/sizeof(bad[0]); ++i)
	{
		fixQuat q = { { bad[i], 0, 0 }, FIX_ONE };
		fixVec3 v = { bad[i], FIX_ONE, 0 };
		CHECK(!fixIsNormalizedQuat(q));
		CHECK(!fixIsValidQuat(q));
		CHECK(!fixIsNormalized(v));
		CHECK(!fixIsValidPlane((fixPlane){ v, 0 }));
	}
	CHECK(fixIsNormalizedQuat(fixQuat_identity));
	CHECK(fixIsNormalized(fixVec3_axisX));
}

static void bounds(void)
{
	const fixed_t coordinates[] = { FIX_MIN, -((fixed_t)1 << 62), -1, 0, 1, (fixed_t)1 << 62, FIX_MAX };
	for (size_t i = 0; i < sizeof(coordinates)/sizeof(coordinates[0]); ++i)
	{
		fixed_t c = coordinates[i];
		fixVec3 p = { c, c, c };
		fixAABB point = { p, p };
		CHECK(equalVec(fixAABB_Center(point), p));
		CHECK(equalVec(fixAABB_Extents(point), fixVec3_zero));
		fixAABBWide wide = { fixPosWideFromVec3(p), fixPosWideFromVec3(p) };
		CHECK(equalVec(fixAABBWide_Center(wide), p));
	}
	fixAABB span = { { FIX_MIN, FIX_MIN, FIX_MIN }, { FIX_MAX, FIX_MAX, FIX_MAX } };
	CHECK(equalVec(fixAABB_Center(span), fixVec3_zero));
	CHECK(fixAABB_Extents(span).x == FIX_MAX);
	fixAABBWide wide = { fixPosWideFromVec3(span.lowerBound), fixPosWideFromVec3(span.upperBound) };
	CHECK(fixAABBWide_Extents(wide).x == FIX_MAX);
	fixedWide_t far = fixInt128ShiftLeft(fixInt128FromI64(1), 100);
	wide.lowerBound = wide.upperBound = (fixPosWide){ far, far, far };
	CHECK(fixAABBWide_Center(wide).x == FIX_MAX);
	far = fixWideNeg(far);
	wide.lowerBound = wide.upperBound = (fixPosWide){ far, far, far };
	CHECK(fixAABBWide_Center(wide).x == FIX_MIN);
}

static void transformedBounds(void)
{
	const fixed_t sizes[] = { 1, 17, FIX_ONE, 8 * FIX_ONE, (fixed_t)1 << 32 };
	const fixQuat rotations[] = {
		{ { 0, 0, 46341 }, 46341 }, { { 32768, 0, 0 }, 56756 },
		{ { 0, 23170, 23170 }, 56756 }, { { 0, 0, 0 }, FIX_ONE }
	};
	for (size_t s = 0; s < sizeof(sizes)/sizeof(sizes[0]); ++s)
		for (size_t r = 0; r < sizeof(rotations)/sizeof(rotations[0]); ++r)
			for (int thin = 0; thin <= 1; ++thin)
			{
				fixed_t k = sizes[s];
				fixAABB a = { { -k, thin ? -1 : -k, -k }, { k, thin ? 2 : k + 1, k } };
				fixTransform t = { { 123, -456, 789 }, rotations[r] };
				fixAABB b = fixAABB_Transform(t, a);
				fixAABBWide aw = { fixPosWideFromVec3(a.lowerBound), fixPosWideFromVec3(a.upperBound) };
				fixAABBWide bw = fixAABBWide_Transform(t, aw);
				CHECK(equalVec(b.lowerBound, fixPosWideToVec3(bw.lowerBound)));
				CHECK(equalVec(b.upperBound, fixPosWideToVec3(bw.upperBound)));
				// Corners and a 3x3x3 grid of interior points. A bound must contain
				// the PUBLIC point transform, with no tolerance or matching oracle.
				for (int x = 0; x < 3; ++x) for (int y = 0; y < 3; ++y) for (int z = 0; z < 3; ++z)
				{
					fixVec3 p = { x == 0 ? a.lowerBound.x : x == 1 ? 0 : a.upperBound.x,
						y == 0 ? a.lowerBound.y : y == 1 ? 0 : a.upperBound.y,
						z == 0 ? a.lowerBound.z : z == 1 ? 0 : a.upperBound.z };
					p = fixTransformPoint(t, p);
					CHECK(p.x >= b.lowerBound.x && p.x <= b.upperBound.x);
					CHECK(p.y >= b.lowerBound.y && p.y <= b.upperBound.y);
					CHECK(p.z >= b.lowerBound.z && p.z <= b.upperBound.z);
				}
			}

	uint64_t seed = UINT64_C(0x63d47bc178dfe205);
	for (int i = 0; i < 5000; ++i)
	{
		fixed_t raw[10];
		for (int j = 0; j < 10; ++j)
		{
			seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17;
			raw[j] = (fixed_t)(seed & ((UINT64_C(1) << 40) - 1)) - ((fixed_t)1 << 39);
		}
		fixQuat q = { { raw[0] >> 24, raw[1] >> 24, raw[2] >> 24 }, raw[3] >> 24 };
		q = fixNormalizeQuat(q);
		CHECK(fabs(normQuat(q) - 1.0) < 0.0001);
		fixVec3 center = { raw[4], raw[5], raw[6] };
		fixVec3 extent = { fixAbs(raw[7]) >> 8, fixAbs(raw[8]) >> 16, fixAbs(raw[9]) >> 24 };
		fixAABB a = { fixVecSub(center, extent), fixVecAdd(center, extent) };
		fixTransform t = { { 107, -213, 319 }, q };
		fixAABB b = fixAABB_Transform(t, a);
		for (int corner = 0; corner < 8; ++corner)
		{
			fixVec3 p = { corner & 1 ? a.lowerBound.x : a.upperBound.x,
				corner & 2 ? a.lowerBound.y : a.upperBound.y, corner & 4 ? a.lowerBound.z : a.upperBound.z };
			p = fixTransformPoint(t, p);
			CHECK(p.x >= b.lowerBound.x && p.x <= b.upperBound.x);
			CHECK(p.y >= b.lowerBound.y && p.y <= b.upperBound.y);
			CHECK(p.z >= b.lowerBound.z && p.z <= b.upperBound.z);
		}
	}
}

static void quantize(void)
{
	const double large[] = { 1e20, DBL_MAX };
	for (size_t i = 0; i < sizeof(large)/sizeof(large[0]); ++i)
	{
		CHECK(fixQuantizeClamped(large[i], FIX_ONE, -100, 100) == 100);
		CHECK(fixQuantizeClamped(-large[i], FIX_ONE, -100, 100) == -100);
		CHECK(fixQuantizeClamped(large[i], 1, INT64_MIN, INT64_MAX) == INT64_MAX);
		CHECK(fixQuantizeClamped(-large[i], 1, INT64_MIN, INT64_MAX) == INT64_MIN);
	}
	CHECK(fixQuantizeClamped(0.5, 1, -10, 10) == 1);
	CHECK(fixQuantizeClamped(-0.5, 1, -10, 10) == -1);
	CHECK(fixQuantizeClamped(0x1p63, 1, INT64_MIN, INT64_MAX) == INT64_MAX);
	CHECK(fixQuantizeClamped(-0x1p63, 1, INT64_MIN, INT64_MAX) == INT64_MIN);
	// Bounds not exactly representable as doubles must still clamp exactly.
	CHECK(fixQuantizeClamped(0x1p53, 1, ((int64_t)1 << 53) + 1, INT64_MAX) == ((int64_t)1 << 53) + 1);
}

static void timeConversion(void)
{
	for (int64_t i = 0; i <= 65536; ++i)
	{
		fixTime t = INT64_MAX - i;
		fixed_t expected = (t >> 16) + ((t & 65535) >= 32768);
		CHECK(fixTimeToFixed(t) == expected);
		t = INT64_MIN + i;
		expected = (t >> 16) + ((t & 65535) >= 32768);
		CHECK(fixTimeToFixed(t) == expected);
	}
}

static void squareRoots(void)
{
	const uint64_t highs[] = { 0, 1, (UINT64_C(1) << 30), (UINT64_C(1) << 40) - 1,
		UINT64_C(1) << 40, UINT64_C(1) << 63, UINT64_MAX };
	const uint64_t lows[] = { 0, 1, UINT64_MAX / 2, UINT64_MAX - 1, UINT64_MAX };
	for (size_t i = 0; i < sizeof(highs)/sizeof(highs[0]); ++i)
		for (size_t j = 0; j < sizeof(lows)/sizeof(lows[0]); ++j)
		{
			fixUInt128 n = fixUInt128Make(highs[i], lows[j]);
			uint64_t r = fixISqrt128High(highs[i], lows[j]);
			CHECK(fixUInt128Le(fixUInt128MulU64(r,r), n));
			if (r != UINT64_MAX) CHECK(fixUInt128Gt(fixUInt128MulU64(r+1,r+1), n));
		}
	// Exact squares and both neighbors exercise rounding of the hardware seed.
	for (uint64_t r = (UINT64_C(1) << 46); r < (UINT64_C(1) << 46) + 4096; ++r)
	{
		fixUInt128 sq = fixUInt128MulU64(r,r);
		fixUInt128 below = fixUInt128Sub(sq, fixUInt128FromU64(1));
		fixUInt128 above = fixUInt128Add(sq, fixUInt128FromU64(1));
		CHECK(fixISqrt128High(fixUInt128Hi(sq), fixUInt128Lo(sq)) == r);
		CHECK(fixISqrt128High(fixUInt128Hi(below), fixUInt128Lo(below)) == r-1);
		CHECK(fixISqrt128High(fixUInt128Hi(above), fixUInt128Lo(above)) == r);
	}
}

int main(int argc, char** argv)
{
	struct { const char* name; void (*run)(void); } tests[] = {
		{ "normalization", normalization }, { "angles", angles }, { "validators", validators },
		{ "bounds", bounds }, { "transformed_bounds", transformedBounds },
		{ "quantize", quantize }, { "time", timeConversion }, { "square_roots", squareRoots }
	};
	for (size_t i = 0; i < sizeof(tests)/sizeof(tests[0]); ++i)
	{
		if (argc > 1 && strcmp(argv[1], tests[i].name) != 0) continue;
		int before = failures;
		tests[i].run();
		printf("%s: %d failures\n", tests[i].name, failures - before);
	}
	return failures ? 1 : 0;
}
