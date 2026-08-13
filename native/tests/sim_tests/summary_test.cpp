// tests/sim_tests/summary_test.cpp
//
// T059：摘要上报与信息权限裁剪单元测试（FR-044/050/051；CHK159/162/165；
// 宪法第 2/7/13 条）。
//
// 覆盖：摘要聚合字段（任务状态/完成度/损失/支援）、上级仅见摘要
// （不含下级内部细节）、下级完整状态、生成时刻快照语义（CHK165）、
// 统一裁剪规则与 JSON 往返序列化。

#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "sim_runtime.h"
#include "sim_state.h"
#include "wfs/sim/loader.h"
#include "wfs/sim/summary.h"

namespace {

using wfs::sim::build_authorized_view_json;
using wfs::sim::build_summary;
using wfs::sim::load_scenario;
using wfs::sim::PlayerCommandResult;
using wfs::sim::SimState;
using wfs::sim::step_sim_state;
using wfs::sim::SummaryReport;

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

void Step(SimState& state, std::uint64_t ticks) {
    for (std::uint64_t i = 0U; i < ticks; ++i) {
        step_sim_state(state);
    }
}

bool InjectMove(SimState& state, const std::string& unit_id, double x, double y) {
    const std::filesystem::path schema = wfs::sim::resolve_schema_path(ScenarioPath(), "command.schema.json");
    nlohmann::json command = {
        {"schema_version", 1},
        {"type", "MOVE"},
        {"target", nlohmann::json{{"kind", "unit"}, {"ref", unit_id}}},
        {"completion", nlohmann::json{{"condition", "reach_point"},
                                      {"params", nlohmann::json{{"point", nlohmann::json{{"x", x}, {"y", y}}}}}}},
        {"intent", "摘要测试机动"},
        {"behavior", nlohmann::json{{"engagement", "balanced"}}},
        {"priority", 1},
        {"deadline", nlohmann::json{{"game_time", 20000}}}};
    return wfs::sim::inject_player_command(state, command.dump(), schema).accepted;
}

bool HasEventPrefix(const SimState& state, const std::string& prefix) {
    for (const wfs::sim::SimEvent& event : state.event_log.events()) {
        if (event.message.starts_with(prefix)) {
            return true;
        }
    }
    return false;
}

TEST(WfsSummaryTest, ReportsAggregatesAndOmitsSubordinateInternals) {
    SimState state = MakeState();
    ASSERT_TRUE(InjectMove(state, "plt-1-sq-1", 0.44, 0.4));
    Step(state, 100U);
    EXPECT_TRUE(HasEventPrefix(state, "SUMMARY_REPORT interaction=SUMMARY_REPORT from=node-plt-1 to=node-co-1 "));

    const SummaryReport* report = state.summaries.LatestFor("node-plt-1", "node-co-1");
    ASSERT_NE(report, nullptr);
    EXPECT_EQ(report->generated_tick, 100U);
    EXPECT_GE(report->missions.active, 1U);
    EXPECT_EQ(report->missions.completed, 0U);
    EXPECT_GE(report->completion_pct, 0U);
    EXPECT_LE(report->completion_pct, 100U);
    EXPECT_EQ(report->losses.soldiers, 0U);
    EXPECT_FALSE(report->needs_support);

    // 统一裁剪：上级只看到下级摘要，不含单位/坐标/武器等内部细节。
    const nlohmann::json view = build_authorized_view_json(state, "node-co-1");
    EXPECT_EQ(view.at("node_id").get<std::string>(), "node-co-1");
    ASSERT_EQ(view.at("own_units").size(), 1U);
    EXPECT_EQ(view.at("own_units")[0].at("id").get<std::string>(), "co-1-cp");
    EXPECT_TRUE(view.at("own_units")[0].contains("x")) << "本节点直属单位必须保有完整状态";
    ASSERT_EQ(view.at("subordinates").size(), 1U);
    EXPECT_EQ(view.at("subordinates")[0].at("from_node").get<std::string>(), "node-plt-1");
    EXPECT_FALSE(view.at("subordinates")[0].contains("units"));
    EXPECT_FALSE(view.at("subordinates")[0].contains("x"));
    EXPECT_FALSE(view.at("subordinates")[0].contains("soldiers"));
}

TEST(WfsSummaryTest, GenerationTickSnapshotSemantics) {
    SimState state = MakeState();
    ASSERT_TRUE(InjectMove(state, "plt-1-sq-1", 0.44, 0.4));
    Step(state, 100U);
    const SummaryReport* first = state.summaries.LatestFor("node-plt-1", "node-co-1");
    ASSERT_NE(first, nullptr);
    const SummaryReport frozen = *first;  // 值快照。
    EXPECT_EQ(frozen.missions.completed, 0U);

    Step(state, 500U);  // tick=600：任务已完成，生成晚摘要。
    const SummaryReport* late = state.summaries.LatestFor("node-plt-1", "node-co-1");
    ASSERT_NE(late, nullptr);
    EXPECT_GE(late->missions.completed, 1U);
    EXPECT_GT(late->generated_tick, frozen.generated_tick);

    // CHK165：生成时刻后状态变化不改写已上报摘要。
    EXPECT_EQ(frozen.missions.completed, 0U);
    const SummaryReport* restored = state.summaries.Find(frozen.id);
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(restored->missions.completed, 0U);
    EXPECT_EQ(restored->missions.active, frozen.missions.active);
}

TEST(WfsSummaryTest, BuildSummaryComputesLossesAndOutcomes) {
    SimState state = MakeState();
    for (wfs::sim::RuntimeUnitState& unit : state.units) {
        if (unit.id == "plt-1-sq-1") {
            unit.destroyed = true;
            unit.soldiers[0].status = wfs::sim::model::SoldierStatus::kCasualty;
        }
    }
    state.mission_outcomes["plt-1-sq-1"] = wfs::sim::MissionOutcomeCounts{2U, 1U, 0U};
    const SummaryReport report = build_summary(state, "node-plt-1", "node-co-1", 42U);
    EXPECT_EQ(report.missions.completed, 2U);
    EXPECT_EQ(report.missions.failed, 1U);
    EXPECT_EQ(report.losses.soldiers, 1U);
    EXPECT_EQ(report.losses.squads, 1U);
    EXPECT_EQ(report.losses.vehicles, 0U);
}

TEST(WfsSummaryTest, RegistryAndOutcomeCountsRoundTrip) {
    SimState state = MakeState();
    ASSERT_TRUE(InjectMove(state, "plt-1-sq-1", 0.44, 0.4));
    Step(state, 100U);
    const nlohmann::json registry_json = state.summaries;
    const auto restored_registry = registry_json.get<wfs::sim::SummaryRegistry>();
    EXPECT_EQ(restored_registry.size(), state.summaries.size());
    const SummaryReport* restored_report = restored_registry.LatestFor("node-plt-1", "node-co-1");
    ASSERT_NE(restored_report, nullptr);
    EXPECT_EQ(restored_report->generated_tick, 100U);

    const nlohmann::json outcomes_json = state.mission_outcomes;
    EXPECT_TRUE(outcomes_json.is_object());
}

}  // namespace
