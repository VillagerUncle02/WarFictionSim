// sim/src/loader.cpp
//
// T013：场景/数据加载与校验框架实现。
//
// 实现策略：分两阶段——先 JSON Schema 结构校验（schema_validator.cpp），
// 再语义校验（版本一致、引用完整性、id 唯一性、指挥节点存在性）。
// 错误列表按"版本 → 区域/单位/目标 id → 引用 → 指挥节点"的固定顺序追加，
// 同一文件必然产生同一错误序列；任何失败都返回结构化 issue，不抛异常、
// 不崩溃（宪法第 12/17 条）。Schema 路径默认按仓库约定从场景文件向上解析，
// 也支持显式路径（临时数据/测试）。

#include "wfs/sim/loader.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "schema_validator.h"

namespace wfs::sim {

namespace {

constexpr const char* kScenarioSchemaFile = "scenario.schema.json";

DataIssue Issue(std::string code, std::string message) {
    return DataIssue{std::move(code), std::move(message)};
}

ScenarioLoadResult Failure(std::vector<DataIssue> issues) {
    return ScenarioLoadResult{std::move(issues), Scenario{}};
}

DataCatalogLoadResult CatalogFailure(std::vector<DataIssue> issues) {
    return DataCatalogLoadResult{std::move(issues), DataCatalog{}};
}

DataLibraryLoadResult LibraryFailure(std::vector<DataIssue> issues) {
    return DataLibraryLoadResult{std::move(issues), DataLibrary{}};
}

bool HasIntegerSchemaVersion(const nlohmann::json& document) {
    return document.contains("schema_version") && document["schema_version"].is_number_integer();
}

// 读取并解析 JSON 文档；失败返回 false 并追加结构化 issue（不抛异常）。
bool ReadJsonDocument(const std::filesystem::path& path, nlohmann::json& document, std::vector<DataIssue>& issues) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        issues.push_back(Issue("IO_ERROR", "无法打开数据文件: " + path.string()));
        return false;
    }
    try {
        document = nlohmann::json::parse(input);
        return true;
    } catch (const nlohmann::json::parse_error& error) {
        issues.push_back(Issue("INVALID_JSON", std::string("数据文件不是合法 JSON: ") + error.what()));
        return false;
    }
}

// 目标条目提取 + 引用校验（objectives 与 failure_conditions 共用，FR-043）。
bool ExtractObjectiveEntry(const nlohmann::json& objective, const std::set<std::string>& unit_ids,
                           const std::set<std::string>& zone_ids, ScenarioObjective& entry,
                           std::vector<DataIssue>& issues) {
    entry.id = objective["id"].get<std::string>();
    entry.kind = objective["kind"].get<std::string>();
    entry.target_ref = objective["target_ref"].get<std::string>();
    if (objective.contains("duration_ticks")) {
        entry.duration_ticks = objective["duration_ticks"].get<std::uint64_t>();
    }
    if (entry.kind == "unit" && !unit_ids.contains(entry.target_ref)) {
        issues.push_back(
            Issue("OBJECTIVE_REF_NOT_FOUND", "目标 " + entry.id + " 引用不存在的单位: " + entry.target_ref));
        return false;
    }
    if (entry.kind == "zone" && !zone_ids.contains(entry.target_ref)) {
        issues.push_back(
            Issue("OBJECTIVE_REF_NOT_FOUND", "目标 " + entry.id + " 引用不存在的区域: " + entry.target_ref));
        return false;
    }
    return true;
}

// 目标/失败条件列表提取：先查重复 id，再校验引用；错误按列表顺序追加。
bool ExtractObjectiveList(const nlohmann::json& list, const std::string& label, const std::set<std::string>& unit_ids,
                          const std::set<std::string>& zone_ids, std::vector<ScenarioObjective>& objectives,
                          std::vector<DataIssue>& issues) {
    std::set<std::string> ids;
    bool valid = true;
    for (const nlohmann::json& item : list) {
        ScenarioObjective entry;
        entry.id = item["id"].get<std::string>();
        if (!ids.insert(entry.id).second) {
            issues.push_back(Issue("DUPLICATE_OBJECTIVE_ID", label + " id 重复: " + entry.id));
            valid = false;
            continue;
        }
        if (!ExtractObjectiveEntry(item, unit_ids, zone_ids, entry, issues)) {
            valid = false;
            continue;
        }
        objectives.push_back(std::move(entry));
    }
    return valid;
}

// 语义校验 + 实体提取。Schema 已保证必需字段存在，此处只做跨字段完整性；
// 失败时按固定顺序追加 issue 并返回 false。
bool ExtractScenarioData(const nlohmann::json& root, const detail::SchemaFileResult& schema_file, Scenario& scenario,
                         std::vector<DataIssue>& issues) {
    const std::int64_t data_version = root["schema_version"].get<std::int64_t>();
    const std::int64_t schema_version = schema_file.schema["schema_version"].get<std::int64_t>();
    if (data_version != schema_version) {
        issues.push_back(Issue("SCHEMA_VERSION_MISMATCH",
                               "场景 schema_version=" + std::to_string(data_version) +
                                   " 与 Schema schema_version=" + std::to_string(schema_version) + " 不一致"));
        return false;
    }

    scenario.schema_version = data_version;
    scenario.id = root["id"].get<std::string>();
    scenario.name = root["name"].get<std::string>();
    if (root.contains("player_node_id")) {
        scenario.player_node_id = root["player_node_id"].get<std::string>();
    }
    scenario.map_width_km = root["map"]["width_km"].get<double>();
    scenario.map_height_km = root["map"]["height_km"].get<double>();
    scenario.tick_hz = root.value("tick_hz", Scenario::kDefaultTickHz);
    scenario.seed = root.value("seed", 0U);
    scenario.time_limit_ticks = root.value("time_limit_ticks", 0U);
    scenario.tutorial = root.value("tutorial", false);
    scenario.save_slot = root.value("save_slot", "");

    // 预先分配容量：避免大场景（预算测试 2000 单位）循环内反复扩容。
    scenario.zones.reserve(root["zones"].size());
    scenario.units.reserve(root["units"].size());
    scenario.objectives.reserve(root["objectives"].size());

    std::set<std::string> zone_ids;
    for (const nlohmann::json& zone : root["zones"]) {
        const std::string zone_id = zone["id"].get<std::string>();
        if (!zone_ids.insert(zone_id).second) {
            issues.push_back(Issue("DUPLICATE_ZONE_ID", "区域 id 重复: " + zone_id));
            continue;
        }
        scenario.zones.push_back(zone_id);
    }

    std::set<std::string> unit_ids;
    for (const nlohmann::json& unit : root["units"]) {
        ScenarioUnit entry;
        entry.id = unit["id"].get<std::string>();
        entry.type = unit["type"].get<std::string>();
        entry.node_id = unit["node_id"].get<std::string>();
        entry.x = unit["x"].get<double>();
        entry.y = unit["y"].get<double>();
        for (const nlohmann::json& ammo : unit["ammo"]) {
            entry.ammo.push_back(ammo.get<std::string>());
        }
        if (!unit_ids.insert(entry.id).second) {
            issues.push_back(Issue("DUPLICATE_UNIT_ID", "单位 id 重复: " + entry.id));
            continue;
        }
        scenario.units.push_back(std::move(entry));
    }

    ExtractObjectiveList(root["objectives"], "目标", unit_ids, zone_ids, scenario.objectives, issues);

    if (!scenario.player_node_id.empty()) {
        const bool exists = std::any_of(scenario.units.begin(), scenario.units.end(), [&](const ScenarioUnit& unit) {
            return unit.node_id == scenario.player_node_id;
        });
        if (!exists) {
            issues.push_back(
                Issue("PLAYER_NODE_NOT_FOUND", "player_node_id 不存在于场景单位中: " + scenario.player_node_id));
        }
    }
    return issues.empty();
}

}  // namespace

std::filesystem::path resolve_schema_path(const std::filesystem::path& data_file, const std::string& schema_file) {
    std::filesystem::path directory =
        data_file.has_parent_path() ? data_file.parent_path() : std::filesystem::current_path();
    for (;;) {
        std::filesystem::path candidate = directory / "contracts" / "schemas" / schema_file;
        if (std::filesystem::is_regular_file(candidate)) {
            return candidate;
        }
        const std::filesystem::path parent = directory.parent_path();
        if (parent == directory) {
            return {};
        }
        directory = parent;
    }
}

ScenarioLoadResult load_scenario(const std::filesystem::path& scenario_path) {
    const std::filesystem::path schema_path = resolve_schema_path(scenario_path, kScenarioSchemaFile);
    if (schema_path.empty()) {
        return Failure({Issue("SCHEMA_NOT_FOUND", "无法按仓库约定从 " + scenario_path.string() +
                                                      " 解析 contracts/schemas/scenario.schema.json")});
    }
    return load_scenario(scenario_path, schema_path);
}

// 双路径重载是 loader.h 公开 API：固定"数据文件在前、Schema 在后"，
// 参数名即语义（scenario_path/schema_path），调用方无需猜测；
// 为两个路径引入包装结构体会降低可读性，故保留显式参数并禁止换序。
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
ScenarioLoadResult load_scenario(const std::filesystem::path& scenario_path, const std::filesystem::path& schema_path) {
    std::vector<DataIssue> issues;
    nlohmann::json root;
    if (!ReadJsonDocument(scenario_path, root, issues)) {
        return Failure(std::move(issues));
    }

    const detail::SchemaFileResult schema_file = detail::load_schema_file(schema_path);
    if (!schema_file.ok()) {
        return Failure({Issue(schema_file.code, schema_file.message)});
    }
    if (!HasIntegerSchemaVersion(schema_file.schema)) {
        return Failure({Issue("SCHEMA_INVALID", "Schema 缺少整数 schema_version 字段: " + schema_path.string())});
    }

    // 第一层：JSON Schema 结构校验（违规已确定性排序）。
    const std::vector<detail::SchemaViolation> violations = schema_file.validate(root);
    for (const detail::SchemaViolation& violation : violations) {
        issues.push_back(Issue("SCHEMA_INVALID", "[" + violation.pointer + "] " + violation.message));
    }
    if (!issues.empty()) {
        return Failure(std::move(issues));
    }

    // 第二层：语义校验 + 实体提取。
    Scenario scenario;
    if (!ExtractScenarioData(root, schema_file, scenario, issues)) {
        return Failure(std::move(issues));
    }

    scenario.raw = std::move(root);
    return ScenarioLoadResult{std::vector<DataIssue>{}, std::move(scenario)};
}

DataCatalogLoadResult load_data_catalog(const std::filesystem::path& data_file) {
    // 仓库约定：<stem>.json ↔ contracts/schemas/<stem>.schema.json。
    const std::string schema_file = data_file.stem().string() + ".schema.json";
    const std::filesystem::path schema_path = resolve_schema_path(data_file, schema_file);
    if (schema_path.empty()) {
        return CatalogFailure({Issue(
            "SCHEMA_NOT_FOUND", "无法按仓库约定从 " + data_file.string() + " 解析 contracts/schemas/" + schema_file)});
    }
    return load_data_catalog(data_file, schema_path);
}

DataCatalogLoadResult load_data_catalog(const std::filesystem::path& data_file,
                                        const std::filesystem::path& schema_path) {
    std::vector<DataIssue> issues;
    nlohmann::json root;
    if (!ReadJsonDocument(data_file, root, issues)) {
        return CatalogFailure(std::move(issues));
    }

    const detail::SchemaFileResult schema_file = detail::load_schema_file(schema_path);
    if (!schema_file.ok()) {
        return CatalogFailure({Issue(schema_file.code, schema_file.message)});
    }
    if (!HasIntegerSchemaVersion(schema_file.schema)) {
        return CatalogFailure(
            {Issue("SCHEMA_INVALID", "Schema 缺少整数 schema_version 字段: " + schema_path.string())});
    }

    const std::vector<detail::SchemaViolation> violations = schema_file.validate(root);
    for (const detail::SchemaViolation& violation : violations) {
        issues.push_back(Issue("SCHEMA_INVALID", "[" + violation.pointer + "] " + violation.message));
    }
    if (!issues.empty()) {
        return CatalogFailure(std::move(issues));
    }

    const std::int64_t data_version = root["schema_version"].get<std::int64_t>();
    const std::int64_t schema_version = schema_file.schema["schema_version"].get<std::int64_t>();
    if (data_version != schema_version) {
        return CatalogFailure(
            {Issue("SCHEMA_VERSION_MISMATCH", "数据文件 schema_version=" + std::to_string(data_version) +
                                                  " 与 Schema schema_version=" + std::to_string(schema_version) +
                                                  " 不一致: " + data_file.string())});
    }

    DataCatalog catalog;
    catalog.schema_version = data_version;
    catalog.kind = root["kind"].get<std::string>();
    catalog.entries.reserve(root["entries"].size());
    std::set<std::string> entry_ids;
    for (const nlohmann::json& entry_json : root["entries"]) {
        const std::string entry_id = entry_json["id"].get<std::string>();
        if (!entry_ids.insert(entry_id).second) {
            issues.push_back(Issue("DUPLICATE_ENTRY_ID", "数据条目 id 重复: " + entry_id));
            continue;
        }
        catalog.entries.push_back(DataEntry{entry_id, catalog.kind, entry_json});
    }
    if (!issues.empty()) {
        return CatalogFailure(std::move(issues));
    }
    return DataCatalogLoadResult{std::vector<DataIssue>{}, std::move(catalog)};
}

// 第二层：跨文件引用完整性（固定顺序：班 → 武器 → 工事）。
bool ValidateDataReferences(const DataLibrary& library, std::vector<DataIssue>& issues) {
    std::set<std::string> weapon_ids;
    std::set<std::string> weapon_categories;
    std::set<std::string> ammo_ids;
    for (const DataEntry& entry : library.weapons.entries) {
        weapon_ids.insert(entry.id);
        weapon_categories.insert(entry.raw["category"].get<std::string>());
    }
    for (const DataEntry& entry : library.ammo.entries) {
        ammo_ids.insert(entry.id);
    }

    const auto check_refs = [&issues](const DataEntry& entry, const std::string& field,
                                      const std::set<std::string>& known, const char* target_kind) {
        for (const nlohmann::json& ref : entry.raw.value(field, nlohmann::json::array())) {
            const std::string ref_id = ref.get<std::string>();
            if (known.contains(ref_id)) {
                continue;
            }
            std::string message = target_kind;
            message += " ";
            message += entry.id;
            message += " 引用不存在的 ";
            message += field;
            message += ": ";
            message += ref_id;
            issues.push_back(Issue("DATA_REF_NOT_FOUND", std::move(message)));
        }
    };

    for (const DataEntry& entry : library.squads.entries) {
        check_refs(entry, "weapons", weapon_ids, "班");
        check_refs(entry, "ammo", ammo_ids, "班");
        for (const nlohmann::json& crewed : entry.raw.value("crewed_equipment", nlohmann::json::array())) {
            const std::string equipment_id = crewed["equipment_id"].get<std::string>();
            if (!weapon_ids.contains(equipment_id)) {
                issues.push_back(
                    Issue("DATA_REF_NOT_FOUND", "班 " + entry.id + " 引用不存在的乘组装备: " + equipment_id));
            }
        }
    }
    for (const DataEntry& entry : library.weapons.entries) {
        check_refs(entry, "compatible_ammo", ammo_ids, "武器");
    }
    for (const DataEntry& entry : library.fortifications.entries) {
        for (const nlohmann::json& ref : entry.raw.value("applicable_weapon_categories", nlohmann::json::array())) {
            const std::string category = ref.get<std::string>();
            if (!weapon_categories.contains(category)) {
                issues.push_back(
                    Issue("DATA_REF_NOT_FOUND", "工事 " + entry.id + " 引用不存在的武器类别: " + category));
            }
        }
    }
    return issues.empty();
}

DataLibraryLoadResult load_data_library(const std::filesystem::path& data_root) {
    const std::filesystem::path first_file = data_root / "units" / "squads.json";
    const std::filesystem::path schema_path = resolve_schema_path(first_file, "squads.schema.json");
    if (schema_path.empty()) {
        return LibraryFailure(
            {Issue("SCHEMA_NOT_FOUND", "无法按仓库约定从 " + first_file.string() + " 解析 contracts/schemas/")});
    }
    return load_data_library(data_root, schema_path.parent_path());
}

DataLibraryLoadResult load_data_library(const std::filesystem::path& data_root,
                                        const std::filesystem::path& schema_dir) {
    // 目录文件固定顺序（宪法第 7 条：错误序列确定）。
    struct CatalogSpec {
        const char* relative_path;
        DataCatalog DataLibrary::* member;
    };
    static const std::array<CatalogSpec, 6> kCatalogSpecs = {{
        {"units/squads.json", &DataLibrary::squads},
        {"units/weapons.json", &DataLibrary::weapons},
        {"units/ammo.json", &DataLibrary::ammo},
        {"terrain/terrain.json", &DataLibrary::terrain},
        {"terrain/fortifications.json", &DataLibrary::fortifications},
        {"terrain/facilities.json", &DataLibrary::facilities},
    }};

    std::vector<DataIssue> issues;
    DataLibrary library;
    for (const CatalogSpec& spec : kCatalogSpecs) {
        const std::filesystem::path file = data_root / spec.relative_path;
        const std::string schema_name = file.stem().string() + ".schema.json";
        DataCatalogLoadResult result = load_data_catalog(file, schema_dir / schema_name);
        for (DataIssue& issue : result.issues) {
            issues.push_back(std::move(issue));
        }
        if (result.ok()) {
            library.*spec.member = std::move(result.catalog);
        }
    }
    if (!issues.empty()) {
        return LibraryFailure(std::move(issues));
    }
    if (!ValidateDataReferences(library, issues)) {
        return LibraryFailure(std::move(issues));
    }
    return DataLibraryLoadResult{std::vector<DataIssue>{}, std::move(library)};
}

}  // namespace wfs::sim
