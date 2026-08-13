// sim/src/command_org.cpp
//
// T057：营级指挥节点与编制校验实现。
//
// 实现策略：
// - 校验与构建分离：validate_command_org 以固定顺序收集结构化 issue
//   （宪法第 17 条：错误不静默、顺序确定），load_command_org 在已校验输入
//   上直接构建状态（非法输入抛 std::invalid_argument）。
// - 指挥层级链：子节点 echelon 必须严格低于父节点（班 < 排 < 连 < 营 < 旅），
//   允许跨级直属（营直接指挥排，FR-001 扁平化），不允许同级/反向隶属。
// - 编制树复用 T025 OrganizationTree 的互反一致与 CanContain 类型约束
//   （步兵排仅步兵班等，FR-013）。
// - 直属班基数 = 节点编制子树内班编制单位数；遍历按节点插入顺序与
//   subordinate_ids 声明顺序，保证确定性（宪法第 7 条）。

#include "wfs/sim/command_org.h"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace wfs::sim {

namespace {

DataIssue Issue(std::string code, std::string message) {
    return DataIssue{std::move(code), std::move(message)};
}

// 指挥节点可扮演层级（FR-001：玩家不扮演班一级）。
bool IsCommandEchelon(const model::Echelon echelon) {
    return echelon == model::Echelon::kPlatoon || echelon == model::Echelon::kCompany ||
           echelon == model::Echelon::kBattalion || echelon == model::Echelon::kBrigade;
}

// 单节点解析：数值/归属越界与未知枚举统一映射为 COMMAND_ORG_NODE_INVALID。
struct ParsedNode {
    model::CommandNode node;
    model::Echelon echelon = model::Echelon::kPlatoon;
    std::string org_unit_id;
    bool valid = false;
};

ParsedNode ParseNode(const nlohmann::json& json) {
    ParsedNode parsed;
    parsed.node.id = json.at("id").get<std::string>();
    parsed.node.name = json.value("name", std::string());
    parsed.node.parent_id = json.value("parent_id", std::string());
    parsed.echelon = model::echelon_from_string(json.at("echelon").get<std::string>());
    parsed.org_unit_id = json.value("org_unit_id", std::string());
    const nlohmann::json& owner = json.at("owner");
    parsed.node.owner.kind = model::owner_kind_from_string(owner.at("kind").get<std::string>());
    parsed.node.owner.ai_id = owner.at("ai_id").get<std::string>();
    parsed.node.command_limit = json.value("command_limit", 0U);
    parsed.node.coordination = json.value("coordination", 0.0);
    parsed.node.experience = json.value("experience", nlohmann::json::object()).get<model::Experience>();
    if (!IsCommandEchelon(parsed.echelon)) {
        throw std::invalid_argument("指挥节点层级必须是 platoon/company/battalion/brigade（不含班）: " +
                                    parsed.node.id);
    }
    if (!parsed.node.is_valid()) {
        throw std::invalid_argument("指挥节点数值越界或归属非法（FR-048）: " + parsed.node.id);
    }
    parsed.valid = true;
    return parsed;
}

// 沿 parent_id 链检测成环；返回 true 表示存在环。
bool HasParentCycle(const std::map<std::string, std::string>& parents, const std::string& start) {
    std::set<std::string> visited;
    std::string current = start;
    while (!current.empty()) {
        if (!visited.insert(current).second) {
            return true;
        }
        const auto iterator = parents.find(current);
        current = iterator == parents.end() ? std::string() : iterator->second;
    }
    return false;
}

// 已通过节点解析与成环校验后的编制树构建（错误经 issue 返回）。
bool BuildOrganizationTree(const nlohmann::json& organizations, std::vector<DataIssue>& issues,
                           model::OrganizationTree& tree) {
    struct ParsedOrganization {
        model::OrganizationUnit unit;
        bool valid = false;
    };
    std::vector<ParsedOrganization> parsed_units;
    std::map<std::string, std::string> parent_links;
    std::map<std::string, std::vector<std::string>> declared_subordinates;
    std::set<std::string> ids;
    for (const nlohmann::json& unit_json : organizations) {
        try {
            const model::OrganizationUnit unit = unit_json.get<model::OrganizationUnit>();
            if (!ids.insert(unit.id).second) {
                issues.push_back(Issue("COMMAND_ORG_ORGANIZATION_DUPLICATE_ID", "编制单位 id 重复: " + unit.id));
                continue;
            }
            parsed_units.push_back(ParsedOrganization{unit, true});
            if (!unit.parent_id.empty()) {
                parent_links.emplace(unit.id, unit.parent_id);
                declared_subordinates[unit.parent_id].push_back(unit.id);
            }
        } catch (const std::exception& error) {
            issues.push_back(Issue("COMMAND_ORG_ORGANIZATION_INVALID", std::string("编制单位非法: ") + error.what()));
        }
    }

    // 全部条目入树后统一接线（JSON 声明顺序无关，与 T025 from_json 同构）。
    for (const ParsedOrganization& parsed : parsed_units) {
        if (!parsed.valid) {
            continue;
        }
        model::OrganizationUnit detached = parsed.unit;
        detached.parent_id.clear();
        detached.subordinate_ids.clear();
        if (!tree.AddUnit(detached)) {
            issues.push_back(Issue("COMMAND_ORG_ORGANIZATION_INVALID", "编制单位入树失败: " + parsed.unit.id));
        }
    }
    for (const auto& [unit_id, parent_id] : parent_links) {
        if (!tree.SetParent(unit_id, parent_id)) {
            issues.push_back(Issue("COMMAND_ORG_ORGANIZATION_LINK_INVALID",
                                   "编制单位父级/类型/成环校验失败: " + unit_id + " -> " + parent_id));
        }
    }
    // 互反一致性：声明的 subordinate_ids 必须与 parent_id 推导一致（FR-013）。
    for (const ParsedOrganization& parsed : parsed_units) {
        if (!parsed.valid) {
            continue;
        }
        std::vector<std::string> expected = declared_subordinates[parsed.unit.id];
        std::sort(expected.begin(), expected.end());
        std::vector<std::string> declared = parsed.unit.subordinate_ids;
        std::sort(declared.begin(), declared.end());
        if (expected != declared) {
            issues.push_back(Issue("COMMAND_ORG_ORGANIZATION_MISMATCH",
                                   "编制单位 parent/subordinate 互反不一致: " + parsed.unit.id));
        }
    }
    return issues.empty();
}

// 编制子树内的班编制单位计数（subordinate_ids 声明顺序递归）。
std::size_t CountSquadsInOrgSubtree(const model::OrganizationTree& organizations, const std::string& org_unit_id) {
    const model::OrganizationUnit* unit = organizations.Find(org_unit_id);
    if (unit == nullptr) {
        return 0U;
    }
    std::size_t count = unit->echelon == model::Echelon::kSquad ? 1U : 0U;
    for (const std::string& child_id : unit->subordinate_ids) {
        count += CountSquadsInOrgSubtree(organizations, child_id);
    }
    return count;
}

}  // namespace

bool CommandOrgState::is_command_node(const std::string& node_id) const {
    return command_tree.Contains(node_id);
}

const model::CommandNode* CommandOrgState::find_node(const std::string& node_id) const {
    return command_tree.Find(node_id);
}

std::vector<std::string> CommandOrgState::children_of(const std::string& node_id) const {
    return command_tree.ChildrenOf(node_id);
}

std::vector<std::string> CommandOrgState::nodes_in_insertion_order() const {
    const std::vector<model::CommandNode> nodes = command_tree.NodesInInsertionOrder();
    std::vector<std::string> ids;
    ids.reserve(nodes.size());
    for (const model::CommandNode& node : nodes) {
        ids.push_back(node.id);
    }
    return ids;
}

std::size_t CommandOrgState::direct_squad_base(const std::string& node_id) const {
    const auto iterator = node_org_unit.find(node_id);
    if (iterator == node_org_unit.end() || iterator->second.empty()) {
        return 0U;
    }
    return CountSquadsInOrgSubtree(organizations, iterator->second);
}

model::Echelon command_echelon(const CommandOrgState& org, const std::string& node_id) noexcept {
    const auto iterator = org.node_echelon.find(node_id);
    return iterator == org.node_echelon.end() ? model::Echelon::kPlatoon : iterator->second;
}

std::size_t direct_squad_base(const CommandOrgState& org, const std::string& node_id) {
    return org.direct_squad_base(node_id);
}

void validate_command_org(const nlohmann::json& raw, std::vector<DataIssue>& issues) {
    if (!raw.contains("command_org")) {
        return;
    }
    const nlohmann::json& org = raw["command_org"];
    if (!org.is_object() || !org.contains("nodes") || !org["nodes"].is_array() || !org.contains("organizations") ||
        !org["organizations"].is_array()) {
        issues.push_back(Issue("COMMAND_ORG_INVALID", "command_org 必须携带 nodes 与 organizations 数组"));
        return;
    }

    // 1) 节点解析与 id 唯一（固定数组顺序）。
    std::vector<ParsedNode> parsed_nodes;
    std::set<std::string> node_ids;
    std::map<std::string, std::string> parent_links;
    for (const nlohmann::json& node_json : org["nodes"]) {
        try {
            ParsedNode parsed = ParseNode(node_json);
            if (!node_ids.insert(parsed.node.id).second) {
                issues.push_back(Issue("COMMAND_ORG_NODE_DUPLICATE_ID", "指挥节点 id 重复: " + parsed.node.id));
                continue;
            }
            if (!parsed.node.parent_id.empty()) {
                parent_links.emplace(parsed.node.id, parsed.node.parent_id);
            }
            parsed_nodes.push_back(std::move(parsed));
        } catch (const std::exception& error) {
            issues.push_back(Issue("COMMAND_ORG_NODE_INVALID", std::string("指挥节点非法: ") + error.what()));
        }
    }

    // 2) 父级存在性、层级链与成环（固定数组顺序）。
    for (const ParsedNode& parsed : parsed_nodes) {
        if (!parsed.valid) {
            continue;
        }
        const std::string& parent_id = parsed.node.parent_id;
        if (parent_id.empty()) {
            continue;
        }
        if (parent_id == parsed.node.id || !node_ids.contains(parent_id)) {
            issues.push_back(Issue("COMMAND_ORG_NODE_PARENT_UNKNOWN",
                                   "指挥节点父级不存在或自引用: " + parsed.node.id + " -> " + parent_id));
            continue;
        }
        const auto parent_iterator = std::find_if(
            parsed_nodes.begin(), parsed_nodes.end(),
            [&](const ParsedNode& candidate) { return candidate.valid && candidate.node.id == parent_id; });
        if (parent_iterator != parsed_nodes.end() && parsed.echelon >= parent_iterator->echelon) {
            issues.push_back(Issue("COMMAND_ORG_NODE_ECHELON_CHAIN",
                                   "指挥层级链非法（子级必须低于父级）: " + parsed.node.id + " -> " + parent_id));
        }
    }
    for (const ParsedNode& parsed : parsed_nodes) {
        if (parsed.valid && HasParentCycle(parent_links, parsed.node.id)) {
            issues.push_back(Issue("COMMAND_ORG_NODE_CYCLE", "指挥树存在环: " + parsed.node.id));
        }
    }

    // 3) 编制树解析与约束（互反一致/类型/无环）。
    model::OrganizationTree organizations;
    BuildOrganizationTree(org["organizations"], issues, organizations);

    // 4) 节点 → 编制单位映射（存在性 + echelon 一致）。
    for (const ParsedNode& parsed : parsed_nodes) {
        if (!parsed.valid) {
            continue;
        }
        if (parsed.org_unit_id.empty()) {
            continue;  // 未声明编制锚点：直属班基数为 0（合法，如纯 HQ 节点）。
        }
        const model::OrganizationUnit* unit = organizations.Find(parsed.org_unit_id);
        if (unit == nullptr) {
            issues.push_back(Issue("COMMAND_ORG_NODE_ORG_UNIT_UNKNOWN",
                                   "指挥节点引用不存在的编制单位: " + parsed.node.id + " -> " + parsed.org_unit_id));
            continue;
        }
        if (unit->echelon != parsed.echelon) {
            issues.push_back(
                Issue("COMMAND_ORG_NODE_ORG_ECHELON_MISMATCH",
                      "指挥节点层级与编制单位层级不一致: " + parsed.node.id + " -> " + parsed.org_unit_id));
        }
    }

    // 5) 场景单位 node_id 必须命中指挥节点（FR-048：每单位归属一个指挥节点）。
    for (const nlohmann::json& unit : raw.value("units", nlohmann::json::array())) {
        const std::string unit_id = unit.value("id", std::string());
        const std::string node_id = unit.value("node_id", std::string());
        if (!node_id.empty() && !node_ids.contains(node_id)) {
            issues.push_back(
                Issue("COMMAND_ORG_UNIT_NODE_UNKNOWN", "场景单位 " + unit_id + " 归属的指挥节点不存在: " + node_id));
        }
    }

    // 6) 玩家扮演节点必须是声明的指挥节点。
    const std::string player_node_id = raw.value("player_node_id", std::string());
    if (!player_node_id.empty() && !node_ids.contains(player_node_id)) {
        issues.push_back(Issue("COMMAND_ORG_PLAYER_NODE_MISSING", "player_node_id 不在指挥节点中: " + player_node_id));
    }
}

CommandOrgLoadResult load_command_org(const nlohmann::json& raw) {
    CommandOrgLoadResult result;
    validate_command_org(raw, result.issues);
    if (!result.ok()) {
        return result;
    }
    if (!raw.contains("command_org")) {
        return result;  // 未配置：保持 configured=false。
    }
    const nlohmann::json& org = raw["command_org"];
    try {
        result.state.command_tree = nlohmann::json{{"nodes", org["nodes"]}}.get<model::CommandTree>();
        result.state.organizations = nlohmann::json{{"units", org["organizations"]}}.get<model::OrganizationTree>();
    } catch (const std::exception& error) {
        throw std::invalid_argument(std::string("command_org 构建失败: ") + error.what());
    }
    for (const nlohmann::json& node_json : org["nodes"]) {
        const std::string node_id = node_json.at("id").get<std::string>();
        result.state.node_echelon[node_id] = model::echelon_from_string(node_json.at("echelon").get<std::string>());
        result.state.node_org_unit[node_id] = node_json.value("org_unit_id", std::string());
    }
    result.state.configured = true;
    return result;
}

void to_json(nlohmann::json& json, const CommandOrgState& state) {
    nlohmann::json nodes = nlohmann::json::array();
    for (const model::CommandNode& node : state.command_tree.NodesInInsertionOrder()) {
        nodes.push_back(nlohmann::json{
            {"node", node},
            {"echelon",
             state.node_echelon.contains(node.id) ? state.node_echelon.at(node.id) : model::Echelon::kPlatoon},
            {"org_unit_id", state.node_org_unit.contains(node.id) ? state.node_org_unit.at(node.id) : std::string()}});
    }
    json = nlohmann::json{
        {"configured", state.configured}, {"nodes", std::move(nodes)}, {"organizations", state.organizations}};
}

void from_json(const nlohmann::json& json, CommandOrgState& state) {
    state = CommandOrgState{};
    state.configured = json.value("configured", false);
    for (const nlohmann::json& node_json : json.value("nodes", nlohmann::json::array())) {
        const model::CommandNode node = node_json.at("node").get<model::CommandNode>();
        const model::Echelon echelon = node_json.value("echelon", model::Echelon::kPlatoon);
        const std::string org_unit_id = node_json.value("org_unit_id", std::string());
        if (!state.command_tree.AddNode(node)) {
            throw std::invalid_argument("指挥组织存档包含重复 id/未知父节点: " + node.id);
        }
        state.node_echelon[node.id] = echelon;
        state.node_org_unit[node.id] = org_unit_id;
    }
    state.organizations = json.at("organizations").get<model::OrganizationTree>();
}

}  // namespace wfs::sim
