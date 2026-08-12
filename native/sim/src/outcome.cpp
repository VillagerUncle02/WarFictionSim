// sim/src/outcome.cpp
//
// T036：基础胜负判定实现。
//
// 实现策略（FR-043；CHK066/070/080/082）：
// - 判定顺序固定：关键失败条件（按场景配置顺序）→ 关键目标全部完成 →
//   部署超时兜底 → 时间上限完成度评定；同 tick 失败优先（CHK080）。
// - 关键目标：unit 目标 = 目标单位被摧毁；zone 目标 = 己方单位在区内且
//   无敌方单位，维持 duration_ticks（进度存 objective_states，随存档）。
// - 时间上限：完成度 = 已完成目标数 / 目标总数；>= partial_victory_threshold
//   判胜利，否则判失败（部分完成按完成度评定，CHK070）。
// - 部署超时兜底：deployment_enabled 且超时仍有玩家单位留在部署区 → 失败。
// - 判定结果一旦 decided 即冻结，不重复上报（确定性）。

#include "wfs/sim/outcome.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "sim_state.h"
#include "wfs/sim/event_log.h"

namespace wfs::sim {

namespace {

double ConfigDouble(const nlohmann::json& json, const char* key, double fallback) {
    if (!json.contains(key)) {
        return fallback;
    }
    const double value = json[key].get<double>();
    if (!std::isfinite(value) || value < 0.0) {
        throw std::invalid_argument(std::string("outcome 配置非法: ") + key);
    }
    return value;
}

std::uint64_t ConfigUint64(const nlohmann::json& json, const char* key, std::uint64_t fallback) {
    if (!json.contains(key)) {
        return fallback;
    }
    const std::uint64_t value = json[key].get<std::uint64_t>();
    if (value == 0U) {
        throw std::invalid_argument(std::string("outcome 配置必须为正: ") + key);
    }
    return value;
}

void LogOutcome(SimState& state, std::string message) {
    state.event_log.append(state.clock.tick(), EventCategory::kSystem, EventSeverity::kCritical, std::move(message));
}

const ZoneCenter* FindZone(const OutcomeConfig& config, const std::string& zone_id) {
    const auto iterator = config.zone_centers.find(zone_id);
    return iterator == config.zone_centers.end() ? nullptr : &iterator->second;
}

bool UnitInZone(const RuntimeUnitState& unit, const ZoneCenter& zone) {
    const double delta_x = unit.x - zone.x;
    const double delta_y = unit.y - zone.y;
    return std::sqrt((delta_x * delta_x) + (delta_y * delta_y)) <= zone.radius_km;
}

// M3：敌我判定按阵营（side），同阵营其他节点单位不算敌人。
bool EnemyInZone(const SimState& state, const ZoneCenter& zone, const std::string& friendly_side_id) {
    return std::ranges::any_of(state.units, [&](const RuntimeUnitState& unit) {
        return unit.side != friendly_side_id && !unit.destroyed && UnitInZone(unit, zone);
    });
}

bool FriendlyInZone(const SimState& state, const ZoneCenter& zone, const std::string& friendly_side_id) {
    return std::ranges::any_of(state.units, [&](const RuntimeUnitState& unit) {
        return unit.side == friendly_side_id && !unit.destroyed && UnitInZone(unit, zone);
    });
}

void Decide(SimState& state, const OutcomeKind kind, const double ratio, std::string reason) {
    state.outcome.decided = true;
    state.outcome.kind = kind;
    state.outcome.completion_ratio = ratio;
    state.outcome.reason = std::move(reason);
    state.outcome.decided_tick = state.clock.tick();
    LogOutcome(state, "OUTCOME_DECIDED kind=" + std::string(to_string(kind)) + " ratio=" + std::to_string(ratio) +
                          " reason=" + state.outcome.reason);
}

}  // namespace

std::string_view to_string(const OutcomeKind kind) noexcept {
    switch (kind) {
        case OutcomeKind::kUndecided:
            return "undecided";
        case OutcomeKind::kVictory:
            return "victory";
        case OutcomeKind::kDefeat:
            return "defeat";
    }
    return "unknown";
}

OutcomeKind outcome_kind_from_string(const std::string_view name) {
    if (name == "undecided") {
        return OutcomeKind::kUndecided;
    }
    if (name == "victory") {
        return OutcomeKind::kVictory;
    }
    if (name == "defeat") {
        return OutcomeKind::kDefeat;
    }
    throw std::invalid_argument("未知胜负结果: " + std::string(name));
}

void to_json(nlohmann::json& json, const OutcomeKind kind) {
    json = to_string(kind);
}

void from_json(const nlohmann::json& json, OutcomeKind& kind) {
    kind = outcome_kind_from_string(json.get<std::string>());
}

OutcomeConfig OutcomeConfig::FromScenario(
    const nlohmann::json& raw) {  // NOLINT(readability-convert-member-functions-to-static)
    OutcomeConfig config;
    if (!raw.contains("outcome") || !raw["outcome"].is_object()) {
        return config;
    }
    const nlohmann::json& json = raw["outcome"];
    config.deployment_enabled = json.value("deployment_enabled", config.deployment_enabled);
    if (json.contains("deployment_deadline_ticks")) {
        config.deployment_deadline_ticks =
            ConfigUint64(json, "deployment_deadline_ticks", config.deployment_deadline_ticks);
    }
    config.deployment_zone = json.value("deployment_zone", config.deployment_zone);
    config.partial_victory_threshold =
        ConfigDouble(json, "partial_victory_threshold", config.partial_victory_threshold);
    if (json.contains("zones") && json["zones"].is_object()) {
        for (const auto& [zone_id, zone_json] : json["zones"].items()) {
            ZoneCenter center;
            center.x = zone_json.value("x", 0.0);
            center.y = zone_json.value("y", 0.0);
            center.radius_km = zone_json.value("radius_km", 0.0);
            config.zone_centers[zone_id] = center;
        }
    }
    // M4：启用部署阶段必须同时配置 deadline 与部署区，否则 tick 0 就会触发
    // 部署超时失败（默认 deadline=0）；非法配置显式拒绝（宪法第 17 条）。
    if (config.deployment_enabled && (config.deployment_deadline_ticks == 0U || config.deployment_zone.empty())) {
        throw std::invalid_argument(
            "outcome 配置非法（deployment_enabled 必须同时配置 deployment_deadline_ticks 与 deployment_zone）");
    }
    if (!config.is_valid()) {
        throw std::invalid_argument("outcome 配置非法（partial_victory_threshold 须在 [0,1]）");
    }
    return config;
}

void to_json(nlohmann::json& json, const ObjectiveRuntimeState& objective) {
    json = nlohmann::json{{"id", objective.id},
                          {"kind", objective.kind},
                          {"target_ref", objective.target_ref},
                          {"duration_ticks", objective.duration_ticks},
                          {"hold_ticks", objective.hold_ticks},
                          {"completed", objective.completed}};
}

void from_json(const nlohmann::json& json, ObjectiveRuntimeState& objective) {
    objective.id = json.at("id").get<std::string>();
    objective.kind = json.at("kind").get<std::string>();
    objective.target_ref = json.at("target_ref").get<std::string>();
    objective.duration_ticks = json.at("duration_ticks").get<std::uint64_t>();
    objective.hold_ticks = json.at("hold_ticks").get<std::uint64_t>();
    objective.completed = json.at("completed").get<bool>();
}

void to_json(nlohmann::json& json, const OutcomeState& outcome) {
    json = nlohmann::json{{"decided", outcome.decided},
                          {"kind", outcome.kind},
                          {"completion_ratio", outcome.completion_ratio},
                          {"reason", outcome.reason},
                          {"decided_tick", outcome.decided_tick}};
}

void from_json(const nlohmann::json& json, OutcomeState& outcome) {
    outcome.decided = json.at("decided").get<bool>();
    outcome.kind = json.at("kind").get<OutcomeKind>();
    outcome.completion_ratio = json.at("completion_ratio").get<double>();
    outcome.reason = json.at("reason").get<std::string>();
    outcome.decided_tick = json.at("decided_tick").get<std::uint64_t>();
}

bool objective_completed(SimState& state, ObjectiveRuntimeState& objective) {
    if (objective.completed) {
        return true;
    }
    if (objective.kind == "unit") {
        for (const RuntimeUnitState& unit : state.units) {
            if (unit.id == objective.target_ref) {
                if (unit.destroyed) {
                    objective.completed = true;
                    return true;
                }
                return false;
            }
        }
        return false;  // 目标单位不存在：不可判定（保持未完成）。
    }
    if (objective.kind == "zone") {
        const ZoneCenter* zone = FindZone(state.outcome_config, objective.target_ref);
        if (zone == nullptr) {
            return false;  // 区域几何缺失：不可求值。
        }
        const std::string friendly = friendly_side(state);
        if (EnemyInZone(state, *zone, friendly) || !FriendlyInZone(state, *zone, friendly)) {
            objective.hold_ticks = 0U;
            return false;
        }
        ++objective.hold_ticks;
        if (objective.duration_ticks > 0U && objective.hold_ticks >= objective.duration_ticks) {
            objective.completed = true;
            return true;
        }
    }
    return false;
}

bool failure_condition_triggered(const SimState& state, const ScenarioObjective& condition) {
    if (condition.kind == "unit") {
        for (const RuntimeUnitState& unit : state.units) {
            if (unit.id == condition.target_ref) {
                return unit.destroyed;
            }
        }
        return false;
    }
    if (condition.kind == "zone") {
        const ZoneCenter* zone = FindZone(state.outcome_config, condition.target_ref);
        if (zone == nullptr) {
            return false;
        }
        return EnemyInZone(state, *zone, friendly_side(state));
    }
    return false;
}

void step_outcome(SimState& state) {
    if (state.outcome.decided) {
        return;  // 判定冻结：不重复上报。
    }
    // 失败优先（FR-043/CHK080）：按场景配置顺序检查关键失败条件。
    for (const ScenarioObjective& condition : state.scenario.failure_conditions) {
        if (failure_condition_triggered(state, condition)) {
            Decide(state, OutcomeKind::kDefeat, 0.0, "FAILURE_CONDITION id=" + condition.id);
            return;
        }
    }
    // 关键目标进度与全部完成检查。
    std::size_t completed_count = 0U;
    for (std::size_t i = 0U; i < state.objective_states.size(); ++i) {
        if (i < state.scenario.objectives.size()) {
            state.objective_states[i].id = state.scenario.objectives[i].id;
        }
        if (objective_completed(state, state.objective_states[i])) {
            ++completed_count;
        }
    }
    const std::size_t total = state.objective_states.size();
    const double ratio = total > 0U ? static_cast<double>(completed_count) / static_cast<double>(total) : 0.0;
    if (total > 0U && completed_count == total) {
        Decide(state, OutcomeKind::kVictory, 1.0, "ALL_OBJECTIVES_COMPLETED");
        return;
    }
    // 部署超时兜底（CHK082）：启用部署阶段且超时仍有单位在部署区。
    if (state.outcome_config.deployment_enabled && !state.outcome_config.deployment_zone.empty() &&
        state.clock.tick() >= state.outcome_config.deployment_deadline_ticks) {
        const ZoneCenter* zone = FindZone(state.outcome_config, state.outcome_config.deployment_zone);
        bool still_deploying = zone != nullptr && FriendlyInZone(state, *zone, friendly_side(state));
        if (still_deploying) {
            Decide(state, OutcomeKind::kDefeat, ratio, "DEPLOYMENT_TIMEOUT");
            return;
        }
    }
    // 时间上限（FR-043）：按关键目标完成度评定。
    if (state.scenario.time_limit_ticks > 0U && state.clock.tick() >= state.scenario.time_limit_ticks) {
        if (ratio >= 1.0) {
            Decide(state, OutcomeKind::kVictory, ratio, "TIME_LIMIT_ALL_COMPLETED");
        } else if (ratio >= state.outcome_config.partial_victory_threshold) {
            Decide(state, OutcomeKind::kVictory, ratio, "TIME_LIMIT_PARTIAL");
        } else if (ratio > 0.0) {
            Decide(state, OutcomeKind::kDefeat, ratio, "TIME_LIMIT_PARTIAL");
        } else {
            Decide(state, OutcomeKind::kDefeat, 0.0, "TIME_LIMIT_NONE");
        }
    }
}

}  // namespace wfs::sim
