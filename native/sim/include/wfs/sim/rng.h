// sim/include/wfs/sim/rng.h
//
// T009：统一确定性 RNG（PCG32）公开接口。
//
// 命名空间约定：wfs::sim 为项目 C++ API 命名空间，与 include 路径 wfs/sim/
// 及 C ABI 前缀 wfs_sim_*（contracts/sim-c-api.md）呼应。
//
// 确定性契约（宪法第 7 条）：
// - 同一 (seed, stream) 必须产生完全相同的序列；stream 是 0 起始的逻辑子流号，
//   不同 stream 之间相互独立且可复现。
// - Rng 仅允许模拟核心（sim/）消费；AI、UI、存档等线程不得直接调用，
//   以保证所有随机数都来自统一 RNG 流。
// - State 可直接序列化进存档（contracts/save-format.md：state_blob 含 RNG 子流
//   状态），restore 后从精确断点继续，不改变后续序列。
// - State::stream 是内部奇数增量（由逻辑流号派生），禁止手工构造/修改；
//   State 构造与 restore 会校验其为奇数，偶数视为损坏状态并抛
//   std::invalid_argument，防止非规范序列进入模拟核心。
//
// 有界取值边界：next_bounded(bound) 要求 bound > 0，返回 [0, bound)；
// bound == 0 时按契约返回 0 且不推进 RNG 状态。

#pragma once

#include <cstdint>

#include "wfs/sim/detail/pcg32.hpp"

namespace wfs::sim {

class Rng {
   public:
    struct State {
        // state 为 PCG 当前 64 位内部状态。
        // stream 为内部奇数增量（由逻辑流号经 (stream << 1) | 1 派生），
        // 保存/恢复时原样使用；禁止手工构造或修改为偶数。
        std::uint64_t state;
        std::uint64_t stream;
    };

    Rng();
    explicit Rng(std::uint64_t seed, std::uint64_t stream = 0U);
    explicit Rng(const State& state);

    std::uint32_t next() noexcept;
    std::uint32_t next_bounded(std::uint32_t bound) noexcept;

    State state() const noexcept;
    void restore(const State& state);
    void reset(std::uint64_t seed, std::uint64_t stream);

   private:
    // 唯一实现：PCG-XSH-RR 核心只存在于 vendor 头文件（detail/pcg32.hpp），
    // 本类持有其状态并按需调用，避免确定性算法双实现漂移（PR #106 review F1）。
    detail::Pcg32Random pcg_;
};

}  // namespace wfs::sim
