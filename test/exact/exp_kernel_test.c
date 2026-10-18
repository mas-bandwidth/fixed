#include "../../src/exact/exp_kernel.h"
#include <stdint.h>
#include <stdio.h>

static int failures = 0;

static void check(int ok, const char* what)
{
    if (!ok) { printf("FAIL: %s\n", what); failures++; }
}

static int u128eq(fixUInt128 a, fixUInt128 b) { return fixUInt128Eq(a, b); }

static int u128leq256_shift25(fixUInt128 a, fixUInt128 b)
{
    fixUInt128 d = fixUInt128Ge(a, b) ? fixUInt128Sub(a, b) : fixUInt128Sub(b, a);
    return fixUInt128Le(d, fixUInt128FromU64(UINT64_C(1) << 25));
}

int main(void)
{
    // T_0 = 2^126, and T_1, T_64, T_127 equal the sollya/exp_kernel.out lines.
    check(fixUInt128Eq(fixUInt128Make(FIX_EXP_T[0].hi, FIX_EXP_T[0].lo),
        fixUInt128Make(UINT64_C(4611686018427387904), UINT64_C(0))), "T_0 = 2^126");
    check(fixUInt128Eq(fixUInt128Make(FIX_EXP_T[1].hi, FIX_EXP_T[1].lo),
        fixUInt128Make(UINT64_C(4636727017470743990), UINT64_C(1340290985584983696))), "T_1");
    check(fixUInt128Eq(fixUInt128Make(FIX_EXP_T[64].hi, FIX_EXP_T[64].lo),
        fixUInt128Make(UINT64_C(6521908912666391106), UINT64_C(3224230822662201168))), "T_64");
    check(fixUInt128Eq(fixUInt128Make(FIX_EXP_T[127].hi, FIX_EXP_T[127].lo),
        fixUInt128Make(UINT64_C(9173560510430823461), UINT64_C(15485958043875579781))), "T_127");

    // LN2_128 words recombine to the digits above.
    check(fixUInt128Eq(
        fixUInt128Make(FIX_EXP_LN2_128_HI, FIX_EXP_LN2_128_LO),
        fixUInt128Make(UINT64_C(12786308645202655659), UINT64_C(14547668686819489455))), "LN2_128 recombine");

    // fixExactMulHi128 on six operand pairs against high halves computed with Python integers.
    {
        fixUInt128 allones = fixUInt128Make(UINT64_C(0xFFFFFFFFFFFFFFFF), UINT64_C(0xFFFFFFFFFFFFFFFF));
        check(fixUInt128Eq(fixExactMulHi128(allones, allones),
            fixUInt128Make(UINT64_C(0xFFFFFFFFFFFFFFFF), UINT64_C(0xFFFFFFFFFFFFFFFE))), "mulhi all-ones");
    }
    check(fixUInt128Eq(fixExactMulHi128(
        fixUInt128Make(UINT64_C(0x8000000000000000), 0),
        fixUInt128Make(UINT64_C(0x8000000000000000), 0)),
        fixUInt128Make(UINT64_C(0x4000000000000000), 0)), "mulhi 2^127*2^127");
    check(fixUInt128Eq(fixExactMulHi128(
        fixUInt128FromU64(0), fixUInt128Make(UINT64_C(0xFFFFFFFFFFFFFFFF), UINT64_C(0xFFFFFFFFFFFFFFFF))),
        fixUInt128FromU64(0)), "mulhi 0*allones");
    check(fixUInt128Eq(fixExactMulHi128(fixUInt128FromU64(1), fixUInt128FromU64(1)),
        fixUInt128FromU64(0)), "mulhi 1*1");
    check(fixUInt128Eq(fixExactMulHi128(
        fixUInt128Make(UINT64_C(0xDEADBEEFCAFEBABE), UINT64_C(0x0123456789ABCDEF)),
        fixUInt128Make(UINT64_C(0xFEEDFACE), UINT64_C(0x0F0F0F0F0F0F0F0F))),
        fixUInt128Make(UINT64_C(3720307828), UINT64_C(11008701590143579340))), "mulhi mixed");
    check(fixUInt128Eq(fixExactMulHi128(
        fixUInt128Make(UINT64_C(0x1111111111111111), UINT64_C(0x2222222222222222)),
        fixUInt128Make(UINT64_C(0x3333333333333333), UINT64_C(0x4444444444444444))),
        fixUInt128Make(UINT64_C(245956587649460688), UINT64_C(4263247519257318595))), "mulhi small");

    // fixExpKernel(0) gives 2^126 and exponent 0.
    {
        fixUInt128 mant;
        int expo;
        fixExpKernel(0, &mant, &expo);
        check(fixUInt128Eq(mant, fixUInt128Make(UINT64_C(0x4000000000000000), 0)), "kernel(0) mantissa = 2^126");
        check(expo == 0, "kernel(0) exponent = 0");
    }

    // 32 inputs: exponent equals the expected m, and |mantissa - round(e^(a/65536)*2^(126-m))| <= 2^25.
    {
        static const int64_t ins[32] = {
            0,
            1,
            -1,
            45426,
            -45426,
            -772243,
            2135026,
            4194304,
            -4194304,
            -1000000,
            -999999,
            -999998,
            -999997,
            -999996,
            -999995,
            -999994,
            -999993,
            -999992,
            -999991,
            -999990,
            -999989,
            1909707,
            -305287,
            -667341,
            381313,
            254907,
            163970,
            -186979,
            -342348,
            2066040,
            1515190,
            -407596,
        };
        static const fixExpKernelU128 exp_mantissa[32] = {
            { UINT64_C(4611686018427387904), UINT64_C(0) },
            { UINT64_C(4611756387708439210), UINT64_C(12489983552981868365) },
            { UINT64_C(9223231300440156842), UINT64_C(12682135377865896385) },
            { UINT64_C(9223358860291989772), UINT64_C(1455278586446115901) },
            { UINT64_C(4611692606718192992), UINT64_C(7670342563131512251) },
            { UINT64_C(4611727650734812210), UINT64_C(10959122993969948108) },
            { UINT64_C(9223315688087014081), UINT64_C(7681520285561960744) },
            { UINT64_C(5806935095052809345), UINT64_C(8326969703335511510) },
            { UINT64_C(7324913257831149231), UINT64_C(14648522672203108453) },
            { UINT64_C(9135698135914238639), UINT64_C(13365315702640443337) },
            { UINT64_C(9135837536668573877), UINT64_C(10962982285982018491) },
            { UINT64_C(9135976939550012049), UINT64_C(8947994753061286890) },
            { UINT64_C(9136116344558585612), UINT64_C(12164709443585106695) },
            { UINT64_C(9136255751694327024), UINT64_C(16146715563455319065) },
            { UINT64_C(9136395160957268744), UINT64_C(7116974589777327130) },
            { UINT64_C(9136534572347443229), UINT64_C(17328191899124497663) },
            { UINT64_C(9136673985864882940), UINT64_C(14382491734543798509) },
            { UINT64_C(9136813401509620336), UINT64_C(11912021059203288182) },
            { UINT64_C(9136952819281687877), UINT64_C(14238856748780906134) },
            { UINT64_C(9137092239181118024), UINT64_C(16375145007109215560) },
            { UINT64_C(9137231661207943239), UINT64_C(4023240783947476353) },
            { UINT64_C(4740905833134605526), UINT64_C(10735488136952461800) },
            { UINT64_C(5597465176544984904), UINT64_C(15633851626917359027) },
            { UINT64_C(5714379271660340538), UINT64_C(17933815145958264240) },
            { UINT64_C(6060490334751744354), UINT64_C(13851858032503537347) },
            { UINT64_C(7045786380212574873), UINT64_C(9628163355144573327) },
            { UINT64_C(7036674044826327928), UINT64_C(12691731550679679391) },
            { UINT64_C(8510122212315615826), UINT64_C(6331329771501646398) },
            { UINT64_C(6359533638188504972), UINT64_C(1565975698553738097) },
            { UINT64_C(6438135311682200179), UINT64_C(13253095603030190987) },
            { UINT64_C(5898518171272725529), UINT64_C(1760240127022000348) },
            { UINT64_C(4699690987172941042), UINT64_C(2260864126059317139) },
        };
        static const int exp_m[32] = {
            0,
            0,
            -1,
            0,
            -1,
            -17,
            46,
            92,
            -93,
            -23,
            -23,
            -23,
            -23,
            -23,
            -23,
            -23,
            -23,
            -23,
            -23,
            -23,
            -23,
            42,
            -7,
            -15,
            8,
            5,
            3,
            -5,
            -8,
            45,
            33,
            -9,
        };
        int i;
        for (i = 0; i < 32; i++)
        {
            fixUInt128 mant;
            int expo;
            fixExpKernel(ins[i], &mant, &expo);
            char buf[64];
            sprintf(buf, "kernel input %lld exponent", (long long)ins[i]);
            check(expo == exp_m[i], buf);
            sprintf(buf, "kernel input %lld mantissa", (long long)ins[i]);
            check(u128leq256_shift25(mant, fixUInt128Make(exp_mantissa[i].hi, exp_mantissa[i].lo)), buf);
        }
    }

    if (failures == 0) printf("exp kernel test passed\n");
    return failures == 0 ? 0 : 1;
}
