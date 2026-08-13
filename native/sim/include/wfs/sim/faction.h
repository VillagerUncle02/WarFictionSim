// sim/include/wfs/sim/faction.h
//
// T052：派系模板模型与加载接口（FR-006/007；data-model.md §13）。
//
// 设计契约：
// - 派系差异仅体现为 各层级默认资源池清单 + 指挥风格参数；指挥逻辑完全
//   复用（FR-006）。资源池承载连排级有限分数总额（support_score）、可用
//   支援种类（support_kinds）与可配属力量清单（entries，含单位类型/火力
//   支援等种类与成本）。
// - 审批层级基线 approval_level ∈ {0,1,2}：0 = 中国内置合成化（上级直接
//   裁决）；1 = 北约（任务链，经一级转发）；2 = 苏俄（整建制申请，经两级
//   转发）。指挥风格参数其余取值空间待 US3 补充（data-model §18 登记）。
// - 派系作为可扩展模板数据（FR-007）：加载执行 JSON Schema + 语义双重校验，
//   非法数据以结构化 issue 报错而非崩溃（宪法第 12/17 条）；pool 使用
//   std::map 固定键序，序列化顺序确定（宪法第 7 条）。

#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "wfs/sim/loader.h"

namespace wfs::sim {

// 资源池条目种类：单位（data/units 类型 id）与火力支援等支援种类。
enum class PoolEntryKind : std::uint8_t {
    kUnit = 0,         // 单位类型（squad/vehicle，交叉校验 data/units）。
    kFireSupport = 1,  // 火力支援（营属迫击炮/大口径火炮/火箭炮；航空后置）。
    kEngineer = 2,     // 工兵。
    kMedical = 3,      // 医疗。
    kLogistics = 4,    // 后勤/补给。
};

std::string_view to_string(PoolEntryKind kind) noexcept;
PoolEntryKind pool_entry_kind_from_string(std::string_view name);

// 资源池条目（data-model §13/§14；cost 为连排级有限分数成本，FR-008）。
struct ResourcePoolEntry {
    std::string id;            // 单位类型 id 或支援种类 id。
    PoolEntryKind kind = PoolEntryKind::kUnit;
    std::uint64_t quantity = 0U;
    std::uint64_t cost = 0U;   // 每次请求该资源的分数成本（quantity 倍增）。

    bool operator==(const ResourcePoolEntry&) const = default;
};

// 单层级资源池：分数总额 + 支援种类 + 可配属力量。
struct EchelonResourcePool {
    std::uint64_t support_score = 0U;         // 连排级每场战斗支援分数总额。
    std::vector<std::string> support_kinds;   // 可用支援种类（火力支援等）。
    std::vector<ResourcePoolEntry> entries;   // 可配属力量清单（固定顺序）。

    bool operator==(const EchelonResourcePool&) const = default;
};

// 派系模板：各层级资源池 + 审批层级基线 + 指挥风格参数。
struct FactionTemplate {
    std::int64_t schema_version = 0;
    std::string id;
    std::string name;
    std::uint32_t approval_level = 0U;  // 0/1/2（FR-006 审批层级基线）。
    // echelon 名（squad/platoon/company/battalion/brigade）→ 资源池。
    std::map<std::string, EchelonResourcePool> pools;
    // 其余指挥风格参数（下发/执行倾向、配属审批简化度）以原始 JSON 承载，
    // 由 US3 消费；字段语义登记于 data-model §18。
    nlohmann::json command_style = nlohmann::json::object();

    bool is_valid() const noexcept {
        return !id.empty() && !name.empty() && approval_level <= 2U;
    }

    bool operator==(const FactionTemplate&) const = default;
};

void to_json(nlohmann::json& json, const ResourcePoolEntry& entry);
void from_json(const nlohmann::json& json, ResourcePoolEntry& entry);
void to_json(nlohmann::json& json, const EchelonResourcePool& pool);
void from_json(const nlohmann::json& json, EchelonResourcePool& pool);
void to_json(nlohmann::json& json, const FactionTemplate& faction);
void from_json(const nlohmann::json& json, FactionTemplate& faction);

struct FactionLoadResult {
    bool ok() const noexcept { return issues.empty(); }

    std::vector<DataIssue> issues;
    FactionTemplate faction;
};

// 加载并校验派系模板：Schema 路径按仓库约定从数据文件向上解析
// （contracts/schemas/faction.schema.json），并做语义校验与 data/units
// 交叉引用检查（宪法 12：数据引用必须存在）。
FactionLoadResult load_faction(const std::filesystem::path& data_file);
// 显式 Schema 路径（测试/临时数据）；跳过 data/units 交叉检查。
FactionLoadResult load_faction(const std::filesystem::path& data_file,
                               const std::filesystem::path& schema_path);
// 显式 Schema + 数据根目录：始终执行 data/units 交叉检查。
FactionLoadResult load_faction(const std::filesystem::path& data_file,
                               const std::filesystem::path& schema_path,
                               const std::filesystem::path& data_root);

}  // namespace wfs::sim
