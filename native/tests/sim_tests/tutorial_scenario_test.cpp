// tests/sim_tests/tutorial_scenario_test.cpp
//
// T038 单元测试：连排级新手教程场景数据。
// 覆盖：场景可加载（Schema + 语义校验）、小规模地图与有限单位、关键目标/
// 失败条件/时间上限、教程独立存档标识（tutorial + save_slot，contracts/
// save-format.md：教程不写入主游戏存档），以及失败条件引用校验（非法引用
// 结构化报错，宪法第 12/17 条）。

#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "test_temp_dir.h"

#include "wfs/sim/loader.h"

namespace {

using wfs::sim::load_scenario;

std::filesystem::path RepoRoot() {
    return WFS_SOURCE_ROOT;
}

std::filesystem::path TutorialScenario() {
    return RepoRoot() / "data" / "scenarios" / "scn-tutorial-platoon.json";
}

std::filesystem::path ScenarioSchema() {
    return RepoRoot() / "contracts" / "schemas" / "scenario.schema.json";
}

// 测试临时目录统一使用共享唯一化设施（F1：pid + 进程内单调序号，防并行冲突）。
using TempDir = wfs::sim::test::TempDir;

nlohmann::json TutorialJson() {
    std::ifstream in(TutorialScenario());
    return nlohmann::json::parse(in);
}

}  // namespace

TEST(WfsTutorialScenarioTest, LoadsTutorialScenarioWithIndependentSave) {
    const auto result = load_scenario(TutorialScenario());
    ASSERT_TRUE(result.ok()) << (result.issues.empty() ? "" : result.issues.front().message);
    EXPECT_EQ(result.scenario.id, "scn-tutorial-platoon");
    EXPECT_TRUE(result.scenario.tutorial);
    EXPECT_EQ(result.scenario.save_slot, "tutorial-scn-tutorial-platoon");
    EXPECT_EQ(result.scenario.time_limit_ticks, 18000u);
    EXPECT_EQ(result.scenario.player_node_id, "node-tutorial-platoon");

    // 小规模战斗：1.5×1.5 km，低于营级 5×5 km 剧本规模。
    EXPECT_DOUBLE_EQ(result.scenario.map_width_km, 1.5);
    EXPECT_DOUBLE_EQ(result.scenario.map_height_km, 1.5);
    EXPECT_LE(result.scenario.units.size(), 10u);

    ASSERT_EQ(result.scenario.objectives.size(), 1u);
    EXPECT_EQ(result.scenario.objectives[0].kind, "zone");
    EXPECT_EQ(result.scenario.objectives[0].target_ref, "zone-objective-hill");
    EXPECT_EQ(result.scenario.objectives[0].duration_ticks, 2400u);

    ASSERT_EQ(result.scenario.failure_conditions.size(), 1u);
    EXPECT_EQ(result.scenario.failure_conditions[0].kind, "unit");
    EXPECT_EQ(result.scenario.failure_conditions[0].target_ref, "tutorial-platoon-cp");
}

TEST(WfsTutorialScenarioTest, InvalidFailureConditionReferenceRejected) {
    nlohmann::json root = TutorialJson();
    root["failure_conditions"] =
        nlohmann::json::array({nlohmann::json{{"id", "fail-ghost"}, {"kind", "unit"}, {"target_ref", "ghost-unit"}}});
    TempDir dir;
    const auto result = load_scenario(dir.Write("broken-tutorial.json", root.dump()), ScenarioSchema());
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.issues.front().code, "OBJECTIVE_REF_NOT_FOUND");
}

TEST(WfsTutorialScenarioTest, NonTutorialScenarioHasNoIndependentSaveSlot) {
    const auto result = load_scenario(RepoRoot() / "data" / "scenarios" / "scn-smoke-test.json");
    ASSERT_TRUE(result.ok());
    EXPECT_FALSE(result.scenario.tutorial);
    EXPECT_TRUE(result.scenario.save_slot.empty());
    EXPECT_EQ(result.scenario.time_limit_ticks, 0u);
}

TEST(WfsTutorialScenarioTest, TutorialRequiresSaveSlot) {
    nlohmann::json root = TutorialJson();
    root.erase("save_slot");
    TempDir dir;
    const auto result = load_scenario(dir.Write("tutorial-no-save-slot.json", root.dump()), ScenarioSchema());
    ASSERT_FALSE(result.ok());
    // Schema if-then 与语义校验双保险：任一层面拒绝均可（F3）。
    const std::string code = result.issues.front().code;
    EXPECT_TRUE(code == "TUTORIAL_SAVE_SLOT_REQUIRED" || code == "SCHEMA_INVALID") << code;
}

TEST(WfsTutorialScenarioTest, NonTutorialRejectsSaveSlot) {
    std::ifstream in(RepoRoot() / "data" / "scenarios" / "scn-smoke-test.json");
    nlohmann::json root = nlohmann::json::parse(in);
    root["save_slot"] = "main-custom-slot";
    TempDir dir;
    const auto result = load_scenario(dir.Write("save-slot-on-non-tutorial.json", root.dump()), ScenarioSchema());
    ASSERT_FALSE(result.ok());
    const std::string code = result.issues.front().code;
    EXPECT_TRUE(code == "SAVE_SLOT_FORBIDDEN" || code == "SCHEMA_INVALID") << code;
}
