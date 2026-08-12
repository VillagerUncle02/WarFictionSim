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

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
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
}

}  // namespace wfs::sim::model
