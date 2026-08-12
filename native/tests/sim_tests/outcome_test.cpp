// tests/sim_tests/outcome_test.cpp
//
// T036 基础胜负判定测试（测试先行：RED → GREEN）。
//
// 覆盖（FR-043；CHK066/070/080/082；宪法第 7 条）：
// - 关键目标全部完成 → 胜利；关键失败条件触发 → 失败优先；
// - 时间上限：全完成胜利、部分完成按完成度阈值评定、未完成失败；
// - 部署超时兜底：启用部署阶段且超时未部署完成 → 失败；
// - 新增状态（失联/情报/任务/胜负）存档往返一致。

#include <cstdint>
#include <filesystem>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "save.h"
#include "sim_runtime.h"
#include "sim_state.h"
#include "state_serialization.h"
#include "test_temp_dir.h"
#include "wfs/sim/event_log.h"
#include "wfs/sim/intel.h"
#include "wfs/sim/loader.h"
#include "wfs/sim/outcome.h"

namespace {

using wfs::sim::GameClock;
using wfs::sim::IntelRecord;
using wfs::sim::load_scenario;
using wfs::sim::OutcomeConfig;
using wfs::sim::OutcomeKind;
using wfs::sim::OutcomeState;
using wfs::sim::RecognitionTier;
using wfs::sim::RuntimeUnitState;
using wfs::sim::SimState;
using wfs::sim::step_outcome;

std::filesystem::path RepoRoot() {
    return WFS_SOURCE_ROOT;
}

std::filesystem::path SampleScenario() {
    return RepoRoot() / "data" / "scenarios" / "scn-smoke-test.json";
}

std::filesystem::path TutorialScenario() {
    return RepoRoot() / "data" / "scenarios" / "scn-tutorial-platoon.json";
}

SimState MakeState(const std::filesystem::path& scenario_path, std::uint64_t seed = 42U) {
    const auto load = load_scenario(scenario_path);
    EXPECT_TRUE(load.ok()) << (load.issues.empty() ? "" : load.issues.front().message);
    SimState state;
    state.scenario = load.scenario;
    state.clock = GameClock(load.scenario.tick_hz);
    state.rng = wfs::sim::Rng(seed, 0U);
    state.seed = seed;
    state.threads = 1;
    state.scenario_path = scenario_path;
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

// 给场景注入 outcome 区域几何（zone-hill 覆盖 squad-a，不覆盖敌方 squad-c）。
void AddOutcomeZones(SimState& state) {
    state.scenario.raw["outcome"] = nlohmann::json{
        {"zones", nlohmann::json{{"zone-hill", nlohmann::json{{"x", 1.0}, {"y", 1.0}, {"radius_km", 0.2}}}}}};
    state.outcome_config = OutcomeConfig::FromScenario(state.scenario.raw);
}

}  // namespace

TEST(WfsOutcomeTest, VictoryWhenAllObjectivesComplete) {
    SimState state = MakeState(SampleScenario());
    AddOutcomeZones(state);
    ASSERT_EQ(state.objective_states.size(), 1U);
    state.objective_states.front().duration_ticks = 3U;
    for (std::uint64_t i = 0U; i < 3U; ++i) {
        step_outcome(state);
    }
    ASSERT_TRUE(state.outcome.decided);
    EXPECT_EQ(state.outcome.kind, OutcomeKind::kVictory);
    EXPECT_DOUBLE_EQ(state.outcome.completion_ratio, 1.0);
    EXPECT_NE(state.outcome.reason.find("ALL_OBJECTIVES_COMPLETED"), std::string::npos);
    EXPECT_TRUE(HasEvent(state.event_log, "OUTCOME_DECIDED kind=victory"));
}

TEST(WfsOutcomeTest, FailureTakesPriorityOverVictory) {
    SimState state = MakeState(TutorialScenario());
    state.scenario.raw["outcome"] = nlohmann::json{
        {"zones",
         nlohmann::json{{"zone-objective-hill", nlohmann::json{{"x", 0.35}, {"y", 0.3}, {"radius_km", 0.1}}}}}};
    state.outcome_config = OutcomeConfig::FromScenario(state.scenario.raw);
    ASSERT_EQ(state.objective_states.size(), 1U);
    state.objective_states.front().duration_ticks = 1U;
    RuntimeUnitState* cp = FindUnit(state, "tutorial-platoon-cp");
    ASSERT_NE(cp, nullptr);
    cp->destroyed = true;  // 关键失败条件：玩家指挥节点被消灭。

    step_outcome(state);
    ASSERT_TRUE(state.outcome.decided);
    EXPECT_EQ(state.outcome.kind, OutcomeKind::kDefeat);
    EXPECT_NE(state.outcome.reason.find("FAILURE_CONDITION"), std::string::npos);
}

TEST(WfsOutcomeTest, TimeLimitRatesPartialCompletion) {
    SimState state = MakeState(SampleScenario());
    AddOutcomeZones(state);
    state.scenario.time_limit_ticks = 5U;
    state.scenario.raw["outcome"]["partial_victory_threshold"] = 0.6;
    state.outcome_config = OutcomeConfig::FromScenario(state.scenario.raw);
    // 两个关键目标：一个完成（unit 目标已摧毁），一个未完成。
    state.scenario.objectives.push_back(wfs::sim::ScenarioObjective{"obj-2", "unit", "squad-c", 0U});
    state.objective_states.push_back(wfs::sim::ObjectiveRuntimeState{"obj-2", "unit", "squad-c", 0U, 0U, false});
    state.objective_states.front().completed = true;

    state.clock.reset(5U);
    step_outcome(state);
    ASSERT_TRUE(state.outcome.decided);
    EXPECT_EQ(state.outcome.kind, OutcomeKind::kDefeat) << "完成度 0.5 < 阈值 0.6 判失败";
    EXPECT_DOUBLE_EQ(state.outcome.completion_ratio, 0.5);

    SimState partial_victory = MakeState(SampleScenario());
    AddOutcomeZones(partial_victory);
    partial_victory.scenario.time_limit_ticks = 5U;
    partial_victory.scenario.raw["outcome"]["partial_victory_threshold"] = 0.4;
    partial_victory.outcome_config = OutcomeConfig::FromScenario(partial_victory.scenario.raw);
    partial_victory.scenario.objectives.push_back(wfs::sim::ScenarioObjective{"obj-2", "unit", "squad-c", 0U});
    partial_victory.objective_states.push_back(
        wfs::sim::ObjectiveRuntimeState{"obj-2", "unit", "squad-c", 0U, 0U, false});
    partial_victory.objective_states.front().completed = true;
    partial_victory.clock.reset(5U);
    step_outcome(partial_victory);
    ASSERT_TRUE(partial_victory.outcome.decided);
    EXPECT_EQ(partial_victory.outcome.kind, OutcomeKind::kVictory) << "完成度 0.5 >= 阈值 0.4 判胜利";
}

TEST(WfsOutcomeTest, DeploymentTimeoutFallback) {
    SimState state = MakeState(TutorialScenario());
    state.scenario.raw["outcome"] = nlohmann::json{
        {"deployment_enabled", true},
        {"deployment_deadline_ticks", 10U},
        {"deployment_zone", "zone-start"},
        {"zones", nlohmann::json{{"zone-start", nlohmann::json{{"x", 0.2}, {"y", 0.2}, {"radius_km", 0.5}}}}}};
    state.outcome_config = OutcomeConfig::FromScenario(state.scenario.raw);
    state.clock.reset(10U);
    step_outcome(state);
    ASSERT_TRUE(state.outcome.decided) << "部署超时兜底必须触发";
    EXPECT_EQ(state.outcome.kind, OutcomeKind::kDefeat);
    EXPECT_NE(state.outcome.reason.find("DEPLOYMENT_TIMEOUT"), std::string::npos);

    SimState deployed = MakeState(TutorialScenario());
    deployed.scenario.raw["outcome"] = state.scenario.raw["outcome"];
    deployed.outcome_config = OutcomeConfig::FromScenario(deployed.scenario.raw);
    for (RuntimeUnitState& unit : deployed.units) {
        if (unit.node_id == deployed.scenario.player_node_id) {
            unit.x = 1.4;
            unit.y = 1.4;  // 全部移出部署区。
        }
    }
    deployed.clock.reset(10U);
    step_outcome(deployed);
    EXPECT_FALSE(deployed.outcome.decided) << "部署完成且无失败/时间上限时不得判定";
}

TEST(WfsOutcomeTest, NewSystemStateRoundTripsThroughSave) {
    using wfs::sim::test::TempDir;
    SimState original = MakeState(TutorialScenario());
    RuntimeUnitState* unit = FindUnit(original, "tutorial-squad-1");
    ASSERT_NE(unit, nullptr);
    unit->out_of_contact = true;
    unit->contact_ticks_remaining = 77U;
    unit->has_last_known = true;
    unit->last_known_x = 0.1;
    unit->last_known_y = 0.2;
    unit->last_known_suppression = 0.25;
    unit->recon_progress_ticks = 3U;
    unit->recon_hold_ticks = 5U;
    unit->recon_shots_fired = 2U;
    unit->recon_detected = true;
    unit->retreating = true;
    unit->mission_loops = true;
    unit->failure_action = "withdraw_to";
    unit->failure_target = "1.0,1.0";

    IntelRecord record;
    record.observer_node_id = "node-tutorial-platoon";
    record.target_unit_id = "tutorial-enemy-squad-1";
    record.tier = RecognitionTier::kT2;
    record.memory_until_tick = 100U;
    record.source_expires_tick = 90U;
    record.source = wfs::sim::IntelSource{"direct", "tutorial-squad-1", "node-tutorial-platoon", 5U};
    record.last_known_x = 1.15;
    record.last_known_y = 1.2;
    record.last_motion_dx = 1.0;
    original.intel_records[wfs::sim::intel_record_key(record.observer_node_id, record.target_unit_id)] = record;
    original.objective_states.front().hold_ticks = 12U;
    original.outcome = OutcomeState{true, OutcomeKind::kDefeat, 0.5, "TEST_REASON", 7U};

    TempDir temp_dir("wfs-outcome-rt");
    const std::filesystem::path path = temp_dir.path() / "roundtrip.wfs";
    EXPECT_EQ(wfs::sim::save_to_file(original, path), WFS_SIM_RESULT_OK);
    SimState restored = MakeState(TutorialScenario());
    EXPECT_EQ(wfs::sim::load_save_into(restored, path), WFS_SIM_RESULT_OK);
    EXPECT_EQ(wfs::sim::serialize_state_json(original).dump(), wfs::sim::serialize_state_json(restored).dump())
        << "新增确定性状态必须完整存档往返";
}

TEST(WfsOutcomeTest, DeploymentEnabledRequiresDeadlineAndZone) {
    // Code Reviewer M4（🟡）：deployment_enabled 而漏配 deadline（默认 0）
    // 会在 tick 0 触发部署超时失败；非法配置必须显式拒绝。
    const nlohmann::json valid_zones{{"zone-start", nlohmann::json{{"x", 0.2}, {"y", 0.2}, {"radius_km", 0.5}}}};
    const nlohmann::json valid_outcome{{"deployment_enabled", true},
                                       {"deployment_deadline_ticks", 100U},
                                       {"deployment_zone", "zone-start"},
                                       {"zones", valid_zones}};
    EXPECT_THROW(wfs::sim::OutcomeConfig::FromScenario(
                     nlohmann::json{{"outcome", nlohmann::json{{"deployment_enabled", true}}}}),
                 std::invalid_argument);
    EXPECT_THROW(wfs::sim::OutcomeConfig::FromScenario(nlohmann::json{
                     {"outcome", nlohmann::json{{"deployment_enabled", true}, {"deployment_deadline_ticks", 100U}}}}),
                 std::invalid_argument);
    EXPECT_NO_THROW(wfs::sim::OutcomeConfig::FromScenario(nlohmann::json{{"outcome", valid_outcome}}));
}

TEST(WfsOutcomeTest, DeploymentEnabledRequiresZoneInZoneCenters) {
    // F2（🟡）：deployment_zone 必须存在于 outcome.zones（zone_centers），
    // 否则 FindZone 返回 nullptr → 部署超时兜底被静默禁用。
    const nlohmann::json valid_zones{{"zone-start", nlohmann::json{{"x", 0.2}, {"y", 0.2}, {"radius_km", 0.5}}}};
    const nlohmann::json valid_outcome{{"deployment_enabled", true},
                                       {"deployment_deadline_ticks", 100U},
                                       {"deployment_zone", "zone-start"},
                                       {"zones", valid_zones}};
    EXPECT_THROW(wfs::sim::OutcomeConfig::FromScenario(nlohmann::json{
                     {"outcome",
                      nlohmann::json{{"deployment_enabled", true},
                                     {"deployment_deadline_ticks", 100U},
                                     {"deployment_zone", "zone-typo"}}}}),
                 std::invalid_argument);
    EXPECT_NO_THROW(wfs::sim::OutcomeConfig::FromScenario(nlohmann::json{{"outcome", valid_outcome}}));
}
