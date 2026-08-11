// tests/sim_tests/math_test.cpp
//
// T008 单元测试：C11 确定性数学库 core_c。
// 覆盖已知输入输出、NaN/Inf 契约与 IEEE-754 位级参考值；
// 三角函数的位级一致性以 MSVC CRT + /fp:precise 为契约边界（见 math.h）。
// 位级黄金值仅在 MSVC CRT + /fp:precise 契约下有效（CI 固定 windows-2022）。

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

#include "wfs/core/math.h"

namespace {

using std::uint64_t;

void ExpectBits(double actual, uint64_t expected_bits) {
    EXPECT_EQ(std::bit_cast<uint64_t>(actual), expected_bits) << "value: " << actual;
}

double Inf() {
    return std::numeric_limits<double>::infinity();
}
double QuietNaN() {
    return std::numeric_limits<double>::quiet_NaN();
}

}  // namespace

TEST(WfsMathTest, SqrtKnownValues) {
    ExpectBits(wfs_math_sqrt(4.0), 0x4000000000000000ULL);  // 2.0
    ExpectBits(wfs_math_sqrt(2.0), 0x3FF6A09E667F3BCDULL);  // IEEE-754 sqrt(2)
    EXPECT_TRUE(std::isnan(wfs_math_sqrt(-1.0)));
    EXPECT_TRUE(std::isinf(wfs_math_sqrt(Inf())) && wfs_math_sqrt(Inf()) > 0.0);
    ExpectBits(wfs_math_sqrt(-0.0), 0x8000000000000000ULL);  // -0.0 保留符号
}

TEST(WfsMathTest, RoundingKnownValues) {
    ExpectBits(wfs_math_floor(2.7), 0x4000000000000000ULL);   // 2.0
    ExpectBits(wfs_math_ceil(2.2), 0x4008000000000000ULL);    // 3.0
    ExpectBits(wfs_math_round(2.5), 0x4008000000000000ULL);   // round 远离零：3.0
    ExpectBits(wfs_math_round(-2.5), 0xC008000000000000ULL);  // -3.0
    ExpectBits(wfs_math_trunc(-2.7), 0xC000000000000000ULL);  // -2.0
    ExpectBits(wfs_math_floor(-0.5), 0xBFF0000000000000ULL);  // -1.0
    EXPECT_TRUE(std::signbit(wfs_math_round(-0.2)));
    EXPECT_TRUE(std::isinf(wfs_math_floor(Inf())) && !std::signbit(wfs_math_floor(Inf())));
    EXPECT_TRUE(std::isinf(wfs_math_ceil(-Inf())) && std::signbit(wfs_math_ceil(-Inf())));
}

TEST(WfsMathTest, FmodKnownValues) {
    ExpectBits(wfs_math_fmod(5.5, 2.0), 0x3FF8000000000000ULL);   // 1.5
    ExpectBits(wfs_math_fmod(-5.5, 2.0), 0xBFF8000000000000ULL);  // -1.5
    EXPECT_TRUE(std::isnan(wfs_math_fmod(1.0, 0.0)));
}

TEST(WfsMathTest, TrigonometricKnownValues) {
    ExpectBits(wfs_math_sin(0.0), 0x0000000000000000ULL);           // +0.0
    ExpectBits(wfs_math_cos(0.0), 0x3FF0000000000000ULL);           // 1.0
    ExpectBits(wfs_math_tan(0.0), 0x0000000000000000ULL);           // +0.0
    ExpectBits(wfs_math_atan(1.0), 0x3FE921FB54442D18ULL);          // pi/4
    ExpectBits(wfs_math_atan2(1.0, 1.0), 0x3FE921FB54442D18ULL);    // pi/4
    ExpectBits(wfs_math_atan2(Inf(), 1.0), 0x3FF921FB54442D18ULL);  // pi/2
    EXPECT_TRUE(std::isnan(wfs_math_sin(Inf())));
}

TEST(WfsMathTest, NaNPropagates) {
    EXPECT_TRUE(std::isnan(wfs_math_sqrt(QuietNaN())));
    EXPECT_TRUE(std::isnan(wfs_math_floor(QuietNaN())));
    EXPECT_TRUE(std::isnan(wfs_math_ceil(QuietNaN())));
    EXPECT_TRUE(std::isnan(wfs_math_round(QuietNaN())));
    EXPECT_TRUE(std::isnan(wfs_math_trunc(QuietNaN())));
    EXPECT_TRUE(std::isnan(wfs_math_sin(QuietNaN())));
    EXPECT_TRUE(std::isnan(wfs_math_cos(QuietNaN())));
    EXPECT_TRUE(std::isnan(wfs_math_tan(QuietNaN())));
    EXPECT_TRUE(std::isnan(wfs_math_atan2(QuietNaN(), 1.0)));
}
