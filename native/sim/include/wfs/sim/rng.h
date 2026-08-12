// sim/include/wfs/sim/rng.h
//
// T009：统一确定性 RNG（PCG32）公开接口。
//
// 确定性契约（宪法第 7 条）：
// - 同一 (seed, stream) 必须产生完全相同的序列；stream 是 0 起始的逻辑子流号，
//   不同 stream 之间相互独立且可复现。
// - Rng 仅允许模拟核心（sim/）消费；AI、UI、存档等线程不得直接调用，
//   以保证所有随机数都来自统一 RNG 流。
// - State 可直接序列化进存档（contracts/save-format.md：state_blob 含 RNG 子流
//   状态），restore 后从精确断点继续，不改变后续序列。
//
// 有界取值边界：next_bounded(bound) 要求 bound > 0，返回 [0, bound)；
// bound == 0 时按契约返回 0 且不推进 RNG 状态。

#pragma once

#include <cstdint>

namespace wfs_sim {

class Rng {
   public:
    struct State {
        // state 为 PCG 当前 64 位内部状态；stream 为内部奇数增量
        // （由逻辑流号经 (stream << 1) | 1 派生），保存/恢复时原样使用。
        std::uint64_t state;
        std::uint64_t stream;
    };

    Rng();
    explicit Rng(std::uint64_t seed, std::uint64_t stream = 0u);
    explicit Rng(const State& state);

    std::uint32_t next();
    std::uint32_t next_bounded(std::uint32_t bound);

    State state() const noexcept;
    void restore(const State& state) noexcept;
    void reset(std::uint64_t seed, std::uint64_t stream);

   private:
    std::uint64_t state_;
    std::uint64_t stream_;
};

}  // namespace wfs_sim
