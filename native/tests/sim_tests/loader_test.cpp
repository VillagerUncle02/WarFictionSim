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

// 测试专用临时目录：仅创建于系统临时目录下带唯一前缀的路径，析构时递归清理
// （路径经校验位于 temp_directory_path 内，避免误删工作区文件）。
class TempDir {
   public:
    TempDir() {
        static int counter = 0;
        path_ = std::filesystem::temp_directory_path() / ("wfs-loader-test-" + std::to_string(counter++));
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

    std::filesystem::path Write(const std::string& filename, const std::string& content) const {
        const std::filesystem::path file = path_ / filename;
        std::ofstream out(file);
        out << content;
        return file;
    }

    std::filesystem::path path() const { return path_; }

   private:
    std::filesystem::path path_;
};

nlohmann::json ValidScenarioJson() {
    std::ifstream in(SampleScenario());
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

TEST(WfsLoaderTest, SchemaVersionMismatchRejected) {
    nlohmann::json root = ValidScenarioJson();
    root["schema_version"] = 999;
    TempDir dir;
    const std::filesystem::path file = dir.Write("version-mismatch.json", root.dump());
    const ScenarioLoadResult result = load_scenario(file, ScenarioSchema());
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.issues.front().code, "SCHEMA_VERSION_MISMATCH");
}

TEST(WfsLoaderTest, OutOfRangeScenarioSchemaVersionReturnsStructuredErrorWithoutThrow) {
    // schema_version 为超出 INT64_MAX 的无符号整数（JSON 解析为 number_unsigned）：
    // is_number_integer() 对其同样为 true，若无范围防护，get<int64_t>() 会抛
    // type_error 或环绕截断，把非法数据误报为内部故障/版本不一致；此处必须返回
    // 结构化 SCHEMA_INVALID、不抛异常（PR #107 review round 4）。
    nlohmann::json root = ValidScenarioJson();
    root["schema_version"] = 9223372036854775808ULL;  // INT64_MAX + 1
    TempDir dir;
    const std::filesystem::path file = dir.Write("out-of-range-version.json", root.dump());
    ScenarioLoadResult result;
    EXPECT_NO_THROW(result = load_scenario(file, ScenarioSchema()));
    ASSERT_FALSE(result.ok());
    ASSERT_FALSE(result.issues.empty());
    EXPECT_EQ(result.issues.front().code, "SCHEMA_INVALID");
}

TEST(WfsLoaderTest, OutOfRangeScenarioSchemaVersionWithLaxSchemaReturnsStructuredErrorWithoutThrow) {
    // 防御纵深：即使 Schema 未约束 schema_version 的 maximum（宽松/临时 Schema 或
    // 公开双路径重载），ExtractScenarioData 也必须在 get<int64_t>() 之前做范围
    // 守卫，返回结构化 SCHEMA_INVALID、不抛 type_error（PR #107 review round 4）。
    nlohmann::json root = ValidScenarioJson();
    root["schema_version"] = 9223372036854775808ULL;  // INT64_MAX + 1
    TempDir dir;
    const std::filesystem::path schema = dir.Write("lax-schema.json", R"({"schema_version":1,"type":"object"})");
    const std::filesystem::path file = dir.Write("out-of-range-version-lax.json", root.dump());
    ScenarioLoadResult result;
    EXPECT_NO_THROW(result = load_scenario(file, schema));
    ASSERT_FALSE(result.ok());
    ASSERT_FALSE(result.issues.empty());
    EXPECT_EQ(result.issues.front().code, "SCHEMA_INVALID");
    EXPECT_NE(result.issues.front().message.find("schema_version"), std::string::npos);
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
                                               {"type", "infantry_squad"},
                                               {"node_id", "platoon-alpha"},
                                               {"x", 1.0},
                                               {"y", 1.0},
                                               {"ammo", nlohmann::json::array({"5.56mm"})}});
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

TEST(WfsLoaderTest, MalformedSchemaReturnsStructuredError) {
    // 畸形 Schema（required 为字符串而非数组）必须返回结构化 SCHEMA_INVALID，
    // 不得触发 nlohmann JSON_ASSERT 中止或让异常逃逸（PR #107 review F1；
    // 宪法第 12/17 条：非法数据报错而非崩溃）。
    TempDir dir;
    const std::filesystem::path schema = dir.Write("bad-schema.json", R"({"schema_version":1,"required":"id"})");
    const std::filesystem::path scenario = dir.Write("bad-scenario.json", ValidScenarioJson().dump());

    const ScenarioLoadResult result = load_scenario(scenario, schema);
    ASSERT_FALSE(result.ok());
    ASSERT_FALSE(result.issues.empty());
    EXPECT_EQ(result.issues.front().code, "SCHEMA_INVALID");
}
