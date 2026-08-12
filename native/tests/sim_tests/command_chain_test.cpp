// tests/sim_tests/command_chain_test.cpp
//
// F4/F11 命令链路修复测试（测试先行：RED → GREEN）。
// 覆盖：
// - F4 撤回/修改必须校验目标命令归属单位：withdraw_command_id 指向其他单位
//   的命令、MODIFY_COMMAND 的 replace_with 目标与自身目标不一致 → 下达阶段
//   拒绝并记录可见错误，队列不改变；
// - F11 时限判定边界：deadline 等于当前 tick 时任务超时（>= 语义）。

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
#include "wfs/sim/rng.h"

namespace {

using wfs::sim::GameClock;
using wfs::sim::inject_player_command;
using wfs::sim::load_scenario;
using wfs::sim::PlayerCommandResult;
using wfs::sim::Rng;
using wfs::sim::RuntimeUnitState;
using wfs::sim::SimState;

std::filesystem::path RepoRoot() {
    return WFS_SOURCE_ROOT;
}

std::filesystem::path TutorialScenario() {
    return RepoRoot() / "data" / "scenarios" / "scn-tutorial-platoon.json";
}

std::filesystem::path SmokeScenario() {
    return RepoRoot() / "data" / "scenarios" / "scn-smoke-test.json";
}

std::filesystem::path CommandSchema() {
    return RepoRoot() / "contracts" / "schemas" / "command.schema.json";
}

SimState MakeState(const std::filesystem::path& scenario_path) {
    const auto load = load_scenario(scenario_path);
    EXPECT_TRUE(load.ok()) << (load.issues.empty() ? "" : load.issues.front().message);
    SimState state;
    state.scenario = load.scenario;
    state.clock = GameClock(load.scenario.tick_hz);
    state.rng = Rng(42U, 0U);
    state.seed = 42U;
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

std::string MoveCommand(const std::string& unit_id) {
    return R"({"schema_version": 1, "type": "MOVE", "target": {"kind": "unit", "ref": ")" + unit_id +
           R"("}, "completion": {"condition": "reach_point", "params": {"point": {"x": 0.32, "y": 0.32}}}, "intent": "测试移动", "behavior": {"engagement": "balanced"}, "priority": 1, "deadline": {"game_time": 20000}})";
}

std::string WithdrawCommand(const std::string& unit_id, const std::string& target_command_id) {
    return R"({"schema_version": 1, "type": "WITHDRAW_COMMAND", "target": {"kind": "unit", "ref": ")" + unit_id +
           R"("}, "completion": {"condition": "withdraw"}, "intent": "测试撤回", "behavior": {"engagement": "balanced"}, "priority": 1, "deadline": {"game_time": 0}, "withdraw_command_id": ")" +
           target_command_id + R"("})";
}

std::string ModifyCommand(const std::string& unit_id, const std::string& replace_target) {
    return R"({"schema_version": 1, "type": "MODIFY_COMMAND", "target": {"kind": "unit", "ref": ")" + unit_id +
           R"("}, "completion": {"condition": "modify"}, "intent": "测试修改", "behavior": {"engagement": "balanced"}, "priority": 1, "deadline": {"game_time": 0}, "replace_with": )" +
           MoveCommand(replace_target) + R"(})";
}

}  // namespace

TEST(WfsCommandChainTest, CrossUnitWithdrawRejectedAtIssue) {
    SimState state = MakeState(TutorialScenario());
    const PlayerCommandResult move = inject_player_command(state, MoveCommand("tutorial-squad-1"), CommandSchema());
    ASSERT_TRUE(move.accepted) << (move.errors.empty() ? "" : move.errors.front().message);
    EXPECT_EQ(move.arrival_seq, 0U);  // cmd-0 属于 tutorial-squad-1。

    // 跨单位撤回：withdraw_command_id=cmd-0 属于 squad-1，命令却指向 squad-2。
    const PlayerCommandResult cross =
        inject_player_command(state, WithdrawCommand("tutorial-squad-2", "cmd-0"), CommandSchema());
    ASSERT_FALSE(cross.accepted) << "跨单位撤回必须在下达阶段拒绝";
    ASSERT_FALSE(cross.errors.empty());
    EXPECT_EQ(cross.errors.front().code, "CHAIN_REJECTED");
    EXPECT_TRUE(HasEvent(state.event_log, "COMMAND_REJECTED command=cmd-1 unit=tutorial-squad-2"))
        << "拒绝必须记录可见事件";
    EXPECT_EQ(state.queue.size(), 1U) << "拒绝不得改变队列";
    EXPECT_EQ(state.command_chain.size(), 1U) << "拒绝不得登记链路条目";

    // 正向控制：同一单位的撤回仍被接受。
    const PlayerCommandResult own =
        inject_player_command(state, WithdrawCommand("tutorial-squad-1", "cmd-0"), CommandSchema());
    ASSERT_TRUE(own.accepted) << (own.errors.empty() ? "" : own.errors.front().message);
}

TEST(WfsCommandChainTest, ModifyReplaceWithCrossUnitRejectedAtIssue) {
    SimState state = MakeState(TutorialScenario());
    const PlayerCommandResult move = inject_player_command(state, MoveCommand("tutorial-squad-1"), CommandSchema());
    ASSERT_TRUE(move.accepted);

    // 修改命令自身目标是 squad-1，替代命令却指向 squad-2 → 拒绝。
    const PlayerCommandResult mismatch =
        inject_player_command(state, ModifyCommand("tutorial-squad-1", "tutorial-squad-2"), CommandSchema());
    ASSERT_FALSE(mismatch.accepted) << "replace_with 目标与命令目标不一致必须在下达阶段拒绝";
    ASSERT_FALSE(mismatch.errors.empty());
    EXPECT_EQ(mismatch.errors.front().code, "CHAIN_REJECTED");
    EXPECT_EQ(state.queue.size(), 1U);
    EXPECT_EQ(state.command_chain.size(), 1U);

    // 正向控制：替代命令目标一致时被接受。
    const PlayerCommandResult consistent =
        inject_player_command(state, ModifyCommand("tutorial-squad-1", "tutorial-squad-1"), CommandSchema());
    ASSERT_TRUE(consistent.accepted) << (consistent.errors.empty() ? "" : consistent.errors.front().message);
}

TEST(WfsCommandChainTest, DeadlineBoundaryFiresAtExactTick) {
    SimState state = MakeState(SmokeScenario());
    RuntimeUnitState* unit = FindUnit(state, "squad-a");
    ASSERT_NE(unit, nullptr);
    unit->mission_active = true;
    unit->mission_command_id = "cmd-x";
    unit->mission_type = "MOVE";
    unit->mission_deadline_ticks = 50U;
    unit->mission_condition = "reach_point";
    unit->mission_params = nlohmann::json::object();

    const nlohmann::json command_json{{"command_id", "cmd-x"},   {"seq", 0U},
                                      {"type", "MOVE"},          {"unit_id", "squad-a"},
                                      {"priority", 0},           {"issue_tick", 0U},
                                      {"delay_ticks", 50U},      {"arrival_tick", 50U},
                                      {"state", "effective"},    {"payload", nlohmann::json::object()},
                                      {"parent_command_id", ""}, {"target_command_id", ""},
                                      {"batch", false}};
    state.command_chain =
        nlohmann::json{{"commands", nlohmann::json::array({command_json})}}.get<wfs::sim::CommandChain>();

    // 边界：tick == deadline 时必须超时（>= 语义，F11）。
    state.clock.reset(50U);
    state.command_chain.ProcessDue(state);
    EXPECT_TRUE(HasEvent(state.event_log, "MISSION_TIMED_OUT unit=squad-a command=cmd-x"))
        << "tick == deadline 边界应触发超时";
    EXPECT_FALSE(FindUnit(state, "squad-a")->mission_active);

    // 负向控制：tick == deadline - 1 不触发。
    SimState before = MakeState(SmokeScenario());
    RuntimeUnitState* before_unit = FindUnit(before, "squad-a");
    before_unit->mission_active = true;
    before_unit->mission_command_id = "cmd-y";
    before_unit->mission_type = "MOVE";
    before_unit->mission_deadline_ticks = 50U;
    before_unit->mission_condition = "reach_point";
    before_unit->mission_params = nlohmann::json::object();
    nlohmann::json before_command = command_json;
    before_command["command_id"] = "cmd-y";
    before_command["arrival_tick"] = 49U;
    before_command["delay_ticks"] = 49U;
    before.command_chain =
        nlohmann::json{{"commands", nlohmann::json::array({before_command})}}.get<wfs::sim::CommandChain>();
    before.clock.reset(49U);
    before.command_chain.ProcessDue(before);
    EXPECT_FALSE(HasEvent(before.event_log, "MISSION_TIMED_OUT"));
    EXPECT_TRUE(FindUnit(before, "squad-a")->mission_active);
}
