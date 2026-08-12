// tests/sim_tests/rng_test.cpp
//
// T009 单元测试：统一确定性 RNG（PCG32）。
// 覆盖同种子序列一致、不同种子/不同流序列不同、有界取值、状态序列化往返，
// 以及 PCG32 参考序列（seed=42/stream=0 等）的黄金样例。

#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>

#include <gtest/gtest.h>

#include "wfs/sim/rng.h"

namespace {

using std::uint32_t;
using std::uint64_t;
using wfs::sim::Rng;

constexpr std::array<uint32_t, 8> kGoldenSeed42Stream0 = {
    565663470u, 3244226384u, 2504567229u, 903561869u, 4026996297u, 2722332799u, 3032858066u, 272411090u,
};

constexpr std::array<uint32_t, 8> kGoldenSeed42Stream1 = {
    1307692281u, 3850602322u, 1491967504u, 4091771729u, 3882238836u, 1795024040u, 2266118430u, 1938801432u,
};

constexpr std::array<uint32_t, 8> kGoldenSeed43Stream0 = {
    1444281420u, 1502347811u, 137890535u, 1057658651u, 2360836715u, 918620879u, 2909095086u, 1771957773u,
};

std::array<uint32_t, 8> FirstEight(Rng rng) {
    std::array<uint32_t, 8> out{};
    for (uint32_t& value : out) {
        value = rng.next();
    }
    return out;
}

void ExpectEqual(const std::array<uint32_t, 8>& actual, const std::array<uint32_t, 8>& expected) {
    EXPECT_EQ(actual, expected);
}

}  // namespace

TEST(WfsRngTest, GoldenReferenceSequence) {
    ExpectEqual(FirstEight(Rng(42u, 0u)), kGoldenSeed42Stream0);
    ExpectEqual(FirstEight(Rng(42u, 1u)), kGoldenSeed42Stream1);
    ExpectEqual(FirstEight(Rng(43u, 0u)), kGoldenSeed43Stream0);
}

TEST(WfsRngTest, SameSeedSameStreamReproducesSequence) {
    const auto first = FirstEight(Rng(123456789u, 7u));
    const auto second = FirstEight(Rng(123456789u, 7u));
    ExpectEqual(first, second);
}

TEST(WfsRngTest, DifferentSeedProducesDifferentSequence) {
    const auto seed42 = FirstEight(Rng(42u, 0u));
    const auto seed43 = FirstEight(Rng(43u, 0u));
    ExpectEqual(seed42, kGoldenSeed42Stream0);
    ExpectEqual(seed43, kGoldenSeed43Stream0);
    EXPECT_NE(seed42, seed43);
}

TEST(WfsRngTest, DifferentStreamProducesDifferentSequence) {
    const auto stream0 = FirstEight(Rng(42u, 0u));
    const auto stream1 = FirstEight(Rng(42u, 1u));
    ExpectEqual(stream0, kGoldenSeed42Stream0);
    ExpectEqual(stream1, kGoldenSeed42Stream1);
    EXPECT_NE(stream0, stream1);
}

TEST(WfsRngTest, BoundedValuesStayInRangeAndAreDeterministic) {
    constexpr uint32_t kBound = 100u;
    Rng first(42u, 0u);
    Rng second(42u, 0u);
    std::array<uint32_t, 32> first_values{};
    std::array<uint32_t, 32> second_values{};
    for (size_t i = 0; i < first_values.size(); ++i) {
        first_values[i] = first.next_bounded(kBound);
        second_values[i] = second.next_bounded(kBound);
        EXPECT_LT(first_values[i], kBound);
        EXPECT_EQ(first_values[i], second_values[i]);
    }
    // 黄金样例：bound=100、seed=42/stream=0 前 12 个有界值。
    EXPECT_EQ(first_values[0], 70u);
    EXPECT_EQ(first_values[1], 84u);
    EXPECT_EQ(first_values[2], 29u);
    EXPECT_EQ(first_values[3], 69u);
    EXPECT_EQ(first_values[4], 97u);
    EXPECT_EQ(first_values[5], 99u);
}

TEST(WfsRngTest, LargeBoundsTriggerRejectionAndStayDeterministic) {
    constexpr std::array<uint32_t, 2> kBounds = {
        std::numeric_limits<uint32_t>::max(),
        0x80000001u,  // 2^31 + 1：threshold 接近 2^31，必然触发拒绝分支
    };
    for (const uint32_t bound : kBounds) {
        Rng first(42u, 0u);
        Rng second(42u, 0u);
        for (int i = 0; i < 16; ++i) {
            const uint32_t first_value = first.next_bounded(bound);
            const uint32_t second_value = second.next_bounded(bound);
            EXPECT_LT(first_value, bound);
            EXPECT_EQ(first_value, second_value);
        }
    }

    // 黄金样例：seed=42/stream=0、bound=2^31+1 前 4 个有界值（拒绝分支可复现）。
    // threshold = (0u - bound) % bound = 2^31-1，首个原始值 565663470 被拒绝，
    // 因此第一个有界值为第二个原始值 3244226384 % 2^31+1 = 1096742735。
    Rng large_bound(42u, 0u);
    EXPECT_EQ(large_bound.next_bounded(0x80000001u), 1096742735u);
    EXPECT_EQ(large_bound.next_bounded(0x80000001u), 357083580u);
    EXPECT_EQ(large_bound.next_bounded(0x80000001u), 1879512648u);
    EXPECT_EQ(large_bound.next_bounded(0x80000001u), 574849150u);
}

TEST(WfsRngTest, SingleBoundAlwaysZero) {
    Rng rng(42u, 0u);
    for (int i = 0; i < 8; ++i) {
        EXPECT_EQ(rng.next_bounded(1u), 0u);
    }
}

TEST(WfsRngTest, ZeroBoundDoesNotConsumeState) {
    Rng rng(42u, 0u);
    const Rng::State before = rng.state();
    EXPECT_EQ(rng.next_bounded(0u), 0u);
    EXPECT_EQ(rng.state().state, before.state);
    EXPECT_EQ(rng.state().stream, before.stream);
}

TEST(WfsRngTest, ResetMatchesFreshConstruction) {
    Rng reset(7u, 9u);
    (void)reset.next();
    (void)reset.next();
    reset.reset(42u, 7u);

    ExpectEqual(FirstEight(reset), FirstEight(Rng(42u, 7u)));
}

TEST(WfsRngTest, StateRestoreRejectsEvenStream) {
    Rng rng(42u, 0u);
    EXPECT_THROW(rng.restore(Rng::State{0u, 2u}), std::invalid_argument);
    EXPECT_THROW(Rng(Rng::State{0u, 4u}), std::invalid_argument);

    // 校验失败不得改变当前 RNG 状态。
    const Rng::State before = rng.state();
    EXPECT_THROW(rng.restore(Rng::State{0u, 6u}), std::invalid_argument);
    EXPECT_EQ(rng.state().state, before.state);
    EXPECT_EQ(rng.state().stream, before.stream);
}

TEST(WfsRngTest, StateRoundTripResumesSequence) {
    Rng original(42u, 0u);
    for (int i = 0; i < 5; ++i) {
        (void)original.next();
    }
    const Rng::State snapshot = original.state();
    const uint32_t expected = original.next();

    Rng resumed(snapshot);
    EXPECT_EQ(resumed.next(), expected);

    Rng restored = original;
    restored.restore(snapshot);
    EXPECT_EQ(restored.next(), expected);
}
