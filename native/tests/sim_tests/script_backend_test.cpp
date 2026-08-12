// tests/sim_tests/script_backend_test.cpp
//
// T021 单元测试：无 AI 脚本后端。
// 覆盖工厂与稳定标识、确定性（同一输入两次输出一致）、按节点选择本节点单位、
// 输出与 LLM 同构（同一命令 JSON 结构）且通过 T014 双重校验、无单位/无区域
// 时返回明确错误（非 AI 兜底可诊断，宪法 9/17）、脚本决策节点枚举（排除玩家
// 节点、按单位数组首次出现顺序去重），以及输出不依赖线程数（宪法 7）。

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "ai_inject.h"
#include "sim_state.h"
#include "wfs/sim/ai/ia_backend.h"
#include "wfs/sim/ai/script_backend.h"
#include "wfs/sim/command_validation.h"
#include "wfs/sim/loader.h"
#include "wfs/sim/rng.h"

namespace {

using wfs::sim::AiDecision;
using wfs::sim::AiDecisionInput;
using wfs::sim::build_ai_input_summary;
using wfs::sim::CommandValidationContext;
using wfs::sim::CommandValidationResult;
using wfs::sim::create_script_backend;
using wfs::sim::GameClock;
using wfs::sim::load_scenario;
using wfs::sim::Rng;
using wfs::sim::script_decision_nodes;
using wfs::sim::SimState;
using wfs::sim::UnitInfo;
using wfs::sim::validate_command;

std::filesystem::path RepoRoot() {
    return WFS_SOURCE_ROOT;
}

std::filesystem::path SampleScenario() {
    return RepoRoot() / "data" / "scenarios" / "scn-smoke-test.json";
}

std::filesystem::path CommandSchema() {
    return RepoRoot() / "contracts" / "schemas" / "command.schema.json";
}

SimState MakeState(std::uint64_t seed = 42U, int threads = 1) {
    const auto load = load_scenario(SampleScenario());
    EXPECT_TRUE(load.ok()) << (load.issues.empty() ? "" : load.issues.front().message);
    SimState state;
    state.scenario = load.scenario;
    state.clock = GameClock(load.scenario.tick_hz);
    state.rng = Rng(seed, 0U);
    state.seed = seed;
    state.threads = threads;
    state.scenario_path = SampleScenario();
    return state;
}

AiDecisionInput InputFor(SimState& state, const std::string& node_id) {
    return AiDecisionInput{node_id, "run_start", state.clock.tick(), build_ai_input_summary(state),
                           wfs::sim::build_ai_events_json(state)};
}

// 与注入路径同源的节点上下文：脚本输出必须能通过 T014 校验（宪法 9）。
CommandValidationContext NodeContext(const std::string& node_id) {
    CommandValidationContext context;
    context.commander_node_id = node_id;
    context.units = {
        UnitInfo{"squad-a", "platoon-alpha", {"5.56mm", "frag_grenade"}},
        UnitInfo{"squad-b", "platoon-alpha", {"5.56mm"}},
        UnitInfo{"squad-c", "enemy-command", {"7.62mm"}},
    };
    context.known_zones = {"zone-hill"};
    return context;
}

}  // namespace

TEST(WfsScriptBackendTest, FactoryReturnsBackendWithStableName) {
    auto backend = create_script_backend();
    ASSERT_NE(backend, nullptr);
    EXPECT_EQ(backend->name(), wfs::sim::kAiBackendScript);
}

TEST(WfsScriptBackendTest, DecisionIsDeterministicAndTargetsOwnNodeUnit) {
    SimState state = MakeState();
    auto backend = create_script_backend();
    ASSERT_NE(backend, nullptr);

    const AiDecision first = backend->decide(InputFor(state, "enemy-command"));
    const AiDecision second = backend->decide(InputFor(state, "enemy-command"));
    ASSERT_TRUE(first.ok()) << first.error;
    ASSERT_TRUE(second.ok()) << second.error;
    EXPECT_EQ(first.command_json, second.command_json);

    const nlohmann::json command = nlohmann::json::parse(first.command_json);
    EXPECT_EQ(command.at("schema_version"), 1);
    EXPECT_EQ(command.at("type"), "SECURE_ZONE");
    EXPECT_EQ(command.at("target").at("kind"), "unit");
    EXPECT_EQ(command.at("target").at("ref"), "squad-c");
    EXPECT_EQ(command.at("completion").at("condition"), "secure_zone");
    EXPECT_EQ(command.at("completion").at("params").at("zone"), "zone-hill");
    EXPECT_EQ(command.at("completion").at("params").at("duration_ticks"), 1200);
    EXPECT_EQ(command.at("behavior").at("engagement"), "balanced");
}

TEST(WfsScriptBackendTest, OutputPassesCommandValidation) {
    SimState state = MakeState();
    auto backend = create_script_backend();
    ASSERT_NE(backend, nullptr);
    const AiDecision decision = backend->decide(InputFor(state, "enemy-command"));
    ASSERT_TRUE(decision.ok()) << decision.error;

    const CommandValidationResult result =
        validate_command(decision.command_json, NodeContext("enemy-command"), CommandSchema());
    EXPECT_TRUE(result.ok()) << (result.errors.empty() ? "" : result.errors.front().message);
}

TEST(WfsScriptBackendTest, DifferentNodeSelectsDifferentUnit) {
    SimState state = MakeState();
    auto backend = create_script_backend();
    ASSERT_NE(backend, nullptr);

    const AiDecision enemy = backend->decide(InputFor(state, "enemy-command"));
    const AiDecision player = backend->decide(InputFor(state, "platoon-alpha"));
    ASSERT_TRUE(enemy.ok()) << enemy.error;
    ASSERT_TRUE(player.ok()) << player.error;
    EXPECT_EQ(nlohmann::json::parse(enemy.command_json).at("target").at("ref"), "squad-c");
    EXPECT_EQ(nlohmann::json::parse(player.command_json).at("target").at("ref"), "squad-a");
}

TEST(WfsScriptBackendTest, NodeWithoutUnitsReturnsExplicitError) {
    SimState state = MakeState();
    auto backend = create_script_backend();
    ASSERT_NE(backend, nullptr);

    const AiDecision decision = backend->decide(InputFor(state, "ghost-node"));
    ASSERT_FALSE(decision.ok());
    EXPECT_FALSE(decision.error.empty());
}

TEST(WfsScriptBackendTest, ScenarioWithoutZonesReturnsExplicitError) {
    SimState state = MakeState();
    nlohmann::json summary = nlohmann::json::parse(build_ai_input_summary(state));
    summary["zones"] = nlohmann::json::array();
    auto backend = create_script_backend();
    ASSERT_NE(backend, nullptr);

    const AiDecisionInput input{"enemy-command", "run_start", 0U, summary.dump(), "[]"};
    const AiDecision decision = backend->decide(input);
    ASSERT_FALSE(decision.ok());
    EXPECT_FALSE(decision.error.empty());
}

TEST(WfsScriptBackendTest, DecisionNodesExcludePlayerAndDeduplicate) {
    const auto load = load_scenario(SampleScenario());
    ASSERT_TRUE(load.ok());
    EXPECT_EQ(script_decision_nodes(load.scenario), (std::vector<std::string>{"enemy-command"}));
}

TEST(WfsScriptBackendTest, DecisionDoesNotDependOnThreadCount) {
    SimState single = MakeState(42U, 1);
    SimState quad = MakeState(42U, 4);
    auto backend = create_script_backend();
    ASSERT_NE(backend, nullptr);

    // 决策输入摘要显式排除线程数（宪法 7），因此 1 线程与 4 线程决策一致。
    EXPECT_EQ(build_ai_input_summary(single), build_ai_input_summary(quad));
    const AiDecision from_single = backend->decide(InputFor(single, "enemy-command"));
    const AiDecision from_quad = backend->decide(InputFor(quad, "enemy-command"));
    ASSERT_TRUE(from_single.ok()) << from_single.error;
    ASSERT_TRUE(from_quad.ok()) << from_quad.error;
    EXPECT_EQ(from_single.command_json, from_quad.command_json);
}
