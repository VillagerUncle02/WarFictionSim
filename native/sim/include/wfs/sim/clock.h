// sim/include/wfs/sim/clock.h
//
// T010：离散 tick 与游戏时钟（GameClock）公开接口。
//
// 确定性契约（宪法第 16 条）：
// - 模拟时间只由显式 advance()/reset() 推进；本类型不读取、也不提供任何现实时钟
//   （墙钟/steady_clock）换算，游戏时间戳（tick、total_us、format()）全部由 tick
//   计数派生，因此相同调用序列必然产生相同时间。
// - 默认固定 20 Hz（tick 间隔 50 ms），频率可在构造时配置（正整数 Hz）；
//   频率只影响"一个 tick 代表多少游戏微秒"，不引入现实时间。
// - tick_duration_us() 为 floor(1'000'000 / tick_hz)，纯整数运算，跨编译器一致。
// - 游戏内日期时间 format() 从 0 tick 的 "D+0 00:00:00.000" 开始、按整天累计，
//   用于日志/事件展示；结算一律使用 tick 计数本身（唯一权威时间表示）。
//
// 有界取值边界：tick_hz 必须满足 1 <= tick_hz <= 1'000'000，否则抛
// std::invalid_argument（宪法第 17 条：配置错误显式报错，不静默吞错）。

#pragma once

#include <cstdint>
#include <string>

namespace wfs::sim {

// 游戏时间戳：离散 tick 计数（现实时间只存在于表现层，见 research.md §3）。
using GameTick = std::uint64_t;

class GameClock {
   public:
    static constexpr std::uint32_t kDefaultTickHz = 20u;

    GameClock();
    explicit GameClock(std::uint32_t tick_hz);

    std::uint32_t tick_hz() const noexcept;
    std::uint64_t tick_duration_us() const noexcept;
    GameTick tick() const noexcept;
    std::uint64_t total_us() const noexcept;

    void reset(GameTick tick = 0u);
    void advance(GameTick ticks = 1u);

    std::string format() const;

   private:
    std::uint32_t tick_hz_;
    GameTick tick_;
};

}  // namespace wfs::sim
