// sim/src/mission_exec.cpp
//
// T034：任务判定与事件上报实现。
//
// 实现策略：
// - 完成/失败判定全部为确定性纯函数（宪法第 7/9 条：AI 不参与判定）：
//   区域驻留（secure_zone）、到达（reach_point）、摧毁目标（destroy_unit）、
//   驱逐/清剿（drive_out/clear）、坚守（hold）、构筑（fortify）、巡逻周期
//   （patrol）按任务参数与单位状态求值；侦察类任务委托 T035。
// - 失败判定：单位被摧毁、兵力损失超过阈值；失败后处置按行为参数执行
//   （withdraw_to 启动撤退、hold 原地、report 只上报）。
// - 超时处置（FR-044）：按配置 fail/cancel/continue；continue 顺延时限，
//   fail/cancel 结束任务并执行失败后处置。
// - 持续任务（FR-044）：周期完成 → MISSION_LOOP_RESTARTED 循环，直到
//   mission_loops 关闭或被新命令取代；状态变化以 EXECUTION 上报上级。

#include "wfs/sim/mission_exec.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "sim_state.h"
#include "wfs/sim/command_chain.h"
#include "wfs/sim/event_log.h"
#include "wfs/sim/model/mission.h"
#include "wfs/sim/movement.h"

namespace wfs::sim {

namespace {

constexpr double kEpsilon = 1e-9;

double ConfigDouble(const nlohmann::json& json, const char* key, double fallback) {
    if (!json.contains(key)) {
        return fallback;
    }
    const double value = json[key].get<double>();
    if (!std::isfinite(value) || value < 0.0) {
        throw std::invalid_argument(std::string("mission 配置非法: ") + key);
    }
    return value;
}

std::uint64_t ConfigUint64(const nlohmann::json& json, const char* key, std::uint64_t fallback) {
    if (!json.contains(key)) {
        return fallback;
    }
    const std::uint64_t value = json[key].get<std::uint64_t>();
    if (value == 0U) {
        throw std::invalid_argument(std::string("mission 配置必须为正: ") + key);
    }
    return value;
}

void LogMission(SimState& state, const EventSeverity severity, std::string message) {
    state.event_log.append(state.clock.tick(), EventCategory::kMission, severity, std::move(message));
}

RuntimeUnitState* FindUnit(SimState& state, const std::string& unit_id) {
    for (RuntimeUnitState& unit : state.units) {
        if (unit.id == unit_id) {
            return &unit;
        }
    }
    return nullptr;
}

// 任务参数携带的区域几何（缺失时不可求值，不产生误完成）。
struct ZoneGeometry {
    double x = 0.0;
    double y = 0.0;
    double radius_km = 0.0;
    bool valid = false;
};

ZoneGeometry MissionZone(const nlohmann::json& params) {
    if (params.contains("zone_x") && params.contains("zone_y") && params.contains("zone_radius_km")) {
        return ZoneGeometry{params["zone_x"].get<double>(), params["zone_y"].get<double>(),
                            params["zone_radius_km"].get<double>(), true};
    }
    return ZoneGeometry{};
}

bool UnitInZone(const RuntimeUnitState& unit, const ZoneGeometry& zone) {
    const double delta_x = unit.x - zone.x;
    const double delta_y = unit.y - zone.y;
    return std::sqrt((delta_x * delta_x) + (delta_y * delta_y)) <= zone.radius_km;
}

// M3：敌我判定按阵营（side），同阵营其他节点单位不算敌人。
bool EnemyInZone(const SimState& state, const ZoneGeometry& zone, const std::string& friendly_side_id) {
    return std::ranges::any_of(state.units, [&](const RuntimeUnitState& unit) {
        return unit.side != friendly_side_id && !unit.destroyed && UnitInZone(unit, zone);
    });
}

bool FriendlyInZone(const SimState& state, const ZoneGeometry& zone, const std::string& friendly_side_id) {
    return std::ranges::any_of(state.units, [&](const RuntimeUnitState& unit) {
        return unit.side == friendly_side_id && !unit.destroyed && UnitInZone(unit, zone);
    });
}

std::size_t CasualtyCount(const RuntimeUnitState& unit) {
    return static_cast<std::size_t>(
        std::count_if(unit.soldiers.begin(), unit.soldiers.end(),
                      [](const model::Soldier& soldier) { return soldier.status == model::SoldierStatus::kCasualty; }));
}

// 取消任务（保留撤退状态：失败后处置由 movement 继续执行）。
void CancelMission(RuntimeUnitState& unit) {
    unit.mission_active = false;
    unit.mission_command_id.clear();
    unit.mission_type.clear();
    unit.mission_priority = 0;
    unit.mission_deadline_ticks = 0U;
    unit.mission_condition.clear();
    unit.mission_params = nlohmann::json::object();
    unit.moving = false;
    unit.stuck = false;
    unit.mission_loops = false;
    unit.failure_action = "report";
    unit.failure_target.clear();
    unit.recon_progress_ticks = 0U;
    unit.recon_hold_ticks = 0U;
}

bool ParsePoint(const std::string& text, double& point_x, double& point_y) {
    const std::size_t comma = text.find(',');
    if (comma == std::string::npos) {
        return false;
    }
    try {
        point_x = std::stod(text.substr(0, comma));
        point_y = std::stod(text.substr(comma + 1U));
    } catch (const std::exception&) {
        return false;
    }
    return std::isfinite(point_x) && std::isfinite(point_y);
}

}  // namespace

MissionExecConfig MissionExecConfig::FromScenario(
    const nlohmann::json& raw) {  // NOLINT(readability-convert-member-functions-to-static)
    MissionExecConfig config;
    if (!raw.contains("mission") || !raw["mission"].is_object()) {
        return config;
    }
    const nlohmann::json& json = raw["mission"];
    config.failure_loss_ratio = ConfigDouble(json, "failure_loss_ratio", config.failure_loss_ratio);
    config.patrol_cycle_ticks = ConfigUint64(json, "patrol_cycle_ticks", config.patrol_cycle_ticks);
    config.fortify_ticks = ConfigUint64(json, "fortify_ticks", config.fortify_ticks);
    config.timeout_resolution = json.value("timeout_resolution", config.timeout_resolution);
    config.timeout_extension_ticks = ConfigUint64(json, "timeout_extension_ticks", config.timeout_extension_ticks);
    if (!config.is_valid()) {
        throw std::invalid_argument("mission 配置非法（timeout_resolution 须为 fail/cancel/continue）");
    }
    return config;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
MissionEvaluation evaluate_mission(SimState& state, RuntimeUnitState& unit) {
    MissionEvaluation result;
    if (!unit.mission_active) {
        return result;
    }
    try {
        (void)model::mission_type_from_string(unit.mission_type);  // 类型注册校验。
    } catch (const std::invalid_argument&) {
        result.failed = true;
        result.reason = "UNKNOWN_MISSION_TYPE";
        return result;
    }
    if (unit.destroyed) {
        result.failed = true;
        result.reason = "DESTROYED";
        return result;
    }
    // 兵力损失阈值（任务参数可覆盖配置基线；确定性判定）。
    const double loss_ratio = unit.mission_params.value("failure_loss_ratio", state.mission_config.failure_loss_ratio);
    const std::size_t total = unit.soldiers.size();
    const double casualty_ratio =
        total > 0U ? static_cast<double>(CasualtyCount(unit)) / static_cast<double>(total) : 0.0;
    if (casualty_ratio > loss_ratio) {
        result.failed = true;
        result.reason = "LOSS_THRESHOLD";
        return result;
    }

    const std::string& condition = unit.mission_condition;
    if (condition == "reach_point") {
        if (!unit.moving && unit.mission_params.contains("point")) {
            const double target_x = unit.mission_params["point"].value("x", 0.0);
            const double target_y = unit.mission_params["point"].value("y", 0.0);
            const double delta_x = unit.x - target_x;
            const double delta_y = unit.y - target_y;
            if (std::sqrt((delta_x * delta_x) + (delta_y * delta_y)) <= state.movement_config.arrival_tolerance_km) {
                result.completed = true;
                result.reason = "REACH_POINT";
            }
        }
    } else if (condition == "secure_zone") {
        const ZoneGeometry zone = MissionZone(unit.mission_params);
        const std::uint64_t duration = unit.mission_params.value("duration_ticks", 0U);
        if (zone.valid && FriendlyInZone(state, zone, unit.side) && !EnemyInZone(state, zone, unit.side)) {
            ++unit.recon_hold_ticks;
            if (duration > 0U && unit.recon_hold_ticks >= duration) {
                result.completed = true;
                result.reason = "SECURE_ZONE_HOLD";
            }
        } else {
            unit.recon_hold_ticks = 0U;
        }
    } else if (condition == "destroy_unit") {
        if (unit.mission_params.contains("target_unit")) {
            const RuntimeUnitState* target = FindUnit(state, unit.mission_params["target_unit"].get<std::string>());
            if (target != nullptr && target->destroyed) {
                result.completed = true;
                result.reason = "TARGET_DESTROYED";
            }
        }
    } else if (condition == "drive_out" || condition == "clear") {
        const ZoneGeometry zone = MissionZone(unit.mission_params);
        const std::uint64_t duration = unit.mission_params.value("duration_ticks", 0U);
        if (zone.valid && !EnemyInZone(state, zone, unit.side)) {
            if (duration == 0U) {
                result.completed = true;
                result.reason = condition == "drive_out" ? "ZONE_CLEARED" : "CLEARED";
            } else {
                ++unit.recon_hold_ticks;
                if (unit.recon_hold_ticks >= duration) {
                    result.completed = true;
                    result.reason = "ZONE_CLEARED_HOLD";
                }
            }
        } else {
            unit.recon_hold_ticks = 0U;
        }
    } else if (condition == "hold") {
        const std::uint64_t duration = unit.mission_params.value("duration_ticks", 0U);
        ++unit.recon_hold_ticks;
        if (duration > 0U && unit.recon_hold_ticks >= duration) {
            result.completed = true;
            result.reason = "HOLD_DURATION";
        }
    } else if (condition == "fortify") {
        const std::uint64_t ticks = unit.mission_params.value("construction_ticks", state.mission_config.fortify_ticks);
        ++unit.recon_hold_ticks;
        if (unit.recon_hold_ticks >= ticks) {
            result.completed = true;
            result.reason = "FORTIFY_COMPLETE";
        }
    } else if (condition == "patrol") {
        const std::uint64_t ticks = unit.mission_params.value("cycle_ticks", state.mission_config.patrol_cycle_ticks);
        ++unit.recon_hold_ticks;
        if (unit.recon_hold_ticks >= ticks) {
            result.completed = true;
            result.reason = "PATROL_CYCLE";
        }
    }
    return result;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void report_mission_status(SimState& state, const RuntimeUnitState& unit, const std::string& status,
                           const std::string& reason) {
    std::string message = "MISSION_STATUS_REPORT interaction=EXECUTION node=" + unit.node_id + " unit=" + unit.id +
                          " command=" + unit.mission_command_id + " state=" + status;
    if (!reason.empty()) {
        message += " reason=" + reason;
    }
    LogMission(state, EventSeverity::kInfo, std::move(message));
}

void apply_failure_action(SimState& state, RuntimeUnitState& unit) {
    const std::string& action = unit.failure_action;
    if (action == "withdraw_to") {
        double target_x = 0.0;
        double target_y = 0.0;
        if (ParsePoint(unit.failure_target, target_x, target_y)) {
            unit.target_x = target_x;
            unit.target_y = target_y;
            unit.retreating = true;
            unit.moving = false;
            LogMission(state, EventSeverity::kInfo,
                       "FAILURE_ACTION_WITHDRAW unit=" + unit.id + " target=(" + std::to_string(target_x) + "," +
                           std::to_string(target_y) + ")");
        } else {
            LogMission(state, EventSeverity::kWarning,
                       "FAILURE_ACTION_INVALID unit=" + unit.id + " target=" + unit.failure_target);
            unit.moving = false;
            unit.retreating = false;
            LogMission(state, EventSeverity::kInfo, "FAILURE_ACTION_HOLD unit=" + unit.id);
        }
    } else if (action == "hold") {
        unit.moving = false;
        unit.retreating = false;
        LogMission(state, EventSeverity::kInfo, "FAILURE_ACTION_HOLD unit=" + unit.id);
    } else {
        unit.moving = false;
        unit.retreating = false;
        LogMission(state, EventSeverity::kInfo, "FAILURE_ACTION_REPORT unit=" + unit.id);
        if (action != "report") {
            LogMission(state, EventSeverity::kWarning, "FAILURE_ACTION_UNKNOWN unit=" + unit.id + " action=" + action);
        }
    }
}

void complete_mission(SimState& state, RuntimeUnitState& unit, const std::string& reason) {
    LogMission(state, EventSeverity::kInfo,
               "MISSION_COMPLETED unit=" + unit.id + " command=" + unit.mission_command_id +
                   " type=" + unit.mission_type + " reason=" + reason);
    ++state.mission_outcomes[unit.id].completed;  // T059：摘要任务状态历史。
    report_mission_status(state, unit, "COMPLETED", reason);
    bool continuous = false;
    try {
        continuous = model::is_continuous_mission(model::mission_type_from_string(unit.mission_type));
    } catch (const std::invalid_argument&) {
        continuous = false;
    }
    if (continuous && unit.mission_loops) {
        // M2：循环任务重启时命令保持 kEffective（不 MarkCompleted），
        // 使 WITHDRAW/MODIFY 仍能寻址并终止该持续任务（FR-044）。
        unit.recon_progress_ticks = 0U;
        unit.recon_hold_ticks = 0U;
        LogMission(state, EventSeverity::kInfo,
                   "MISSION_LOOP_RESTARTED unit=" + unit.id + " command=" + unit.mission_command_id);
    } else {
        state.command_chain.MarkCompleted(unit.mission_command_id);
        CancelMission(unit);
    }
}

void fail_mission(SimState& state, RuntimeUnitState& unit, const std::string& reason) {
    LogMission(state, EventSeverity::kWarning,
               "MISSION_FAILED unit=" + unit.id + " command=" + unit.mission_command_id + " type=" + unit.mission_type +
                   " reason=" + reason);
    ++state.mission_outcomes[unit.id].failed;  // T059：摘要任务状态历史。
    report_mission_status(state, unit, "FAILED", reason);
    apply_failure_action(state, unit);
    state.command_chain.MarkCompleted(unit.mission_command_id);
    CancelMission(unit);
}

void step_missions(SimState& state) {
    for (RuntimeUnitState& unit : state.units) {
        if (!unit.mission_active) {
            continue;
        }
        if (unit.destroyed) {
            fail_mission(state, unit, "DESTROYED");
            continue;
        }
        model::MissionType type;
        try {
            type = model::mission_type_from_string(unit.mission_type);
        } catch (const std::invalid_argument&) {
            continue;
        }
        if (model::is_recon_mission(type)) {
            continue;  // 侦察类任务判定由 T035 recon_tasks 负责。
        }
        MissionEvaluation evaluation = evaluate_mission(state, unit);
        if (evaluation.failed) {
            fail_mission(state, unit, evaluation.reason);
        } else if (evaluation.completed) {
            complete_mission(state, unit, evaluation.reason);
        }
    }
}

void handle_mission_timeout(SimState& state, const std::string& command_id) {
    ChainCommand* command = state.command_chain.FindMutable(command_id);
    RuntimeUnitState* unit = nullptr;
    if (command != nullptr) {
        unit = FindUnit(state, command->unit_id);
    } else {
        // 链外直接调用（测试/恢复路径）：按 mission_command_id 回退查找。
        for (RuntimeUnitState& candidate : state.units) {
            if (candidate.mission_active && candidate.mission_command_id == command_id) {
                unit = &candidate;
                break;
            }
        }
    }
    if (unit == nullptr || !unit->mission_active) {
        if (command != nullptr) {
            command->state = CommandState::kCompleted;
        }
        return;
    }
    const std::string type_name = unit->mission_type;
    if (state.mission_config.timeout_resolution == "continue") {
        unit->mission_deadline_ticks = state.clock.tick() + state.mission_config.timeout_extension_ticks;
        LogMission(state, EventSeverity::kInfo,
                   "MISSION_TIMEOUT_CONTINUED unit=" + unit->id + " command=" + command_id +
                       " new_deadline=" + std::to_string(unit->mission_deadline_ticks));
        return;  // 命令保持 effective，任务继续。
    }
    LogMission(state, EventSeverity::kWarning,
               "MISSION_TIMED_OUT unit=" + unit->id + " command=" + command_id + " type=" + type_name);
    ++state.mission_outcomes[unit->id].timed_out;  // T059：摘要任务状态历史。
    report_mission_status(state, *unit, "TIMED_OUT", "DEADLINE");
    apply_failure_action(state, *unit);
    if (command != nullptr) {
        command->state = CommandState::kCompleted;
    }
    CancelMission(*unit);
}

}  // namespace wfs::sim
