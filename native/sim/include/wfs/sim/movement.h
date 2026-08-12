// sim/include/wfs/sim/movement.h
//
// T030：机动系统公开接口。
//
// 设计契约（FR-022/023/060；data-model.md §10）：
// - 路径移动：单位沿直线向任务目标移动，每 tick 位移 = 基准速度 × 地形
//   速度系数 × 队形系数 × 环境系数 × 压制降效；通行限制（封锁/深水无桥）
//   由统一 Passability 数据判定，两栖单位可进入水域但速度显著下降。
// - 队形（FR-060）：行军/战斗两态按任务类型自动选择（可经行为参数覆盖），
//   切换需要 formation_switch_ticks 耗时，切换期间不移动。
// - 烟幕（FR-023）：烟幕区域提供遮蔽（降低命中/观察），持续一段时间后
//   消散；本模块负责烟幕的登记、衰减与遮蔽查询。
// - 地形数据驱动：场景 raw["terrain"] 声明地形网格单元，速度系数/通行/
//   遮蔽来自 data/terrain 目录条目；无声明时按平原处理。
// - 所有函数为纯确定性计算（不读现实时钟），位移累积与到达判定使用
//   double 固定公式（宪法第 7 条）。

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "wfs/sim/loader.h"
#include "wfs/sim/model/combat.h"
#include "wfs/sim/model/mission.h"
#include "wfs/sim/model/terrain.h"

namespace wfs::sim {

struct SimState;  // 内部运行时状态；仅步进函数需要完整定义。

// 机动配置（默认基线；场景 raw["movement"] 可覆盖）。
struct MovementConfig {
    double infantry_march_speed_mps = 1.5;       // 步兵行军队形基准速度（m/s）。
    double infantry_combat_speed_mps = 0.9;      // 步兵战斗队形基准速度（m/s）。
    double vehicle_march_speed_mps = 12.0;       // 载具行军队形基准速度（m/s）。
    double vehicle_combat_speed_mps = 6.0;       // 载具战斗队形基准速度（m/s）。
    double march_speed_factor = 1.0;             // 行军队形速度系数。
    double combat_speed_factor = 0.6;            // 战斗队形速度系数（机动慢）。
    std::uint64_t formation_switch_ticks = 60U;  // 队形切换耗时（默认 3s）。
    double suppressed_speed_penalty = 0.5;       // 压制对速度的降效比例。
    double arrival_tolerance_km = 0.02;          // 到达判定容差（km）。
    double water_speed_factor = 0.5;             // 水域两栖速度系数（FR-022）。
    double smoke_concealment = 0.8;              // 烟幕遮蔽强度（FR-023）。
    std::uint64_t smoke_duration_ticks = 300U;   // 烟幕持续时长（默认 15s）。

    static MovementConfig FromScenario(const nlohmann::json& raw);
};

// 场景地形网格单元（raw["terrain"]，可选）。
struct TerrainCell {
    std::string id;
    std::string terrain_id;  // 引用 data/terrain 条目。
    double x = 0.0;
    double y = 0.0;
    double width_km = 0.0;
    double height_km = 0.0;

    bool operator==(const TerrainCell&) const = default;
};

// 烟幕区域：位置/半径/剩余时长（FR-023）。
struct SmokeArea {
    std::string id;
    double x = 0.0;
    double y = 0.0;
    double radius_m = 0.0;
    std::uint64_t ticks_remaining = 0U;

    bool operator==(const SmokeArea&) const = default;
};

inline void to_json(nlohmann::json& json, const SmokeArea& area) {
    json = nlohmann::json{{"id", area.id},
                          {"x", area.x},
                          {"y", area.y},
                          {"radius_m", area.radius_m},
                          {"ticks_remaining", area.ticks_remaining}};
}

inline void from_json(const nlohmann::json& json, SmokeArea& area) {
    area.id = json.at("id").get<std::string>();
    area.x = json.at("x").get<double>();
    area.y = json.at("y").get<double>();
    area.radius_m = json.at("radius_m").get<double>();
    area.ticks_remaining = json.at("ticks_remaining").get<std::uint64_t>();
}

// 地形采样结果（速度系数 + 通行 + 遮蔽），供移动与战斗共用。
struct TerrainSample {
    double speed_multiplier = 1.0;
    bool blocked = false;                    // 完全封锁。
    bool water_requires_amphibious = false;  // 深水无桥，需两栖。
    double concealment = 0.0;
    double cover = 0.0;
};

std::vector<TerrainCell> terrain_cells_from_scenario(const nlohmann::json& raw);

// 在 (x, y)（km）采样地形：库条目按 id 查表，网格单元按声明顺序取首个
// 包含该点的单元；无匹配时返回平原默认。
TerrainSample terrain_sample_at(const std::vector<model::TerrainElement>& library,
                                const std::vector<TerrainCell>& cells, double x, double y);

// 烟幕遮蔽强度：位于任一烟幕半径内返回配置值，否则 0。
double smoke_concealment_at(const std::vector<SmokeArea>& smoke, double x, double y);

// 队形自动选择（FR-060）：接敌/防御/侦察类任务 → 战斗队形；机动类任务
// → 行军队形；行为参数 behavior.formation（"march"/"combat"）可覆盖。
model::Formation formation_for_mission(model::MissionType type, const nlohmann::json& behavior);

// 单位基准速度（m/s）：按载具/步兵与队形取基线，乘环境机动系数与压制降效。
double unit_speed_mps(bool is_vehicle, model::Formation formation, double suppression,
                      double environment_mobility_multiplier, const MovementConfig& config);

// 推进一个 tick 的机动结算：队形切换、移动、到达完成、烟幕衰减。
// 与战斗/命令链路共用同一 SimState，固定调用顺序保证确定性。
void step_movement(SimState& state);

}  // namespace wfs::sim
