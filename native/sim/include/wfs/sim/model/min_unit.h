// sim/include/wfs/sim/model/min_unit.h
//
// T025：最小可指挥单位 / 最小配备单位模型。
//
// 设计契约（data-model.md §3）：
// - 最小可指挥单位（MinCommandUnit）：连排级为单辆载具或班组，营级为
//   连排级单位（kSquad/kVehicle/kPlatoon/kCompany 覆盖两种规模）。
// - 最小配备单位（MinEquippedUnit）：单个士兵或单辆车，是构成最小可指挥
//   单位的原子成分；weight_kg/heavy_equipment 承载泅渡重量上限与重装备
//   判定（FR-022），由后续机动/战斗任务消费。
// - parent_unit_id 支持战术分队临时组合（data-model §4：跨行政编制临时
//   组合、任务结束归建），本模型只记录归属关系。
// - 值类型支持 nlohmann::json 往返序列化（宪法第 13 条）。

#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace wfs::sim::model {

// 最小可指挥单位类型：连排级 = 班组/载具；营级 = 连排级单位。
enum class MinUnitKind : std::uint8_t {
    kSquad = 0,
    kVehicle = 1,
    kPlatoon = 2,
    kCompany = 3,
};

// 最小配备单位类型：单兵或单车。
enum class EquippedUnitKind : std::uint8_t {
    kSoldier = 0,
    kVehicle = 1,
};

std::string_view to_string(MinUnitKind kind) noexcept;
MinUnitKind min_unit_kind_from_string(std::string_view name);
std::string_view to_string(EquippedUnitKind kind) noexcept;
EquippedUnitKind equipped_unit_kind_from_string(std::string_view name);

void to_json(nlohmann::json& json, MinUnitKind kind);
void from_json(const nlohmann::json& json, MinUnitKind& kind);
void to_json(nlohmann::json& json, EquippedUnitKind kind);
void from_json(const nlohmann::json& json, EquippedUnitKind& kind);

// 最小配备单位：单兵/单车 + 重量与重装备标志（FR-022）。
struct MinEquippedUnit {
    std::string id;
    EquippedUnitKind kind = EquippedUnitKind::kSoldier;
    std::string combat_ref;  // 关联 T026 战斗模型 id（Soldier/Vehicle）。
    double weight_kg = 0.0;
    bool heavy_equipment = false;

    bool operator==(const MinEquippedUnit&) const = default;
};

// 最小可指挥单位：由最小配备单位构成，可挂到战术分队（临时组合）。
struct MinCommandUnit {
    std::string id;
    MinUnitKind kind = MinUnitKind::kSquad;
    std::string organization_id;                 // 行政编制 id（organization.h）。
    std::vector<std::string> equipped_unit_ids;  // 构成该单位的最小配备单位。
    std::string parent_unit_id;                  // 上级最小可指挥单位（战术分队，可空）。

    bool operator==(const MinCommandUnit&) const = default;
};

void to_json(nlohmann::json& json, const MinEquippedUnit& unit);
void from_json(const nlohmann::json& json, MinEquippedUnit& unit);
void to_json(nlohmann::json& json, const MinCommandUnit& unit);
void from_json(const nlohmann::json& json, MinCommandUnit& unit);

// ---- 内联实现 ----

inline std::string_view to_string(const MinUnitKind kind) noexcept {
    switch (kind) {
        case MinUnitKind::kSquad:
            return "squad";
        case MinUnitKind::kVehicle:
            return "vehicle";
        case MinUnitKind::kPlatoon:
            return "platoon";
        case MinUnitKind::kCompany:
            return "company";
    }
    return "unknown";
}

inline MinUnitKind min_unit_kind_from_string(const std::string_view name) {
    if (name == "squad") {
        return MinUnitKind::kSquad;
    }
    if (name == "vehicle") {
        return MinUnitKind::kVehicle;
    }
    if (name == "platoon") {
        return MinUnitKind::kPlatoon;
    }
    if (name == "company") {
        return MinUnitKind::kCompany;
    }
    throw std::invalid_argument("未知最小可指挥单位类型: " + std::string(name));
}

inline std::string_view to_string(const EquippedUnitKind kind) noexcept {
    switch (kind) {
        case EquippedUnitKind::kSoldier:
            return "soldier";
        case EquippedUnitKind::kVehicle:
            return "vehicle";
    }
    return "unknown";
}

inline EquippedUnitKind equipped_unit_kind_from_string(const std::string_view name) {
    if (name == "soldier") {
        return EquippedUnitKind::kSoldier;
    }
    if (name == "vehicle") {
        return EquippedUnitKind::kVehicle;
    }
    throw std::invalid_argument("未知最小配备单位类型: " + std::string(name));
}

inline void to_json(nlohmann::json& json, const MinUnitKind kind) {
    json = to_string(kind);
}

inline void from_json(const nlohmann::json& json, MinUnitKind& kind) {
    kind = min_unit_kind_from_string(json.get<std::string>());
}

inline void to_json(nlohmann::json& json, const EquippedUnitKind kind) {
    json = to_string(kind);
}

inline void from_json(const nlohmann::json& json, EquippedUnitKind& kind) {
    kind = equipped_unit_kind_from_string(json.get<std::string>());
}

inline void to_json(nlohmann::json& json, const MinEquippedUnit& unit) {
    json = nlohmann::json{{"id", unit.id},
                          {"kind", unit.kind},
                          {"combat_ref", unit.combat_ref},
                          {"weight_kg", unit.weight_kg},
                          {"heavy_equipment", unit.heavy_equipment}};
}

inline void from_json(const nlohmann::json& json, MinEquippedUnit& unit) {
    unit.id = json.at("id").get<std::string>();
    unit.kind = json.at("kind").get<EquippedUnitKind>();
    unit.combat_ref = json.at("combat_ref").get<std::string>();
    unit.weight_kg = json.at("weight_kg").get<double>();
    unit.heavy_equipment = json.at("heavy_equipment").get<bool>();
}

inline void to_json(nlohmann::json& json, const MinCommandUnit& unit) {
    json = nlohmann::json{{"id", unit.id},
                          {"kind", unit.kind},
                          {"organization_id", unit.organization_id},
                          {"equipped_unit_ids", unit.equipped_unit_ids},
                          {"parent_unit_id", unit.parent_unit_id}};
}

inline void from_json(const nlohmann::json& json, MinCommandUnit& unit) {
    unit.id = json.at("id").get<std::string>();
    unit.kind = json.at("kind").get<MinUnitKind>();
    unit.organization_id = json.at("organization_id").get<std::string>();
    unit.equipped_unit_ids = json.at("equipped_unit_ids").get<std::vector<std::string>>();
    unit.parent_unit_id = json.at("parent_unit_id").get<std::string>();
}

}  // namespace wfs::sim::model
