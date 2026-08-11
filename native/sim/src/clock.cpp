// sim/src/clock.cpp
//
// T010：离散 tick 与游戏时钟（GameClock）实现。
//
// 实现策略：时间完全由 tick 计数表示，频率只在换算 tick -> 游戏微秒时参与；
// 本文件不包含任何 <chrono> 现实时钟调用，从结构上保证宪法第 16 条
// "禁止读取现实时钟参与结算"。格式化输出使用整数除法与 std::format，
// 不依赖浮点，跨编译器一致。

#include <format>
#include <stdexcept>

#include "wfs/sim/clock.h"

namespace wfs::sim {

namespace {

constexpr std::uint64_t kUsPerSecond = 1'000'000u;
constexpr std::uint64_t kUsPerMinute = 60'000'000u;
constexpr std::uint64_t kUsPerHour = 3'600'000'000u;
constexpr std::uint64_t kUsPerDay = 86'400'000'000u;

}  // namespace

GameClock::GameClock() : GameClock(kDefaultTickHz) {}

GameClock::GameClock(std::uint32_t tick_hz) : tick_hz_(tick_hz), tick_(0u) {
    if (tick_hz_ == 0u || tick_hz_ > 1'000'000u) {
        throw std::invalid_argument("wfs::sim::GameClock: tick_hz must be in [1, 1000000]");
    }
}

std::uint32_t GameClock::tick_hz() const noexcept {
    return tick_hz_;
}

std::uint64_t GameClock::tick_duration_us() const noexcept {
    return kUsPerSecond / tick_hz_;
}

GameTick GameClock::tick() const noexcept {
    return tick_;
}

std::uint64_t GameClock::total_us() const noexcept {
    return tick_ * tick_duration_us();
}

void GameClock::reset(GameTick tick) {
    tick_ = tick;
}

// tick_ 为无符号整数，累加溢出按无符号回绕处理（定义行为）但无业务语义：
// 20 Hz 下 tick 计数约 292 亿年、total_us 约 58 万年才会回绕，
// 远超出任何实际模拟时长。
void GameClock::advance(GameTick ticks) {
    tick_ += ticks;
}

std::string GameClock::format() const {
    const std::uint64_t us = total_us();
    const std::uint64_t days = us / kUsPerDay;
    const std::uint64_t remainder = us % kUsPerDay;
    const std::uint64_t hours = remainder / kUsPerHour;
    const std::uint64_t minutes = (remainder % kUsPerHour) / kUsPerMinute;
    const std::uint64_t seconds = (remainder % kUsPerMinute) / kUsPerSecond;
    const std::uint64_t millis = (remainder % kUsPerSecond) / 1000u;
    return std::format("D+{} {:02}:{:02}:{:02}.{:03}", days, hours, minutes, seconds, millis);
}

}  // namespace wfs::sim
