// tests/sim_tests/side_faction_test.cpp
//
// Code Reviewer M3（🟡）：敌我判定不得以 node_id 是否相同为准——营级多节点
// 下同阵营不同节点会被误判为敌方。修复后阵营由数据驱动 side 字段表达，
// 缺省回退按 node_id 分组（与旧场景兼容）；本文件覆盖：
// - secure_zone/clear/drive_out 忽略同阵营其他节点单位；
// - 侦察任务不被同阵营单位发现失败；
// - 场景 side 字段数据驱动加载。

#include <cstdint>
#include <filesystem>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "sim_runtime.h"
#include "sim_state.h"
#include "test_temp_dir.h"
#include "wfs/sim/event_log.h"
#include "wfs/sim/loader.h"
#include "wfs/sim/mission_exec.h"
#include "wfs/sim/recon_tasks.h"

namespace {

using wfs::sim::GameClock;
using wfs::sim::load_scenario;
using wfs::sim::RuntimeUnitState;
using wfs::sim::SimState;
using wfs::sim::step_missions;
using wfs::sim::step_recon_tasks;

std::filesystem::path RepoRoot() {
    return WFS_SOURCE_ROOT;
}

std::filesystem::path SampleScenario() {
    return RepoRoot() / "data" / "scenarios" / "scn-smoke-test.json";
}

SimState MakeState(std::uint64_t seed = 42U) {
    const auto load = load_scenario(SampleScenario());
    EXPECT_TRUE(load.ok()) << (load.issues.empty() ? "" : load.issues.front().message);
    SimState state;
    state.scenario = load.scenario;
    state.clock = GameClock(load.scenario.tick_hz);
    state.rng = wfs::sim::Rng(seed, 0U);
    state.seed = seed;
    state.threads = 1;
    state.scenario_path = SampleScenario();
    wfs::sim::initialize_runtime_state(state);
    return state;
}

RuntimeUnitState* FindUnit(SimState& state, const std::string& unit_id) {
    for (RuntimeUnitState& unit : state.units) {
        if (unit.id == unit_id) {
            return &unit;
        }
    }
    return nullptr;
}

bool HasEvent(const wfs::sim::EventLog& log, const std::string& prefix) {
    for (const wfs::sim::SimEvent& event : log.events()) {
        if (event.message.starts_with(prefix)) {
            return true;
        }
    }
    return false;
}

// 在指定位置加入一个同阵营但不同指挥节点下的单位（营级多节点场景）。
void AddSameSideOtherNodeUnit(SimState& state, const std::string& unit_id, double x, double y) {
    RuntimeUnitState* template_unit = FindUnit(state, "squad-b");
    ASSERT_NE(template_unit, nullptr);
    RuntimeUnitState unit;
    unit.id = unit_id;
    unit.type = template_unit->type;
    unit.node_id = "platoon-bravo";
    unit.side = "blue";
    unit.x = x;
    unit.y = y;
    unit.soldiers = template_unit->soldiers;
    unit.crew_count = unit.soldiers.size();
    state.units.push_back(std::move(unit));
}

void SetMission(RuntimeUnitState& unit, const std::string& type, const std::string& condition,
                const nlohmann::json& params) {
    unit.mission_active = true;
    unit.mission_command_id = "cmd-side-test";
    unit.mission_type = type;
    unit.mission_condition = condition;
    unit.mission_params = params;
    unit.mission_priority = 1;
    unit.mission_loops = false;
    unit.failure_action = "report";
    unit.failure_target.clear();
}

}  // namespace

TEST(WfsSideFactionTest, MultiNodeSameFactionDoesNotBlockSecureZone) {
    SimState state = MakeState();
    RuntimeUnitState* unit = FindUnit(state, "squad-a");
    ASSERT_NE(unit, nullptr);
    unit->side = "blue";
    FindUnit(state, "squad-b")->side = "blue";
    FindUnit(state, "squad-c")->side = "red";
    AddSameSideOtherNodeUnit(state, "squad-friendly-other-node", 1.0, 1.05);

    SetMission(*unit, "SECURE_ZONE", "secure_zone",
               nlohmann::json{{"zone", "zone-hill"},
                              {"zone_x", 1.0},
                              {"zone_y", 1.0},
                              {"zone_radius_km", 0.1},
                              {"duration_ticks", 2}});
    step_missions(state);
    step_missions(state);
    EXPECT_TRUE(HasEvent(state.event_log, "MISSION_COMPLETED unit=squad-a type=SECURE_ZONE"))
        << "同阵营其他节点单位不得被视为区域内敌人";

    // 控制组：真正的敌方单位进入区域则不能完成。
    SimState blocked = MakeState();
    RuntimeUnitState* blocked_unit = FindUnit(blocked, "squad-a");
    blocked_unit->side = "blue";
    FindUnit(blocked, "squad-c")->side = "red";
    AddSameSideOtherNodeUnit(blocked, "squad-friendly-other-node", 1.0, 1.05);
    RuntimeUnitState* enemy = FindUnit(blocked, "squad-c");
    enemy->x = 1.02;
    enemy->y = 1.02;
    SetMission(*blocked_unit, "SECURE_ZONE", "secure_zone",
               nlohmann::json{{"zone", "zone-hill"},
                              {"zone_x", 1.0},
                              {"zone_y", 1.0},
                              {"zone_radius_km", 0.1},
                              {"duration_ticks", 2}});
    step_missions(blocked);
    step_missions(blocked);
    EXPECT_FALSE(HasEvent(blocked.event_log, "MISSION_COMPLETED unit=squad-a type=SECURE_ZONE"))
        << "敌方单位在区内必须阻止区域目标完成";
}

TEST(WfsSideFactionTest, DriveOutIgnoresSameFactionInZone) {
    SimState state = MakeState();
    RuntimeUnitState* unit = FindUnit(state, "squad-a");
    ASSERT_NE(unit, nullptr);
    unit->side = "blue";
    FindUnit(state, "squad-b")->side = "blue";
    FindUnit(state, "squad-c")->side = "red";
    AddSameSideOtherNodeUnit(state, "squad-friendly-other-node", 1.0, 1.05);

    SetMission(*unit, "DRIVE_OUT", "drive_out",
               nlohmann::json{{"zone", "zone-hill"},
                              {"zone_x", 1.0},
                              {"zone_y", 1.0},
                              {"zone_radius_km", 0.1}});
    step_missions(state);
    EXPECT_TRUE(HasEvent(state.event_log, "MISSION_COMPLETED unit=squad-a type=DRIVE_OUT"))
        << "驱逐判定必须忽略同阵营其他节点单位";
}

TEST(WfsSideFactionTest, FriendlyUnitDoesNotDetectRecon) {
    SimState state = MakeState();
    RuntimeUnitState* unit = FindUnit(state, "squad-a");
    ASSERT_NE(unit, nullptr);
    unit->side = "blue";
    FindUnit(state, "squad-b")->side = "blue";
    FindUnit(state, "squad-c")->side = "red";
    AddSameSideOtherNodeUnit(state, "squad-friendly-other-node", 1.05, 1.05);
    state.recon_config.detection_base_probability = 1.0;

    unit->mission_active = true;
    unit->mission_command_id = "cmd-recon";
    unit->mission_type = "HIDDEN_RECON";
    unit->mission_condition = "recon";
    unit->mission_params = nlohmann::json{{"point", {{"x", 1.0}, {"y", 1.0}}}};
    unit->mission_priority = 1;
    unit->mission_loops = false;
    unit->failure_action = "report";

    step_recon_tasks(state);
    EXPECT_FALSE(HasEvent(state.event_log, "RECON_DETECTED unit=squad-a"))
        << "同阵营单位不得触发侦察被发现";
    EXPECT_TRUE(FindUnit(state, "squad-a")->mission_active);
}

TEST(WfsSideFactionTest, ScenarioSideFieldIsDataDriven) {
    using wfs::sim::test::TempDir;
    TempDir temp_dir("wfs-side");
    const nlohmann::json unit_a{{"id", "squad-a"},
                                {"type", "squad-rifle-us"},
                                {"node_id", "platoon-alpha"},
                                {"side", "blue"},
                                {"x", 1.0},
                                {"y", 1.0},
                                {"ammo", nlohmann::json::array({"ammo-556"})}};
    const nlohmann::json unit_c{{"id", "squad-c"},
                                {"type", "squad-rifle-opposition"},
                                {"node_id", "enemy-command"},
                                {"side", "red"},
                                {"x", 4.0},
                                {"y", 4.0},
                                {"ammo", nlohmann::json::array({"ammo-762"})}};
    const nlohmann::json scenario{
        {"schema_version", 1},
        {"id", "scn-side-test"},
        {"name", "Side Test"},
        {"player_node_id", "platoon-alpha"},
        {"map", {{"width_km", 5.0}, {"height_km", 5.0}}},
        {"tick_hz", 20},
        {"seed", 42},
        {"zones", nlohmann::json::array()},
        {"units", nlohmann::json::array({unit_a, unit_c})},
        {"objectives", nlohmann::json::array()},
    };
    const std::filesystem::path path = temp_dir.Write("scenario.json", scenario.dump());
    const auto load = load_scenario(path, RepoRoot() / "contracts" / "schemas" / "scenario.schema.json");
    ASSERT_TRUE(load.ok()) << (load.issues.empty() ? "" : load.issues.front().message);
    ASSERT_EQ(load.scenario.units.size(), 2U);
    EXPECT_EQ(load.scenario.units[0].side, "blue");
    EXPECT_EQ(load.scenario.units[1].side, "red");
}
