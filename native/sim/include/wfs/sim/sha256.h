// sim/include/wfs/sim/sha256.h
//
// T016：SHA-256 确定性哈希公开接口。
//
// 设计契约（research.md §7 决策：轻量自实现、避免新依赖；宪法第 7/18 条）：
// - 实现固定为 FIPS 180-4 的 SHA-256，纯字节级运算，不依赖现实时钟、
//   平台汇编或外部库；同一输入必然产生同一摘要（确定性）。
// - 摘要以固定 32 字节数组或 64 个十六进制小写字符返回，供状态哈希
//   （wfs_sim_get_state_hash）、存档校验（WFS-SAVE state_hash）与
//   可复现构建校验复用。
// - 本接口不提供增量流式接口（v1 状态/存档均为内存中的完整字节串，
//   一次哈希足够）；如未来需要流式大文件哈希再另行扩展。

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace wfs::sim {

// SHA-256 摘要字节数（32 字节 / 64 个十六进制字符）。
constexpr std::size_t kSha256DigestSize = 32U;

using Sha256Digest = std::array<std::uint8_t, kSha256DigestSize>;

// 对完整输入计算 SHA-256 摘要（FIPS 180-4）。
Sha256Digest sha256(const std::string& data);

// 对完整输入计算 SHA-256 并返回 64 个十六进制小写字符。
std::string sha256_hex(const std::string& data);

// 将已计算摘要编码为 64 个十六进制小写字符。
std::string sha256_hex(const Sha256Digest& digest);

}  // namespace wfs::sim
