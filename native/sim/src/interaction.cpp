// sim/src/interaction.cpp
//
// T051：四种上下级交互类型约束实现。
//
// 实现策略：
// - 解析强制枚举：interaction_type 必须是 TASK_DISPATCH/EXECUTION/
//   SUMMARY_REPORT/SUPPORT_REQUEST 之一（FR-050），未知/缺失显式报错；
//   错误按"类型 → 收发节点"固定顺序收集（宪法第 7/17 条）。
// - 同级经上级转发（FR-028/050）：同层级节点不直接横向通信，路由经最近
//   共同上级；跨层级直接送达；路由为确定性纯函数（沿父链计算深度与 LCA）。

#include "wfs/sim/interaction.h"

#include <cstdint>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace wfs::sim {

namespace {

std::size_t NodeDepth(const model::CommandTree& tree, const std::string& node_id) {
    std::size_t depth = 0U;
    std::string current = node_id;
    while (!current.empty()) {
        const model::CommandNode* node = tree.Find(current);
        if (node == nullptr) {
            break;
        }
        current = node->parent_id;
        if (!current.empty()) {
            ++depth;
        }
    }
    return depth;
}

// 最近共同上级；无共同上级返回空串。
std::string LowestCommonAncestor(const model::CommandTree& tree, const std::string& first,
                                 const std::string& second) {
    std::set<std::string> ancestors;
    std::string current = first;
    while (!current.empty()) {
        ancestors.insert(current);
        const model::CommandNode* node = tree.Find(current);
        current = node == nullptr ? "" : node->parent_id;
    }
    current = second;
    while (!current.empty()) {
        if (ancestors.contains(current)) {
            return current;
        }
        const model::CommandNode* node = tree.Find(current);
        current = node == nullptr ? "" : node->parent_id;
    }
    return {};
}

}  // namespace

std::string_view to_string(const InteractionType type) noexcept {
    switch (type) {
        case InteractionType::kTaskDispatch:
            return "TASK_DISPATCH";
        case InteractionType::kExecution:
            return "EXECUTION";
        case InteractionType::kSummaryReport:
            return "SUMMARY_REPORT";
        case InteractionType::kSupportRequest:
            return "SUPPORT_REQUEST";
    }
    return "UNKNOWN";
}

InteractionType interaction_type_from_string(const std::string_view name) {
    if (name == "TASK_DISPATCH") {
        return InteractionType::kTaskDispatch;
    }
    if (name == "EXECUTION") {
        return InteractionType::kExecution;
    }
    if (name == "SUMMARY_REPORT") {
        return InteractionType::kSummaryReport;
    }
    if (name == "SUPPORT_REQUEST") {
        return InteractionType::kSupportRequest;
    }
    throw std::invalid_argument("未知交互类型: " + std::string(name));
}

InteractionParseResult parse_interaction(const nlohmann::json& json) {
    InteractionParseResult result;
    if (!json.is_object()) {
        result.errors.push_back("交互 JSON 必须是对象");
        return result;
    }
    if (!json.contains("interaction_type")) {
        result.errors.push_back("缺少 interaction_type（四种交互类型强制枚举，command-schema §5）");
    } else if (!json["interaction_type"].is_string()) {
        result.errors.push_back("interaction_type 必须是字符串");
    } else {
        try {
            result.record.type = interaction_type_from_string(json["interaction_type"].get<std::string>());
        } catch (const std::invalid_argument& error) {
            result.errors.push_back(error.what());
        }
    }
    if (!json.contains("from_node") || !json["from_node"].is_string() ||
        json["from_node"].get<std::string>().empty()) {
        result.errors.push_back("缺少非空 from_node");
    } else {
        result.record.from_node = json["from_node"].get<std::string>();
    }
    if (!json.contains("to_node") || !json["to_node"].is_string() || json["to_node"].get<std::string>().empty()) {
        result.errors.push_back("缺少非空 to_node");
    } else {
        result.record.to_node = json["to_node"].get<std::string>();
    }
    result.record.payload = json.value("payload", nlohmann::json::object());
    return result;
}

InteractionRouting route_interaction(const model::CommandTree& tree, const std::string& sender,
                                     const std::string& receiver) {
    InteractionRouting routing;
    if (!tree.Contains(sender)) {
        routing.error = "发送方节点不存在: " + sender;
        return routing;
    }
    if (!tree.Contains(receiver)) {
        routing.error = "接收方节点不存在: " + receiver;
        return routing;
    }
    if (sender == receiver) {
        routing.error = "交互收发双方不能是同一节点";
        return routing;
    }

    const std::size_t sender_depth = NodeDepth(tree, sender);
    const std::size_t receiver_depth = NodeDepth(tree, receiver);
    if (sender_depth != receiver_depth) {
        // 跨层级：直接送达（上级↔下级）。
        routing.hops = {sender, receiver};
        return routing;
    }

    // 同层级：经最近共同上级转发（FR-028/050，同级不直接横向通信）。
    const std::string superior = LowestCommonAncestor(tree, sender, receiver);
    if (superior.empty() || superior == sender || superior == receiver) {
        routing.error = "同层级节点无共同上级可经转发";
        return routing;
    }
    routing.hops = {sender, superior, receiver};
    return routing;
}

void to_json(nlohmann::json& json, const InteractionRecord& record) {
    json = nlohmann::json{{"interaction_type", to_string(record.type)},
                          {"from_node", record.from_node},
                          {"to_node", record.to_node},
                          {"payload", record.payload}};
}

void from_json(const nlohmann::json& json, InteractionRecord& record) {
    const InteractionParseResult parsed = parse_interaction(json);
    if (!parsed.ok()) {
        throw std::invalid_argument(parsed.errors.front());
    }
    record = parsed.record;
}

}  // namespace wfs::sim
