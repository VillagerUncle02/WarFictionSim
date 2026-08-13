// sim/include/wfs/sim/command_org.h
//
// T057：营级指挥节点与编制校验公开接口。
//
// 设计契约（FR-001/013/015/016/048；data-model.md §1–2；宪法第 12/17 条）：
// - 场景可选段 raw["command_org"] 声明指挥节点（连排/连/营/旅，不含班）与
//   行政编制单位（班 → 排 → 连 → 营 → 旅）；未配置时保持旧场景行为
//   （configured=false，全部 US3 子系统不激活）。
// - 校验按固定顺序收集结构化 issue（code + message），非法编制显式拒绝
//   加载：id 唯一、单父、无环、指挥层级链（子级 echelon 必须低于父级）、
//   节点编制单位存在且 echelon 一致、场景单位 node_id 必须命中指挥节点。
// - 直属班协调能力基数（FR-016 落地基线）：节点编制子树内的班编制单位数
//   （班是行政编制叶子；配属/非直属单位不进编制树、天然不计入），与
//   "按班而非按总人数"的口径一致；混合编成按最小编制单位（班）计。
// - 指挥树/编制树分别复用 T025 的 CommandTree/OrganizationTree 约束，
//   CommandOrgState 随存档/快照序列化（宪法第 13 条）。

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "wfs/sim/loader.h"
#include "wfs/sim/model/command_node.h"
#include "wfs/sim/model/organization.h"

namespace wfs::sim {

// 指挥组织状态：指挥树 + 编制树 + 节点层级/编制映射（确定性，随存档序列化）。
struct CommandOrgState {
    model::CommandTree command_tree;
    model::OrganizationTree organizations;
    // 节点 id → 指挥层级（连排/连/营/旅；不含班，FR-001）。
    std::map<std::string, model::Echelon> node_echelon;
    // 节点 id → 行政编制单位 id（计算直属班基数的锚点）。
    std::map<std::string, std::string> node_org_unit;
    bool configured = false;  // 场景未配置 command_org 时为 false。

    bool is_command_node(const std::string& node_id) const;
    const model::CommandNode* find_node(const std::string& node_id) const;
    std::vector<std::string> children_of(const std::string& node_id) const;
    std::vector<std::string> nodes_in_insertion_order() const;
    // 节点编制子树内的班编制单位数（直属班协调能力基数，FR-016）。
    std::size_t direct_squad_base(const std::string& node_id) const;
};

// 节点指挥层级；未知 id 返回 kPlatoon（连排级基线），与"缺省连排级"语义一致。
model::Echelon command_echelon(const CommandOrgState& org, const std::string& node_id) noexcept;

// 直属班协调能力基数（FR-016 落地基线，详见 data-model.md §18 登记）。
std::size_t direct_squad_base(const CommandOrgState& org, const std::string& node_id);

struct CommandOrgLoadResult {
    bool ok() const noexcept { return issues.empty(); }

    CommandOrgState state;
    std::vector<DataIssue> issues;
};

// 从场景 raw JSON 构建指挥组织（未经校验的输入抛 std::invalid_argument，
// 宪法第 17 条；生产路径由 loader 先经 validate_command_org 拒绝非法数据）。
CommandOrgLoadResult load_command_org(const nlohmann::json& raw);

// loader 语义校验入口：raw 含 command_org 时执行全部校验并按固定顺序追加
// issue；不含该段时不做任何检查（旧场景兼容）。
void validate_command_org(const nlohmann::json& raw, std::vector<DataIssue>& issues);

void to_json(nlohmann::json& json, const CommandOrgState& state);
void from_json(const nlohmann::json& json, CommandOrgState& state);

}  // namespace wfs::sim
