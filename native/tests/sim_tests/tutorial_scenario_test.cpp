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

// 测试专用临时目录（与 loader_test 相同的安全清理约定）。
class TempDir {
   public:
    TempDir() {
        static int counter = 0;
        path_ = std::filesystem::temp_directory_path() / ("wfs-tutorial-test-" + std::to_string(counter++));
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
    }

    ~TempDir() {
        const std::filesystem::path temp_root = std::filesystem::temp_directory_path();
        const std::filesystem::path normalized = path_.lexically_normal();
        if (normalized.string().starts_with(temp_root.string())) {
            std::error_code ec;
            std::filesystem::remove_all(normalized, ec);
        }
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    std::filesystem::path Write(const std::string& filename, const nlohmann::json& content) const {
        const std::filesystem::path file = path_ / filename;
        std::ofstream out(file);
        out << content.dump();
        return file;
    }

   private:
    std::filesystem::path path_;
};

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
    const auto result = load_scenario(dir.Write("broken-tutorial.json", root), ScenarioSchema());
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
    const auto result = load_scenario(dir.Write("tutorial-no-save-slot.json", root), ScenarioSchema());
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
    const auto result = load_scenario(dir.Write("save-slot-on-non-tutorial.json", root), ScenarioSchema());
    ASSERT_FALSE(result.ok());
    const std::string code = result.issues.front().code;
    EXPECT_TRUE(code == "SAVE_SLOT_FORBIDDEN" || code == "SCHEMA_INVALID") << code;
}
