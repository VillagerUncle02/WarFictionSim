// sim/src/recon_tasks.cpp
//
// T035：侦察类任务判定机制实现。
//
// 实现策略（FR-042；CHK064 实现阶段判定机制基线）：
// - HIDDEN_RECON：目标点潜伏 hidden_hold_ticks 未被发现 → 完成；每 tick
//   对侦测半径内敌方做统一 RNG 发现判定，被发现 → RECON_DETECTED 判失败。
// - INFILTRATE_RECON：三阶段（接近 → 潜伏 → 返回撤离点），阶段号存
//   recon_progress_ticks、潜伏时长存 recon_hold_ticks；返回使用 retreating
//   驱动的机动路径（与失败后处置共用 movement 的撤退通道）。
// - OBSERVATION_POST：持续任务，每 tick 对视野内敌方登记情报（复用
//   observe_pair），观察周期满 → 周期完成并循环；被压制超阈值判失败。
// - FIRE_RECON：按确定性轮数射击（不消耗随机），轮数满足完成；被压制
//   无法撤离 → FIRE_RECON_PINNED 失败，并自动上报请求支援。

#include "wfs/sim/recon_tasks.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "sim_state.h"
#include "wfs/sim/event_log.h"
#include "wfs/sim/intel.h"
#include "wfs/sim/mission_exec.h"
#include "wfs/sim/model/mission.h"
#include "wfs/sim/movement.h"

namespace wfs::sim {

namespace {

constexpr std::uint32_t kProbabilityScale = 1000U;

double ConfigDouble(const nlohmann::json& json, const char* key, double fallback) {
    if (!json.contains(key)) {
        return fallback;
    }
    const double value = json[key].get<double>();
    if (!std::isfinite(value) || value < 0.0) {
        throw std::invalid_argument(std::string("recon 配置非法: ") + key);
    }
    return value;
}

std::uint64_t ConfigUint64(const nlohmann::json& json, const char* key, std::uint64_t fallback) {
    if (!json.contains(key)) {
        return fallback;
    }
    const std::uint64_t value = json[key].get<std::uint64_t>();
    if (value == 0U) {
        throw std::invalid_argument(std::string("recon 配置必须为正: ") + key);
    }
    return value;
}

std::uint32_t ConfigUint32(const nlohmann::json& json, const char* key, std::uint32_t fallback) {
    if (!json.contains(key)) {
        return fallback;
    }
    const std::uint32_t value = json[key].get<std::uint32_t>();
    if (value == 0U) {
        throw std::invalid_argument(std::string("recon 配置必须为正: ") + key);
    }
    return value;
}

void LogRecon(SimState& state, const EventSeverity severity, std::string message) {
    state.event_log.append(state.clock.tick(), EventCategory::kMission, severity, std::move(message));
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
double DistanceKm(const RuntimeUnitState& lhs, double target_x, double target_y) {
    const double delta_x = lhs.x - target_x;
    const double delta_y = lhs.y - target_y;
    return std::sqrt((delta_x * delta_x) + (delta_y * delta_y));
}

bool AtPoint(const RuntimeUnitState& unit, const nlohmann::json& point, double tolerance) {
    if (!point.is_object() || !point.contains("x") || !point.contains("y")) {
        return false;
    }
    return DistanceKm(unit, point["x"].get<double>(), point["y"].get<double>()) <= tolerance;
}

// 单位当前位置的隐蔽值（地形采样；固定输入固定输出）。
double ConcealmentAt(const SimState& state, const RuntimeUnitState& unit) {
    const TerrainSample terrain = terrain_sample_at(state.terrain_library, state.terrain_cells, unit.x, unit.y);
    return std::clamp(terrain.concealment, 0.0, 1.0);
}

// 发现判定：侦测半径内任一敌方按概率触发（统一 RNG）。
bool DetectionRoll(SimState& state, const RuntimeUnitState& unit) {
    const double concealment = ConcealmentAt(state, unit);
    const double probability = state.recon_config.detection_base_probability * (1.0 - concealment) *
                               (unit.moving ? state.recon_config.moving_detection_multiplier : 1.0);
    if (probability <= 0.0) {
        return false;
    }
    for (const RuntimeUnitState& enemy : state.units) {
        if (enemy.side == unit.side || enemy.destroyed) {
            continue;
        }
        if (DistanceKm(enemy, unit.x, unit.y) <= state.recon_config.detection_range_km) {
            const std::uint32_t roll = state.rng.next_bounded(kProbabilityScale);
            if (roll < static_cast<std::uint32_t>(probability * static_cast<double>(kProbabilityScale))) {
                return true;
            }
        }
    }
    return false;
}

// 侦察任务失败（被发现/被压制）：统一失败处置入口。
void FailRecon(SimState& state, RuntimeUnitState& unit, const std::string& reason) {
    wfs::sim::fail_mission(state, unit, reason);
}

}  // namespace

ReconConfig ReconConfig::FromScenario(
    const nlohmann::json& raw) {  // NOLINT(readability-convert-member-functions-to-static)
    ReconConfig config;
    if (!raw.contains("recon") || !raw["recon"].is_object()) {
        return config;
    }
    const nlohmann::json& json = raw["recon"];
    config.detection_base_probability =
        ConfigDouble(json, "detection_base_probability", config.detection_base_probability);
    config.detection_range_km = ConfigDouble(json, "detection_range_km", config.detection_range_km);
    config.moving_detection_multiplier =
        ConfigDouble(json, "moving_detection_multiplier", config.moving_detection_multiplier);
    config.hidden_hold_ticks = ConfigUint64(json, "hidden_hold_ticks", config.hidden_hold_ticks);
    config.infiltrate_hold_ticks = ConfigUint64(json, "infiltrate_hold_ticks", config.infiltrate_hold_ticks);
    config.observation_cycle_ticks = ConfigUint64(json, "observation_cycle_ticks", config.observation_cycle_ticks);
    config.fire_recon_rounds = ConfigUint32(json, "fire_recon_rounds", config.fire_recon_rounds);
    config.failure_suppression_threshold =
        ConfigDouble(json, "failure_suppression_threshold", config.failure_suppression_threshold);
    if (!config.is_valid()) {
        throw std::invalid_argument("recon 配置非法");
    }
    return config;
}

namespace {

// HIDDEN_RECON / INFILTRATE_RECON 判定。
void StepHiddenInfiltrate(SimState& state, RuntimeUnitState& unit, model::MissionType type) {
    const double tolerance = state.movement_config.arrival_tolerance_km;
    if (!unit.recon_detected && DetectionRoll(state, unit)) {
        unit.recon_detected = true;
        LogRecon(state, EventSeverity::kWarning, "RECON_DETECTED unit=" + unit.id + " type=" + unit.mission_type);
        FailRecon(state, unit, "RECON_DETECTED");
        return;
    }
    if (type == model::MissionType::kHiddenRecon) {
        if (AtPoint(unit, unit.mission_params.value("point", nlohmann::json::object()), tolerance)) {
            ++unit.recon_hold_ticks;
            if (unit.recon_hold_ticks >= state.recon_config.hidden_hold_ticks) {
                wfs::sim::complete_mission(state, unit, "HIDDEN_RECON_HOLD");
            }
        }
        return;
    }

    // INFILTRATE_RECON 三阶段：0=接近，1=潜伏，2=返回撤离点。
    const nlohmann::json target_point = unit.mission_params.value("point", nlohmann::json::object());
    const nlohmann::json exit_point = unit.mission_params.value("exit_point", target_point);
    switch (unit.recon_progress_ticks) {
        case 0U:
            if (AtPoint(unit, target_point, tolerance)) {
                unit.recon_progress_ticks = 1U;
                LogRecon(state, EventSeverity::kInfo, "RECON_INFILTRATE_AT_TARGET unit=" + unit.id);
            }
            break;
        case 1U:
            ++unit.recon_hold_ticks;
            if (unit.recon_hold_ticks >= state.recon_config.infiltrate_hold_ticks) {
                unit.recon_progress_ticks = 2U;
                unit.retreating = true;
                unit.moving = false;
                unit.target_x = exit_point.value("x", 0.0);
                unit.target_y = exit_point.value("y", 0.0);
                LogRecon(state, EventSeverity::kInfo, "RECON_INFILTRATE_RETURNING unit=" + unit.id);
            }
            break;
        case 2U:
            if (AtPoint(unit, exit_point, tolerance)) {
                wfs::sim::complete_mission(state, unit, "INFILTRATE_RETURN");
            }
            break;
        default:
            break;
    }
}

// OBSERVATION_POST 判定：观察周期循环 + 情报登记。
void StepObservationPost(SimState& state, RuntimeUnitState& unit) {
    for (const RuntimeUnitState& target : state.units) {
        if (target.side == unit.side || target.destroyed) {
            continue;
        }
        wfs::sim::observe_pair(state, unit, target);  // 观察哨向情报板登记。
    }
    ++unit.recon_hold_ticks;
    if (unit.recon_hold_ticks >= state.recon_config.observation_cycle_ticks) {
        wfs::sim::complete_mission(state, unit, "OBSERVATION_CYCLE");
    }
}

// FIRE_RECON 判定：确定性射击轮数；被压制无法撤离 → 失败并请求支援。
void StepFireRecon(SimState& state, RuntimeUnitState& unit) {
    if (unit.suppression >= state.recon_config.failure_suppression_threshold) {
        LogRecon(state, EventSeverity::kWarning,
                 "RECON_PINNED unit=" + unit.id + " suppression=" + std::to_string(unit.suppression));
        LogRecon(state, EventSeverity::kInfo,
                 "AUTO_SUPPORT_REQUEST node=" + unit.node_id + " unit=" + unit.id + " reason=FIRE_RECON_PINNED");
        FailRecon(state, unit, "FIRE_RECON_PINNED");
        return;
    }
    ++unit.recon_shots_fired;
    if (unit.recon_shots_fired >= state.recon_config.fire_recon_rounds) {
        wfs::sim::complete_mission(state, unit, "FIRE_RECON_ROUNDS");
    }
}

}  // namespace

void step_recon_tasks(SimState& state) {
    for (RuntimeUnitState& unit : state.units) {
        if (!unit.mission_active || unit.destroyed) {
            continue;
        }
        model::MissionType type;
        try {
            type = model::mission_type_from_string(unit.mission_type);
        } catch (const std::invalid_argument&) {
            continue;
        }
        if (!model::is_recon_mission(type)) {
            continue;
        }
        switch (type) {
            case model::MissionType::kHiddenRecon:
            case model::MissionType::kInfiltrateRecon:
                StepHiddenInfiltrate(state, unit, type);
                break;
            case model::MissionType::kObservationPost:
                StepObservationPost(state, unit);
                break;
            case model::MissionType::kFireRecon:
                StepFireRecon(state, unit);
                break;
            default:
                break;
        }
    }
}

}  // namespace wfs::sim
