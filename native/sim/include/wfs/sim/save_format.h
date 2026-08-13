// sim/include/wfs/sim/save_format.h
//
// T017：存档格式契约公开常量与迁移链接口。
//
// 设计契约（contracts/save-format.md；宪法第 13 条）：
// - 存档布局固定为 magic("WFS-SAVE"，8 字节) + format_version(u32 LE) +
//   header_json(长度前缀 u32 LE) + state_blob(长度前缀 u64 LE) +
//   state_hash(SHA-256，32 字节)，加载时校验 magic/版本/哈希，校验失败
//   明确报错、禁止静默恢复（宪法第 17 条）。
// - 破坏性变更必须递增 kCurrentSaveFormatVersion，并在 migrate_state 的
//   迁移链中登记逐级迁移步骤；旧版本无法迁移时显式报错（宪法第 13 条）。
// - v1 是第一版格式：migrate_state(from==1, to==1) 为恒等；任何 from<1、
//   from>当前版本或 to>当前版本的调用都抛 std::invalid_argument。

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <nlohmann/json.hpp>

namespace wfs::sim {

// 存档 magic 字节（与 contracts/save-format.md 一致）。
inline constexpr std::array<char, 8> kSaveMagic = {'W', 'F', 'S', '-', 'S', 'A', 'V', 'E'};

// 首个存档格式版本（v1）；当前版本为 kCurrentSaveFormatVersion。
inline constexpr std::uint32_t kFirstSaveFormatVersion = 1U;
inline constexpr std::uint32_t kCurrentSaveFormatVersion = 1U;

// 内嵌 state_hash 的 SHA-256 原始字节数。
inline constexpr std::size_t kSaveStateHashSize = 32U;

// 迁移链：把状态从 from_version 逐级迁移到 to_version（两者都必须落在
// [kFirstSaveFormatVersion, kCurrentSaveFormatVersion] 且 to >= from）。
// 当前 v1 为第一版，仅支持恒等迁移；未来破坏性变更在此登记迁移步骤。
nlohmann::json migrate_state(const nlohmann::json& state, std::uint32_t from_version, std::uint32_t to_version);

}  // namespace wfs::sim
