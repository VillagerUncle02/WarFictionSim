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
#include <cstdint>
#include <fstream>
#include <new>
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

bool HasIntegerSchemaVersion(const nlohmann::json& document) {
    if (!document.contains("schema_version") || !document["schema_version"].is_number_integer()) {
        return false;
    }
    // 为什么需要范围防护：schema_version 最终存入 Scenario::schema_version
    // （std::int64_t），而本版 nlohmann 的 is_number_integer() 对 number_unsigned
    // 同样返回 true。若超大 unsigned（> INT64_MAX）漏判，后续 get<std::int64_t>()
    // 会被环绕截断成负数（部分 nlohmann 版本直接抛 type_error），把非法数据
    // 误报为版本不一致甚至逃出 load_scenario（宪法第 12/17 条）。必须先判
    // is_number_unsigned 再 get<std::uint64_t>()，避免类型不符时抛异常。
    const nlohmann::json& version = document["schema_version"];
    return !(version.is_number_unsigned() && version.get<std::uint64_t>() > static_cast<std::uint64_t>(INT64_MAX));
}

// 读取并解析 JSON 文档；失败返回 false 并追加结构化 issue（不抛异常）。
bool ReadJsonDocument(const std::filesystem::path& path, nlohmann::json& document, std::vector<DataIssue>& issues) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        issues.push_back(Issue("IO_ERROR", "无法打开场景文件: " + path.string()));
        return false;
    }
    try {
        document = nlohmann::json::parse(input);
        return true;
    } catch (const nlohmann::json::parse_error& error) {
        issues.push_back(Issue("INVALID_JSON", std::string("场景不是合法 JSON: ") + error.what()));
        return false;
    }
}

// 语义校验 + 实体提取。Schema 已保证必需字段存在，此处只做跨字段完整性；
// 失败时按固定顺序追加 issue 并返回 false。
bool ExtractScenarioData(const nlohmann::json& root, const detail::SchemaFileResult& schema_file, Scenario& scenario,
                         std::vector<DataIssue>& issues) {
    // 防御纵深：主链路由 JSON Schema 层拦截（scenario.schema.json 已约束 maximum），
    // 但双路径重载允许传入未约束范围的 Schema；在两次 get 之前统一守卫，
    // 缺失/类型非法/超出 int64 范围一律结构化 SCHEMA_INVALID，不触碰可能环绕
    // 截断或抛 type_error 的裸 get（PR #107 review round 4；宪法第 12/17 条）。
    if (!HasIntegerSchemaVersion(root) || !HasIntegerSchemaVersion(schema_file.schema)) {
        issues.push_back(Issue("SCHEMA_INVALID", "场景/Schema schema_version 缺失、类型非法或超出 int64 范围"));
        return false;
    }
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

    std::set<std::string> objective_ids;
    for (const nlohmann::json& objective : root["objectives"]) {
        ScenarioObjective entry;
        entry.id = objective["id"].get<std::string>();
        entry.kind = objective["kind"].get<std::string>();
        entry.target_ref = objective["target_ref"].get<std::string>();
        if (objective.contains("duration_ticks")) {
            entry.duration_ticks = objective["duration_ticks"].get<std::uint64_t>();
        }
        if (!objective_ids.insert(entry.id).second) {
            issues.push_back(Issue("DUPLICATE_OBJECTIVE_ID", "目标 id 重复: " + entry.id));
            continue;
        }
        if (entry.kind == "unit" && !unit_ids.contains(entry.target_ref)) {
            issues.push_back(
                Issue("OBJECTIVE_REF_NOT_FOUND", "目标 " + entry.id + " 引用不存在的单位: " + entry.target_ref));
            continue;
        }
        if (entry.kind == "zone" && !zone_ids.contains(entry.target_ref)) {
            issues.push_back(
                Issue("OBJECTIVE_REF_NOT_FOUND", "目标 " + entry.id + " 引用不存在的区域: " + entry.target_ref));
            continue;
        }
        scenario.objectives.push_back(std::move(entry));
    }

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
        return Failure({Issue("SCHEMA_INVALID",
                              "Schema schema_version 缺失、类型非法或超出 int64 范围: " + schema_path.string())});
    }

    // 第一层：JSON Schema 结构校验（违规已确定性排序）。
    std::vector<detail::SchemaViolation> violations;
    try {
        violations = schema_file.validate(root);
    } catch (const std::bad_alloc&) {
        // 内存耗尽属内部故障，不得折叠为 SCHEMA_INVALID（伪装成非法数据），透传给上层定位。
        throw;
    } catch (const std::exception& error) {
        return Failure({Issue("SCHEMA_INVALID", std::string("Schema 校验失败: ") + error.what())});
    }
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

}  // namespace wfs::sim
