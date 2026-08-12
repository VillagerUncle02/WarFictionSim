// sim/src/state_serialization.h
//
// 内部共享：状态快照 / 状态哈希 / 存档状态序列化接口（T016/T017）。
//
// 确定性契约（宪法第 7 条）：
// - serialize_state_json 是状态哈希与存档 state_blob 的同一权威序列化：
//   包含 tick、seed、scenario_id、RNG 状态、队列、已处理事件计数、事件
//   日志与 AI 决策日志（T020）；显式排除 threads/scenario_path（线程数与
//   调度不影响状态哈希）。决策日志为空/游标为 0 时省略字段，保持旧存档
//   兼容（加载后再次序列化与旧 blob 字节一致）。
// - nlohmann::json 对象键按字典序输出（std::map），队列/事件日志按确定性
//   顺序（(tick, seq) / seq 升序）输出，因此同一状态必然产生同一字节流。

#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "sim_state.h"

namespace wfs::sim {

// 只读快照 JSON（表现层消费，含事件日志摘要；不含内部 RNG 细节）。
nlohmann::json build_snapshot_json(const SimState& state);

// 快照紧凑文本（dump()，键序确定）。
std::string build_snapshot_text(const SimState& state);

// 权威状态 JSON（状态哈希与存档 state_blob 共用；不含线程数）。
nlohmann::json serialize_state_json(const SimState& state);

// 状态哈希：sha256_hex(serialize_state_json(state).dump())。
std::string compute_state_hash_hex(const SimState& state);

}  // namespace wfs::sim
