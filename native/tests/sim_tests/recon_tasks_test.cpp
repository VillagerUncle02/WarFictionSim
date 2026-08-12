// tests/sim_tests/recon_tasks_test.cpp
//
// T035 侦察类任务判定机制测试（测试先行：RED → GREEN）。
//
// 覆盖（FR-042/043/044；CHK064 实现阶段判定机制基线）：
// - HIDDEN_RECON：潜伏时长满足完成；被发现 → RECON_DETECTED 判失败；
// - INFILTRATE_RECON：潜入 → 潜伏 → 返回撤离点三阶段判定；
// - OBSERVATION_POST：持续观察周期循环完成，并向情报板登记情报；
// - FIRE_RECON：射击轮数满足完成；被压制无法撤离 → 失败并自动上报请求支援。

#include <cstdint>
#include <filesystem>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "sim_runtime.h"
#include "sim_state.h"
#include "wfs/sim/event_log.h"
#include "wfs/sim/intel.h"
#include "wfs/sim/loader.h"
#include "wfs/sim/recon_tasks.h"

namespace {

using wfs::sim::GameClock;
using wfs::sim::load_scenario;
using wfs::sim::RuntimeUnitState;
using wfs::sim::SimState;
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

void SetReconMission(RuntimeUnitState& unit, const std::string& type, const nlohmann::json& params) {
    unit.mission_active = true;
    unit.mission_command_id = "cmd-recon";
    unit.mission_type = type;
    unit.mission_condition = "recon";
    unit.mission_params = params;
    unit.mission_priority = 1;
    unit.mission_loops = false;
    unit.failure_action = "report";
    unit.failure_target.clear();
}

}  // namespace

TEST(WfsReconTasksTest, HiddenReconCompletesAfterHold) {
    SimState state = MakeState();
    RuntimeUnitState* unit = FindUnit(state, "squad-a");
    ASSERT_NE(unit, nullptr);
    state.recon_config.hidden_hold_ticks = 3U;
    SetReconMission(*unit, "HIDDEN_RECON", nlohmann::json{{"point", {{"x", 1.0}, {"y", 1.0}}}});
    for (std::uint64_t i = 0U; i < 3U; ++i) {
        step_recon_tasks(state);
    }
    EXPECT_TRUE(HasEvent(state.event_log, "MISSION_COMPLETED unit=squad-a command=cmd-recon type=HIDDEN_RECON"));
    EXPECT_FALSE(FindUnit(state, "squad-a")->mission_active);
}

TEST(WfsReconTasksTest, HiddenReconFailsWhenDetected) {
    SimState state = MakeState();
    RuntimeUnitState* unit = FindUnit(state, "squad-a");
    ASSERT_NE(unit, nullptr);
    RuntimeUnitState* enemy = FindUnit(state, "squad-c");
    ASSERT_NE(enemy, nullptr);
    enemy->x = 1.05;
    enemy->y = 1.05;
    state.recon_config.detection_base_probability = 1.0;  // 必被发现。
    SetReconMission(*unit, "HIDDEN_RECON", nlohmann::json{{"point", {{"x", 1.0}, {"y", 1.0}}}});

    step_recon_tasks(state);
    EXPECT_TRUE(HasEvent(state.event_log, "RECON_DETECTED unit=squad-a type=HIDDEN_RECON"));
    EXPECT_TRUE(HasEvent(state.event_log, "MISSION_FAILED unit=squad-a command=cmd-recon type=HIDDEN_RECON "
                                          "reason=RECON_DETECTED"));
    EXPECT_FALSE(FindUnit(state, "squad-a")->mission_active);
}

TEST(WfsReconTasksTest, InfiltrateReconThreePhaseDetermination) {
    SimState state = MakeState();
    RuntimeUnitState* unit = FindUnit(state, "squad-a");
    ASSERT_NE(unit, nullptr);
    state.recon_config.infiltrate_hold_ticks = 2U;
    SetReconMission(*unit, "INFILTRATE_RECON",
                    nlohmann::json{{"point", {{"x", 1.2}, {"y", 1.2}}},
                                   {"exit_point", {{"x", 0.9}, {"y", 0.9}}}});

    // 阶段 0：接近目标。
    step_recon_tasks(state);
    EXPECT_EQ(unit->recon_progress_ticks, 0U);

    // 到达目标 → 阶段 1 潜伏。
    unit->x = 1.2;
    unit->y = 1.2;
    step_recon_tasks(state);
    EXPECT_EQ(unit->recon_progress_ticks, 1U);
    EXPECT_TRUE(HasEvent(state.event_log, "RECON_INFILTRATE_AT_TARGET unit=squad-a"));

    // 潜伏满 → 阶段 2 返回撤离点。
    step_recon_tasks(state);
    step_recon_tasks(state);
    EXPECT_EQ(unit->recon_progress_ticks, 2U);
    EXPECT_TRUE(unit->retreating);
    EXPECT_DOUBLE_EQ(unit->target_x, 0.9);
    EXPECT_DOUBLE_EQ(unit->target_y, 0.9);

    // 回到撤离点 → 完成。
    unit->x = 0.9;
    unit->y = 0.9;
    step_recon_tasks(state);
    EXPECT_TRUE(HasEvent(state.event_log, "MISSION_COMPLETED unit=squad-a command=cmd-recon type=INFILTRATE_RECON "
                                          "reason=INFILTRATE_RETURN"));
    EXPECT_FALSE(FindUnit(state, "squad-a")->mission_active);
}

TEST(WfsReconTasksTest, ObservationPostCyclesAndRegistersIntel) {
    SimState state = MakeState();
    RuntimeUnitState* unit = FindUnit(state, "squad-a");
    ASSERT_NE(unit, nullptr);
    RuntimeUnitState* enemy = FindUnit(state, "squad-c");
    ASSERT_NE(enemy, nullptr);
    enemy->x = 1.05;
    enemy->y = 1.05;
    state.recon_config.observation_cycle_ticks = 3U;
    SetReconMission(*unit, "OBSERVATION_POST", nlohmann::json::object());
    unit->mission_loops = true;

    for (std::uint64_t i = 0U; i < 3U; ++i) {
        step_recon_tasks(state);
    }
    EXPECT_TRUE(HasEvent(state.event_log, "MISSION_COMPLETED unit=squad-a command=cmd-recon type=OBSERVATION_POST"));
    EXPECT_TRUE(HasEvent(state.event_log, "MISSION_LOOP_RESTARTED unit=squad-a command=cmd-recon"));
    EXPECT_TRUE(FindUnit(state, "squad-a")->mission_active);
    const wfs::sim::IntelRecord* record =
        wfs::sim::find_intel(state, "platoon-alpha", "squad-c");
    ASSERT_NE(record, nullptr) << "观察哨必须向情报板登记敌方情报";
    EXPECT_EQ(record->source.unit_id, "squad-a");
}

TEST(WfsReconTasksTest, FireReconCompletesAfterRoundsAndFailsWhenPinned) {
    SimState state = MakeState();
    RuntimeUnitState* unit = FindUnit(state, "squad-a");
    ASSERT_NE(unit, nullptr);
    state.recon_config.fire_recon_rounds = 3U;
    SetReconMission(*unit, "FIRE_RECON", nlohmann::json{{"point", {{"x", 1.0}, {"y", 1.0}}}});
    for (std::uint32_t i = 0U; i < 3U; ++i) {
        step_recon_tasks(state);
    }
    EXPECT_TRUE(HasEvent(state.event_log, "MISSION_COMPLETED unit=squad-a command=cmd-recon type=FIRE_RECON "
                                          "reason=FIRE_RECON_ROUNDS"));
    EXPECT_FALSE(FindUnit(state, "squad-a")->mission_active);

    SimState pinned = MakeState();
    RuntimeUnitState* pinned_unit = FindUnit(pinned, "squad-a");
    ASSERT_NE(pinned_unit, nullptr);
    pinned_unit->suppression = 1.0;
    SetReconMission(*pinned_unit, "FIRE_RECON", nlohmann::json{{"point", {{"x", 1.0}, {"y", 1.0}}}});
    step_recon_tasks(pinned);
    EXPECT_TRUE(HasEvent(pinned.event_log, "MISSION_FAILED unit=squad-a command=cmd-recon type=FIRE_RECON "
                                           "reason=FIRE_RECON_PINNED"));
    EXPECT_TRUE(HasEvent(pinned.event_log, "AUTO_SUPPORT_REQUEST node=platoon-alpha unit=squad-a")) 
        << "火力侦察被拖住必须自动上报并请求支援";
    EXPECT_FALSE(FindUnit(pinned, "squad-a")->mission_active);
}

