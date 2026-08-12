// sim/include/wfs/sim/ai/decision_log.h
//
// T020：AI 决策日志（决策点记录）公开接口。
//
// 设计契约（宪法第 10 条；data-model.md §17；T085 将在此基础上补滚动保留/
// 查询接口）：
// - 每个 AI 决策点记录：状态快照摘要 + 发给 AI 的输入（input_json/events_json）
//   + AI 返回内容（output_json）+ 校验结果，作为调试与复盘依据；拒绝的决策
//   同样记录（非法输出可追溯，宪法第 17 条禁止静默吞错）。
// - 到达时序（arrival_tick, arrival_seq）是 SC-001/CHK052 回放的关键字段：
//   相同 AI 输出序列 + 相同到达时序必须复现相同状态；拒绝的决策 arrival_seq
//   为 0（未入队）。
// - 决策日志是确定性状态的一部分：随存档序列化/恢复（save.cpp），并进入
//   状态哈希，保证"相同输入（含 AI 输出序列）→ 相同哈希"（宪法第 7 条）。
// - 本阶段按追加顺序保存全部记录（不驱逐）；T085 负责滚动保留与查询。

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "wfs/sim/clock.h"
#include "wfs/sim/command_validation.h"

namespace wfs::sim {

// 单个 AI 决策点记录（宪法 10：状态快照摘要 + 输入 + 返回 + 校验结果）。
struct AiDecisionRecord {
    std::string decision_id;      // 决策唯一标识（注入方提供或自动生成）。
    std::string node_id;          // 决策所属指挥节点（空 = 未知/未指定）。
    std::string trigger;          // 触发原因（事件名）。
    GameTick arrival_tick = 0U;   // 决策到达（入队）时的游戏 tick。
    std::uint64_t arrival_seq = 0U;  // 队列单调序列号；0 = 拒绝未入队。
    std::string state_hash;       // 决策点状态哈希（注入前，T016）。
    std::string input_json;       // 状态快照摘要（发给 AI 的输入）。
    std::string events_json;      // 决策点可见事件（JSON 数组）。
    std::string output_json;      // AI 返回的命令 JSON（含被拒绝的输出）。
    bool validation_ok = false;   // T014 双重校验是否通过。
    std::vector<ValidationError> validation_errors;  // 校验错误（确定性顺序）。

    bool operator==(const AiDecisionRecord&) const = default;
};

// 决策日志：按注入/记录顺序追加，全程确定性。
class DecisionLog {
   public:
    void append(AiDecisionRecord record);

    const std::vector<AiDecisionRecord>& entries() const noexcept { return entries_; }
    std::size_t size() const noexcept { return entries_.size(); }
    bool empty() const noexcept { return entries_.empty(); }

   private:
    std::vector<AiDecisionRecord> entries_;
};

// 存档/状态哈希用确定性 JSON 序列化（save-format.md：state_blob 完整可序列化）。
nlohmann::json decision_record_to_json(const AiDecisionRecord& record);
nlohmann::json decision_log_to_json(const DecisionLog& log);

// 从存档恢复决策日志；字段缺失/损坏时抛 std::invalid_argument 或
// nlohmann::json::exception，由 load_save_into 映射为 INVALID_DATA
// （宪法第 17 条：损坏状态显式报错，不静默恢复）。
DecisionLog decision_log_from_json(const nlohmann::json& json);

}  // namespace wfs::sim
