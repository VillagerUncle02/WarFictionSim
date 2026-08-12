// sim/include/wfs/sim/outcome.h
//
// T036：基础胜负判定公开接口。
//
// 设计契约（FR-043；CHK066/070/080/082）：
// - 场景由关键目标/关键失败条件/时间上限构成：关键目标全部完成即胜利，
//   任一关键失败条件触发即结束且失败优先于同 tick 的目标完成。
// - 时间上限到期按关键目标完成度评定：全部完成胜利、未完成失败、部分完成
//   按 partial_victory_threshold 阈值评定（完成度数据可见）。
// - 部署阶段启用时，超时未完成部署（仍有玩家单位留在部署区）→ 失败兜底。
// - 目标进度（驻留时长/完成标志）与判定结果进确定性状态，随存档序列化。

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "wfs/sim/loader.h"

namespace wfs::sim {

struct SimState;  // 内部运行时状态（sim_state.h）。

// 胜负结果（FR-043）。
enum class OutcomeKind : std::uint8_t {
    kUndecided = 0,
    kVictory = 1,
    kDefeat = 2,
};

std::string_view to_string(OutcomeKind kind) noexcept;
OutcomeKind outcome_kind_from_string(std::string_view name);
void to_json(nlohmann::json& json, OutcomeKind kind);
void from_json(const nlohmann::json& json, OutcomeKind& kind);

// 目标区域几何（数据驱动：raw["outcome"]["zones"][id]）。
struct ZoneCenter {
    double x = 0.0;
    double y = 0.0;
    double radius_km = 0.0;

    bool operator==(const ZoneCenter&) const = default;
};

// 胜负配置（场景 raw["outcome"] 可覆盖）。
struct OutcomeConfig {
    bool deployment_enabled = false;
    std::uint64_t deployment_deadline_ticks = 0U;
    std::string deployment_zone;                     // 空 = 无部署区（视为不启用兜底）。
    double partial_victory_threshold = 0.5;          // 时间上限部分完成度阈值。
    std::map<std::string, ZoneCenter> zone_centers;  // 目标/部署区几何。

    static OutcomeConfig FromScenario(const nlohmann::json& raw);

    bool is_valid() const noexcept { return partial_victory_threshold >= 0.0 && partial_victory_threshold <= 1.0; }
};

// 关键目标运行期进度（与场景 objectives 顺序一一对应）。
struct ObjectiveRuntimeState {
    std::string id;
    std::string kind;  // "unit" | "zone"。
    std::string target_ref;
    std::uint64_t duration_ticks = 0U;
    std::uint64_t hold_ticks = 0U;  // 已维持时长（区域目标）。
    bool completed = false;

    bool operator==(const ObjectiveRuntimeState&) const = default;
};

void to_json(nlohmann::json& json, const ObjectiveRuntimeState& objective);
void from_json(const nlohmann::json& json, ObjectiveRuntimeState& objective);

// 胜负判定结果（decided 后冻结，随存档序列化）。
struct OutcomeState {
    bool decided = false;
    OutcomeKind kind = OutcomeKind::kUndecided;
    double completion_ratio = 0.0;
    std::string reason;
    std::uint64_t decided_tick = 0U;

    bool operator==(const OutcomeState&) const = default;
};

void to_json(nlohmann::json& json, const OutcomeState& outcome);
void from_json(const nlohmann::json& json, OutcomeState& outcome);

// 判定单个关键目标完成（区域目标累计驻留时长；纯确定性）。
bool objective_completed(SimState& state, ObjectiveRuntimeState& objective);

// 判定单个关键失败条件触发（失败优先，按配置顺序记录）。
bool failure_condition_triggered(const SimState& state, const ScenarioObjective& condition);

// 推进一 tick 的胜负判定：失败优先 → 胜利 → 部署超时兜底 → 时间上限。
void step_outcome(SimState& state);

}  // namespace wfs::sim
