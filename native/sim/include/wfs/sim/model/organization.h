// sim/include/wfs/sim/model/organization.h
//
// T025：编制单位模型（OrganizationUnit）。
//
// 设计契约（data-model.md §2）：
// - 编制层级固定为 班 → 排 → 连 → 营 → 旅（可扩展枚举按字符串序列化）。
// - 类型约束由 CanContain 表达：步兵排仅步兵班；装甲排仅装甲单元；合成排
//   可混成但要求步兵班兵员能力不低于 minimum_soldier_capability；连/营/旅
//   递归包含下一级，且非合成编制保持同类型。
// - coordination 是"按直接下级编制单位数计算"的协调能力载体（data-model
//   §2），数值计算与降级由 T029+ 实现，本模型只承载字段。
// - 值类型支持 nlohmann::json 往返序列化（宪法第 13 条：存档完整可序列化）。

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace wfs::sim::model {

// 编制层级（data-model.md §2：班 → 排 → 连 → 营 → 旅）。
enum class Echelon : std::uint8_t {
    kSquad = 0,
    kPlatoon = 1,
    kCompany = 2,
    kBattalion = 3,
    kBrigade = 4,
};

// 编制类型：决定 CanContain 的同类型约束与合成排的最低能力要求。
enum class OrganizationKind : std::uint8_t {
    kInfantry = 0,
    kArmored = 1,
    kCombined = 2,  // 合成：可混成，要求最低兵员能力。
    kSupport = 3,   // 支援类（工兵/医疗/后勤等）。
};

std::string_view to_string(Echelon echelon) noexcept;
Echelon echelon_from_string(std::string_view name);
std::string_view to_string(OrganizationKind kind) noexcept;
OrganizationKind organization_kind_from_string(std::string_view name);

void to_json(nlohmann::json& json, Echelon echelon);
void from_json(const nlohmann::json& json, Echelon& echelon);
void to_json(nlohmann::json& json, OrganizationKind kind);
void from_json(const nlohmann::json& json, OrganizationKind& kind);

// 编制单位：层级/类型/上级/兵员能力/协调能力/直接下级清单。
struct OrganizationUnit {
    std::string id;
    std::string name;
    Echelon echelon = Echelon::kSquad;
    OrganizationKind kind = OrganizationKind::kInfantry;
    std::string parent_id;                     // 编制树上级（空 = 顶层）。
    double soldier_capability = 0.0;           // 步兵班兵员能力 [0,1]。
    double minimum_soldier_capability = 0.0;   // 合成排要求的最低兵员能力。
    double coordination = 0.0;                 // 协调能力（按直接下级单位数）。
    std::vector<std::string> subordinate_ids;  // 直接下级编制 id。

    // 类型约束（data-model.md §2）；非法组合返回 false。
    bool CanContain(const OrganizationUnit& child) const noexcept;

    // 数值域校验（F7）：能力/协调 [0,1]；subordinate_ids 无重复、无自引用。
    bool is_valid() const noexcept {
        if (!std::isfinite(soldier_capability) || soldier_capability < 0.0 || soldier_capability > 1.0 ||
            !std::isfinite(minimum_soldier_capability) || minimum_soldier_capability < 0.0 ||
            minimum_soldier_capability > 1.0 || !std::isfinite(coordination) || coordination < 0.0 ||
            coordination > 1.0) {
            return false;
        }
        for (std::size_t i = 0; i < subordinate_ids.size(); ++i) {
            if (subordinate_ids[i] == id) {
                return false;
            }
            for (std::size_t j = i + 1; j < subordinate_ids.size(); ++j) {
                if (subordinate_ids[i] == subordinate_ids[j]) {
                    return false;
                }
            }
        }
        return true;
    }

    bool operator==(const OrganizationUnit&) const = default;
};

void to_json(nlohmann::json& json, const OrganizationUnit& unit);
void from_json(const nlohmann::json& json, OrganizationUnit& unit);

// ---- 内联实现 ----

inline std::string_view to_string(const Echelon echelon) noexcept {
    switch (echelon) {
        case Echelon::kSquad:
            return "squad";
        case Echelon::kPlatoon:
            return "platoon";
        case Echelon::kCompany:
            return "company";
        case Echelon::kBattalion:
            return "battalion";
        case Echelon::kBrigade:
            return "brigade";
    }
    return "unknown";
}

inline Echelon echelon_from_string(const std::string_view name) {
    if (name == "squad") {
        return Echelon::kSquad;
    }
    if (name == "platoon") {
        return Echelon::kPlatoon;
    }
    if (name == "company") {
        return Echelon::kCompany;
    }
    if (name == "battalion") {
        return Echelon::kBattalion;
    }
    if (name == "brigade") {
        return Echelon::kBrigade;
    }
    throw std::invalid_argument("未知编制层级: " + std::string(name));
}

inline std::string_view to_string(const OrganizationKind kind) noexcept {
    switch (kind) {
        case OrganizationKind::kInfantry:
            return "infantry";
        case OrganizationKind::kArmored:
            return "armored";
        case OrganizationKind::kCombined:
            return "combined";
        case OrganizationKind::kSupport:
            return "support";
    }
    return "unknown";
}

inline OrganizationKind organization_kind_from_string(const std::string_view name) {
    if (name == "infantry") {
        return OrganizationKind::kInfantry;
    }
    if (name == "armored") {
        return OrganizationKind::kArmored;
    }
    if (name == "combined") {
        return OrganizationKind::kCombined;
    }
    if (name == "support") {
        return OrganizationKind::kSupport;
    }
    throw std::invalid_argument("未知编制类型: " + std::string(name));
}

inline void to_json(nlohmann::json& json, const Echelon echelon) {
    json = to_string(echelon);
}

inline void from_json(const nlohmann::json& json, Echelon& echelon) {
    echelon = echelon_from_string(json.get<std::string>());
}

inline void to_json(nlohmann::json& json, const OrganizationKind kind) {
    json = to_string(kind);
}

inline void from_json(const nlohmann::json& json, OrganizationKind& kind) {
    kind = organization_kind_from_string(json.get<std::string>());
}

inline bool OrganizationUnit::CanContain(const OrganizationUnit& child) const noexcept {
    if (echelon == Echelon::kSquad || child.echelon == echelon) {
        return false;  // 班不可包含；同级不可互相包含。
    }
    const bool same_kind = kind == OrganizationKind::kCombined || child.kind == kind;
    switch (echelon) {
        case Echelon::kPlatoon:
            if (child.echelon != Echelon::kSquad) {
                return false;
            }
            if (kind == OrganizationKind::kInfantry) {
                return child.kind == OrganizationKind::kInfantry;
            }
            if (kind == OrganizationKind::kArmored) {
                return child.kind == OrganizationKind::kArmored;
            }
            if (kind == OrganizationKind::kCombined) {
                // 合成排可混成；步兵班必须达到最低兵员能力（data-model §2）。
                if (child.kind == OrganizationKind::kInfantry) {
                    return child.soldier_capability >= minimum_soldier_capability;
                }
                return child.kind == OrganizationKind::kArmored || child.kind == OrganizationKind::kSupport;
            }
            return same_kind;  // 支援排。
        case Echelon::kCompany:
            return child.echelon == Echelon::kPlatoon && same_kind;
        case Echelon::kBattalion:
            return child.echelon == Echelon::kCompany && same_kind;
        case Echelon::kBrigade:
            return child.echelon == Echelon::kBattalion && same_kind;
        case Echelon::kSquad:
            return false;
    }
    return false;
}

inline void to_json(nlohmann::json& json, const OrganizationUnit& unit) {
    json = nlohmann::json{{"id", unit.id},
                          {"name", unit.name},
                          {"echelon", unit.echelon},
                          {"kind", unit.kind},
                          {"parent_id", unit.parent_id},
                          {"soldier_capability", unit.soldier_capability},
                          {"minimum_soldier_capability", unit.minimum_soldier_capability},
                          {"coordination", unit.coordination},
                          {"subordinate_ids", unit.subordinate_ids}};
}

inline void from_json(const nlohmann::json& json, OrganizationUnit& unit) {
    unit.id = json.at("id").get<std::string>();
    unit.name = json.at("name").get<std::string>();
    unit.echelon = json.at("echelon").get<Echelon>();
    unit.kind = json.at("kind").get<OrganizationKind>();
    unit.parent_id = json.at("parent_id").get<std::string>();
    unit.soldier_capability = json.at("soldier_capability").get<double>();
    unit.minimum_soldier_capability = json.at("minimum_soldier_capability").get<double>();
    unit.coordination = json.at("coordination").get<double>();
    unit.subordinate_ids = json.at("subordinate_ids").get<std::vector<std::string>>();
    if (!unit.is_valid()) {
        throw std::invalid_argument("编制单位数值越界或下级引用重复: " + unit.id);
    }
}

// 编制树（F6）：维护 parent_id 与 subordinate_ids 互反一致，校验引用存在、
// 无环与 CanContain 类型约束（参照 CommandTree 的约束方式）。
class OrganizationTree {
   public:
    // 添加节点：id 唯一；parent_id 非空时必须已存在且满足 CanContain，
    // 成功后自动互反接线（把本节点加入父节点 subordinate_ids）。
    bool AddUnit(OrganizationUnit unit);
    // 把 unit_id 挂到 parent_id 下（parent_id 空 = 根）；失败返回 false 且树不变。
    bool SetParent(const std::string& unit_id, const std::string& parent_id);
    // 语义同 SetParent(child_id, parent_id)：显式表达"为 parent 增加下级"。
    bool AddSubordinate(const std::string& parent_id, const std::string& child_id);

    const OrganizationUnit* Find(const std::string& id) const;
    bool Contains(const std::string& id) const;
    std::size_t size() const noexcept { return units_.size(); }
    bool empty() const noexcept { return units_.empty(); }

    std::vector<OrganizationUnit> UnitsInInsertionOrder() const { return units_; }
    std::vector<std::string> SubordinatesOf(const std::string& id) const;
    void Clear() noexcept { units_.clear(); }

   private:
    OrganizationUnit* FindMutable(const std::string& id);
    bool IsDescendantOf(const std::string& candidate, const std::string& ancestor) const;
    void Attach(OrganizationUnit& child, OrganizationUnit& parent);
    void Detach(OrganizationUnit& child);

    std::vector<OrganizationUnit> units_;
};

void to_json(nlohmann::json& json, const OrganizationTree& tree);
// 反序列化失败（重复 id/未知父节点/类型不匹配/互反不一致）抛 std::invalid_argument。
void from_json(const nlohmann::json& json, OrganizationTree& tree);

inline OrganizationUnit* OrganizationTree::FindMutable(const std::string& id) {
    for (OrganizationUnit& unit : units_) {
        if (unit.id == id) {
            return &unit;
        }
    }
    return nullptr;
}

inline const OrganizationUnit* OrganizationTree::Find(const std::string& id) const {
    for (const OrganizationUnit& unit : units_) {
        if (unit.id == id) {
            return &unit;
        }
    }
    return nullptr;
}

inline bool OrganizationTree::Contains(const std::string& id) const {
    return Find(id) != nullptr;
}

inline bool OrganizationTree::IsDescendantOf(const std::string& candidate, const std::string& ancestor) const {
    const OrganizationUnit* current = Find(candidate);
    while (current != nullptr && !current->parent_id.empty()) {
        if (current->parent_id == ancestor) {
            return true;
        }
        current = Find(current->parent_id);
    }
    return false;
}

inline void OrganizationTree::Attach(OrganizationUnit& child, OrganizationUnit& parent) {
    child.parent_id = parent.id;
    if (std::find(parent.subordinate_ids.begin(), parent.subordinate_ids.end(), child.id) ==
        parent.subordinate_ids.end()) {
        parent.subordinate_ids.push_back(child.id);
    }
}

inline void OrganizationTree::Detach(OrganizationUnit& child) {
    if (child.parent_id.empty()) {
        return;
    }
    if (OrganizationUnit* old_parent = FindMutable(child.parent_id)) {
        std::erase(old_parent->subordinate_ids, child.id);
    }
    child.parent_id.clear();
}

inline bool OrganizationTree::AddUnit(OrganizationUnit unit) {
    if (Contains(unit.id)) {
        return false;
    }
    if (!unit.subordinate_ids.empty()) {
        return false;  // 下级接线统一走 SetParent/AddSubordinate（F6）。
    }
    const std::string parent_id = unit.parent_id;
    if (!parent_id.empty()) {
        if (parent_id == unit.id || !Contains(parent_id)) {
            return false;
        }
        if (!Find(parent_id)->CanContain(unit)) {
            return false;
        }
    }
    units_.push_back(std::move(unit));
    if (!parent_id.empty()) {
        Attach(*FindMutable(units_.back().id), *FindMutable(parent_id));
    }
    return true;
}

inline bool OrganizationTree::SetParent(const std::string& unit_id, const std::string& parent_id) {
    OrganizationUnit* child = FindMutable(unit_id);
    if (child == nullptr) {
        return false;
    }
    if (!parent_id.empty()) {
        if (parent_id == unit_id || !Contains(parent_id)) {
            return false;
        }
        OrganizationUnit* parent = FindMutable(parent_id);
        if (!parent->CanContain(*child)) {
            return false;
        }
        if (IsDescendantOf(parent_id, unit_id)) {
            return false;  // 父节点位于本节点子树内 → 成环。
        }
        Detach(*child);
        Attach(*child, *parent);
        return true;
    }
    Detach(*child);
    return true;
}

inline bool OrganizationTree::AddSubordinate(const std::string& parent_id, const std::string& child_id) {
    return SetParent(child_id, parent_id);
}

inline std::vector<std::string> OrganizationTree::SubordinatesOf(const std::string& id) const {
    const OrganizationUnit* unit = Find(id);
    return unit == nullptr ? std::vector<std::string>{} : unit->subordinate_ids;
}

inline void to_json(nlohmann::json& json, const OrganizationTree& tree) {
    json = nlohmann::json{{"units", tree.UnitsInInsertionOrder()}};
}

inline void from_json(const nlohmann::json& json, OrganizationTree& tree) {
    OrganizationTree candidate;
    std::vector<std::pair<std::string, std::string>> parent_links;
    std::map<std::string, std::vector<std::string>> expected_subordinates;
    for (const nlohmann::json& unit_json : json.at("units")) {
        const OrganizationUnit unit = unit_json.get<OrganizationUnit>();
        parent_links.emplace_back(unit.id, unit.parent_id);
        if (!unit.parent_id.empty()) {
            expected_subordinates[unit.parent_id].push_back(unit.id);
        }
        OrganizationUnit detached = unit;
        detached.parent_id.clear();
        detached.subordinate_ids.clear();
        if (!candidate.AddUnit(detached)) {
            throw std::invalid_argument("编制树包含重复 id: " + unit.id);
        }
    }
    // 父节点可能晚于子节点出现：全部入树后再统一接线（顺序无关，F6）。
    for (const auto& [unit_id, parent_id] : parent_links) {
        if (!parent_id.empty() && !candidate.SetParent(unit_id, parent_id)) {
            throw std::invalid_argument("编制树父节点/类型/成环校验失败: " + unit_id);
        }
    }
    // 互反一致性：JSON 声明的 subordinate_ids 必须与 parent_id 推导一致（F6）。
    for (const nlohmann::json& unit_json : json.at("units")) {
        const OrganizationUnit unit = unit_json.get<OrganizationUnit>();
        std::vector<std::string> expected = expected_subordinates[unit.id];
        std::sort(expected.begin(), expected.end());
        std::vector<std::string> declared = unit.subordinate_ids;
        std::sort(declared.begin(), declared.end());
        if (expected != declared) {
            throw std::invalid_argument("编制树 parent/subordinate 互反不一致: " + unit.id);
        }
    }
    tree = std::move(candidate);
}

}  // namespace wfs::sim::model
