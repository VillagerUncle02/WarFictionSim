// tests/sim_tests/comm_test.cpp
//
// T060：通信状态模型单元测试（FR-077；CHK163；宪法第 2/7/13 条）。
//
// 覆盖：通信范围四类修正（装备功率/保障部队/地形/民用设施）、中断与恢复
// 独立判定、中断与失联取更严（link_effective）、通信中断门控指令到达、
// 层级链路中断冻结情报上送与 JSON 往返序列化。

#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "sim_runtime.h"
#include "sim_state.h"
#include "wfs/sim/comm.h"
#include "wfs/sim/loader.h"

namespace {

using wfs::sim::CommConfig;
using wfs::sim::CommLinkKind;
using wfs::sim::effective_comm_range_km;
using wfs::sim::load_scenario;
using wfs::sim::node_link_effective;
using wfs::sim::SimState;
using wfs::sim::step_sim_state;
using wfs::sim::unit_link_effective;

std::filesystem::path ScenarioPath() {
    return std::filesystem::path(WFS_SOURCE_ROOT) / "data" / "scenarios" / "scn-intel-roles.json";
}

SimState MakeState() {
    const auto load = load_scenario(ScenarioPath());
    EXPECT_TRUE(load.ok());
    SimState state;
    state.scenario = load.scenario;
    state.clock = wfs::sim::GameClock(load.scenario.tick_hz);
    state.rng = wfs::sim::Rng(7U, 0U);
    state.seed = 7U;
    state.threads = 1;
    state.scenario_path = ScenarioPath();
    wfs::sim::initialize_runtime_state(state);
    return state;
}

wfs::sim::RuntimeUnitState* FindUnit(SimState& state, const std::string& unit_id) {
    for (wfs::sim::RuntimeUnitState& unit : state.units) {
        if (unit.id == unit_id) {
            return &unit;
        }
    }
    return nullptr;
}

void Step(SimState& state, std::uint64_t ticks) {
    for (std::uint64_t i = 0U; i < ticks; ++i) {
        step_sim_state(state);
    }
}

bool HasEventPrefix(const SimState& state, const std::string& prefix) {
    for (const wfs::sim::SimEvent& event : state.event_log.events()) {
        if (event.message.starts_with(prefix)) {
            return true;
        }
    }
    return false;
}

TEST(WfsCommTest, RangeCombinesPowerSupportTerrainAndCivilianModifiers) {
    CommConfig config;
    config.base_range_km = 5.0;
    config.power_scale = 1.0;
    config.support_force_bonus_km = 2.0;
    config.support_disabled_factor = 0.4;
    config.terrain_penalty_km = {{"terrain-forest", 0.5}, {"terrain-building", 0.8}};
    config.civilian_facility_bonus_km = 1.5;
    config.civilian_facilities.push_back(wfs::sim::CivilianCommFacility{"facility-comms-1", 0.0, 0.0, 0.5});
    config.min_range_km = 0.5;

    // base_range_km × 装备功率平均 × power_scale：5 × 2 × 1 → 10 km。
    EXPECT_DOUBLE_EQ(effective_comm_range_km(config, 2.0, 2.0, 0.0, "", "", 1.0, 1.0, 2.0, 2.0), 10.0);
    // 保障部队失能：5 + 2×0.4 = 5.8 km。
    EXPECT_DOUBLE_EQ(effective_comm_range_km(config, 1.0, 1.0, 0.4, "", "", 1.0, 1.0, 2.0, 2.0), 5.8);
    // 地形修正取端点较严者：5 − max(0.5, 0.8) = 4.2 km。
    EXPECT_DOUBLE_EQ(
        effective_comm_range_km(config, 1.0, 1.0, 0.0, "terrain-forest", "terrain-building", 1.0, 1.0, 2.0, 2.0), 4.2);
    // 民用通讯设施：端点位于半径内 +1.5 → 6.5 km。
    EXPECT_DOUBLE_EQ(effective_comm_range_km(config, 1.0, 1.0, 0.0, "", "", 0.0, 0.0, 5.0, 5.0), 6.5);
    // 下界保底：极端地形罚值不回落到 min_range 以下。
    CommConfig clamped = config;
    clamped.terrain_penalty_km["terrain-forest"] = 100.0;
    EXPECT_DOUBLE_EQ(
        effective_comm_range_km(clamped, 1.0, 1.0, 0.0, "terrain-forest", "terrain-forest", 0.0, 0.0, 2.0, 2.0),
        clamped.min_range_km);
}

TEST(WfsCommTest, RangeFormulaIncludesBaseRangeMultiplier) {
    // S4：与 data-model.md §18 登记口径一致——base_range_km × 装备功率平均
    // × power_scale + 保障增益 × 系数 − 地形罚值 + 民用增益，下限保底。
    CommConfig config;
    config.base_range_km = 8.0;
    config.power_scale = 1.0;
    config.support_force_bonus_km = 0.0;
    config.support_disabled_factor = 0.4;
    config.civilian_facility_bonus_km = 0.0;
    config.min_range_km = 0.0;
    EXPECT_DOUBLE_EQ(effective_comm_range_km(config, 1.0, 1.0, 0.0, "", "", 0.0, 0.0, 1.0, 1.0), 8.0);
    // 功率平均参与乘算：8 × ((2+2)/2) × 1 = 16 km。
    EXPECT_DOUBLE_EQ(effective_comm_range_km(config, 2.0, 2.0, 0.0, "", "", 0.0, 0.0, 1.0, 1.0), 16.0);
}

TEST(WfsCommTest, OutageRestoreAndStricterOfContact) {
    SimState state = MakeState();
    Step(state, 1U);
    EXPECT_TRUE(unit_link_effective(state, "plt-1-sq-2"));

    // 超出范围 → 中断（冻结最后已知位置）。
    wfs::sim::RuntimeUnitState* unit = FindUnit(state, "plt-1-sq-2");
    ASSERT_NE(unit, nullptr);
    unit->x = 6.5;
    unit->y = 0.1;
    Step(state, 1U);
    EXPECT_FALSE(unit_link_effective(state, "plt-1-sq-2"));
    EXPECT_TRUE(HasEventPrefix(state, "COMM_OUTAGE kind=unit from=plt-1-sq-2 to=node-plt-1 reason=OUT_OF_RANGE "));
    const wfs::sim::CommLinkStatus* link = state.comm_state.Find(CommLinkKind::kUnit, "plt-1-sq-2", "node-plt-1");
    ASSERT_NE(link, nullptr);
    EXPECT_FALSE(link->connected);
    EXPECT_DOUBLE_EQ(link->last_known_x, 0.3);
    EXPECT_DOUBLE_EQ(link->last_known_y, 0.1);

    // 回到范围 → 独立恢复。
    unit->x = 0.3;
    unit->y = 0.1;
    Step(state, 1U);
    EXPECT_TRUE(unit_link_effective(state, "plt-1-sq-2"));
    EXPECT_TRUE(HasEventPrefix(state, "COMM_RESTORED kind=unit from=plt-1-sq-2 to=node-plt-1 "));

    // 中断与失联取更严：通信连通但单位失联 → 链路不生效；恢复各自独立。
    unit->out_of_contact = true;
    unit->contact_ticks_remaining = 1000U;
    EXPECT_FALSE(unit_link_effective(state, "plt-1-sq-2"));
    EXPECT_TRUE(state.comm_state.Find(CommLinkKind::kUnit, "plt-1-sq-2", "node-plt-1")->connected);
    unit->out_of_contact = false;
    EXPECT_TRUE(unit_link_effective(state, "plt-1-sq-2"));
}

TEST(WfsCommTest, OutageDefersCommandArrival) {
    SimState state = MakeState();
    wfs::sim::RuntimeUnitState* unit = FindUnit(state, "plt-1-sq-2");
    ASSERT_NE(unit, nullptr);
    unit->x = 6.5;
    unit->y = 0.1;
    const std::filesystem::path schema = wfs::sim::resolve_schema_path(ScenarioPath(), "command.schema.json");
    nlohmann::json command = {
        {"schema_version", 1},
        {"type", "MOVE"},
        {"target", nlohmann::json{{"kind", "unit"}, {"ref", "plt-1-sq-2"}}},
        {"completion", nlohmann::json{{"condition", "reach_point"},
                                      {"params", nlohmann::json{{"point", nlohmann::json{{"x", 0.3}, {"y", 0.14}}}}}}},
        {"intent", "通信中断门控测试"},
        {"behavior", nlohmann::json{{"engagement", "balanced"}}},
        {"priority", 0},
        {"deadline", nlohmann::json{{"game_time", 20000}}}};
    ASSERT_TRUE(wfs::sim::inject_player_command(state, command.dump(), schema).accepted);

    Step(state, 150U);
    EXPECT_FALSE(HasEventPrefix(state, "MISSION_COMPLETED unit=plt-1-sq-2 ")) << "中断期间指令不得到达";

    unit->x = 0.3;
    unit->y = 0.1;
    Step(state, 500U);
    EXPECT_TRUE(HasEventPrefix(state, "MISSION_COMPLETED unit=plt-1-sq-2 ")) << "恢复后指令应按序到达并完成";
}

TEST(WfsCommTest, NodeLinkOutageFreezesHierarchySync) {
    SimState state = MakeState();
    // 缩小通信范围使排↔连层级链路超距（FR-077 范围判定），其余链路保持连通。
    state.comm_config.base_range_km = 0.5;
    Step(state, 1U);
    EXPECT_FALSE(node_link_effective(state, "node-plt-1", "node-co-1"));

    Step(state, 199U);  // tick=200：连级同步，子节点链路中断 → 冻结。
    bool frozen_sync = false;
    for (const wfs::sim::SimEvent& event : state.event_log.events()) {
        if (event.tick == 200U && event.message.starts_with("INTEL_SYNC node=node-co-1 level=company tick=200 ") &&
            event.message.find("enemy=0") != std::string::npos) {
            frozen_sync = true;
        }
    }
    EXPECT_TRUE(frozen_sync) << "层级链路中断时不得合并新敌情（最后已知冻结）";
}

TEST(WfsCommTest, CommStateRoundTripsThroughJson) {
    SimState state = MakeState();
    Step(state, 1U);
    const nlohmann::json json = state.comm_state;
    const auto restored = json.get<wfs::sim::CommState>();
    EXPECT_EQ(restored.links.size(), state.comm_state.links.size());
    const wfs::sim::CommLinkStatus* original = state.comm_state.Find(CommLinkKind::kUnit, "plt-1-sq-2", "node-plt-1");
    const wfs::sim::CommLinkStatus* restored_link = restored.Find(CommLinkKind::kUnit, "plt-1-sq-2", "node-plt-1");
    ASSERT_NE(original, nullptr);
    ASSERT_NE(restored_link, nullptr);
    EXPECT_EQ(restored_link->connected, original->connected);
}

}  // namespace
