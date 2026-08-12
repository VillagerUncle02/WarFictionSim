// sim/include/wfs/sim/loader.h
//
// T013：场景/数据加载与校验框架公开接口。
//
// 设计契约（宪法第 12/17 条；data/README.md；research.md §7）：
// - 一切可自定义内容以 JSON 数据定义，加载时执行 JSON Schema + 语义双重校验，
//   非法数据以结构化 issue（code + message）报错，绝不崩溃、绝不静默吞错。
// - 每个数据文件与每个 Schema 都携带 schema_version；数据文件版本必须与
//   Schema 版本一致，不一致按非法数据拒绝（宪法第 13 条的精神：版本化）。
// - Schema 文件默认按仓库约定解析：从数据文件所在目录向上查找
//   contracts/schemas/<schema_file>（T007 建立的运行时契约目录）；
//   调用方也可显式传入 schema 路径（测试/工具使用临时数据时）。
// - 语义校验覆盖 Schema 表达不了的引用完整性：单位 id 唯一、目标 id 唯一、
//   目标引用必须存在、player_node_id 必须对应场景内指挥节点。
// - 加载结果错误列表按固定顺序生成，保证确定性（宪法第 7 条）。
// - 性能：启动校验预算 ≤5s（plan.md Performance Goals），由 loader_test
//   的预算测试守护。

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace wfs::sim {

// 结构化加载/校验问题：code 为稳定错误码，message 为人类可读说明。
struct DataIssue {
    std::string code;
    std::string message;
};

struct ScenarioUnit {
    std::string id;
    std::string type;
    std::string node_id;
    double x = 0.0;
    double y = 0.0;
    std::vector<std::string> ammo;
};

struct ScenarioObjective {
    std::string id;
    std::string kind;  // "unit" | "zone"
    std::string target_ref;
    std::uint64_t duration_ticks = 0U;  // 0 = 未指定
};

struct Scenario {
    // 与 GameClock::kDefaultTickHz 保持同一默认值（20 Hz，research.md §3）。
    static constexpr std::uint32_t kDefaultTickHz = 20U;

    std::int64_t schema_version = 0;
    std::string id;
    std::string name;
    std::string player_node_id;  // 空 = 未指定（无越权过滤）
    double map_width_km = 0.0;
    double map_height_km = 0.0;
    std::uint32_t tick_hz = kDefaultTickHz;
    std::uint64_t seed = 0U;
    std::vector<std::string> zones;
    std::vector<ScenarioUnit> units;
    std::vector<ScenarioObjective> objectives;
    // 原始 JSON 纯数据：快照/存档/决策日志可直接复用，不跨语言共享对象。
    nlohmann::json raw = nlohmann::json::object();
};

struct ScenarioLoadResult {
    bool ok() const noexcept { return issues.empty(); }

    std::vector<DataIssue> issues;
    Scenario scenario;
};

// 按仓库约定解析 Schema 路径：从 data_file 所在目录逐级向上，寻找
// 第一个包含 contracts/schemas/<schema_file> 的仓库根；找不到返回空路径。
std::filesystem::path resolve_schema_path(const std::filesystem::path& data_file, const std::string& schema_file);

// 加载并校验场景：Schema 路径按约定从场景文件解析。
ScenarioLoadResult load_scenario(const std::filesystem::path& scenario_path);

// 加载并校验场景：显式指定 Schema 路径（测试/临时数据使用）。
ScenarioLoadResult load_scenario(const std::filesystem::path& scenario_path, const std::filesystem::path& schema_path);

}  // namespace wfs::sim
