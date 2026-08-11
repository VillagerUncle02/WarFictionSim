// sim/include/wfs/sim/detail/pcg32.hpp
//
// Vendored PCG32（PCG-XSH-RR 64/32，Melissa E. O'Neill 的 PCG 系列）。
//
// Source:
//   https://www.pcg-random.org/
//   https://github.com/imneme/pcg-c-basic/blob/master/pcg_basic.h
// License: MIT
//   Copyright (c) 2014 Melissa E. O'Neill <oneill@pcg-random.org>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
//
// 决策依据（research.md §2）：PCG32 状态小、易于按种子+流划分固定子流，
// 仅使用无符号整数运算，适合作为确定性模拟核心的统一随机源。

#pragma once

#include <cstdint>

namespace wfs::sim::detail {

struct Pcg32Random {
    std::uint64_t state;
    std::uint64_t inc;
};

inline std::uint32_t pcg32_random_r(Pcg32Random& rng) noexcept {
    const std::uint64_t oldstate = rng.state;
    rng.state = oldstate * 6364136223846793005ULL + rng.inc;
    const std::uint32_t xorshifted = static_cast<std::uint32_t>(((oldstate >> 18u) ^ oldstate) >> 27u);
    const std::uint32_t rot = static_cast<std::uint32_t>(oldstate >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((0u - rot) & 31u));
}

inline void pcg32_srandom_r(Pcg32Random& rng, std::uint64_t initstate, std::uint64_t initseq) noexcept {
    rng.state = 0u;
    rng.inc = (initseq << 1u) | 1u;
    pcg32_random_r(rng);
    rng.state += initstate;
    pcg32_random_r(rng);
}

}  // namespace wfs::sim::detail
