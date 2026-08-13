// tests/sim_tests/loader_test.cpp
//
// T013 单元测试：场景/数据加载与校验框架。
// 覆盖合法场景加载、非法 JSON/缺失字段报错（不崩溃）、Schema 版本不匹配、
// 语义引用校验（重复 id/未知引用/未知指挥节点）、错误确定性，
// 以及启动校验预算 ≤5s（plan.md Performance Goals）。

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "test_temp_dir.h"

#include "wfs/sim/loader.h"

namespace {

using wfs::sim::DataIssue;
using wfs::sim::load_scenario;
using wfs::sim::resolve_schema_path;
using wfs::sim::ScenarioLoadResult;

std::filesystem::path RepoRoot() {
    return WFS_SOURCE_ROOT;
}

std::filesystem::path SampleScenario() {
    return RepoRoot() / "data" / "scenarios" / "scn-smoke-test.json";
}

std::filesystem::path ScenarioSchema() {
    return RepoRoot() / "contracts" / "schemas" / "scenario.schema.json";
}

// 测试临时目录统一使用共享唯一化设施（F1：pid + 进程内单调序号，防并行冲突）。
using TempDir = wfs::sim::test::TempDir;

nlohmann::json ValidScenarioJson() {
    std::ifstream in(SampleScenario());
    return nlohmann::json::parse(in);
}

// T054 派系支援场景（scn-support-faction-china.json）：support 契约负例基座。
nlohmann::json SupportScenarioJson() {
    std::ifstream in(RepoRoot() / "data" / "scenarios" / "scn-support-faction-china.json");
    return nlohmann::json::parse(in);
}

std::vector<std::string> IssueCodes(const ScenarioLoadResult& result) {
    std::vector<std::string> codes;
    codes.reserve(result.issues.size());
    for (const DataIssue& issue : result.issues) {
        codes.push_back(issue.code);
    }
    return codes;
}

}  // namespace

TEST(WfsLoaderTest, ResolvesSchemaByRepoConvention) {
    EXPECT_EQ(resolve_schema_path(SampleScenario(), "scenario.schema.json"), ScenarioSchema());
}

TEST(WfsLoaderTest, LoadsValidSampleScenario) {
    const ScenarioLoadResult result = load_scenario(SampleScenario());
    ASSERT_TRUE(result.ok()) << (result.issues.empty() ? "" : result.issues.front().message);
    EXPECT_EQ(result.scenario.schema_version, 1);
    EXPECT_EQ(result.scenario.id, "scn-smoke-test");
    EXPECT_EQ(result.scenario.name, "Smoke Test Scenario (Foundational)");
    EXPECT_EQ(result.scenario.player_node_id, "platoon-alpha");
    EXPECT_DOUBLE_EQ(result.scenario.map_width_km, 5.0);
    EXPECT_DOUBLE_EQ(result.scenario.map_height_km, 5.0);
    EXPECT_EQ(result.scenario.tick_hz, 20u);
    EXPECT_EQ(result.scenario.seed, 42u);
    ASSERT_EQ(result.scenario.zones.size(), 1u);
    EXPECT_EQ(result.scenario.zones.front(), "zone-hill");
    ASSERT_EQ(result.scenario.units.size(), 3u);
    EXPECT_EQ(result.scenario.units[0].id, "squad-a");
    EXPECT_EQ(result.scenario.units[0].node_id, "platoon-alpha");
    EXPECT_EQ(result.scenario.units[0].ammo.size(), 2u);
    ASSERT_EQ(result.scenario.objectives.size(), 1u);
    EXPECT_EQ(result.scenario.objectives.front().target_ref, "zone-hill");
    EXPECT_EQ(result.scenario.objectives.front().duration_ticks, 1200u);
}

TEST(WfsLoaderTest, InvalidJsonReportsStructuredError) {
    TempDir dir;
    const std::filesystem::path file = dir.Write("invalid.json", "{ this is not json");
    const ScenarioLoadResult result = load_scenario(file, ScenarioSchema());
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.issues.front().code, "INVALID_JSON");
    EXPECT_FALSE(result.issues.front().message.empty());
}

TEST(WfsLoaderTest, MissingRequiredFieldReportsSchemaError) {
    nlohmann::json root = ValidScenarioJson();
    root.erase("units");
    TempDir dir;
    const std::filesystem::path file = dir.Write("missing-units.json", root.dump());
    const ScenarioLoadResult result = load_scenario(file, ScenarioSchema());
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.issues.front().code, "SCHEMA_INVALID");
}

// T054 复审 F2：support.faction_id 非法值（未知 id/错误类型）在 Schema 层
// 结构化拒绝（SCHEMA_INVALID），不再落入运行期 SUPPORT_CONFIG_INVALID 事件
// 路径（宪法 12：数据契约先于运行期兜底）。
TEST(WfsLoaderTest, InvalidSupportFactionIdRejectedBySchema) {
    TempDir dir;
    for (const nlohmann::json& bad_faction : {nlohmann::json("faction-mars"), nlohmann::json(42)}) {
        nlohmann::json root = SupportScenarioJson();
        root["support"]["faction_id"] = bad_faction;
        const std::filesystem::path file = dir.Write("invalid-support-faction.json", root.dump());
        const ScenarioLoadResult result = load_scenario(file, ScenarioSchema());
        ASSERT_FALSE(result.ok());
        EXPECT_EQ(result.issues.front().code, "SCHEMA_INVALID");
        EXPECT_FALSE(result.issues.front().message.empty());
    }
}

TEST(WfsLoaderTest, SchemaVersionMismatchRejected) {
    nlohmann::json root = ValidScenarioJson();
    root["schema_version"] = 999;
    TempDir dir;
    const std::filesystem::path file = dir.Write("version-mismatch.json", root.dump());
    const ScenarioLoadResult result = load_scenario(file, ScenarioSchema());
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.issues.front().code, "SCHEMA_VERSION_MISMATCH");
}

TEST(WfsLoaderTest, DuplicateUnitIdRejected) {
    nlohmann::json root = ValidScenarioJson();
    root["units"].push_back(root["units"][0]);
    TempDir dir;
    const std::filesystem::path file = dir.Write("duplicate-unit.json", root.dump());
    const ScenarioLoadResult result = load_scenario(file, ScenarioSchema());
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.issues.front().code, "DUPLICATE_UNIT_ID");
}

TEST(WfsLoaderTest, DuplicateZoneIdRejected) {
    nlohmann::json root = ValidScenarioJson();
    root["zones"].push_back(root["zones"][0]);
    TempDir dir;
    const std::filesystem::path file = dir.Write("duplicate-zone.json", root.dump());
    const ScenarioLoadResult result = load_scenario(file, ScenarioSchema());
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.issues.front().code, "DUPLICATE_ZONE_ID");
}

TEST(WfsLoaderTest, ObjectiveReferencingUnknownUnitRejected) {
    nlohmann::json root = ValidScenarioJson();
    root["objectives"] =
        nlohmann::json::array({nlohmann::json{{"id", "obj-ghost"}, {"kind", "unit"}, {"target_ref", "ghost-unit"}}});
    TempDir dir;
    const std::filesystem::path file = dir.Write("unknown-objective-ref.json", root.dump());
    const ScenarioLoadResult result = load_scenario(file, ScenarioSchema());
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.issues.front().code, "OBJECTIVE_REF_NOT_FOUND");
}

TEST(WfsLoaderTest, PlayerNodeMustExistInScenario) {
    nlohmann::json root = ValidScenarioJson();
    root["player_node_id"] = "ghost-node";
    TempDir dir;
    const std::filesystem::path file = dir.Write("ghost-player-node.json", root.dump());
    const ScenarioLoadResult result = load_scenario(file, ScenarioSchema());
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.issues.front().code, "PLAYER_NODE_NOT_FOUND");
}

TEST(WfsLoaderTest, NonexistentFileReportsIoError) {
    const std::filesystem::path missing = TempDir{}.path() / "does-not-exist.json";
    const ScenarioLoadResult result = load_scenario(missing, ScenarioSchema());
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.issues.front().code, "IO_ERROR");
}

TEST(WfsLoaderTest, ErrorsAreDeterministic) {
    nlohmann::json root = ValidScenarioJson();
    root["units"].push_back(root["units"][0]);
    root["player_node_id"] = "ghost-node";
    TempDir dir;
    const std::filesystem::path file = dir.Write("deterministic-errors.json", root.dump());

    const ScenarioLoadResult first = load_scenario(file, ScenarioSchema());
    const ScenarioLoadResult second = load_scenario(file, ScenarioSchema());
    ASSERT_FALSE(first.ok());
    ASSERT_FALSE(second.ok());
    ASSERT_EQ(first.issues.size(), second.issues.size());
    for (std::size_t i = 0; i < first.issues.size(); ++i) {
        EXPECT_EQ(first.issues[i].code, second.issues[i].code);
        EXPECT_EQ(first.issues[i].message, second.issues[i].message);
    }
    // 固定顺序：重复单位 → 未知指挥节点（版本不匹配属硬性契约失败，提前返回，
    // 见 SchemaVersionMismatchRejected 单测）。
    EXPECT_EQ(IssueCodes(first), (std::vector<std::string>{"DUPLICATE_UNIT_ID", "PLAYER_NODE_NOT_FOUND"}));
}

TEST(WfsLoaderTest, ValidationCompletesWithinBudget) {
    // 启动校验预算 ≤5s：用约 2000 个单位的合成场景做压力回归，远高于 v1 营级
    // 规模（300–800 实体），若校验路径出现复杂度退化会在此暴露。
    nlohmann::json root = ValidScenarioJson();
    root["units"].clear();
    for (int i = 0; i < 2000; ++i) {
        root["units"].push_back(nlohmann::json{{"id", "unit-" + std::to_string(i)},
                                               {"type", "squad-rifle-us"},
                                               {"node_id", "platoon-alpha"},
                                               {"x", 1.0},
                                               {"y", 1.0},
                                               {"ammo", nlohmann::json::array({"ammo-556"})}});
    }
    TempDir dir;
    const std::filesystem::path file = dir.Write("budget.json", root.dump());

    const auto start = std::chrono::steady_clock::now();
    const ScenarioLoadResult result = load_scenario(file, ScenarioSchema());
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);

    ASSERT_TRUE(result.ok()) << (result.issues.empty() ? "" : result.issues.front().message);
    EXPECT_LT(elapsed.count(), 5000);
    EXPECT_EQ(result.scenario.units.size(), 2000u);
}

TEST(WfsLoaderTest, ScenarioUnitTypeMustExistInDataCatalog) {
    nlohmann::json root = ValidScenarioJson();
    root["units"][0]["type"] = "ghost-squad";
    TempDir dir;
    const std::filesystem::path file = dir.Write("unknown-unit-type.json", root.dump());
    const ScenarioLoadResult result = load_scenario(file, ScenarioSchema());
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.issues.front().code, "UNIT_TYPE_NOT_FOUND");
}

TEST(WfsLoaderTest, ScenarioUnitAmmoMustExistInDataCatalog) {
    nlohmann::json root = ValidScenarioJson();
    root["units"][0]["ammo"] = nlohmann::json::array({"ghost-ammo"});
    TempDir dir;
    const std::filesystem::path file = dir.Write("unknown-unit-ammo.json", root.dump());
    const ScenarioLoadResult result = load_scenario(file, ScenarioSchema());
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.issues.front().code, "UNIT_AMMO_NOT_FOUND");
}

TEST(WfsLoaderTest, ScenarioUnitAmmoMustMatchSquadTypeWeapons) {
    nlohmann::json root = ValidScenarioJson();
    root["units"][0]["ammo"] = nlohmann::json::array({"ammo-127"});  // 与班类型武器兼容集不匹配。
    TempDir dir;
    const std::filesystem::path file = dir.Write("incompatible-unit-ammo.json", root.dump());
    const ScenarioLoadResult result = load_scenario(file, ScenarioSchema());
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.issues.front().code, "UNIT_AMMO_INCOMPATIBLE");
}

TEST(WfsLoaderTest, CustomSchemaPathSkipsLibraryValidationForBackCompat) {
    // F6：Schema 不在仓库 contracts/schemas 下时，双路径重载不推导数据根
    // （旧 API 兼容），未知弹药不会被拒绝。
    nlohmann::json root = ValidScenarioJson();
    root["units"][0]["ammo"] = nlohmann::json::array({"ghost-ammo"});
    TempDir dir;
    std::ifstream schema_in(ScenarioSchema());
    const std::filesystem::path schema_path =
        dir.Write("custom-scenario.schema.json", nlohmann::json::parse(schema_in).dump());
    const std::filesystem::path file = dir.Write("custom-schema-scenario.json", root.dump());
    const ScenarioLoadResult result = load_scenario(file, schema_path);
    ASSERT_TRUE(result.ok()) << (result.issues.empty() ? "" : result.issues.front().message);
}

TEST(WfsLoaderTest, ExplicitDataRootEnablesLibraryValidation) {
    nlohmann::json root = ValidScenarioJson();
    root["units"][0]["ammo"] = nlohmann::json::array({"ghost-ammo"});
    TempDir dir;
    std::ifstream schema_in(ScenarioSchema());
    const std::filesystem::path schema_path =
        dir.Write("custom-scenario.schema.json", nlohmann::json::parse(schema_in).dump());
    const std::filesystem::path file = dir.Write("custom-schema-scenario.json", root.dump());
    const ScenarioLoadResult result = load_scenario(file, schema_path, RepoRoot() / "data");
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.issues.front().code, "UNIT_AMMO_NOT_FOUND");
}
