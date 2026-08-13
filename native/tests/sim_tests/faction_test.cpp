// tests/sim_tests/faction_test.cpp
//
// T052 单元测试：派系模板加载与 Schema 校验。
// 覆盖（FR-006/007；data-model §13；宪法第 12 条）：
// - 三大派系模板加载通过且审批层级基线 0/1/2、资源池结构正确；
// - 非法数据（审批层级越界/条目 id 重复/引用不存在的单位）结构化报错；
// - FactionTemplate 序列化往返（键序确定，宪法第 7 条）。

#include <cstdint>
#include <filesystem>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "test_temp_dir.h"
#include "wfs/sim/faction.h"

namespace {

using wfs::sim::FactionTemplate;
using wfs::sim::load_faction;
using wfs::sim::PoolEntryKind;
using wfs::sim::ResourcePoolEntry;
using wfs::sim::test::TempDir;

std::filesystem::path RepoRoot() {
    return WFS_SOURCE_ROOT;
}

std::filesystem::path FactionFile(const std::string& name) {
    return RepoRoot() / "data" / "factions" / (name + ".json");
}

std::filesystem::path FactionSchema() {
    return RepoRoot() / "contracts" / "schemas" / "faction.schema.json";
}

nlohmann::json MinimalFaction(std::uint32_t approval_level = 0U) {
    return nlohmann::json{
        {"schema_version", 1},
        {"id", "faction-test"},
        {"name", "测试派系"},
        {"approval_level", approval_level},
        {"resource_pools",
         {{"battalion",
           {{"support_score", 10},
            {"support_kinds", nlohmann::json::array()},
            {"entries",
             nlohmann::json::array(
                 {{{"id", "squad-rifle-us"}, {"kind", "unit"}, {"quantity", 1}, {"cost", 5}}})}}}}},
    };
}

bool HasIssue(const wfs::sim::FactionLoadResult& result, const std::string& code) {
    for (const wfs::sim::DataIssue& issue : result.issues) {
        if (issue.code == code) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST(WfsFactionTest, ThreeBuiltInFactionsLoadWithApprovalBaselines) {
    const auto china = load_faction(FactionFile("faction-china"));
    ASSERT_TRUE(china.ok()) << (china.issues.empty() ? "" : china.issues.front().message);
    EXPECT_EQ(china.faction.id, "faction-china");
    EXPECT_EQ(china.faction.name, "中国");
    EXPECT_EQ(china.faction.approval_level, 0U);  // 内置合成化：上级直接裁决。
    const auto china_battalion = china.faction.pools.at("battalion");
    EXPECT_EQ(china_battalion.support_score, 60U);
    EXPECT_EQ(china_battalion.entries.size(), 5U);

    const auto nato = load_faction(FactionFile("faction-nato"));
    ASSERT_TRUE(nato.ok()) << (nato.issues.empty() ? "" : nato.issues.front().message);
    EXPECT_EQ(nato.faction.name, "北约");
    EXPECT_EQ(nato.faction.approval_level, 1U);  // 任务链：经一级转发。
    // 北约营级资源池不含大口径火炮（旅级后置）——范围外请求据此拒绝。
    for (const ResourcePoolEntry& entry : nato.faction.pools.at("battalion").entries) {
        EXPECT_NE(entry.id, "artillery-152");
    }
    EXPECT_EQ(nato.faction.pools.at("battalion").entries.front().id, "squad-mortar-team");

    const auto russia = load_faction(FactionFile("faction-russia"));
    ASSERT_TRUE(russia.ok()) << (russia.issues.empty() ? "" : russia.issues.front().message);
    EXPECT_EQ(russia.faction.name, "苏俄");
    EXPECT_EQ(russia.faction.approval_level, 2U);  // 整建制申请：经两级转发。
    EXPECT_EQ(russia.faction.pools.at("battalion").entries.size(), 2U);  // 默认资源池最小。
}

TEST(WfsFactionTest, InvalidApprovalLevelIsRejectedBySchema) {
    TempDir temp_dir("wfs-faction");
    const std::filesystem::path path = temp_dir.Write("bad-approval.json", MinimalFaction(3U).dump());
    const auto result = load_faction(path, FactionSchema());
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(HasIssue(result, "SCHEMA_INVALID"));
}

TEST(WfsFactionTest, DuplicateEntryIdsAreRejectedSemantically) {
    TempDir temp_dir("wfs-faction");
    nlohmann::json faction = MinimalFaction();
    faction["resource_pools"]["battalion"]["entries"].push_back(
        nlohmann::json{{"id", "squad-rifle-us"}, {"kind", "unit"}, {"quantity", 1}, {"cost", 5}});
    const std::filesystem::path path = temp_dir.Write("duplicate-entry.json", faction.dump());
    const auto result = load_faction(path, FactionSchema());
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(HasIssue(result, "DUPLICATE_ENTRY_ID"));
}

TEST(WfsFactionTest, UnitEntryReferencingMissingUnitIsRejected) {
    TempDir temp_dir("wfs-faction");
    nlohmann::json faction = MinimalFaction();
    faction["resource_pools"]["battalion"]["entries"][0]["id"] = "unit-does-not-exist";
    const std::filesystem::path path = temp_dir.Write("missing-unit.json", faction.dump());
    // 显式数据根目录 → 执行 data/units 交叉校验（宪法 12：引用必须存在）。
    const auto result = load_faction(path, FactionSchema(), RepoRoot() / "data");
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(HasIssue(result, "DATA_REF_NOT_FOUND"));
}

TEST(WfsFactionTest, FactionTemplateSerializeRoundTrip) {
    const auto china = load_faction(FactionFile("faction-china"));
    ASSERT_TRUE(china.ok());
    const nlohmann::json json = china.faction;
    const FactionTemplate restored = json.get<FactionTemplate>();
    EXPECT_EQ(restored, china.faction);
    // 键序确定：std::map 序列化后键按字典序（宪法第 7 条）。
    const nlohmann::json pools = json.at("resource_pools");
    std::vector<std::string> keys;
    for (auto it = pools.begin(); it != pools.end(); ++it) {
        keys.push_back(it.key());
    }
    ASSERT_EQ(keys.size(), 4U);
    EXPECT_EQ(keys[0], "battalion");
    EXPECT_EQ(keys[1], "company");
    EXPECT_EQ(keys[2], "platoon");
    EXPECT_EQ(keys[3], "squad");
}
