// tests/sim_tests/clock_test.cpp
//
// T010 单元测试：离散 tick 与游戏时钟（GameClock）。
// 覆盖默认 20 Hz 换算、显式推进、时间戳单调、频率配置、非法频率拒绝、
// 游戏内日期时间格式化，以及"无现实时钟参与"的行为契约
// （状态只随显式调用变化，重复读取不漂移；宪法第 16 条）。

#include <cstdint>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include "wfs/sim/clock.h"

namespace {

using std::uint64_t;
using wfs::sim::GameClock;

}  // namespace

TEST(WfsClockTest, DefaultIs20Hz) {
    const GameClock clock;
    EXPECT_EQ(clock.tick_hz(), 20u);
    EXPECT_EQ(clock.tick_duration_us(), 50'000u);
    EXPECT_EQ(clock.tick(), 0u);
    EXPECT_EQ(clock.total_us(), 0u);
}

TEST(WfsClockTest, ExplicitAdvanceMovesNTicks) {
    GameClock clock;
    clock.advance(5u);
    EXPECT_EQ(clock.tick(), 5u);
    EXPECT_EQ(clock.total_us(), 250'000u);

    clock.advance(1u);
    EXPECT_EQ(clock.tick(), 6u);
    EXPECT_EQ(clock.total_us(), 300'000u);

    clock.advance(0u);  // 显式 0 为无操作。
    EXPECT_EQ(clock.tick(), 6u);
}

TEST(WfsClockTest, TimestampsAreMonotonic) {
    GameClock clock;
    uint64_t previous = clock.total_us();
    for (std::uint32_t i = 0u; i < 100u; ++i) {
        clock.advance(1u);
        EXPECT_GT(clock.total_us(), previous);
        EXPECT_EQ(clock.tick(), i + 1u);
        previous = clock.total_us();
    }
}

TEST(WfsClockTest, ConfigurableTickRate) {
    GameClock ten_hz(10u);
    EXPECT_EQ(ten_hz.tick_hz(), 10u);
    EXPECT_EQ(ten_hz.tick_duration_us(), 100'000u);
    ten_hz.advance(3u);
    EXPECT_EQ(ten_hz.total_us(), 300'000u);

    GameClock twenty_five_hz(25u);
    EXPECT_EQ(twenty_five_hz.tick_hz(), 25u);
    EXPECT_EQ(twenty_five_hz.tick_duration_us(), 40'000u);
}

TEST(WfsClockTest, InvalidTickRateRejected) {
    EXPECT_THROW(GameClock(0u), std::invalid_argument);
    EXPECT_THROW(GameClock(1'000'001u), std::invalid_argument);
}

TEST(WfsClockTest, BoundaryTickRatesAccepted) {
    GameClock one_hz(1u);
    EXPECT_EQ(one_hz.tick_hz(), 1u);
    EXPECT_EQ(one_hz.tick_duration_us(), 1'000'000u);
    one_hz.advance(1u);
    EXPECT_EQ(one_hz.total_us(), 1'000'000u);

    GameClock max_hz(1'000'000u);
    EXPECT_EQ(max_hz.tick_hz(), 1'000'000u);
    EXPECT_EQ(max_hz.tick_duration_us(), 1u);
    max_hz.advance(250u);
    EXPECT_EQ(max_hz.total_us(), 250u);
}

TEST(WfsClockTest, NonDivisibleFrequencyUsesFloor) {
    GameClock three_hz(3u);
    EXPECT_EQ(three_hz.tick_hz(), 3u);
    EXPECT_EQ(three_hz.tick_duration_us(), 333'333u);  // floor(1'000'000 / 3)。
    three_hz.advance(1u);
    EXPECT_EQ(three_hz.total_us(), 333'333u);
    three_hz.advance(2u);
    EXPECT_EQ(three_hz.total_us(), 999'999u);
    EXPECT_EQ(three_hz.format(), "D+0 00:00:00.999");
    three_hz.advance(1u);
    EXPECT_EQ(three_hz.total_us(), 1'333'332u);
}

TEST(WfsClockTest, ResetSetsTick) {
    GameClock clock;
    clock.advance(40u);
    clock.reset();
    EXPECT_EQ(clock.tick(), 0u);
    EXPECT_EQ(clock.total_us(), 0u);

    clock.advance(7u);
    clock.reset(100u);
    EXPECT_EQ(clock.tick(), 100u);
    EXPECT_EQ(clock.total_us(), 5'000'000u);
    EXPECT_EQ(clock.format(), "D+0 00:00:05.000");
}

TEST(WfsClockTest, FormatsGameDateTime) {
    GameClock clock;
    EXPECT_EQ(clock.format(), "D+0 00:00:00.000");

    clock.advance(20u);  // 20 Hz × 20 ticks = 1 游戏秒。
    EXPECT_EQ(clock.format(), "D+0 00:00:01.000");

    clock.advance(59u * 20u);  // 再 +59 秒。
    EXPECT_EQ(clock.format(), "D+0 00:01:00.000");

    clock.advance(59u * 60u * 20u);  // 再 +59 分钟。
    EXPECT_EQ(clock.format(), "D+0 01:00:00.000");

    clock.advance(23u * 3600u * 20u);  // 再 +23 小时，跨天。
    EXPECT_EQ(clock.format(), "D+1 00:00:00.000");
}

TEST(WfsClockTest, FormattingMatchesConfigurableRate) {
    GameClock ten_hz(10u);
    ten_hz.advance(10u);
    EXPECT_EQ(ten_hz.format(), "D+0 00:00:01.000");
    ten_hz.advance(86'400u * 10u);
    EXPECT_EQ(ten_hz.format(), "D+1 00:00:01.000");  // 1 天 + 1 秒。
}

TEST(WfsClockTest, StateOnlyChangesThroughExplicitCalls) {
    GameClock clock;
    const auto tick_before = clock.tick();
    const auto us_before = clock.total_us();
    const auto format_before = clock.format();
    // 重复只读访问之间没有任何显式推进：状态必须保持不变（无现实时钟参与）。
    EXPECT_EQ(clock.tick(), tick_before);
    EXPECT_EQ(clock.total_us(), us_before);
    EXPECT_EQ(clock.format(), format_before);
}

TEST(WfsClockTest, SameInputsProduceSameState) {
    GameClock first;
    GameClock second;
    for (int i = 0; i < 37; ++i) {
        first.advance(2u);
        second.advance(2u);
    }
    EXPECT_EQ(first.tick(), second.tick());
    EXPECT_EQ(first.total_us(), second.total_us());
    EXPECT_EQ(first.format(), second.format());
}
