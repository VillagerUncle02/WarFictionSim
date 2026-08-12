// sim/src/movement.cpp
//
// T030：机动系统实现。
//
// 实现策略：
// - 移动结算为纯确定性函数：单位每 tick 沿目标直线推进，位移 = 基准速度
//   × 地形速度系数 × 队形系数 × 环境机动系数 × 压制降效；到达判定使用
//   容差距离，全部 double 固定公式（宪法第 7 条，不读现实时钟）。
// - 地形采样：场景 raw["terrain"] 声明网格单元，引用 data/terrain 条目；
//   通行限制由模型 Passability 的 can_traverse 语义实现（深水无桥需两栖，
//   两栖速度显著下降，FR-022）。
// - 队形切换：两态队形按任务类型自动选择 + 行为参数覆盖（FR-060），切换
//   期间不移动，耗时由配置承载。
// - 烟幕：登记/衰减/遮蔽查询（FR-023）；战斗系统通过
//   smoke_concealment_at 查询命中遮蔽。

#include "wfs/sim/movement.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "sim_state.h"
#include "wfs/sim/event_log.h"

namespace wfs::sim {

namespace {

constexpr double kMetersPerKilometer = 1000.0;

// 读取可覆盖配置字段（缺省回退；非法类型显式报错，宪法 17）。
double ConfigDouble(const nlohmann::json& json, const char* key, double fallback) {
    if (!json.contains(key)) {
        return fallback;
    }
    const double value = json[key].get<double>();
    if (!std::isfinite(value) || value < 0.0) {
        throw std::invalid_argument(std::string("movement 配置非法: ") + key);
    }
    return value;
}

std::uint64_t ConfigUint64(const nlohmann::json& json, const char* key, std::uint64_t fallback) {
    if (!json.contains(key)) {
        return fallback;
    }
    const std::uint64_t value = json[key].get<std::uint64_t>();
    if (value == 0U) {
        throw std::invalid_argument(std::string("movement 配置必须为正: ") + key);
    }
    return value;
}

}  // namespace

MovementConfig MovementConfig::FromScenario(
    const nlohmann::json& raw) {  // NOLINT(readability-convert-member-functions-to-static)
    MovementConfig config;
    if (!raw.contains("movement") || !raw["movement"].is_object()) {
        return config;
    }
    const nlohmann::json& json = raw["movement"];
    config.infantry_march_speed_mps = ConfigDouble(json, "infantry_march_speed_mps", config.infantry_march_speed_mps);
    config.infantry_combat_speed_mps =
        ConfigDouble(json, "infantry_combat_speed_mps", config.infantry_combat_speed_mps);
    config.vehicle_march_speed_mps = ConfigDouble(json, "vehicle_march_speed_mps", config.vehicle_march_speed_mps);
    config.vehicle_combat_speed_mps = ConfigDouble(json, "vehicle_combat_speed_mps", config.vehicle_combat_speed_mps);
    config.march_speed_factor = ConfigDouble(json, "march_speed_factor", config.march_speed_factor);
    config.combat_speed_factor = ConfigDouble(json, "combat_speed_factor", config.combat_speed_factor);
    config.formation_switch_ticks = ConfigUint64(json, "formation_switch_ticks", config.formation_switch_ticks);
    config.suppressed_speed_penalty = ConfigDouble(json, "suppressed_speed_penalty", config.suppressed_speed_penalty);
    config.arrival_tolerance_km = ConfigDouble(json, "arrival_tolerance_km", config.arrival_tolerance_km);
    config.water_speed_factor = ConfigDouble(json, "water_speed_factor", config.water_speed_factor);
    config.smoke_concealment = ConfigDouble(json, "smoke_concealment", config.smoke_concealment);
    config.smoke_duration_ticks = ConfigUint64(json, "smoke_duration_ticks", config.smoke_duration_ticks);
    return config;
}

std::vector<TerrainCell> terrain_cells_from_scenario(const nlohmann::json& raw) {
    std::vector<TerrainCell> cells;
    if (!raw.contains("terrain") || !raw["terrain"].is_array()) {
        return cells;
    }
    for (const nlohmann::json& entry : raw["terrain"]) {
        TerrainCell cell;
        cell.id = entry.at("id").get<std::string>();
        cell.terrain_id = entry.at("terrain_id").get<std::string>();
        cell.x = entry.at("x").get<double>();
        cell.y = entry.at("y").get<double>();
        cell.width_km = entry.value("width_km", 0.0);
        cell.height_km = entry.value("height_km", 0.0);
        if (cell.width_km <= 0.0 || cell.height_km <= 0.0) {
            throw std::invalid_argument("地形网格单元宽高必须为正: " + cell.id);
        }
        cells.push_back(std::move(cell));
    }
    return cells;
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
TerrainSample terrain_sample_at(const std::vector<model::TerrainElement>& library,
                                const std::vector<TerrainCell>& cells, const double x_km, const double y_km) {
    std::map<std::string, const model::TerrainElement*> by_id;
    for (const model::TerrainElement& element : library) {
        by_id.emplace(element.id, &element);
    }
    TerrainSample sample;  // 平原默认：可通过、速度 1.0、无遮蔽。
    for (const TerrainCell& cell : cells) {
        const bool inside =
            x_km >= cell.x && x_km <= cell.x + cell.width_km && y_km >= cell.y && y_km <= cell.y + cell.height_km;
        if (!inside) {
            continue;
        }
        const auto iterator = by_id.find(cell.terrain_id);
        if (iterator == by_id.end()) {
            continue;  // 未知地形条目按平原处理（加载阶段已交叉校验数据目录）。
        }
        const model::TerrainElement& element = *iterator->second;
        sample.speed_multiplier = element.passability.speed_multiplier;
        sample.blocked = element.passability.blocked;
        sample.water_requires_amphibious = element.passability.is_water && element.passability.requires_bridge;
        sample.concealment = element.concealment;
        sample.cover = element.cover;
        return sample;  // 首个包含该点的单元（声明顺序，确定性）。
    }
    return sample;
}
// NOLINTEND(bugprone-easily-swappable-parameters)

// 坐标参数语义由名字区分，保持与 terrain_sample_at 一致的几何签名。
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
double smoke_concealment_at(const std::vector<SmokeArea>& smoke, const double x_km, const double y_km) {
    for (const SmokeArea& area : smoke) {
        if (area.ticks_remaining == 0U) {
            continue;
        }
        const double delta_x = (x_km - area.x) * kMetersPerKilometer;
        const double delta_y = (y_km - area.y) * kMetersPerKilometer;
        if (std::sqrt((delta_x * delta_x) + (delta_y * delta_y)) <= area.radius_m) {
            return 1.0;
        }
    }
    return 0.0;
}
// NOLINTEND(bugprone-easily-swappable-parameters)

model::Formation formation_for_mission(const model::MissionType type, const nlohmann::json& behavior) {
    if (behavior.is_object() && behavior.contains("formation")) {
        return model::formation_from_string(behavior["formation"].get<std::string>());
    }
    model::Formation formation = model::Formation::kMarch;
    switch (type) {
        // 接敌/防御/侦察/控制类任务：战斗队形（机动慢、接敌准备好，FR-060）。
        case model::MissionType::kAttack:
        case model::MissionType::kDefend:
        case model::MissionType::kSecureZone:
        case model::MissionType::kClear:
        case model::MissionType::kDriveOut:
        case model::MissionType::kHiddenRecon:
        case model::MissionType::kInfiltrateRecon:
        case model::MissionType::kObservationPost:
        case model::MissionType::kFireRecon:
            formation = model::Formation::kCombat;
            break;
        // 机动/工程/支援类任务：行军队形（机动快）。
        case model::MissionType::kMove:
        case model::MissionType::kPatrol:
        case model::MissionType::kFortify:
        case model::MissionType::kSupportRequest:
        case model::MissionType::kCount:
            formation = model::Formation::kMarch;
            break;
    }
    return formation;
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
double unit_speed_mps(const bool is_vehicle, const model::Formation formation, const double suppression,
                      const double environment_mobility_multiplier, const MovementConfig& config) {
    double base = config.infantry_march_speed_mps;
    if (is_vehicle) {
        base =
            formation == model::Formation::kCombat ? config.vehicle_combat_speed_mps : config.vehicle_march_speed_mps;
    } else if (formation == model::Formation::kCombat) {
        base = config.infantry_combat_speed_mps;
    }
    const double formation_factor =
        formation == model::Formation::kCombat ? config.combat_speed_factor : config.march_speed_factor;
    const double suppression_factor = 1.0 - (std::clamp(suppression, 0.0, 1.0) * config.suppressed_speed_penalty);
    return base * formation_factor * suppression_factor * std::clamp(environment_mobility_multiplier, 0.0, 1.0);
}
// NOLINTEND(bugprone-easily-swappable-parameters)

namespace {

void LogMovement(SimState& state, const EventSeverity severity, std::string message) {
    state.event_log.append(state.clock.tick(), EventCategory::kMission, severity, std::move(message));
}

}  // namespace

// 机动结算按固定顺序（队形切换→启动→地形判定→位移→到达）处理，拆分
// 会隐藏确定性的时序依赖。
// NOLINTBEGIN(readability-function-cognitive-complexity)
void step_movement(SimState& state) {
    const MovementConfig& config = state.movement_config;
    const double environment_mobility =
        state.scenario.raw.value("environment", nlohmann::json::object()).value("mobility_multiplier", 1.0);
    const double tick_seconds = static_cast<double>(state.clock.tick_duration_us()) / 1'000'000.0;  // 秒/tick。

    for (RuntimeUnitState& unit : state.units) {
        if (unit.destroyed) {
            continue;
        }
        // 队形切换耗时（FR-060）：切换期间不移动。
        if (unit.formation_switch_remaining > 0U) {
            --unit.formation_switch_remaining;
            if (unit.formation_switch_remaining == 0U) {
                unit.formation = unit.requested_formation;
                LogMovement(state, EventSeverity::kInfo,
                            "FORMATION_CHANGED unit=" + unit.id +
                                " formation=" + std::string(model::to_string(unit.formation)));
            }
            continue;  // 切换期间禁止移动。
        }

        // 移动任务启动（UNIT_MOVING）：仅当任务为 reach_point 且队形就绪。
        if (unit.mission_active && unit.mission_condition == "reach_point" && !unit.moving && !unit.out_of_contact) {
            unit.moving = true;
            LogMovement(state, EventSeverity::kInfo,
                        "UNIT_MOVING unit=" + unit.id + " target=(" + std::to_string(unit.target_x) + "," +
                            std::to_string(unit.target_y) +
                            ") formation=" + std::string(model::to_string(unit.formation)));
        }
        if (!unit.moving) {
            continue;
        }

        // 地形速度系数与通行限制（FR-022）。
        const TerrainSample terrain = terrain_sample_at(state.terrain_library, state.terrain_cells, unit.x, unit.y);
        const bool blocked = terrain.blocked || (terrain.water_requires_amphibious && !unit.amphibious);
        if (blocked) {
            if (!unit.stuck) {
                unit.stuck = true;
                LogMovement(state, EventSeverity::kWarning, "UNIT_STUCK unit=" + unit.id);
            }
            continue;
        }

        double speed = unit_speed_mps(unit.is_vehicle, unit.formation, unit.suppression, environment_mobility, config);
        speed *= terrain.speed_multiplier;
        if (terrain.water_requires_amphibious) {
            speed *= config.water_speed_factor;  // 两栖进入水域速度显著下降。
        }
        const double step_km = speed * tick_seconds / kMetersPerKilometer;

        const double delta_x = unit.target_x - unit.x;
        const double delta_y = unit.target_y - unit.y;
        const double distance = std::sqrt((delta_x * delta_x) + (delta_y * delta_y));
        if (distance <= config.arrival_tolerance_km) {
            unit.moving = false;
            if (unit.mission_active && unit.mission_condition == "reach_point") {
                LogMovement(state, EventSeverity::kInfo,
                            "MISSION_COMPLETED unit=" + unit.id + " command=" + unit.mission_command_id +
                                " type=" + unit.mission_type);
                state.command_chain.MarkCompleted(unit.mission_command_id);
                unit.mission_active = false;
                unit.mission_command_id.clear();
            }
            unit.stuck = false;
            continue;
        }

        const double move = std::min(step_km, distance);
        unit.x += delta_x / distance * move;
        unit.y += delta_y / distance * move;
        if (distance - move <= config.arrival_tolerance_km) {
            unit.x = unit.target_x;
            unit.y = unit.target_y;
            unit.moving = false;
            if (unit.mission_active && unit.mission_condition == "reach_point") {
                LogMovement(state, EventSeverity::kInfo,
                            "MISSION_COMPLETED unit=" + unit.id + " command=" + unit.mission_command_id +
                                " type=" + unit.mission_type);
                state.command_chain.MarkCompleted(unit.mission_command_id);
                unit.mission_active = false;
                unit.mission_command_id.clear();
            }
            unit.stuck = false;
        }
    }

    // 烟幕衰减（FR-023）：按登记顺序递减，到期移除。
    for (SmokeArea& area : state.smoke_areas) {
        if (area.ticks_remaining > 0U) {
            --area.ticks_remaining;
        }
    }
    std::erase_if(state.smoke_areas, [](const SmokeArea& area) { return area.ticks_remaining == 0U; });
}
// NOLINTEND(readability-function-cognitive-complexity)

}  // namespace wfs::sim
