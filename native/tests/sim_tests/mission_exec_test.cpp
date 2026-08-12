// tests/sim_tests/mission_exec_test.cpp
//
// T034 任务判定与事件上报测试（测试先行：RED → GREEN）。
//
// 覆盖（FR-043/044/045；宪法第 7 条：AI 不参与任务判定）：
// - 确定性完成判定：secure_zone 按区域驻留时长完成，reach_point 到达完成；
// - 失败判定：兵力损失超过阈值判失败并上报；
// - 超时处置：超时按配置判失败/继续，失败后处置（撤退/坚守/上报）生效；
// - 持续任务循环：循环周期完成 → MISSION_LOOP_RESTARTED，终止后退出；
// - 任务状态事件上报上级（interaction=EXECUTION）。

#include <cstdint>
#include <filesystem>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "sim_runtime.h"
#include "sim_state.h"
#include "wfs/sim/command_chain.h"
#include "wfs/sim/event_log.h"
#include "wfs/sim/loader.h"
#include "wfs/sim/mission_exec.h"

namespace {

using wfs::sim::GameClock;
using wfs::sim::handle_mission_timeout;
using wfs::sim::load_scenario;
using wfs::sim::RuntimeUnitState;
using wfs::sim::SimState;
using wfs::sim::step_missions;

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

void SetMission(RuntimeUnitState& unit, const std::string& type, const std::string& condition,
                const nlohmann::json& params) {
    unit.mission_active = true;
    unit.mission_command_id = "cmd-test";
    unit.mission_type = type;
    unit.mission_condition = condition;
    unit.mission_params = params;
    unit.mission_priority = 1;
    unit.mission_loops = false;
    unit.failure_action = "report";
    unit.failure_target.clear();
}

}  // namespace

TEST(WfsMissionExecTest, SecureZoneCompletesAfterHoldDuration) {
    SimState state = MakeState();
    RuntimeUnitState* unit = FindUnit(state, "squad-a");
    ASSERT_NE(unit, nullptr);
    SetMission(*unit, "SECURE_ZONE", "secure_zone",
               nlohmann::json{{"zone", "zone-hill"},
                              {"zone_x", 1.0},
                              {"zone_y", 1.0},
                              {"zone_radius_km", 0.1},
                              {"duration_ticks", 5}});
    for (std::uint64_t i = 0U; i < 5U; ++i) {
        step_missions(state);
    }
    EXPECT_TRUE(HasEvent(state.event_log, "MISSION_COMPLETED unit=squad-a command=cmd-test type=SECURE_ZONE"))
        << "区域驻留时长满足后必须确定性完成";
    EXPECT_TRUE(HasEvent(state.event_log, "MISSION_STATUS_REPORT interaction=EXECUTION node=platoon-alpha "
                                          "unit=squad-a command=cmd-test state=COMPLETED"));
    EXPECT_FALSE(FindUnit(state, "squad-a")->mission_active);
}

TEST(WfsMissionExecTest, ReachPointCompletesOnArrival) {
    SimState state = MakeState();
    RuntimeUnitState* unit = FindUnit(state, "squad-a");
    ASSERT_NE(unit, nullptr);
    SetMission(*unit, "MOVE", "reach_point", nlohmann::json{{"point", {{"x", 1.0}, {"y", 1.0}}}});
    unit->moving = false;
    step_missions(state);
    EXPECT_TRUE(HasEvent(state.event_log, "MISSION_COMPLETED unit=squad-a command=cmd-test type=MOVE"));
    EXPECT_FALSE(FindUnit(state, "squad-a")->mission_active);
}

TEST(WfsMissionExecTest, FailsOnLossThresholdAndReports) {
    SimState state = MakeState();
    RuntimeUnitState* unit = FindUnit(state, "squad-a");
    ASSERT_NE(unit, nullptr);
    ASSERT_GE(unit->soldiers.size(), 5U);
    for (std::size_t i = 0U; i < 5U; ++i) {
        unit->soldiers[i].status = wfs::sim::model::SoldierStatus::kCasualty;
    }
    SetMission(*unit, "ATTACK", "destroy_unit", nlohmann::json{{"target_unit", "squad-c"}});
    step_missions(state);
    EXPECT_TRUE(HasEvent(state.event_log, "MISSION_FAILED unit=squad-a command=cmd-test type=ATTACK reason=LOSS_THRESHOLD"));
    EXPECT_TRUE(HasEvent(state.event_log, "MISSION_STATUS_REPORT interaction=EXECUTION node=platoon-alpha "
                                          "unit=squad-a command=cmd-test state=FAILED"));
    EXPECT_FALSE(FindUnit(state, "squad-a")->mission_active);
}

TEST(WfsMissionExecTest, ContinuousPatrolLoopsUntilStopped) {
    SimState state = MakeState();
    RuntimeUnitState* unit = FindUnit(state, "squad-a");
    ASSERT_NE(unit, nullptr);
    SetMission(*unit, "PATROL", "patrol", nlohmann::json{{"cycle_ticks", 3}});
    unit->mission_loops = true;
    for (std::uint64_t i = 0U; i < 3U; ++i) {
        step_missions(state);
    }
    EXPECT_TRUE(HasEvent(state.event_log, "MISSION_COMPLETED unit=squad-a command=cmd-test type=PATROL"));
    EXPECT_TRUE(HasEvent(state.event_log, "MISSION_LOOP_RESTARTED unit=squad-a command=cmd-test"));
    EXPECT_TRUE(FindUnit(state, "squad-a")->mission_active) << "持续任务循环期间任务必须保持激活";

    FindUnit(state, "squad-a")->mission_loops = false;
    for (std::uint64_t i = 0U; i < 3U; ++i) {
        step_missions(state);
    }
    EXPECT_FALSE(FindUnit(state, "squad-a")->mission_active) << "循环关闭后周期完成应终止任务";
}

TEST(WfsMissionExecTest, TimeoutResolutionFailAppliesFailureAction) {
    SimState state = MakeState();
    RuntimeUnitState* unit = FindUnit(state, "squad-a");
    ASSERT_NE(unit, nullptr);
    SetMission(*unit, "MOVE", "reach_point", nlohmann::json{{"point", {{"x", 0.32}, {"y", 0.32}}}});
    unit->mission_deadline_ticks = state.clock.tick();
    unit->failure_action = "withdraw_to";
    unit->failure_target = "2.0,2.0";

    handle_mission_timeout(state, "cmd-test");
    EXPECT_TRUE(HasEvent(state.event_log, "MISSION_TIMED_OUT unit=squad-a command=cmd-test type=MOVE"));
    EXPECT_FALSE(FindUnit(state, "squad-a")->mission_active);
    EXPECT_TRUE(FindUnit(state, "squad-a")->retreating) << "失败后处置 withdraw_to 必须启动撤退";
    EXPECT_DOUBLE_EQ(FindUnit(state, "squad-a")->target_x, 2.0);
    EXPECT_DOUBLE_EQ(FindUnit(state, "squad-a")->target_y, 2.0);
    EXPECT_TRUE(HasEvent(state.event_log, "MISSION_STATUS_REPORT interaction=EXECUTION node=platoon-alpha "
                                          "unit=squad-a command=cmd-test state=TIMED_OUT"));
    EXPECT_TRUE(HasEvent(state.event_log, "FAILURE_ACTION_WITHDRAW unit=squad-a"));
}

TEST(WfsMissionExecTest, TimeoutResolutionContinueExtendsDeadline) {
    SimState state = MakeState();
    state.mission_config.timeout_resolution = "continue";
    RuntimeUnitState* unit = FindUnit(state, "squad-a");
    ASSERT_NE(unit, nullptr);
    SetMission(*unit, "MOVE", "reach_point", nlohmann::json{{"point", {{"x", 0.32}, {"y", 0.32}}}});
    unit->mission_deadline_ticks = state.clock.tick();

    handle_mission_timeout(state, "cmd-test");
    EXPECT_TRUE(HasEvent(state.event_log, "MISSION_TIMEOUT_CONTINUED unit=squad-a command=cmd-test"));
    EXPECT_TRUE(FindUnit(state, "squad-a")->mission_active);
    EXPECT_GT(FindUnit(state, "squad-a")->mission_deadline_ticks, state.clock.tick());
}

