// sim/src/rng.cpp
//
// T009：统一确定性 RNG（PCG32）实现。
//
// 实现策略：使用 vendor 的 PCG-XSH-RR 64/32 核心（detail/pcg32.hpp），
// 本文件只负责公开 API 的种子/流映射、有界拒绝采样与状态序列化。
// 有界取值采用 PCG 标准的无偏拒绝采样（threshold 拒绝法），避免取模偏差
// 破坏后续随机消费顺序的确定性。

#include "wfs/sim/rng.h"

#include "wfs/sim/detail/pcg32.hpp"

namespace wfs_sim {

Rng::Rng() : Rng(0u, 0u) {}

Rng::Rng(std::uint64_t seed, std::uint64_t stream) {
    reset(seed, stream);
}

Rng::Rng(const State& state) : state_(state.state), stream_(state.stream) {}

std::uint32_t Rng::next() {
    detail::Pcg32Random pcg{state_, stream_};
    const std::uint32_t value = detail::pcg32_random_r(pcg);
    state_ = pcg.state;
    stream_ = pcg.inc;
    return value;
}

std::uint32_t Rng::next_bounded(std::uint32_t bound) {
    if (bound == 0u) {
        return 0u;
    }
    const std::uint32_t threshold = (0u - bound) % bound;
    for (;;) {
        const std::uint32_t value = next();
        if (value >= threshold) {
            return value % bound;
        }
    }
}

Rng::State Rng::state() const noexcept {
    return State{state_, stream_};
}

void Rng::restore(const State& state) noexcept {
    state_ = state.state;
    stream_ = state.stream;
}

void Rng::reset(std::uint64_t seed, std::uint64_t stream) {
    detail::Pcg32Random pcg{};
    detail::pcg32_srandom_r(pcg, seed, stream);
    state_ = pcg.state;
    stream_ = pcg.inc;
}

}  // namespace wfs_sim
