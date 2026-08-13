// sim/src/faction.cpp
//
// T052：派系模板加载与校验实现。
//
// 实现策略（宪法第 12/17 条；data-model §13）：分三层校验——
//   1) JSON Schema 结构校验（faction.schema.json：审批层级 0/1/2、
//      资源池 echelon 键枚举、条目结构）；
//   2) schema_version 一致性 + 语义校验（条目 id 在池内唯一）；
//   3) data/units 交叉校验：kind == unit 的条目 id 必须存在于 squads 或
//      vehicles 目录（数据引用必须存在，data/README 约定）。
// 非法数据以结构化 issue 报错而非崩溃；pool 用 std::map 保证序列化键序
// 确定（宪法第 7 条）。Schema 路径默认按仓库约定从数据文件向上解析。

#include "wfs/sim/faction.h"

#include <cstdint>
#include <fstream>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "schema_validator.h"

namespace wfs::sim {

namespace {

constexpr const char* kFactionSchemaFile = "faction.schema.json";

DataIssue Issue(std::string code, std::string message) {
    return DataIssue{std::move(code), std::move(message)};
}

FactionLoadResult Failure(std::vector<DataIssue> issues) {
    return FactionLoadResult{std::move(issues), FactionTemplate{}};
}

bool ReadJsonDocument(const std::filesystem::path& path, nlohmann::json& document, std::vector<DataIssue>& issues) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        issues.push_back(Issue("IO_ERROR", "无法打开派系数据文件: " + path.string()));
        return false;
    }
    try {
        document = nlohmann::json::parse(input);
        return true;
    } catch (const nlohmann::json::parse_error& error) {
        issues.push_back(Issue("INVALID_JSON", std::string("派系数据不是合法 JSON: ") + error.what()));
        return false;
    }
}

bool HasIntegerSchemaVersion(const nlohmann::json& document) {
    return document.contains("schema_version") && document["schema_version"].is_number_integer();
}

// 语义校验：同一资源池内条目 id 唯一。
void CheckPoolEntryIds(const FactionTemplate& faction, std::vector<DataIssue>& issues) {
    for (const auto& [echelon, pool] : faction.pools) {
        std::set<std::string> ids;
        for (const ResourcePoolEntry& entry : pool.entries) {
            if (!ids.insert(entry.id).second) {
                issues.push_back(
                    Issue("DUPLICATE_ENTRY_ID", "派系 " + faction.id + " " + echelon + " 池条目 id 重复: " + entry.id));
            }
        }
    }
}

// data/units 交叉校验：单位类条目必须引用存在的单位类型（宪法 12）。
void CheckUnitReferences(const FactionTemplate& faction, const DataLibrary& library, std::vector<DataIssue>& issues) {
    std::set<std::string> unit_ids;
    for (const DataEntry& entry : library.squads.entries) {
        unit_ids.insert(entry.id);
    }
    for (const DataEntry& entry : library.vehicles.entries) {
        unit_ids.insert(entry.id);
    }
    for (const auto& [echelon, pool] : faction.pools) {
        for (const ResourcePoolEntry& entry : pool.entries) {
            if (entry.kind == PoolEntryKind::kUnit && !unit_ids.contains(entry.id)) {
                issues.push_back(Issue("DATA_REF_NOT_FOUND",
                                       "派系 " + faction.id + " " + echelon + " 池引用不存在的单位类型: " + entry.id));
            }
        }
    }
}

std::filesystem::path RepoDataRoot(const std::filesystem::path& schema_path) {
    return schema_path.parent_path().parent_path().parent_path() / "data";
}

bool IsRepoContractSchema(const std::filesystem::path& schema_path) {
    return schema_path.parent_path().filename() == "schemas" &&
           schema_path.parent_path().parent_path().filename() == "contracts";
}

// 内部实现：data_root 为空跳过 data/units 交叉校验（显式双路径重载兼容）。
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
FactionLoadResult LoadFactionInternal(const std::filesystem::path& data_file, const std::filesystem::path& schema_path,
                                      const std::filesystem::path* data_root) {
    std::vector<DataIssue> issues;
    nlohmann::json root;
    if (!ReadJsonDocument(data_file, root, issues)) {
        return Failure(std::move(issues));
    }

    const detail::SchemaFileResult schema_file = detail::load_schema_file(schema_path);
    if (!schema_file.ok()) {
        return Failure({Issue(schema_file.code, schema_file.message)});
    }
    if (!HasIntegerSchemaVersion(schema_file.schema)) {
        return Failure({Issue("SCHEMA_INVALID", "Schema 缺少整数 schema_version 字段: " + schema_path.string())});
    }

    const std::vector<detail::SchemaViolation> violations = schema_file.validate(root);
    for (const detail::SchemaViolation& violation : violations) {
        issues.push_back(Issue("SCHEMA_INVALID", "[" + violation.pointer + "] " + violation.message));
    }
    if (!issues.empty()) {
        return Failure(std::move(issues));
    }

    const std::int64_t data_version = root["schema_version"].get<std::int64_t>();
    const std::int64_t schema_version = schema_file.schema["schema_version"].get<std::int64_t>();
    if (data_version != schema_version) {
        return Failure({Issue("SCHEMA_VERSION_MISMATCH",
                              "派系数据 schema_version=" + std::to_string(data_version) + " 与 Schema schema_version=" +
                                  std::to_string(schema_version) + " 不一致: " + data_file.string())});
    }

    FactionTemplate faction;
    try {
        faction = root.get<FactionTemplate>();
    } catch (const std::exception& error) {
        return Failure({Issue("DATA_INVALID", std::string("派系模板语义非法: ") + error.what())});
    }

    CheckPoolEntryIds(faction, issues);
    if (data_root != nullptr) {
        const DataLibraryLoadResult library = load_data_library(*data_root);
        for (const DataIssue& issue : library.issues) {
            issues.push_back(issue);
        }
        if (library.ok()) {
            CheckUnitReferences(faction, library.library, issues);
        }
    }
    if (!issues.empty()) {
        return Failure(std::move(issues));
    }
    return FactionLoadResult{std::vector<DataIssue>{}, std::move(faction)};
}

}  // namespace

std::string_view to_string(const PoolEntryKind kind) noexcept {
    switch (kind) {
        case PoolEntryKind::kUnit:
            return "unit";
        case PoolEntryKind::kFireSupport:
            return "fire_support";
        case PoolEntryKind::kEngineer:
            return "engineer";
        case PoolEntryKind::kMedical:
            return "medical";
        case PoolEntryKind::kLogistics:
            return "logistics";
    }
    return "unknown";
}

PoolEntryKind pool_entry_kind_from_string(const std::string_view name) {
    if (name == "unit") {
        return PoolEntryKind::kUnit;
    }
    if (name == "fire_support") {
        return PoolEntryKind::kFireSupport;
    }
    if (name == "engineer") {
        return PoolEntryKind::kEngineer;
    }
    if (name == "medical") {
        return PoolEntryKind::kMedical;
    }
    if (name == "logistics") {
        return PoolEntryKind::kLogistics;
    }
    throw std::invalid_argument("未知资源池条目种类: " + std::string(name));
}

void to_json(nlohmann::json& json, const ResourcePoolEntry& entry) {
    json = nlohmann::json{
        {"id", entry.id}, {"kind", to_string(entry.kind)}, {"quantity", entry.quantity}, {"cost", entry.cost}};
}

void from_json(const nlohmann::json& json, ResourcePoolEntry& entry) {
    entry.id = json.at("id").get<std::string>();
    entry.kind = pool_entry_kind_from_string(json.at("kind").get<std::string>());
    entry.quantity = json.at("quantity").get<std::uint64_t>();
    entry.cost = json.at("cost").get<std::uint64_t>();
    if (entry.id.empty()) {
        throw std::invalid_argument("资源池条目 id 为空");
    }
}

void to_json(nlohmann::json& json, const EchelonResourcePool& pool) {
    json = nlohmann::json{
        {"support_score", pool.support_score}, {"support_kinds", pool.support_kinds}, {"entries", pool.entries}};
}

void from_json(const nlohmann::json& json, EchelonResourcePool& pool) {
    pool.support_score = json.value("support_score", 0U);
    pool.support_kinds = json.at("support_kinds").get<std::vector<std::string>>();
    pool.entries = json.at("entries").get<std::vector<ResourcePoolEntry>>();
}

void to_json(nlohmann::json& json, const FactionTemplate& faction) {
    json = nlohmann::json{{"schema_version", faction.schema_version},
                          {"id", faction.id},
                          {"name", faction.name},
                          {"approval_level", faction.approval_level},
                          {"resource_pools", faction.pools},
                          {"command_style", faction.command_style}};
}

void from_json(const nlohmann::json& json, FactionTemplate& faction) {
    faction.schema_version = json.at("schema_version").get<std::int64_t>();
    faction.id = json.at("id").get<std::string>();
    faction.name = json.at("name").get<std::string>();
    faction.approval_level = json.at("approval_level").get<std::uint32_t>();
    faction.pools = json.at("resource_pools").get<std::map<std::string, EchelonResourcePool>>();
    faction.command_style = json.value("command_style", nlohmann::json::object());
    if (!faction.is_valid()) {
        throw std::invalid_argument("派系模板字段非法: " + faction.id);
    }
}

FactionLoadResult load_faction(const std::filesystem::path& data_file) {
    const std::filesystem::path schema_path = resolve_schema_path(data_file, kFactionSchemaFile);
    if (schema_path.empty()) {
        return Failure({Issue("SCHEMA_NOT_FOUND", "无法按仓库约定从 " + data_file.string() +
                                                      " 解析 contracts/schemas/faction.schema.json")});
    }
    return load_faction(data_file, schema_path, RepoDataRoot(schema_path));
}

FactionLoadResult load_faction(const std::filesystem::path& data_file, const std::filesystem::path& schema_path) {
    if (IsRepoContractSchema(schema_path)) {
        return load_faction(data_file, schema_path, RepoDataRoot(schema_path));
    }
    return LoadFactionInternal(data_file, schema_path, nullptr);
}

FactionLoadResult load_faction(const std::filesystem::path& data_file, const std::filesystem::path& schema_path,
                               const std::filesystem::path& data_root) {
    return LoadFactionInternal(data_file, schema_path, &data_root);
}

}  // namespace wfs::sim
