// tests/sim_tests/mission_command_test.cpp
//
// Code Reviewer M1（🔴）：PATROL/FORTIFY/HIDDEN_RECON 等任务在命令校验层
// 被 CONDITION_NOT_EVALUABLE 拒绝，端到端不可用（FR-042 全部 13 种任务类型
// 应可用）。测试先行：先断言注入被接受并推进判定，再实现注册。

#include <cstdint>
#include <filesystem>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "sim_runtime.h"
#include "sim_state.h"
#include "wfs/sim/event_log.h"
#include "wfs/sim/loader.h"

namespace {

using wfs::sim::GameClock;
using wfs::sim::inject_player_command;
using wfs::sim::load_scenario;
using wfs::sim::RuntimeUnitState;
using wfs::sim::SimState;
using wfs::sim::step_sim_state;

std::filesystem::path RepoRoot() {
    return WFS_SOURCE_ROOT;
}

std::filesystem::path SampleScenario() {
    return RepoRoot() / "data" / "scenarios" / "scn-smoke-test.json";
}

std::filesystem::path CommandSchema() {
    return RepoRoot() / "contracts" / "schemas" / "command.schema.json";
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

// 步进直到任务生效（通讯延迟 60–200 tick 内），返回是否已生效。
bool WaitMissionActive(SimState& state, const std::string& unit_id) {
    for (std::uint64_t i = 0U; i < 300U; ++i) {
        step_sim_state(state);
        RuntimeUnitState* unit = FindUnit(state, unit_id);
        if (unit != nullptr && unit->mission_active) {
            return true;
        }
    }
    return false;
}

std::string PatrolCommand() {
    return R"({"schema_version": 1, "type": "PATROL", "target": {"kind": "unit", "ref": "squad-a"}, )"
           R"("completion": {"condition": "patrol", "params": {"cycle_ticks": 3}}, "intent": "巡逻", )"
           R"("behavior": {"engagement": "balanced"}, "priority": 1, "deadline": {"game_time": 20000}})";
}

std::string FortifyCommand() {
    return R"({"schema_version": 1, "type": "FORTIFY", "target": {"kind": "unit", "ref": "squad-a"}, )"
           R"("completion": {"condition": "fortify", "params": {"construction_ticks": 3}}, "intent": "构筑工事", )"
           R"("behavior": {"engagement": "balanced"}, "priority": 1, "deadline": {"game_time": 20000}})";
}

std::string HiddenReconCommand() {
    return R"({"schema_version": 1, "type": "HIDDEN_RECON", "target": {"kind": "unit", "ref": "squad-a"}, )"
           R"("completion": {"condition": "recon", "params": {"point": {"x": 1.0, "y": 1.0}}}, )"
           R"("intent": "隐蔽侦察", "behavior": {"engagement": "balanced"}, "priority": 1, )"
           R"("deadline": {"game_time": 20000}})";
}

}  // namespace

TEST(WfsMissionCommandTest, PatrolCommandAcceptedAndJudged) {
    SimState state = MakeState();
    const auto result = inject_player_command(state, PatrolCommand(), CommandSchema());
    ASSERT_TRUE(result.accepted) << "PATROL 必须通过命令校验（当前被 CONDITION_NOT_EVALUABLE 拒绝）";
    ASSERT_TRUE(WaitMissionActive(state, "squad-a"));
    for (std::uint64_t i = 0U; i < 3U; ++i) {
        step_sim_state(state);
    }
    EXPECT_TRUE(HasEvent(state.event_log, "MISSION_COMPLETED unit=squad-a type=PATROL"))
        << "巡逻周期必须由任务判定系统推进完成";
}

TEST(WfsMissionCommandTest, FortifyCommandAcceptedAndJudged) {
    SimState state = MakeState();
    const auto result = inject_player_command(state, FortifyCommand(), CommandSchema());
    ASSERT_TRUE(result.accepted) << "FORTIFY 必须通过命令校验";
    ASSERT_TRUE(WaitMissionActive(state, "squad-a"));
    for (std::uint64_t i = 0U; i < 3U; ++i) {
        step_sim_state(state);
    }
    EXPECT_TRUE(HasEvent(state.event_log, "MISSION_COMPLETED unit=squad-a type=FORTIFY"))
        << "构筑工事必须按 construction_ticks 确定性完成";
}

TEST(WfsMissionCommandTest, HiddenReconCommandAcceptedAndJudged) {
    SimState state = MakeState();
    const auto result = inject_player_command(state, HiddenReconCommand(), CommandSchema());
    ASSERT_TRUE(result.accepted) << "HIDDEN_RECON 必须通过命令校验";
    ASSERT_TRUE(WaitMissionActive(state, "squad-a"));
    state.recon_config.hidden_hold_ticks = 3U;
    for (std::uint64_t i = 0U; i < 3U; ++i) {
        step_sim_state(state);
    }
    EXPECT_TRUE(HasEvent(state.event_log, "MISSION_COMPLETED unit=squad-a type=HIDDEN_RECON"))
        << "隐蔽侦察必须按潜伏时长判定完成";
}

