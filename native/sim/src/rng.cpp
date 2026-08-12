// sim/src/rng.cpp
//
// T009：统一确定性 RNG（PCG32）实现。
//
// 实现策略：使用 vendor 的 PCG-XSH-RR 64/32 核心（detail/pcg32.hpp），
// 本文件只负责公开 API 的种子/流映射、有界拒绝采样与状态序列化。
// 有界取值采用 PCG 标准的无偏拒绝采样（threshold 拒绝法），避免取模偏差
// 破坏后续随机消费顺序的确定性。

#include <stdexcept>

#include "wfs/sim/rng.h"

#include "wfs/sim/detail/pcg32.hpp"

namespace wfs::sim {

namespace {

void ValidateStream(std::uint64_t stream) {
    if ((stream & 1U) == 0U) {
        throw std::invalid_argument("wfs::sim::Rng: State.stream must be an odd internal increment");
    }
}

}  // namespace

Rng::Rng() : Rng(0U, 0U) {}

Rng::Rng(std::uint64_t seed, std::uint64_t stream) {
    reset(seed, stream);
}

Rng::Rng(const State& state) {
    ValidateStream(state.stream);
    state_ = state.state;
    stream_ = state.stream;
}

namespace {
// PCG32 参考实现常量（与 vendor 头文件一致，集中命名便于确定性审查）。
constexpr std::uint64_t kPcgMultiplier = 6364136223846793005ULL;
constexpr std::uint32_t kPcgRotMask = 31U;
}  // namespace

std::uint32_t Rng::next() noexcept {
    const std::uint64_t oldstate = state_;
    state_ = (oldstate * kPcgMultiplier) + stream_;
    const std::uint32_t xorshifted = static_cast<std::uint32_t>(((oldstate >> 18U) ^ oldstate) >> 27U);
    const std::uint32_t rot = static_cast<std::uint32_t>(oldstate >> 59U);
    return (xorshifted >> rot) | (xorshifted << ((0U - rot) & kPcgRotMask));
}

std::uint32_t Rng::next_bounded(std::uint32_t bound) noexcept {
    if (bound == 0U) {
        return 0U;
    }
    const std::uint32_t threshold = (0U - bound) % bound;
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

void Rng::restore(const State& state) {
    ValidateStream(state.stream);
    state_ = state.state;
    stream_ = state.stream;
}

void Rng::reset(std::uint64_t seed, std::uint64_t stream) {
    detail::Pcg32Random pcg{};
    detail::pcg32_srandom_r(pcg, seed, stream);
    state_ = pcg.state;
    stream_ = pcg.inc;
}

}  // namespace wfs::sim
