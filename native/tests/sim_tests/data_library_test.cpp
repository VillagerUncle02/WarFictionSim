// tests/sim_tests/data_library_test.cpp
//
// T037 单元测试：基础数据文件（data/units/、data/terrain/）加载与校验。
// 覆盖：每个目录文件按仓库约定解析 Schema、schema_version 一致、条目 id
// 唯一、跨文件引用完整性（班→武器/弹药、武器→弹药、工事→武器类别）由
// 加载器强制，非法数据结构化报错且错误序列确定（宪法第 7/12/17 条）。

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "wfs/sim/loader.h"

namespace {

using wfs::sim::DataIssue;
using wfs::sim::load_data_catalog;
using wfs::sim::load_data_library;
using wfs::sim::resolve_schema_path;

std::filesystem::path RepoRoot() {
    return WFS_SOURCE_ROOT;
}

std::filesystem::path DataRoot() {
    return RepoRoot() / "data";
}

std::filesystem::path SchemaDir() {
    return RepoRoot() / "contracts" / "schemas";
}

// 测试专用临时目录：仅创建于系统临时目录下带唯一前缀的路径，析构时递归清理
// （路径经校验位于 temp_directory_path 内，避免误删工作区文件）。
class TempDir {
   public:
    TempDir() {
        static int counter = 0;
        path_ = std::filesystem::temp_directory_path() / ("wfs-data-test-" + std::to_string(counter++));
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
        std::filesystem::create_directories(path_ / "units");
        std::filesystem::create_directories(path_ / "terrain");
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

    void Write(const std::string& relative, const nlohmann::json& content) const {
        const std::filesystem::path file = path_ / relative;
        std::filesystem::create_directories(file.parent_path());
        std::ofstream out(file);
        out << content.dump();
    }

    std::filesystem::path path() const { return path_; }

   private:
    std::filesystem::path path_;
};

nlohmann::json ReadFile(const std::filesystem::path& path) {
    std::ifstream in(path);
    return nlohmann::json::parse(in);
}

// 把仓库基线数据复制到临时目录，仅覆盖指定相对路径的文件内容。
void CopyBaselineTo(TempDir& dir, const std::string& override_relative, const nlohmann::json& override_content) {
    for (const std::string& relative :
         {"units/squads.json", "units/weapons.json", "units/ammo.json", "terrain/terrain.json",
          "terrain/fortifications.json", "terrain/facilities.json"}) {
        if (relative == override_relative) {
            dir.Write(relative, override_content);
        } else {
            dir.Write(relative, ReadFile(DataRoot() / relative));
        }
    }
}

std::vector<std::string> IssueCodes(const wfs::sim::DataLibraryLoadResult& result) {
    std::vector<std::string> codes;
    codes.reserve(result.issues.size());
    for (const DataIssue& issue : result.issues) {
        codes.push_back(issue.code);
    }
    return codes;
}

}  // namespace

TEST(WfsDataLibraryTest, ResolvesSchemasByRepoConvention) {
    EXPECT_EQ(resolve_schema_path(DataRoot() / "units" / "squads.json", "squads.schema.json"),
              SchemaDir() / "squads.schema.json");
    EXPECT_EQ(resolve_schema_path(DataRoot() / "units" / "weapons.json", "weapons.schema.json"),
              SchemaDir() / "weapons.schema.json");
    EXPECT_EQ(resolve_schema_path(DataRoot() / "units" / "ammo.json", "ammo.schema.json"),
              SchemaDir() / "ammo.schema.json");
    EXPECT_EQ(resolve_schema_path(DataRoot() / "terrain" / "terrain.json", "terrain.schema.json"),
              SchemaDir() / "terrain.schema.json");
    EXPECT_EQ(resolve_schema_path(DataRoot() / "terrain" / "fortifications.json", "fortifications.schema.json"),
              SchemaDir() / "fortifications.schema.json");
    EXPECT_EQ(resolve_schema_path(DataRoot() / "terrain" / "facilities.json", "facilities.schema.json"),
              SchemaDir() / "facilities.schema.json");
}

TEST(WfsDataLibraryTest, EachCatalogLoadsIndividually) {
    const auto squads = load_data_catalog(DataRoot() / "units" / "squads.json");
    ASSERT_TRUE(squads.ok()) << (squads.issues.empty() ? "" : squads.issues.front().message);
    EXPECT_EQ(squads.catalog.schema_version, 1);
    EXPECT_EQ(squads.catalog.kind, "squad");
    EXPECT_GT(squads.catalog.entries.size(), 0u);

    const auto weapons = load_data_catalog(DataRoot() / "units" / "weapons.json");
    ASSERT_TRUE(weapons.ok());
    EXPECT_GT(weapons.catalog.entries.size(), 0u);

    const auto ammo = load_data_catalog(DataRoot() / "units" / "ammo.json");
    ASSERT_TRUE(ammo.ok());
    EXPECT_GT(ammo.catalog.entries.size(), 0u);

    const auto terrain = load_data_catalog(DataRoot() / "terrain" / "terrain.json");
    ASSERT_TRUE(terrain.ok());
    EXPECT_GT(terrain.catalog.entries.size(), 0u);

    const auto fortifications = load_data_catalog(DataRoot() / "terrain" / "fortifications.json");
    ASSERT_TRUE(fortifications.ok());
    EXPECT_GT(fortifications.catalog.entries.size(), 0u);

    const auto facilities = load_data_catalog(DataRoot() / "terrain" / "facilities.json");
    ASSERT_TRUE(facilities.ok());
    EXPECT_GT(facilities.catalog.entries.size(), 0u);
}

TEST(WfsDataLibraryTest, BaselineLibraryLoadsWithCrossReferences) {
    const auto result = load_data_library(DataRoot());
    ASSERT_TRUE(result.ok()) << (result.issues.empty() ? "" : result.issues.front().message);
    EXPECT_EQ(result.library.squads.schema_version, 1);
    EXPECT_EQ(result.library.weapons.schema_version, 1);
    EXPECT_EQ(result.library.ammo.schema_version, 1);
    EXPECT_EQ(result.library.terrain.schema_version, 1);
    EXPECT_EQ(result.library.fortifications.schema_version, 1);
    EXPECT_EQ(result.library.facilities.schema_version, 1);
    EXPECT_GE(result.library.squads.entries.size(), 5u);
    EXPECT_GE(result.library.weapons.entries.size(), 7u);
    EXPECT_GE(result.library.ammo.entries.size(), 8u);
    EXPECT_GE(result.library.terrain.entries.size(), 9u);
    EXPECT_GE(result.library.fortifications.entries.size(), 6u);
    EXPECT_GE(result.library.facilities.entries.size(), 10u);
}

TEST(WfsDataLibraryTest, BaselineContainsKeyTerrainRules) {
    const auto result = load_data_library(DataRoot());
    ASSERT_TRUE(result.ok());
    const auto find = [&](const std::string& id) {
        const auto& entries = result.library.terrain.entries;
        const auto it = std::find_if(entries.begin(), entries.end(),
                                     [&](const wfs::sim::DataEntry& entry) { return entry.id == id; });
        return it != entries.end() ? it->raw : nlohmann::json::object();
    };
    const nlohmann::json river = find("terrain-river");
    EXPECT_TRUE(river.at("passability").at("is_water").get<bool>());
    EXPECT_TRUE(river.at("passability").at("requires_bridge").get<bool>());
    const nlohmann::json plain = find("terrain-plain");
    EXPECT_DOUBLE_EQ(plain.at("passability").at("speed_multiplier").get<double>(), 1.0);
}

TEST(WfsDataLibraryTest, DuplicateEntryIdRejected) {
    TempDir dir;
    nlohmann::json squads = ReadFile(DataRoot() / "units" / "squads.json");
    squads["entries"].push_back(squads["entries"][0]);
    CopyBaselineTo(dir, "units/squads.json", squads);

    const auto result = load_data_library(dir.path(), SchemaDir());
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.issues.front().code, "DUPLICATE_ENTRY_ID");
}

TEST(WfsDataLibraryTest, UnknownWeaponReferenceRejected) {
    TempDir dir;
    nlohmann::json squads = ReadFile(DataRoot() / "units" / "squads.json");
    squads["entries"][0]["weapons"] = nlohmann::json::array({"ghost-weapon"});
    squads["entries"][0]["ammo"] = nlohmann::json::array();  // 避免兼容性检查噪音。
    CopyBaselineTo(dir, "units/squads.json", squads);

    const auto result = load_data_library(dir.path(), SchemaDir());
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.issues.front().code, "DATA_REF_NOT_FOUND");
    EXPECT_NE(result.issues.front().message.find("ghost-weapon"), std::string::npos);
}

TEST(WfsDataLibraryTest, SquadAmmoMustBeCompatibleWithWeapons) {
    TempDir dir;
    nlohmann::json squads = ReadFile(DataRoot() / "units" / "squads.json");
    squads["entries"][0]["ammo"] = nlohmann::json::array({"ammo-atgm"});  // 与步枪/班机不兼容。
    CopyBaselineTo(dir, "units/squads.json", squads);

    const auto result = load_data_library(dir.path(), SchemaDir());
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.issues.front().code, "DATA_AMMO_INCOMPATIBLE");
}

TEST(WfsDataLibraryTest, SchemaVersionMismatchRejected) {
    TempDir dir;
    nlohmann::json squads = ReadFile(DataRoot() / "units" / "squads.json");
    squads["schema_version"] = 999;
    CopyBaselineTo(dir, "units/squads.json", squads);

    const auto result = load_data_library(dir.path(), SchemaDir());
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.issues.front().code, "SCHEMA_VERSION_MISMATCH");
}

TEST(WfsDataLibraryTest, ReferenceErrorsAreDeterministic) {
    TempDir dir;
    nlohmann::json squads = ReadFile(DataRoot() / "units" / "squads.json");
    squads["entries"][0]["weapons"] = nlohmann::json::array({"ghost-weapon"});
    squads["entries"][0]["ammo"] = nlohmann::json::array();  // 避免兼容性检查噪音。
    squads["entries"][1]["ammo"] = nlohmann::json::array({"ghost-ammo"});
    CopyBaselineTo(dir, "units/squads.json", squads);

    const auto first = load_data_library(dir.path(), SchemaDir());
    const auto second = load_data_library(dir.path(), SchemaDir());
    ASSERT_FALSE(first.ok());
    ASSERT_FALSE(second.ok());
    EXPECT_EQ(IssueCodes(first), IssueCodes(second));
    ASSERT_EQ(first.issues.size(), 2u);
    EXPECT_NE(first.issues[0].message.find("ghost-weapon"), std::string::npos);
    EXPECT_NE(first.issues[1].message.find("ghost-ammo"), std::string::npos);
}
