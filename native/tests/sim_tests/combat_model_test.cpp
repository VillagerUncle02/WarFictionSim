// tests/sim_tests/combat_model_test.cpp
//
// T026 单元测试：士兵/班组/载具/武器/弹药战斗模型。
// 覆盖：弹药双属性（对甲/对人员）、武器弹药兼容、四方向防护（前/侧/上/底）、
// 模块状态、乘员/载员与弃车判定（FR-062）、重装备标志与泅渡规则（FR-022）、
// 班组区域结算所需字段（人数/占地半径/平均防护/掩蔽、装备最低操作人数），
// 以及全部类型的 JSON 序列化往返。

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "wfs/sim/model/combat.h"

namespace {

namespace model = wfs::sim::model;

using model::Ammo;
using model::AntiArmorProfile;
using model::AntiPersonnelProfile;
using model::ArmorClass;
using model::ArmorProfile;
using model::CoverState;
using model::CrewedEquipment;
using model::DirectionalArmor;
using model::Experience;
using model::Formation;
using model::ModuleState;
using model::ModuleStatus;
using model::Protection;
using model::Soldier;
using model::SoldierStatus;
using model::Squad;
using model::Vehicle;
using model::VehicleState;
using model::WarheadKind;
using model::Weapon;

nlohmann::json RoundTrip(const nlohmann::json& input) {
    return nlohmann::json::parse(input.dump());
}

Soldier MakeSoldier(const std::string& id, SoldierStatus status = SoldierStatus::kNormal, bool heavy = false) {
    return Soldier{
        id,     "Soldier " + id, "rifle-556",        Protection{true, ArmorClass::kLight}, Experience{0.7, 0.5, 0.4},
        status, heavy,           heavy ? 38.0 : 12.0};
}

Ammo MakeKineticAmmo() {
    return Ammo{"ammo-556",
                "5.56mm Ball",
                WarheadKind::kKinetic,
                AntiArmorProfile{8.0, 12.0},
                AntiPersonnelProfile{0.0, 2.0, 0.5},
                false,
                0.012};
}

}  // namespace

TEST(WfsCombatModelTest, EnumStringsRoundTrip) {
    EXPECT_EQ(model::to_string(WarheadKind::kKinetic), "kinetic");
    EXPECT_EQ(model::to_string(WarheadKind::kChemical), "chemical");
    EXPECT_EQ(model::to_string(WarheadKind::kSpecial), "special");
    EXPECT_EQ(model::warhead_kind_from_string("chemical"), WarheadKind::kChemical);
    EXPECT_THROW(model::warhead_kind_from_string("plasma"), std::invalid_argument);

    EXPECT_EQ(model::to_string(SoldierStatus::kSuppressed), "suppressed");
    EXPECT_EQ(model::soldier_status_from_string("casualty"), SoldierStatus::kCasualty);
    EXPECT_THROW(model::soldier_status_from_string("wounded"), std::invalid_argument);

    EXPECT_EQ(model::to_string(ArmorClass::kHeavy), "heavy");
    EXPECT_EQ(model::armor_class_from_string("light"), ArmorClass::kLight);
    EXPECT_EQ(model::to_string(Formation::kMarch), "march");
    EXPECT_EQ(model::formation_from_string("combat"), Formation::kCombat);
    EXPECT_EQ(model::to_string(CoverState::kFull), "full");
    EXPECT_EQ(model::cover_state_from_string("partial"), CoverState::kPartial);
    EXPECT_EQ(model::to_string(VehicleState::kSeverelyDamaged), "severely_damaged");
    EXPECT_EQ(model::vehicle_state_from_string("destroyed"), VehicleState::kDestroyed);
    EXPECT_EQ(model::to_string(ModuleState::kDisabled), "disabled");
    EXPECT_EQ(model::module_state_from_string("degraded"), ModuleState::kDegraded);
}

TEST(WfsCombatModelTest, AmmoDualProfilesRoundTrip) {
    const Ammo ammo = MakeKineticAmmo();
    const Ammo restored = RoundTrip(nlohmann::json(ammo)).get<Ammo>();
    EXPECT_EQ(restored, ammo);
    EXPECT_DOUBLE_EQ(restored.anti_armor.penetration_mm, 8.0);
    EXPECT_DOUBLE_EQ(restored.anti_personnel.fragment_radius_m, 2.0);

    const Ammo smoke = Ammo{"ammo-smoke",
                            "Smoke Round",
                            WarheadKind::kSpecial,
                            AntiArmorProfile{0.0, 0.0},
                            AntiPersonnelProfile{0.0, 0.0, 0.0},
                            true,
                            0.5};
    EXPECT_EQ(RoundTrip(nlohmann::json(smoke)).get<Ammo>(), smoke);
}

TEST(WfsCombatModelTest, WeaponCompatibilityAndRoundTrip) {
    Weapon weapon;
    weapon.id = "rifle-556";
    weapon.name = "5.56mm Rifle";
    weapon.category = "rifle";
    weapon.warhead_kind = WarheadKind::kKinetic;
    weapon.effective_range_m = 500.0;
    weapon.accuracy = 0.8;
    weapon.weight_kg = 3.6;
    weapon.heavy_equipment = false;
    weapon.min_crew = 1U;
    weapon.compatible_ammo = {"ammo-556", "ammo-frag-grenade"};

    EXPECT_TRUE(weapon.supports_ammo("ammo-556"));
    EXPECT_FALSE(weapon.supports_ammo("ammo-127"));
    const Weapon restored = RoundTrip(nlohmann::json(weapon)).get<Weapon>();
    EXPECT_EQ(restored, weapon);
}

TEST(WfsCombatModelTest, SoldierSwimRuleAndRoundTrip) {
    const Soldier light = MakeSoldier("s1");
    EXPECT_TRUE(light.can_swim());

    const Soldier heavy = MakeSoldier("s2", SoldierStatus::kNormal, true);
    EXPECT_FALSE(heavy.can_swim());  // 重装备不可泅渡（FR-022）
    EXPECT_FALSE(heavy.is_casualty());

    const Soldier casualty = MakeSoldier("s3", SoldierStatus::kCasualty);
    EXPECT_TRUE(casualty.is_casualty());

    const Soldier restored = RoundTrip(nlohmann::json(light)).get<Soldier>();
    EXPECT_EQ(restored, light);
}

TEST(WfsCombatModelTest, ProtectionScore) {
    const Protection heavy{true, ArmorClass::kHeavy};
    EXPECT_DOUBLE_EQ(heavy.armor_score(), 3.0);
    const Protection light{false, ArmorClass::kLight};
    EXPECT_DOUBLE_EQ(light.armor_score(), 1.0);
    const Protection none{false, ArmorClass::kNone};
    EXPECT_DOUBLE_EQ(none.armor_score(), 0.0);
}

TEST(WfsCombatModelTest, SquadAggregatesAndCrewedEquipment) {
    Squad squad;
    squad.id = "squad-1";
    squad.name = "Rifle Squad";
    squad.soldiers = {MakeSoldier("s1"), MakeSoldier("s2"), MakeSoldier("s3", SoldierStatus::kCasualty)};
    squad.footprint_radius_m = 15.0;
    squad.formation = Formation::kCombat;
    squad.cover = CoverState::kPartial;
    squad.crewed_equipment = {CrewedEquipment{"saw-556", 1U}, CrewedEquipment{"hmg-127", 3U}};

    EXPECT_EQ(squad.soldier_count(), 3u);
    EXPECT_EQ(squad.effective_soldier_count(), 2u);  // 伤亡不计入可用人数
    EXPECT_TRUE(squad.equipment_operational("saw-556"));
    EXPECT_FALSE(squad.equipment_operational("hmg-127"));  // 3 人要求，可用仅 2 人
    EXPECT_FALSE(squad.equipment_operational("missing"));

    const Squad restored = RoundTrip(nlohmann::json(squad)).get<Squad>();
    EXPECT_EQ(restored, squad);
    EXPECT_DOUBLE_EQ(restored.average_protection_score(), 2.0);  // 头盔 1 + 轻型防弹衣 1
}

TEST(WfsCombatModelTest, VehicleArmorModulesAndOccupants) {
    Vehicle vehicle;
    vehicle.id = "vehicle-1";
    vehicle.name = "IFV";
    vehicle.chassis_id = "chassis-ifv";
    vehicle.armor = ArmorProfile{DirectionalArmor{60.0, 250.0}, DirectionalArmor{40.0, 180.0},
                                 DirectionalArmor{25.0, 80.0}, DirectionalArmor{15.0, 30.0}};
    vehicle.modules = ModuleStatus{ModuleState::kFunctional, ModuleState::kDegraded, ModuleState::kDisabled};
    vehicle.state = VehicleState::kModuleDamage;
    vehicle.amphibious = true;
    vehicle.heavy_equipment = false;
    vehicle.crew_capacity = 3U;
    vehicle.passenger_capacity = 6U;
    vehicle.crew = {MakeSoldier("crew1"), MakeSoldier("crew2")};
    vehicle.passengers = {MakeSoldier("pax1"), MakeSoldier("pax2"), MakeSoldier("pax3")};

    EXPECT_FALSE(vehicle.can_abandon());
    EXPECT_TRUE(vehicle.modules.any_disabled());
    EXPECT_TRUE(vehicle.modules.any_degraded());
    EXPECT_EQ(vehicle.total_occupants(), 5u);

    vehicle.state = VehicleState::kSeverelyDamaged;
    EXPECT_TRUE(vehicle.can_abandon());  // 严重受损可弃车（FR-062）
    vehicle.state = VehicleState::kDestroyed;
    vehicle.modules = ModuleStatus{ModuleState::kDisabled, ModuleState::kDisabled, ModuleState::kDisabled};
    EXPECT_TRUE(vehicle.can_abandon());

    const Vehicle restored = RoundTrip(nlohmann::json(vehicle)).get<Vehicle>();
    EXPECT_EQ(restored, vehicle);
    EXPECT_DOUBLE_EQ(restored.armor.front.kinetic_mm, 60.0);
    EXPECT_DOUBLE_EQ(restored.armor.bottom.chemical_mm, 30.0);
}

TEST(WfsCombatModelTest, FourDirectionArmorProfileRoundTrip) {
    const ArmorProfile profile{DirectionalArmor{1.0, 2.0}, DirectionalArmor{3.0, 4.0}, DirectionalArmor{5.0, 6.0},
                               DirectionalArmor{7.0, 8.0}};
    const ArmorProfile restored = RoundTrip(nlohmann::json(profile)).get<ArmorProfile>();
    EXPECT_EQ(restored, profile);
    EXPECT_DOUBLE_EQ(restored.side.kinetic_mm, 3.0);
    EXPECT_DOUBLE_EQ(restored.top.chemical_mm, 6.0);
}

TEST(WfsCombatModelTest, InvalidNumericRangesRejected) {
    // 弹药杀伤力越界 [0,1]。
    Ammo bad_ammo = MakeKineticAmmo();
    bad_ammo.anti_personnel.lethality = 1.5;
    EXPECT_FALSE(bad_ammo.is_valid());
    EXPECT_THROW(RoundTrip(nlohmann::json(bad_ammo)).get<Ammo>(), std::invalid_argument);

    // 武器精度越界。
    Weapon bad_weapon;
    bad_weapon.id = "w-bad";
    bad_weapon.accuracy = 1.2;
    bad_weapon.compatible_ammo = {"ammo-556"};
    EXPECT_FALSE(bad_weapon.is_valid());
    EXPECT_THROW(RoundTrip(nlohmann::json(bad_weapon)).get<Weapon>(), std::invalid_argument);

    // 士兵负负重。
    Soldier bad_soldier = MakeSoldier("s-bad");
    bad_soldier.carry_weight_kg = -1.0;
    EXPECT_FALSE(bad_soldier.is_valid());
    EXPECT_THROW(RoundTrip(nlohmann::json(bad_soldier)).get<Soldier>(), std::invalid_argument);

    // 班组重复士兵 id。
    Squad bad_squad;
    bad_squad.id = "squad-bad";
    bad_squad.soldiers = {MakeSoldier("dup"), MakeSoldier("dup")};
    EXPECT_FALSE(bad_squad.is_valid());
    EXPECT_THROW(RoundTrip(nlohmann::json(bad_squad)).get<Squad>(), std::invalid_argument);

    // 载具摧毁状态与模块不一致。
    Vehicle bad_vehicle;
    bad_vehicle.id = "vehicle-bad";
    bad_vehicle.state = VehicleState::kDestroyed;
    bad_vehicle.modules = ModuleStatus{ModuleState::kFunctional, ModuleState::kFunctional, ModuleState::kFunctional};
    EXPECT_FALSE(bad_vehicle.is_valid());
    EXPECT_THROW(RoundTrip(nlohmann::json(bad_vehicle)).get<Vehicle>(), std::invalid_argument);

    // 载员超过容量。
    Vehicle overflow;
    overflow.id = "vehicle-overflow";
    overflow.passenger_capacity = 1U;
    overflow.passengers = {MakeSoldier("p1"), MakeSoldier("p2")};
    EXPECT_FALSE(overflow.is_valid());
    EXPECT_THROW(RoundTrip(nlohmann::json(overflow)).get<Vehicle>(), std::invalid_argument);

    // 方向防护负值。
    DirectionalArmor bad_armor{-5.0, 0.0};
    EXPECT_FALSE(bad_armor.is_valid());
    EXPECT_THROW(RoundTrip(nlohmann::json(bad_armor)).get<DirectionalArmor>(), std::invalid_argument);
}

TEST(WfsCombatModelTest, NaNValuesRejected) {
    const double nan = std::numeric_limits<double>::quiet_NaN();

    Ammo ammo = MakeKineticAmmo();
    ammo.weight_kg = nan;
    EXPECT_FALSE(ammo.is_valid());
    EXPECT_THROW(nlohmann::json(ammo).get<Ammo>(), std::invalid_argument);

    Weapon weapon;
    weapon.id = "w-nan";
    weapon.accuracy = nan;
    EXPECT_FALSE(weapon.is_valid());
    EXPECT_THROW(nlohmann::json(weapon).get<Weapon>(), std::invalid_argument);

    Soldier soldier = MakeSoldier("s-nan");
    soldier.carry_weight_kg = nan;
    EXPECT_FALSE(soldier.is_valid());
    EXPECT_THROW(nlohmann::json(soldier).get<Soldier>(), std::invalid_argument);

    DirectionalArmor armor{nan, 0.0};
    EXPECT_FALSE(armor.is_valid());
    EXPECT_THROW(nlohmann::json(armor).get<DirectionalArmor>(), std::invalid_argument);
}
