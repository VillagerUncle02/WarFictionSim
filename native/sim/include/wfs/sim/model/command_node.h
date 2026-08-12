// sim/include/wfs/sim/model/command_node.h
//
// T025：指挥节点模型（CommandNode/NodeOwner/Experience/CommandTree）。
//
// 设计契约（data-model.md §1；FR-015/048）：
// - 每个指挥节点恰好由一个归属方接管：玩家节点（kPlayer）或 AI 指挥官
//   （kAi，ai_id 必填）；两者互斥且玩家节点不允许残留 AI 归属，保证
//   "每节点单一 AI 归属"（FR-048）在类型层面可表达、可校验。
// - 指挥树以 parent_id 表达层级：AddNode/SetParent 强制 单父、无环、id 唯一，
//   失败返回 false 且保持树不变（宪法第 17 条：显式报错，不静默吞错）。
// - command_limit/coordination/experience 是 FR-015 软性降级的数据基础：
//   本模型只负责统计与暴露超限判定，具体降级曲线由后续任务（T029+）实现。
// - 节点顺序按插入顺序保存（NodesInInsertionOrder），JSON 序列化与遍历
//   顺序确定（宪法第 7 条）。
// - 所有值类型支持 nlohmann::json 往返序列化（存档/快照复用，宪法第 13 条）。

#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace wfs::sim::model {

// 指挥节点归属类型：玩家接管或 AI 指挥官（FR-048）。
enum class OwnerKind : std::uint8_t {
    kPlayer = 0,  // 玩家扮演节点。
    kAi = 1,      // AI 指挥官节点。
};

std::string_view to_string(OwnerKind kind) noexcept;
OwnerKind owner_kind_from_string(std::string_view name);
void to_json(nlohmann::json& json, OwnerKind kind);
void from_json(const nlohmann::json& json, OwnerKind& kind);

// 节点归属：kind == kAi 时必须携带非空 ai_id；kPlayer 时 ai_id 必须为空。
struct NodeOwner {
    OwnerKind kind = OwnerKind::kAi;
    std::string ai_id;

    bool is_player() const noexcept { return kind == OwnerKind::kPlayer; }
    bool is_ai() const noexcept { return kind == OwnerKind::kAi; }
    // 单一归属约束（FR-048）：AI 节点必须有唯一 AI id，玩家节点不得挂 AI。
    bool is_valid() const noexcept { return is_ai() ? !ai_id.empty() : ai_id.empty(); }

    bool operator==(const NodeOwner&) const = default;
};

void to_json(nlohmann::json& json, const NodeOwner& owner);
void from_json(const nlohmann::json& json, NodeOwner& owner);

// 经验三维（data-model.md §1/§5）：受训程度/服役时长/战斗经验，值域 [0,1]。
struct Experience {
    double training = 0.0;
    double service = 0.0;
    double combat = 0.0;

    bool is_valid() const noexcept {
        return training >= 0.0 && training <= 1.0 && service >= 0.0 && service <= 1.0 && combat >= 0.0 && combat <= 1.0;
    }

    bool operator==(const Experience&) const = default;
};

void to_json(nlohmann::json& json, const Experience& experience);
void from_json(const nlohmann::json& json, Experience& experience);

// 指挥节点：归属 + 指挥上限 + 协调能力 + 经验三维。
struct CommandNode {
    std::string id;
    std::string name;
    std::string parent_id;  // 空 = 根节点。
    NodeOwner owner;
    std::uint32_t command_limit = 0U;  // 直接下级数量上限（FR-015）。
    double coordination = 0.0;         // 协调能力 [0,1]。
    Experience experience;

    bool is_root() const noexcept { return parent_id.empty(); }

    bool is_valid() const noexcept {
        return owner.is_valid() && coordination >= 0.0 && coordination <= 1.0 && experience.is_valid();
    }

    bool operator==(const CommandNode&) const = default;
};

void to_json(nlohmann::json& json, const CommandNode& node);
void from_json(const nlohmann::json& json, CommandNode& node);

// 指挥树：约束单父/无环/id 唯一；按插入顺序保存以保证确定性。
class CommandTree {
   public:
    // 添加节点：id 必须唯一；parent_id 非空时必须已存在且不是自身。
    // 失败返回 false 且树保持不变。
    bool AddNode(CommandNode node);
    // 移动节点到新父节点：校验 存在性/自环/祖先成环。parent_id 空 = 根。
    bool SetParent(const std::string& node_id, const std::string& parent_id);

    const CommandNode* Find(const std::string& id) const;
    bool Contains(const std::string& id) const;
    std::size_t size() const noexcept { return nodes_.size(); }
    bool empty() const noexcept { return nodes_.empty(); }

    // 按插入顺序返回全部节点（JSON 序列化与遍历的权威顺序）。
    std::vector<CommandNode> NodesInInsertionOrder() const { return nodes_; }
    // 直接下级 id，按插入顺序。
    std::vector<std::string> ChildrenOf(const std::string& id) const;
    // 直接下级数量（协调能力按下级单位数计算，data-model.md §2）。
    std::size_t DirectSubordinateCount(const std::string& id) const;
    // 超过指挥上限 → 软性降级入口（FR-015；未知 id 返回 false）。
    bool IsOverLimit(const std::string& id) const;

    void Clear() noexcept { nodes_.clear(); }

   private:
    CommandNode* FindMutable(const std::string& id);
    // candidate 是否位于 ancestor 的子树内（用于成环检测）。
    bool IsDescendantOf(const std::string& candidate, const std::string& ancestor) const;

    std::vector<CommandNode> nodes_;
};

void to_json(nlohmann::json& json, const CommandTree& tree);
// 反序列化失败（重复 id/未知父节点/父节点缺失）抛 std::invalid_argument。
void from_json(const nlohmann::json& json, CommandTree& tree);

// ---- 内联实现（头文件契约 + 值语义，保持 T025 自包含）----

inline std::string_view to_string(OwnerKind kind) noexcept {
    switch (kind) {
        case OwnerKind::kPlayer:
            return "player";
        case OwnerKind::kAi:
            return "ai";
    }
    return "unknown";
}

inline OwnerKind owner_kind_from_string(const std::string_view name) {
    if (name == "player") {
        return OwnerKind::kPlayer;
    }
    if (name == "ai") {
        return OwnerKind::kAi;
    }
    throw std::invalid_argument("未知指挥节点归属类型: " + std::string(name));
}

inline void to_json(nlohmann::json& json, const OwnerKind kind) {
    json = to_string(kind);
}

inline void from_json(const nlohmann::json& json, OwnerKind& kind) {
    kind = owner_kind_from_string(json.get<std::string>());
}

inline void to_json(nlohmann::json& json, const NodeOwner& owner) {
    json = nlohmann::json{{"kind", owner.kind}, {"ai_id", owner.ai_id}};
}

inline void from_json(const nlohmann::json& json, NodeOwner& owner) {
    owner.kind = json.at("kind").get<OwnerKind>();
    owner.ai_id = json.at("ai_id").get<std::string>();
    if (!owner.is_valid()) {
        throw std::invalid_argument("节点归属违反单一 AI 归属约束（FR-048）");
    }
}

inline void to_json(nlohmann::json& json, const Experience& experience) {
    json = nlohmann::json{
        {"training", experience.training}, {"service", experience.service}, {"combat", experience.combat}};
}

inline void from_json(const nlohmann::json& json, Experience& experience) {
    experience.training = json.at("training").get<double>();
    experience.service = json.at("service").get<double>();
    experience.combat = json.at("combat").get<double>();
    if (!experience.is_valid()) {
        throw std::invalid_argument("经验三维取值必须在 [0,1] 内");
    }
}

inline void to_json(nlohmann::json& json, const CommandNode& node) {
    json = nlohmann::json{{"id", node.id},
                          {"name", node.name},
                          {"parent_id", node.parent_id},
                          {"owner", node.owner},
                          {"command_limit", node.command_limit},
                          {"coordination", node.coordination},
                          {"experience", node.experience}};
}

inline void from_json(const nlohmann::json& json, CommandNode& node) {
    node.id = json.at("id").get<std::string>();
    node.name = json.at("name").get<std::string>();
    node.parent_id = json.at("parent_id").get<std::string>();
    node.owner = json.at("owner").get<NodeOwner>();
    node.command_limit = json.at("command_limit").get<std::uint32_t>();
    node.coordination = json.at("coordination").get<double>();
    node.experience = json.at("experience").get<Experience>();
    if (!node.is_valid()) {
        throw std::invalid_argument("指挥节点数值越界或归属非法: " + node.id);
    }
}

inline CommandNode* CommandTree::FindMutable(const std::string& id) {
    for (CommandNode& node : nodes_) {
        if (node.id == id) {
            return &node;
        }
    }
    return nullptr;
}

inline const CommandNode* CommandTree::Find(const std::string& id) const {
    for (const CommandNode& node : nodes_) {
        if (node.id == id) {
            return &node;
        }
    }
    return nullptr;
}

inline bool CommandTree::Contains(const std::string& id) const {
    return Find(id) != nullptr;
}

inline bool CommandTree::IsDescendantOf(const std::string& candidate, const std::string& ancestor) const {
    const CommandNode* current = Find(candidate);
    while (current != nullptr && !current->parent_id.empty()) {
        if (current->parent_id == ancestor) {
            return true;
        }
        current = Find(current->parent_id);
    }
    return false;
}

inline bool CommandTree::AddNode(CommandNode node) {
    if (Contains(node.id)) {
        return false;
    }
    if (!node.parent_id.empty()) {
        if (node.parent_id == node.id || !Contains(node.parent_id)) {
            return false;
        }
    }
    nodes_.push_back(std::move(node));
    return true;
}

inline bool CommandTree::SetParent(const std::string& node_id, const std::string& parent_id) {
    CommandNode* node = FindMutable(node_id);
    if (node == nullptr) {
        return false;
    }
    if (!parent_id.empty()) {
        if (parent_id == node_id || !Contains(parent_id)) {
            return false;
        }
        if (IsDescendantOf(parent_id, node_id)) {
            return false;  // 父节点位于本节点子树内 → 成环。
        }
    }
    node->parent_id = parent_id;
    return true;
}

inline std::vector<std::string> CommandTree::ChildrenOf(const std::string& id) const {
    std::vector<std::string> children;
    for (const CommandNode& node : nodes_) {
        if (node.parent_id == id) {
            children.push_back(node.id);
        }
    }
    return children;
}

inline std::size_t CommandTree::DirectSubordinateCount(const std::string& id) const {
    return ChildrenOf(id).size();
}

inline bool CommandTree::IsOverLimit(const std::string& id) const {
    const CommandNode* node = Find(id);
    return node != nullptr && DirectSubordinateCount(id) > node->command_limit;
}

inline void to_json(nlohmann::json& json, const CommandTree& tree) {
    json = nlohmann::json{{"nodes", tree.NodesInInsertionOrder()}};
}

inline void from_json(const nlohmann::json& json, CommandTree& tree) {
    CommandTree candidate;
    for (const nlohmann::json& node_json : json.at("nodes")) {
        const CommandNode node = node_json.get<CommandNode>();
        if (!candidate.AddNode(node)) {
            throw std::invalid_argument("指挥树包含重复 id/未知父节点: " + node.id);
        }
    }
    tree = std::move(candidate);
}

}  // namespace wfs::sim::model
