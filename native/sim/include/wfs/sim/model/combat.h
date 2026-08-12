// sim/include/wfs/sim/model/combat.h
//
// T026：战斗单元模型（Ammo/Weapon/Soldier/Squad/Vehicle）。
//
// 设计契约（data-model.md §5–8；FR-022/060/062）：
// - 弹药双属性：对甲（穿深/基准伤害）与对人员（破片/冲击波半径、杀伤力）；
//   动能/化学能/特种三类 warhead_kind 承载距离衰减与穿深固定等差异的判定
//   入口（结算公式由 T031 实现）。
// - 士兵：防护（头盔/轻重防弹衣）、经验三维、状态机（正常/压制/失联/伤亡/
//   弹药）、重装备标志与负重——can_swim() 落实"泅渡不携带重装备"（FR-022）。
// - 班组：人数/占地半径/平均防护/掩蔽状态 + 最低操作人数装备（FR-060 区域
//   目标结算的数据基础）；伤亡导致装备不可用由 equipment_operational 表达。
// - 载具：四方向防护（前/侧/上/底，动能+化学能）、模块状态（机动/观瞄/装填）、
//   状态机（正常→模块受损→严重受损→摧毁）、乘员/载员容量与花名册、
//   浮渡能力；can_abandon() 是弃车规则（FR-062）的模型级入口。
// - 所有值类型支持 nlohmann::json 往返序列化（宪法第 13 条）。

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "wfs/sim/model/command_node.h"

namespace wfs::sim::model {

// 弹药战斗部类别：动能（距离衰减）、化学能（穿深固定）、特种（烟幕等）。
enum class WarheadKind : std::uint8_t {
    kKinetic = 0,
    kChemical = 1,
    kSpecial = 2,
};

// 士兵状态（data-model.md §5）。
enum class SoldierStatus : std::uint8_t {
    kNormal = 0,
    kSuppressed = 1,
    kOutOfContact = 2,
    kCasualty = 3,
    kOutOfAmmo = 4,
};

// 防弹衣等级（data-model.md §5：头盔/轻重防弹衣）。
enum class ArmorClass : std::uint8_t {
    kNone = 0,
    kLight = 1,
    kHeavy = 2,
};

// 队形（FR-060：行军/战斗两态基础队形）。
enum class Formation : std::uint8_t {
    kMarch = 0,
    kCombat = 1,
};

// 掩蔽状态（FR-060 区域目标结算输入）。
enum class CoverState : std::uint8_t {
    kNone = 0,
    kPartial = 1,
    kFull = 2,
};

// 载具状态机（data-model.md §6）。
enum class VehicleState : std::uint8_t {
    kOperational = 0,
    kModuleDamage = 1,
    kSeverelyDamaged = 2,
    kDestroyed = 3,
};

// 模块状态（data-model.md §6：移动/观瞄/装填）。
enum class ModuleState : std::uint8_t {
    kFunctional = 0,
    kDegraded = 1,
    kDisabled = 2,
};

std::string_view to_string(WarheadKind kind) noexcept;
WarheadKind warhead_kind_from_string(std::string_view name);
std::string_view to_string(SoldierStatus status) noexcept;
SoldierStatus soldier_status_from_string(std::string_view name);
std::string_view to_string(ArmorClass armor) noexcept;
ArmorClass armor_class_from_string(std::string_view name);
std::string_view to_string(Formation formation) noexcept;
Formation formation_from_string(std::string_view name);
std::string_view to_string(CoverState cover) noexcept;
CoverState cover_state_from_string(std::string_view name);
std::string_view to_string(VehicleState state) noexcept;
VehicleState vehicle_state_from_string(std::string_view name);
std::string_view to_string(ModuleState state) noexcept;
ModuleState module_state_from_string(std::string_view name);

void to_json(nlohmann::json& json, WarheadKind kind);
void from_json(const nlohmann::json& json, WarheadKind& kind);
void to_json(nlohmann::json& json, SoldierStatus status);
void from_json(const nlohmann::json& json, SoldierStatus& status);
void to_json(nlohmann::json& json, ArmorClass armor);
void from_json(const nlohmann::json& json, ArmorClass& armor);
void to_json(nlohmann::json& json, Formation formation);
void from_json(const nlohmann::json& json, Formation& formation);
void to_json(nlohmann::json& json, CoverState cover);
void from_json(const nlohmann::json& json, CoverState& cover);
void to_json(nlohmann::json& json, VehicleState state);
void from_json(const nlohmann::json& json, VehicleState& state);
void to_json(nlohmann::json& json, ModuleState state);
void from_json(const nlohmann::json& json, ModuleState& state);

// ---- 弹药 ----

// 对甲属性（data-model.md §8）：穿深与基准伤害。
struct AntiArmorProfile {
    double penetration_mm = 0.0;
    double base_damage = 0.0;

    bool is_valid() const noexcept {
        return std::isfinite(penetration_mm) && penetration_mm >= 0.0 && std::isfinite(base_damage) &&
               base_damage >= 0.0;
    }

    bool operator==(const AntiArmorProfile&) const = default;
};

// 对人员属性（data-model.md §8）：冲击波/破片半径与杀伤力。
struct AntiPersonnelProfile {
    double blast_radius_m = 0.0;
    double fragment_radius_m = 0.0;
    double lethality = 0.0;

    bool is_valid() const noexcept {
        return std::isfinite(blast_radius_m) && blast_radius_m >= 0.0 && std::isfinite(fragment_radius_m) &&
               fragment_radius_m >= 0.0 && std::isfinite(lethality) && lethality >= 0.0 && lethality <= 1.0;
    }

    bool operator==(const AntiPersonnelProfile&) const = default;
};

struct Ammo {
    std::string id;
    std::string name;
    WarheadKind warhead_kind = WarheadKind::kKinetic;
    AntiArmorProfile anti_armor;
    AntiPersonnelProfile anti_personnel;
    bool special_effect = false;  // 烟幕等基础特种弹药（data-model §8）。
    double weight_kg = 0.0;

    bool is_valid() const noexcept {
        return anti_armor.is_valid() && anti_personnel.is_valid() && std::isfinite(weight_kg) && weight_kg >= 0.0;
    }

    bool operator==(const Ammo&) const = default;
};

// ---- 武器 ----

struct Weapon {
    std::string id;
    std::string name;
    std::string category;  // 适用工事类别匹配（FR-067），如 "mg"/"mortar"/"atgm"。
    WarheadKind warhead_kind = WarheadKind::kKinetic;
    double effective_range_m = 0.0;
    double accuracy = 0.0;  // 基准精度 [0,1]。
    double weight_kg = 0.0;
    bool heavy_equipment = false;  // 多人伺候重装备（FR-022/FR-062）。
    std::uint32_t min_crew = 1U;
    std::vector<std::string> compatible_ammo;

    bool supports_ammo(const std::string& ammo_id) const {
        return std::find(compatible_ammo.begin(), compatible_ammo.end(), ammo_id) != compatible_ammo.end();
    }

    bool is_valid() const noexcept {
        if (!std::isfinite(effective_range_m) || effective_range_m < 0.0 || !std::isfinite(accuracy) ||
            accuracy < 0.0 || accuracy > 1.0 || !std::isfinite(weight_kg) || weight_kg < 0.0 || min_crew == 0U) {
            return false;
        }
        for (std::size_t i = 0; i < compatible_ammo.size(); ++i) {
            for (std::size_t j = i + 1; j < compatible_ammo.size(); ++j) {
                if (compatible_ammo[i] == compatible_ammo[j]) {
                    return false;
                }
            }
        }
        return true;
    }

    bool operator==(const Weapon&) const = default;
};

// ---- 士兵 ----

struct Protection {
    bool helmet = false;
    ArmorClass body_armor = ArmorClass::kNone;

    // 简单防护评分（0–3）：头盔 1 + 轻/重防弹衣 1/2；班组平均防护用。
    double armor_score() const noexcept {
        double score = helmet ? 1.0 : 0.0;
        switch (body_armor) {
            case ArmorClass::kNone:
                break;
            case ArmorClass::kLight:
                score += 1.0;
                break;
            case ArmorClass::kHeavy:
                score += 2.0;
                break;
        }
        return score;
    }

    bool operator==(const Protection&) const = default;
};

struct Soldier {
    std::string id;
    std::string name;
    std::string weapon_id;
    Protection protection;
    Experience experience;
    SoldierStatus status = SoldierStatus::kNormal;
    bool carries_heavy_equipment = false;  // 重装备标志（FR-022/FR-062）。
    double carry_weight_kg = 0.0;

    // 泅渡规则（FR-022）：不携带重装备才可泅渡。
    bool can_swim() const noexcept { return !carries_heavy_equipment; }
    bool is_casualty() const noexcept { return status == SoldierStatus::kCasualty; }

    bool is_valid() const noexcept {
        return experience.is_valid() && std::isfinite(carry_weight_kg) && carry_weight_kg >= 0.0;
    }

    bool operator==(const Soldier&) const = default;
};

// ---- 班组 ----

// 需要最低操作人数的装备（data-model.md §7：人员损失导致装备不可用/降效）。
struct CrewedEquipment {
    std::string equipment_id;
    std::uint32_t min_crew = 1U;

    bool operator==(const CrewedEquipment&) const = default;
};

struct Squad {
    std::string id;
    std::string name;
    std::vector<Soldier> soldiers;
    double footprint_radius_m = 0.0;  // 占地半径（FR-060 区域结算）。
    Formation formation = Formation::kMarch;
    CoverState cover = CoverState::kNone;
    std::vector<CrewedEquipment> crewed_equipment;

    std::size_t soldier_count() const noexcept { return soldiers.size(); }
    // 排除伤亡后的可用人数。
    std::size_t effective_soldier_count() const noexcept {
        return static_cast<std::size_t>(std::count_if(soldiers.begin(), soldiers.end(),
                                                      [](const Soldier& soldier) { return !soldier.is_casualty(); }));
    }
    // 平均防护评分（空班组为 0，确定性纯函数）。
    double average_protection_score() const noexcept {
        if (soldiers.empty()) {
            return 0.0;
        }
        double total = 0.0;
        for (const Soldier& soldier : soldiers) {
            total += soldier.protection.armor_score();
        }
        return total / static_cast<double>(soldiers.size());
    }
    // 装备可用性：有效人数 >= 最低操作人数。
    bool equipment_operational(const std::string& equipment_id) const {
        const auto it =
            std::find_if(crewed_equipment.begin(), crewed_equipment.end(),
                         [&](const CrewedEquipment& equipment) { return equipment.equipment_id == equipment_id; });
        return it != crewed_equipment.end() && effective_soldier_count() >= it->min_crew;
    }

    bool is_valid() const noexcept {
        if (!std::isfinite(footprint_radius_m) || footprint_radius_m < 0.0) {
            return false;
        }
        for (std::size_t i = 0; i < soldiers.size(); ++i) {
            for (std::size_t j = i + 1; j < soldiers.size(); ++j) {
                if (soldiers[i].id == soldiers[j].id) {
                    return false;
                }
            }
        }
        for (std::size_t i = 0; i < crewed_equipment.size(); ++i) {
            for (std::size_t j = i + 1; j < crewed_equipment.size(); ++j) {
                if (crewed_equipment[i].equipment_id == crewed_equipment[j].equipment_id) {
                    return false;
                }
            }
        }
        return true;
    }

    bool operator==(const Squad&) const = default;
};

// ---- 载具 ----

// 单方向防护：动能（mm 均质钢当量）与化学能防护。
struct DirectionalArmor {
    double kinetic_mm = 0.0;
    double chemical_mm = 0.0;

    bool is_valid() const noexcept {
        return std::isfinite(kinetic_mm) && kinetic_mm >= 0.0 && std::isfinite(chemical_mm) && chemical_mm >= 0.0;
    }

    bool operator==(const DirectionalArmor&) const = default;
};

// 四方向防护（data-model.md §6：前/侧/上/底）。
struct ArmorProfile {
    DirectionalArmor front;
    DirectionalArmor side;
    DirectionalArmor top;
    DirectionalArmor bottom;

    bool is_valid() const noexcept {
        return front.is_valid() && side.is_valid() && top.is_valid() && bottom.is_valid();
    }

    bool operator==(const ArmorProfile&) const = default;
};

// 模块状态组合（data-model.md §6：移动/光电观瞄/装填）。
struct ModuleStatus {
    ModuleState mobility = ModuleState::kFunctional;
    ModuleState optics = ModuleState::kFunctional;
    ModuleState reloading = ModuleState::kFunctional;

    bool any_disabled() const noexcept {
        return mobility == ModuleState::kDisabled || optics == ModuleState::kDisabled ||
               reloading == ModuleState::kDisabled;
    }
    bool any_degraded() const noexcept {
        return mobility == ModuleState::kDegraded || optics == ModuleState::kDegraded ||
               reloading == ModuleState::kDegraded;
    }

    bool operator==(const ModuleStatus&) const = default;
};

struct Vehicle {
    std::string id;
    std::string name;
    std::string chassis_id;
    ArmorProfile armor;
    ModuleStatus modules;
    VehicleState state = VehicleState::kOperational;
    bool amphibious = false;                // 浮渡能力（FR-022）。
    bool heavy_equipment = false;           // 重装备标志（弃车后不可泅渡，FR-062）。
    std::uint32_t crew_capacity = 0U;       // 乘员编制容量（FR-062）。
    std::uint32_t passenger_capacity = 0U;  // 搭乘步兵容量（FR-062）。
    std::vector<Soldier> crew;              // 乘员（操作车辆人员）。
    std::vector<Soldier> passengers;        // 载员（搭乘车辆人员）。
    std::vector<Weapon> weapons;

    // 弃车规则（FR-062）：严重受损或被摧毁后可弃车。
    bool can_abandon() const noexcept {
        return state == VehicleState::kSeverelyDamaged || state == VehicleState::kDestroyed;
    }
    std::size_t total_occupants() const noexcept { return crew.size() + passengers.size(); }

    bool is_valid() const noexcept {
        if (!armor.is_valid() || crew.size() > crew_capacity || passengers.size() > passenger_capacity) {
            return false;
        }
        // 花名册唯一性：同一士兵 id 不得重复占用席位，否则弃车后按 FR-062
        // 拆分出的车组/搭乘班组会携带重复 id，破坏班组标识唯一性。
        for (std::size_t i = 0; i < crew.size(); ++i) {
            for (std::size_t j = i + 1; j < crew.size(); ++j) {
                if (crew[i].id == crew[j].id) {
                    return false;
                }
            }
        }
        for (std::size_t i = 0; i < passengers.size(); ++i) {
            for (std::size_t j = i + 1; j < passengers.size(); ++j) {
                if (passengers[i].id == passengers[j].id) {
                    return false;
                }
            }
        }
        // 同一士兵 id 不得同时占用乘员与载员两类席位。
        for (std::size_t i = 0; i < crew.size(); ++i) {
            for (std::size_t j = 0; j < passengers.size(); ++j) {
                if (crew[i].id == passengers[j].id) {
                    return false;
                }
            }
        }
        // 状态与模块一致性：正常状态不得有模块瘫痪；摧毁状态必须全部瘫痪。
        if (state == VehicleState::kOperational && modules.any_disabled()) {
            return false;
        }
        if (state == VehicleState::kDestroyed &&
            (modules.mobility != ModuleState::kDisabled || modules.optics != ModuleState::kDisabled ||
             modules.reloading != ModuleState::kDisabled)) {
            return false;
        }
        // 待量化项：规格（data-model.md §6 / FR-062）未定义 kSeverelyDamaged/
        // kModuleDamage 必须对应的模块降级/瘫痪组合，故暂不对这两个状态做
        // 模块一致性校验，避免自行发明规则；待规格明确后再补充。
        return true;
    }

    bool operator==(const Vehicle&) const = default;
};

// ---- 序列化 ----

void to_json(nlohmann::json& json, const AntiArmorProfile& profile);
void from_json(const nlohmann::json& json, AntiArmorProfile& profile);
void to_json(nlohmann::json& json, const AntiPersonnelProfile& profile);
void from_json(const nlohmann::json& json, AntiPersonnelProfile& profile);
void to_json(nlohmann::json& json, const Ammo& ammo);
void from_json(const nlohmann::json& json, Ammo& ammo);
void to_json(nlohmann::json& json, const Weapon& weapon);
void from_json(const nlohmann::json& json, Weapon& weapon);
void to_json(nlohmann::json& json, const Protection& protection);
void from_json(const nlohmann::json& json, Protection& protection);
void to_json(nlohmann::json& json, const Soldier& soldier);
void from_json(const nlohmann::json& json, Soldier& soldier);
void to_json(nlohmann::json& json, const CrewedEquipment& equipment);
void from_json(const nlohmann::json& json, CrewedEquipment& equipment);
void to_json(nlohmann::json& json, const Squad& squad);
void from_json(const nlohmann::json& json, Squad& squad);
void to_json(nlohmann::json& json, const DirectionalArmor& armor);
void from_json(const nlohmann::json& json, DirectionalArmor& armor);
void to_json(nlohmann::json& json, const ArmorProfile& profile);
void from_json(const nlohmann::json& json, ArmorProfile& profile);
void to_json(nlohmann::json& json, const ModuleStatus& modules);
void from_json(const nlohmann::json& json, ModuleStatus& modules);
void to_json(nlohmann::json& json, const Vehicle& vehicle);
void from_json(const nlohmann::json& json, Vehicle& vehicle);

// ---- 内联实现 ----

inline std::string_view to_string(const WarheadKind kind) noexcept {
    switch (kind) {
        case WarheadKind::kKinetic:
            return "kinetic";
        case WarheadKind::kChemical:
            return "chemical";
        case WarheadKind::kSpecial:
            return "special";
    }
    return "unknown";
}

inline WarheadKind warhead_kind_from_string(const std::string_view name) {
    if (name == "kinetic") {
        return WarheadKind::kKinetic;
    }
    if (name == "chemical") {
        return WarheadKind::kChemical;
    }
    if (name == "special") {
        return WarheadKind::kSpecial;
    }
    throw std::invalid_argument("未知战斗部类别: " + std::string(name));
}

inline std::string_view to_string(const SoldierStatus status) noexcept {
    switch (status) {
        case SoldierStatus::kNormal:
            return "normal";
        case SoldierStatus::kSuppressed:
            return "suppressed";
        case SoldierStatus::kOutOfContact:
            return "out_of_contact";
        case SoldierStatus::kCasualty:
            return "casualty";
        case SoldierStatus::kOutOfAmmo:
            return "out_of_ammo";
    }
    return "unknown";
}

inline SoldierStatus soldier_status_from_string(const std::string_view name) {
    if (name == "normal") {
        return SoldierStatus::kNormal;
    }
    if (name == "suppressed") {
        return SoldierStatus::kSuppressed;
    }
    if (name == "out_of_contact") {
        return SoldierStatus::kOutOfContact;
    }
    if (name == "casualty") {
        return SoldierStatus::kCasualty;
    }
    if (name == "out_of_ammo") {
        return SoldierStatus::kOutOfAmmo;
    }
    throw std::invalid_argument("未知士兵状态: " + std::string(name));
}

inline std::string_view to_string(const ArmorClass armor) noexcept {
    switch (armor) {
        case ArmorClass::kNone:
            return "none";
        case ArmorClass::kLight:
            return "light";
        case ArmorClass::kHeavy:
            return "heavy";
    }
    return "unknown";
}

inline ArmorClass armor_class_from_string(const std::string_view name) {
    if (name == "none") {
        return ArmorClass::kNone;
    }
    if (name == "light") {
        return ArmorClass::kLight;
    }
    if (name == "heavy") {
        return ArmorClass::kHeavy;
    }
    throw std::invalid_argument("未知防弹衣等级: " + std::string(name));
}

inline std::string_view to_string(const Formation formation) noexcept {
    switch (formation) {
        case Formation::kMarch:
            return "march";
        case Formation::kCombat:
            return "combat";
    }
    return "unknown";
}

inline Formation formation_from_string(const std::string_view name) {
    if (name == "march") {
        return Formation::kMarch;
    }
    if (name == "combat") {
        return Formation::kCombat;
    }
    throw std::invalid_argument("未知队形: " + std::string(name));
}

inline std::string_view to_string(const CoverState cover) noexcept {
    switch (cover) {
        case CoverState::kNone:
            return "none";
        case CoverState::kPartial:
            return "partial";
        case CoverState::kFull:
            return "full";
    }
    return "unknown";
}

inline CoverState cover_state_from_string(const std::string_view name) {
    if (name == "none") {
        return CoverState::kNone;
    }
    if (name == "partial") {
        return CoverState::kPartial;
    }
    if (name == "full") {
        return CoverState::kFull;
    }
    throw std::invalid_argument("未知掩蔽状态: " + std::string(name));
}

inline std::string_view to_string(const VehicleState state) noexcept {
    switch (state) {
        case VehicleState::kOperational:
            return "operational";
        case VehicleState::kModuleDamage:
            return "module_damage";
        case VehicleState::kSeverelyDamaged:
            return "severely_damaged";
        case VehicleState::kDestroyed:
            return "destroyed";
    }
    return "unknown";
}

inline VehicleState vehicle_state_from_string(const std::string_view name) {
    if (name == "operational") {
        return VehicleState::kOperational;
    }
    if (name == "module_damage") {
        return VehicleState::kModuleDamage;
    }
    if (name == "severely_damaged") {
        return VehicleState::kSeverelyDamaged;
    }
    if (name == "destroyed") {
        return VehicleState::kDestroyed;
    }
    throw std::invalid_argument("未知载具状态: " + std::string(name));
}

inline std::string_view to_string(const ModuleState state) noexcept {
    switch (state) {
        case ModuleState::kFunctional:
            return "functional";
        case ModuleState::kDegraded:
            return "degraded";
        case ModuleState::kDisabled:
            return "disabled";
    }
    return "unknown";
}

inline ModuleState module_state_from_string(const std::string_view name) {
    if (name == "functional") {
        return ModuleState::kFunctional;
    }
    if (name == "degraded") {
        return ModuleState::kDegraded;
    }
    if (name == "disabled") {
        return ModuleState::kDisabled;
    }
    throw std::invalid_argument("未知模块状态: " + std::string(name));
}

inline void to_json(nlohmann::json& json, const WarheadKind kind) {
    json = to_string(kind);
}
inline void from_json(const nlohmann::json& json, WarheadKind& kind) {
    kind = warhead_kind_from_string(json.get<std::string>());
}
inline void to_json(nlohmann::json& json, const SoldierStatus status) {
    json = to_string(status);
}
inline void from_json(const nlohmann::json& json, SoldierStatus& status) {
    status = soldier_status_from_string(json.get<std::string>());
}
inline void to_json(nlohmann::json& json, const ArmorClass armor) {
    json = to_string(armor);
}
inline void from_json(const nlohmann::json& json, ArmorClass& armor) {
    armor = armor_class_from_string(json.get<std::string>());
}
inline void to_json(nlohmann::json& json, const Formation formation) {
    json = to_string(formation);
}
inline void from_json(const nlohmann::json& json, Formation& formation) {
    formation = formation_from_string(json.get<std::string>());
}
inline void to_json(nlohmann::json& json, const CoverState cover) {
    json = to_string(cover);
}
inline void from_json(const nlohmann::json& json, CoverState& cover) {
    cover = cover_state_from_string(json.get<std::string>());
}
inline void to_json(nlohmann::json& json, const VehicleState state) {
    json = to_string(state);
}
inline void from_json(const nlohmann::json& json, VehicleState& state) {
    state = vehicle_state_from_string(json.get<std::string>());
}
inline void to_json(nlohmann::json& json, const ModuleState state) {
    json = to_string(state);
}
inline void from_json(const nlohmann::json& json, ModuleState& state) {
    state = module_state_from_string(json.get<std::string>());
}

inline void to_json(nlohmann::json& json, const AntiArmorProfile& profile) {
    json = nlohmann::json{{"penetration_mm", profile.penetration_mm}, {"base_damage", profile.base_damage}};
}
inline void from_json(const nlohmann::json& json, AntiArmorProfile& profile) {
    profile.penetration_mm = json.at("penetration_mm").get<double>();
    profile.base_damage = json.at("base_damage").get<double>();
    if (!profile.is_valid()) {
        throw std::invalid_argument("对甲属性越界（必须非负）");
    }
}

inline void to_json(nlohmann::json& json, const AntiPersonnelProfile& profile) {
    json = nlohmann::json{{"blast_radius_m", profile.blast_radius_m},
                          {"fragment_radius_m", profile.fragment_radius_m},
                          {"lethality", profile.lethality}};
}
inline void from_json(const nlohmann::json& json, AntiPersonnelProfile& profile) {
    profile.blast_radius_m = json.at("blast_radius_m").get<double>();
    profile.fragment_radius_m = json.at("fragment_radius_m").get<double>();
    profile.lethality = json.at("lethality").get<double>();
    if (!profile.is_valid()) {
        throw std::invalid_argument("对人员属性越界（lethality 必须在 [0,1]）");
    }
}

inline void to_json(nlohmann::json& json, const Ammo& ammo) {
    json = nlohmann::json{{"id", ammo.id},
                          {"name", ammo.name},
                          {"warhead_kind", ammo.warhead_kind},
                          {"anti_armor", ammo.anti_armor},
                          {"anti_personnel", ammo.anti_personnel},
                          {"special_effect", ammo.special_effect},
                          {"weight_kg", ammo.weight_kg}};
}
inline void from_json(const nlohmann::json& json, Ammo& ammo) {
    ammo.id = json.at("id").get<std::string>();
    ammo.name = json.at("name").get<std::string>();
    ammo.warhead_kind = json.at("warhead_kind").get<WarheadKind>();
    ammo.anti_armor = json.at("anti_armor").get<AntiArmorProfile>();
    ammo.anti_personnel = json.at("anti_personnel").get<AntiPersonnelProfile>();
    ammo.special_effect = json.at("special_effect").get<bool>();
    ammo.weight_kg = json.at("weight_kg").get<double>();
    if (!ammo.is_valid()) {
        throw std::invalid_argument("弹药数值越界: " + ammo.id);
    }
}

inline void to_json(nlohmann::json& json, const Weapon& weapon) {
    json = nlohmann::json{{"id", weapon.id},
                          {"name", weapon.name},
                          {"category", weapon.category},
                          {"warhead_kind", weapon.warhead_kind},
                          {"effective_range_m", weapon.effective_range_m},
                          {"accuracy", weapon.accuracy},
                          {"weight_kg", weapon.weight_kg},
                          {"heavy_equipment", weapon.heavy_equipment},
                          {"min_crew", weapon.min_crew},
                          {"compatible_ammo", weapon.compatible_ammo}};
}
inline void from_json(const nlohmann::json& json, Weapon& weapon) {
    weapon.id = json.at("id").get<std::string>();
    weapon.name = json.at("name").get<std::string>();
    weapon.category = json.at("category").get<std::string>();
    weapon.warhead_kind = json.at("warhead_kind").get<WarheadKind>();
    weapon.effective_range_m = json.at("effective_range_m").get<double>();
    weapon.accuracy = json.at("accuracy").get<double>();
    weapon.weight_kg = json.at("weight_kg").get<double>();
    weapon.heavy_equipment = json.at("heavy_equipment").get<bool>();
    weapon.min_crew = json.at("min_crew").get<std::uint32_t>();
    weapon.compatible_ammo = json.at("compatible_ammo").get<std::vector<std::string>>();
    if (!weapon.is_valid()) {
        throw std::invalid_argument("武器数值越界或兼容弹药重复: " + weapon.id);
    }
}

inline void to_json(nlohmann::json& json, const Protection& protection) {
    json = nlohmann::json{{"helmet", protection.helmet}, {"body_armor", protection.body_armor}};
}
inline void from_json(const nlohmann::json& json, Protection& protection) {
    protection.helmet = json.at("helmet").get<bool>();
    protection.body_armor = json.at("body_armor").get<ArmorClass>();
}

inline void to_json(nlohmann::json& json, const Soldier& soldier) {
    json = nlohmann::json{{"id", soldier.id},
                          {"name", soldier.name},
                          {"weapon_id", soldier.weapon_id},
                          {"protection", soldier.protection},
                          {"experience", soldier.experience},
                          {"status", soldier.status},
                          {"carries_heavy_equipment", soldier.carries_heavy_equipment},
                          {"carry_weight_kg", soldier.carry_weight_kg}};
}
inline void from_json(const nlohmann::json& json, Soldier& soldier) {
    soldier.id = json.at("id").get<std::string>();
    soldier.name = json.at("name").get<std::string>();
    soldier.weapon_id = json.at("weapon_id").get<std::string>();
    soldier.protection = json.at("protection").get<Protection>();
    soldier.experience = json.at("experience").get<Experience>();
    soldier.status = json.at("status").get<SoldierStatus>();
    soldier.carries_heavy_equipment = json.at("carries_heavy_equipment").get<bool>();
    soldier.carry_weight_kg = json.at("carry_weight_kg").get<double>();
    if (!soldier.is_valid()) {
        throw std::invalid_argument("士兵数值越界: " + soldier.id);
    }
}

inline void to_json(nlohmann::json& json, const CrewedEquipment& equipment) {
    json = nlohmann::json{{"equipment_id", equipment.equipment_id}, {"min_crew", equipment.min_crew}};
}
inline void from_json(const nlohmann::json& json, CrewedEquipment& equipment) {
    equipment.equipment_id = json.at("equipment_id").get<std::string>();
    equipment.min_crew = json.at("min_crew").get<std::uint32_t>();
}

inline void to_json(nlohmann::json& json, const Squad& squad) {
    json = nlohmann::json{{"id", squad.id},
                          {"name", squad.name},
                          {"soldiers", squad.soldiers},
                          {"footprint_radius_m", squad.footprint_radius_m},
                          {"formation", squad.formation},
                          {"cover", squad.cover},
                          {"crewed_equipment", squad.crewed_equipment}};
}
inline void from_json(const nlohmann::json& json, Squad& squad) {
    squad.id = json.at("id").get<std::string>();
    squad.name = json.at("name").get<std::string>();
    squad.soldiers = json.at("soldiers").get<std::vector<Soldier>>();
    squad.footprint_radius_m = json.at("footprint_radius_m").get<double>();
    squad.formation = json.at("formation").get<Formation>();
    squad.cover = json.at("cover").get<CoverState>();
    squad.crewed_equipment = json.at("crewed_equipment").get<std::vector<CrewedEquipment>>();
    if (!squad.is_valid()) {
        throw std::invalid_argument("班组数值越界或成员/装备 id 重复: " + squad.id);
    }
}

inline void to_json(nlohmann::json& json, const DirectionalArmor& armor) {
    json = nlohmann::json{{"kinetic_mm", armor.kinetic_mm}, {"chemical_mm", armor.chemical_mm}};
}
inline void from_json(const nlohmann::json& json, DirectionalArmor& armor) {
    armor.kinetic_mm = json.at("kinetic_mm").get<double>();
    armor.chemical_mm = json.at("chemical_mm").get<double>();
    if (!armor.is_valid()) {
        throw std::invalid_argument("方向防护越界（必须非负）");
    }
}

inline void to_json(nlohmann::json& json, const ArmorProfile& profile) {
    json = nlohmann::json{
        {"front", profile.front}, {"side", profile.side}, {"top", profile.top}, {"bottom", profile.bottom}};
}
inline void from_json(const nlohmann::json& json, ArmorProfile& profile) {
    profile.front = json.at("front").get<DirectionalArmor>();
    profile.side = json.at("side").get<DirectionalArmor>();
    profile.top = json.at("top").get<DirectionalArmor>();
    profile.bottom = json.at("bottom").get<DirectionalArmor>();
    if (!profile.is_valid()) {
        throw std::invalid_argument("四方向防护越界");
    }
}

inline void to_json(nlohmann::json& json, const ModuleStatus& modules) {
    json = nlohmann::json{{"mobility", modules.mobility}, {"optics", modules.optics}, {"reloading", modules.reloading}};
}
inline void from_json(const nlohmann::json& json, ModuleStatus& modules) {
    modules.mobility = json.at("mobility").get<ModuleState>();
    modules.optics = json.at("optics").get<ModuleState>();
    modules.reloading = json.at("reloading").get<ModuleState>();
}

inline void to_json(nlohmann::json& json, const Vehicle& vehicle) {
    json = nlohmann::json{{"id", vehicle.id},
                          {"name", vehicle.name},
                          {"chassis_id", vehicle.chassis_id},
                          {"armor", vehicle.armor},
                          {"modules", vehicle.modules},
                          {"state", vehicle.state},
                          {"amphibious", vehicle.amphibious},
                          {"heavy_equipment", vehicle.heavy_equipment},
                          {"crew_capacity", vehicle.crew_capacity},
                          {"passenger_capacity", vehicle.passenger_capacity},
                          {"crew", vehicle.crew},
                          {"passengers", vehicle.passengers},
                          {"weapons", vehicle.weapons}};
}
inline void from_json(const nlohmann::json& json, Vehicle& vehicle) {
    vehicle.id = json.at("id").get<std::string>();
    vehicle.name = json.at("name").get<std::string>();
    vehicle.chassis_id = json.at("chassis_id").get<std::string>();
    vehicle.armor = json.at("armor").get<ArmorProfile>();
    vehicle.modules = json.at("modules").get<ModuleStatus>();
    vehicle.state = json.at("state").get<VehicleState>();
    vehicle.amphibious = json.at("amphibious").get<bool>();
    vehicle.heavy_equipment = json.at("heavy_equipment").get<bool>();
    vehicle.crew_capacity = json.at("crew_capacity").get<std::uint32_t>();
    vehicle.passenger_capacity = json.at("passenger_capacity").get<std::uint32_t>();
    vehicle.crew = json.at("crew").get<std::vector<Soldier>>();
    vehicle.passengers = json.at("passengers").get<std::vector<Soldier>>();
    vehicle.weapons = json.at("weapons").get<std::vector<Weapon>>();
    if (!vehicle.is_valid()) {
        throw std::invalid_argument("载具数值越界或状态与模块不一致: " + vehicle.id);
    }
}

}  // namespace wfs::sim::model
