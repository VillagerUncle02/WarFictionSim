// sim/include/wfs/sim/summary.h
//
// T059：摘要上报与信息权限裁剪公开接口。
//
// 设计契约（FR-044/050/051；command-schema.md §4/§5；宪法第 7/13 条）：
// - 下级摘要只含聚合字段：任务状态分布（执行中/完成/失败/超时）、完成度、
//   损失摘要（人员/载具/班组）与支援需求；不含下级内部细节（单位 id/坐标/
//   武器/单兵），上级只能经摘要了解下级（FR-051）。
// - 摘要以生成时刻的状态为准（快照语义，CHK165）：生成后下级状态变化不改写
//   已上报摘要，上级按自身层级同步间隔获取最新摘要（FR-028/051）。
// - 生成节奏 = 上级节点的层级同步间隔（营级 15s 等），与 T058 层级同步
//   共用同一间隔语义。
// - 统一裁剪规则：build_authorized_view_json 输出本节点权限内可见信息
//   （本节点直属单位完整状态 + 下级摘要），同一裁剪逻辑供简报与 AI 决策
//   输入复用（FR-039/051）。
// - SummaryRegistry/任务结果统计随存档序列化（宪法第 13 条）。

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace wfs::sim {

struct SimState;  // 内部运行时状态（sim_state.h）。

// 摘要生成配置（场景 raw["summary"] 可覆盖；command_org 未配置时整体不激活）。
struct SummaryConfig {
    bool enabled = true;

    static SummaryConfig FromScenario(const nlohmann::json& raw);
};

// 任务状态分布（摘要字段，FR-044 状态机投影）。
struct SummaryMissionCounts {
    std::uint64_t active = 0U;  // 下达/确认/生效中。
    std::uint64_t completed = 0U;
    std::uint64_t failed = 0U;
    std::uint64_t timed_out = 0U;

    bool operator==(const SummaryMissionCounts&) const = default;
};

// 损失摘要（FR-043/051：人员/载具/班组三类）。
struct SummaryLosses {
    std::uint64_t soldiers = 0U;
    std::uint64_t vehicles = 0U;
    std::uint64_t squads = 0U;

    bool operator==(const SummaryLosses&) const = default;
};

// 单份摘要：字段在 generated_tick 冻结（快照语义），此后不可变。
struct SummaryReport {
    std::string id;  // "sum-<n>"（确定性单调）。
    std::string from_node;
    std::string to_node;
    std::uint64_t generated_tick = 0U;
    SummaryMissionCounts missions;
    std::uint32_t completion_pct = 0U;  // 0–100（整数百分比，四舍五入）。
    SummaryLosses losses;
    std::uint64_t support_requests = 0U;
    bool needs_support = false;

    bool operator==(const SummaryReport&) const = default;
};

// 每单位任务结果累计（摘要任务状态与完成度的历史来源）。
struct MissionOutcomeCounts {
    std::uint64_t completed = 0U;
    std::uint64_t failed = 0U;
    std::uint64_t timed_out = 0U;

    bool operator==(const MissionOutcomeCounts&) const = default;
};

void to_json(nlohmann::json& json, const SummaryMissionCounts& counts);
void from_json(const nlohmann::json& json, SummaryMissionCounts& counts);
void to_json(nlohmann::json& json, const SummaryLosses& losses);
void from_json(const nlohmann::json& json, SummaryLosses& losses);
void to_json(nlohmann::json& json, const SummaryReport& report);
void from_json(const nlohmann::json& json, SummaryReport& report);
void to_json(nlohmann::json& json, const MissionOutcomeCounts& counts);
void from_json(const nlohmann::json& json, MissionOutcomeCounts& counts);

// 摘要登记表：生成顺序即确定性顺序；按 (from,to) 记录上次生成 tick 供
// 上级层级间隔调度。
class SummaryRegistry {
   public:
    SummaryReport* Add(SummaryReport report);

    const SummaryReport* Find(const std::string& id) const;
    std::vector<SummaryReport> RecordsInInsertionOrder() const { return reports_; }
    // 某下级→上级的最新摘要；无记录返回 nullptr。
    const SummaryReport* LatestFor(const std::string& from_node, const std::string& to_node) const;
    std::uint64_t LastReportTick(const std::string& from_node, const std::string& to_node) const;
    void SetLastReportTick(const std::string& from_node, const std::string& to_node, std::uint64_t tick);
    std::size_t size() const noexcept { return reports_.size(); }
    bool empty() const noexcept { return reports_.empty(); }
    void Clear() noexcept;

   private:
    friend void to_json(nlohmann::json& json, const SummaryRegistry& registry);
    friend void from_json(const nlohmann::json& json, SummaryRegistry& registry);

    std::vector<SummaryReport> reports_;
    std::map<std::string, std::uint64_t> last_report_tick_;  // "from:to" → tick。
};

void to_json(nlohmann::json& json, const SummaryRegistry& registry);
void from_json(const nlohmann::json& json, SummaryRegistry& registry);

// 生成下级摘要：在调用时刻读取状态并冻结（快照语义）。
SummaryReport build_summary(const SimState& state, const std::string& from_node, const std::string& to_node,
                            std::uint64_t generated_tick);

// 推进一 tick 的摘要上报：按上级层级同步间隔为每个（下级→上级）对生成
// 摘要并记录 SUMMARY_REPORT 事件（固定节点插入顺序，确定性）。
void step_summaries(SimState& state);

// 统一裁剪规则（FR-039/051）：本节点权限内可见信息 =
// 本节点直属单位完整状态 + 下级最新摘要（不含下级内部细节）。
nlohmann::json build_authorized_view_json(const SimState& state, const std::string& node_id);

}  // namespace wfs::sim
