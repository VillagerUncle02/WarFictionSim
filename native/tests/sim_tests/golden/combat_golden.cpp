// tests/sim_tests/golden/combat_golden.cpp
//
// T024 战斗结算黄金测试（quickstart §3.3；FR-054–065；宪法 2/7）。
//
// 覆盖：
// - 命中/伤害/压制/失联：固定种子两次运行逐字段一致（同输入同输出），
//   并与登记的黄金样例 JSON 完全一致（黄金变更流程同 golden-run.hash）；
// - 区域结算→个人防护衔接（FR-060/061）：覆盖率→命中人数→逐士兵穿深判定；
// - 目标选择黄金样例（FR-060：威胁 + 距离 + 弹药适配确定性排序）；
// - 自动选弹与不匹配降级黄金样例（FR-058/060：最合适可用弹药 + 明确提示）。
//
// 本文件先于 T031 编写：在 combat.h 实现前编译失败即为 RED 证据；
// 实现完成后按测试头部注释流程登记黄金样例（固定输入下的已知结果）。

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "wfs/sim/combat.h"
#include "wfs/sim/model/combat.h"
#include "wfs/sim/rng.h"

namespace {

using wfs::sim::AmmoSelectionInput;
using wfs::sim::AmmoSelectionResult;
using wfs::sim::AreaEngagementInput;
using wfs::sim::AreaEngagementResult;
using wfs::sim::CombatConfig;
using wfs::sim::ContactLossInput;
using wfs::sim::ContactLossResult;
using wfs::sim::DamageInput;
using wfs::sim::DamageResult;
using wfs::sim::HitDirection;
using wfs::sim::HitInput;
using wfs::sim::HitResult;
using wfs::sim::Rng;
using wfs::sim::SuppressionInput;
using wfs::sim::SuppressionResult;
using wfs::sim::TargetCandidate;
using wfs::sim::TargetSelectionInput;
using wfs::sim::TargetSelectionResult;
using wfs::sim::model::Ammo;
using wfs::sim::model::AntiArmorProfile;
using wfs::sim::model::AntiPersonnelProfile;
using wfs::sim::model::ArmorClass;
using wfs::sim::model::ArmorProfile;
using wfs::sim::model::CoverState;
using wfs::sim::model::DirectionalArmor;
using wfs::sim::model::EngagementPolicy;
using wfs::sim::model::Experience;
using wfs::sim::model::Formation;
using wfs::sim::model::Protection;
using wfs::sim::model::Soldier;
using wfs::sim::model::Squad;
using wfs::sim::model::Vehicle;
using wfs::sim::model::WarheadKind;
using wfs::sim::model::Weapon;

constexpr std::uint64_t kGoldenSeed = 42U;
constexpr std::uint64_t kGoldenStream = 7U;
constexpr std::uint64_t kContactLossStream = 8U;  // 该流下失联判定命中（黄金样例）。

// 黄金样例登记表：固定输入 + 固定种子下的逐字段期望 JSON。
// 修改公式/模型后按 golden-run.hash 同流程显式更新并人工确认确定性语义。
const std::map<std::string, std::string>& GoldenSamples() {
    static const std::map<std::string, std::string> samples = {
        {"hit", R"({"hit":false,"hit_chance":0.19200000000000003,"roll":0.4554726032074541})"},
        {"kinetic_penetrate",
         R"({"armor_mm":25.0,"capped":false,"damage":50.0,"damage_multiplier":1.0,"effective_penetration_mm":25.5,"note":"","overmatch":false,"penetrated":true})"},
        {"kinetic_no_penetrate",
         R"({"armor_mm":80.0,"capped":false,"damage":0.0,"damage_multiplier":0.0,"effective_penetration_mm":7.6,"note":"未击穿","overmatch":false,"penetrated":false})"},
        {"chemical_penetrate_cap",
         R"({"armor_mm":20.0,"capped":true,"damage":500.0,"damage_multiplier":2.0,"effective_penetration_mm":300.0,"note":"化学能上限","overmatch":false,"penetrated":true})"},
        {"overmatch_decay",
         R"({"armor_mm":5.0,"capped":false,"damage":25.342633942103483,"damage_multiplier":0.5068526788420696,"effective_penetration_mm":25.5,"note":"过穿衰减","overmatch":true,"penetrated":true})"},
        {"area_engagement",
         R"({"coverage":0.6,"hit_count":5,"outcomes":[{"armor_penetrated":false,"armor_score":2.0,"casualty":true,"soldier_id":"squad-a-s0","suppressed":false},{"armor_penetrated":false,"armor_score":2.0,"casualty":true,"soldier_id":"squad-a-s1","suppressed":false},{"armor_penetrated":false,"armor_score":2.0,"casualty":true,"soldier_id":"squad-a-s2","suppressed":false},{"armor_penetrated":false,"armor_score":2.0,"casualty":true,"soldier_id":"squad-a-s3","suppressed":false},{"armor_penetrated":false,"armor_score":2.0,"casualty":false,"soldier_id":"squad-a-s4","suppressed":true}],"suppression_added":0.1388888888888889})"},
        {"suppression", R"({"added":0.5,"total":0.7})"},
        {"contact_loss", R"({"duration_ticks":2267,"lost":true,"probability":0.24000000000000002})"},
        {"target_selection",
         R"({"ranked":[{"distance_m":250.0,"target_armor_mm":0.0,"target_is_vehicle":false,"threat":1.0,"unit_id":"enemy-squad-threat"},{"distance_m":150.0,"target_armor_mm":0.0,"target_is_vehicle":false,"threat":0.5,"unit_id":"enemy-squad-near"},{"distance_m":300.0,"target_armor_mm":0.0,"target_is_vehicle":false,"threat":0.8,"unit_id":"enemy-squad-far"}],"score":0.75,"target_id":"enemy-squad-threat"})"},
        {"target_selection_ammo_fit_ap",
         R"({"ranked":[{"distance_m":250.0,"target_armor_mm":20.0,"target_is_vehicle":true,"threat":1.0,"unit_id":"vehicle-far"},{"distance_m":200.0,"target_armor_mm":0.0,"target_is_vehicle":false,"threat":1.0,"unit_id":"infantry-near"}],"score":1.0,"target_id":"vehicle-far"})"},
        {"target_selection_ammo_fit_ball",
         R"({"ranked":[{"distance_m":200.0,"target_armor_mm":0.0,"target_is_vehicle":false,"threat":1.0,"unit_id":"infantry-near"},{"distance_m":250.0,"target_armor_mm":20.0,"target_is_vehicle":true,"threat":1.0,"unit_id":"vehicle-far"}],"score":0.6033000000000001,"target_id":"infantry-near"})"},
        {"ammo_selection_infantry",
         R"({"ammo_id":"ammo-frag-grenade","effectiveness":1.2,"mismatch":false,"note":""})"},
        {"ammo_selection_vehicle",
         R"({"ammo_id":"ammo-556","effectiveness":15.600000000000001,"mismatch":false,"note":""})"},
        {"ammo_mismatch_degradation",
         R"({"ammo_id":"ammo-556","effectiveness":0.96,"mismatch":true,"note":"弹药不匹配/效果有限"})"},
    };
    return samples;
}

void AssertGolden(const std::string& name, const nlohmann::json& actual) {
    const std::string text = actual.dump();
    const auto it = GoldenSamples().find(name);
    ASSERT_NE(it, GoldenSamples().end()) << "缺少黄金样例登记: " << name;
    EXPECT_EQ(text, it->second) << "黄金样例不一致: " << name << "\n实际值: " << text;
}

Weapon MakeRifle() {
    Weapon weapon;
    weapon.id = "rifle-556";
    weapon.name = "5.56mm Rifle";
    weapon.category = "rifle";
    weapon.warhead_kind = WarheadKind::kKinetic;
    weapon.effective_range_m = 500.0;
    weapon.accuracy = 0.8;
    weapon.min_crew = 1U;
    weapon.compatible_ammo = {"ammo-556", "ammo-frag-grenade"};
    return weapon;
}

Ammo Make556() {
    return Ammo{"ammo-556",
                "5.56mm Ball",
                WarheadKind::kKinetic,
                AntiArmorProfile{8.0, 12.0},
                AntiPersonnelProfile{0.0, 2.0, 0.5},
                false,
                0.012};
}

Ammo Make127() {
    return Ammo{"ammo-127",
                "12.7mm AP",
                WarheadKind::kKinetic,
                AntiArmorProfile{30.0, 50.0},
                AntiPersonnelProfile{0.0, 4.0, 0.9},
                false,
                0.12};
}

Ammo MakeRpg() {
    return Ammo{"ammo-rpg",
                "RPG HEAT",
                WarheadKind::kChemical,
                AntiArmorProfile{300.0, 250.0},
                AntiPersonnelProfile{6.0, 12.0, 0.85},
                false,
                2.1};
}

Ammo MakeHe60() {
    return Ammo{"ammo-he-60",
                "60mm HE",
                WarheadKind::kChemical,
                AntiArmorProfile{40.0, 80.0},
                AntiPersonnelProfile{12.0, 25.0, 1.0},
                false,
                1.7};
}

Ammo MakeSmoke() {
    return Ammo{"ammo-smoke",
                "Smoke Round",
                WarheadKind::kSpecial,
                AntiArmorProfile{0.0, 0.0},
                AntiPersonnelProfile{0.0, 0.0, 0.0},
                true,
                0.5};
}

Squad MakeRifleSquad() {
    Squad squad;
    squad.id = "squad-a";
    squad.name = "US Rifle Squad";
    squad.footprint_radius_m = 15.0;
    squad.formation = Formation::kMarch;
    squad.cover = CoverState::kPartial;
    for (std::uint32_t i = 0U; i < 9U; ++i) {
        Soldier soldier;
        soldier.id = "squad-a-s" + std::to_string(i);
        soldier.name = "S" + std::to_string(i);
        soldier.weapon_id = "rifle-556";
        soldier.experience = Experience{0.5, 0.5, 0.4};
        soldier.protection = Protection{true, ArmorClass::kLight};
        if (i >= 7U) {
            soldier.protection = Protection{false, ArmorClass::kNone};  // 混编无防护样本。
        }
        squad.soldiers.push_back(std::move(soldier));
    }
    return squad;
}

Vehicle MakeLightVehicle() {
    Vehicle vehicle;
    vehicle.id = "vehicle-1";
    vehicle.name = "Light Armored Vehicle";
    vehicle.chassis_id = "chassis-light";
    vehicle.armor = ArmorProfile{DirectionalArmor{80.0, 60.0}, DirectionalArmor{25.0, 20.0},
                                 DirectionalArmor{10.0, 8.0}, DirectionalArmor{5.0, 4.0}};
    vehicle.crew_capacity = 3U;
    vehicle.passenger_capacity = 5U;
    for (std::uint32_t i = 0U; i < 3U; ++i) {
        Soldier crew;
        crew.id = "vehicle-1-c" + std::to_string(i);
        crew.name = "C" + std::to_string(i);
        crew.experience = Experience{0.6, 0.6, 0.5};
        crew.protection = Protection{true, ArmorClass::kLight};
        vehicle.crew.push_back(std::move(crew));
    }
    for (std::uint32_t i = 0U; i < 5U; ++i) {
        Soldier passenger;
        passenger.id = "vehicle-1-p" + std::to_string(i);
        passenger.name = "P" + std::to_string(i);
        passenger.experience = Experience{0.5, 0.5, 0.4};
        passenger.protection = Protection{true, ArmorClass::kLight};
        vehicle.passengers.push_back(std::move(passenger));
    }
    return vehicle;
}

TEST(WfsCombatGolden, HitResolutionStableAndMatchesGolden) {
    const CombatConfig config = CombatConfig::Defaults();
    const Weapon rifle = MakeRifle();
    const Ammo ammo = Make556();
    const HitInput input{&rifle, &ammo, 300.0, Formation::kMarch, CoverState::kPartial, false, 15.0, 0.0,
                         0.0,    0.5,   1.0};
    Rng first(kGoldenSeed, kGoldenStream);
    Rng second(kGoldenSeed, kGoldenStream);
    const HitResult result = wfs::sim::resolve_hit(input, config, first);
    const HitResult repeated = wfs::sim::resolve_hit(input, config, second);
    EXPECT_EQ(nlohmann::json(result).dump(), nlohmann::json(repeated).dump())
        << "同种子两次命中结算不一致（确定性违约）";
    AssertGolden("hit", nlohmann::json(result));
}

TEST(WfsCombatGolden, DamageKineticPenetrationAndNonPenetration) {
    const CombatConfig config = CombatConfig::Defaults();
    const Vehicle vehicle = MakeLightVehicle();
    const Ammo ap = Make127();
    const Ammo ball = Make556();

    const DamageResult penetrated = wfs::sim::resolve_damage(
        DamageInput{&ap, 300.0, &vehicle.armor, wfs::sim::vehicle_hp(vehicle), HitDirection::kSide}, config);
    AssertGolden("kinetic_penetrate", nlohmann::json(penetrated));

    const DamageResult rejected = wfs::sim::resolve_damage(
        DamageInput{&ball, 100.0, &vehicle.armor, wfs::sim::vehicle_hp(vehicle), HitDirection::kFront}, config);
    AssertGolden("kinetic_no_penetrate", nlohmann::json(rejected));
}

TEST(WfsCombatGolden, DamageChemicalPenetrationCapAndOvermatchDecay) {
    const CombatConfig config = CombatConfig::Defaults();
    const Vehicle vehicle = MakeLightVehicle();
    const Ammo heat = MakeRpg();
    const Ammo he60 = MakeHe60();

    const DamageResult chemical = wfs::sim::resolve_damage(
        DamageInput{&heat, 400.0, &vehicle.armor, wfs::sim::vehicle_hp(vehicle), HitDirection::kSide}, config);
    AssertGolden("chemical_penetrate_cap", nlohmann::json(chemical));

    // 薄甲过穿：12.7mm AP 对 5mm 底部装甲，动能差值过大触发过穿衰减（FR-057）。
    const Ammo ap = Make127();
    const DamageResult overmatch = wfs::sim::resolve_damage(
        DamageInput{&ap, 300.0, &vehicle.armor, wfs::sim::vehicle_hp(vehicle), HitDirection::kBottom}, config);
    AssertGolden("overmatch_decay", nlohmann::json(overmatch));
}

TEST(WfsCombatGolden, AreaEngagementBridgesPersonalProtection) {
    const CombatConfig config = CombatConfig::Defaults();
    const Squad squad = MakeRifleSquad();
    const Ammo he = MakeHe60();
    Rng first(kGoldenSeed, kGoldenStream);
    Rng second(kGoldenSeed, kGoldenStream);
    const AreaEngagementResult result =
        wfs::sim::resolve_area_engagement(AreaEngagementInput{&he, &squad, CoverState::kPartial, 0.0}, config, first);
    const AreaEngagementResult repeated =
        wfs::sim::resolve_area_engagement(AreaEngagementInput{&he, &squad, CoverState::kPartial, 0.0}, config, second);
    EXPECT_EQ(nlohmann::json(result).dump(), nlohmann::json(repeated).dump())
        << "同种子两次区域结算不一致（确定性违约）";
    AssertGolden("area_engagement", nlohmann::json(result));
}

TEST(WfsCombatGolden, SuppressionAndContactLossDeterministic) {
    const CombatConfig config = CombatConfig::Defaults();
    const SuppressionResult suppression = wfs::sim::resolve_suppression(SuppressionInput{0.2, 0.5, false}, config);
    AssertGolden("suppression", nlohmann::json(suppression));

    Rng first(kGoldenSeed, kContactLossStream);
    Rng second(kGoldenSeed, kContactLossStream);
    const ContactLossResult lost = wfs::sim::resolve_contact_loss(ContactLossInput{0.9, 0.4, true}, config, first);
    const ContactLossResult repeated = wfs::sim::resolve_contact_loss(ContactLossInput{0.9, 0.4, true}, config, second);
    EXPECT_EQ(nlohmann::json(lost).dump(), nlohmann::json(repeated).dump()) << "同种子两次失联结算不一致（确定性违约）";
    EXPECT_GE(lost.duration_ticks, config.contact_loss_min_ticks);
    EXPECT_LE(lost.duration_ticks, config.contact_loss_max_ticks);
    AssertGolden("contact_loss", nlohmann::json(lost));
}

TEST(WfsCombatGolden, AreaSuppressionAppliedMatchesHitRatioScaledLogValue) {
    // FR-063 回归（PR #111 第 2 轮 R3-1）：区域火力实际施加的单位压制必须按
    // 命中比例缩放，且与 COMBAT_AREA_HIT 日志记录的 suppression_added 一致。
    const CombatConfig config = CombatConfig::Defaults();
    const Squad squad = MakeRifleSquad();
    const Ammo he = MakeHe60();
    Rng rng(kGoldenSeed, kGoldenStream);
    const AreaEngagementResult area =
        wfs::sim::resolve_area_engagement(AreaEngagementInput{&he, &squad, CoverState::kPartial, 0.0}, config, rng);
    ASSERT_LT(area.hit_count, squad.soldiers.size()) << "样本需覆盖命中比例 < 1 的局部命中场景";
    ASSERT_GT(area.suppression_added, 0.0);

    const double hit_ratio = static_cast<double>(area.hit_count) / static_cast<double>(squad.soldiers.size());
    const SuppressionResult suppression =
        wfs::sim::resolve_suppression(SuppressionInput{0.2, he.anti_personnel.lethality, true, hit_ratio}, config);
    EXPECT_DOUBLE_EQ(suppression.added, config.suppression_area_gain * he.anti_personnel.lethality * hit_ratio);
    EXPECT_DOUBLE_EQ(suppression.added, area.suppression_added) << "实际施加的压制增量应与日志 suppression_added 一致";
    EXPECT_DOUBLE_EQ(suppression.total, 0.2 + suppression.added);
}

TEST(WfsCombatGolden, TargetSelectionGoldenSample) {
    const CombatConfig config = CombatConfig::Defaults();
    const Weapon rifle = MakeRifle();
    const TargetSelectionInput input{
        &rifle,
        0.0,
        0.0,
        {{"enemy-squad-near", 150.0, 0.5}, {"enemy-squad-far", 300.0, 0.8}, {"enemy-squad-threat", 250.0, 1.0}},
        EngagementPolicy::kAggressive};
    const TargetSelectionResult result = wfs::sim::select_target(input, config);
    AssertGolden("target_selection", nlohmann::json(result));
}

TEST(WfsCombatGolden, TargetSelectionIncludesAmmoFit) {
    // F6：目标选择评分必须纳入弹药适配（ammo_fit_weight），AP 弹药可用时
    // 更远的装甲目标可反超更近的步兵目标；仅普通弹时距离主导。
    const CombatConfig config = CombatConfig::Defaults();
    Weapon rifle = MakeRifle();
    rifle.compatible_ammo = {"ammo-556", "ammo-at"};
    const Ammo ball = Make556();
    const Ammo ap = Ammo{
        "ammo-at", "Test AP", WarheadKind::kKinetic, AntiArmorProfile{30.0, 100.0}, AntiPersonnelProfile{0.0, 1.0, 0.5},
        false,     1.0};
    const std::vector<TargetCandidate> candidates{
        {"vehicle-far", 250.0, 1.0, true, 20.0},
        {"infantry-near", 200.0, 1.0, false, 0.0},
    };

    const TargetSelectionResult with_ap = wfs::sim::select_target(
        TargetSelectionInput{&rifle, 0.0, 0.0, candidates, EngagementPolicy::kBalanced, {&ap}}, config);
    AssertGolden("target_selection_ammo_fit_ap", nlohmann::json(with_ap));

    const TargetSelectionResult with_ball = wfs::sim::select_target(
        TargetSelectionInput{&rifle, 0.0, 0.0, candidates, EngagementPolicy::kBalanced, {&ball}}, config);
    AssertGolden("target_selection_ammo_fit_ball", nlohmann::json(with_ball));
}

TEST(WfsCombatGolden, AutoAmmoSelectionAndMismatchDegradation) {
    const CombatConfig config = CombatConfig::Defaults();
    const Weapon rifle = MakeRifle();
    const Ammo ball = Make556();
    const Ammo grenade = Ammo{"ammo-frag-grenade",
                              "Fragmentation Grenade",
                              WarheadKind::kChemical,
                              AntiArmorProfile{0.0, 10.0},
                              AntiPersonnelProfile{5.0, 15.0, 0.6},
                              false,
                              0.4};
    const Ammo smoke = MakeSmoke();

    const AmmoSelectionResult infantry =
        wfs::sim::select_ammo(AmmoSelectionInput{&rifle, {&ball, &grenade}, false, 0.0, 150.0}, config);
    AssertGolden("ammo_selection_infantry", nlohmann::json(infantry));

    const AmmoSelectionResult vehicle =
        wfs::sim::select_ammo(AmmoSelectionInput{&rifle, {&ball, &grenade}, true, 5.0, 100.0}, config);
    AssertGolden("ammo_selection_vehicle", nlohmann::json(vehicle));

    // 不匹配降级：装甲目标但只有 5.56 弹 → 按最合适可用弹药开火并明确提示。
    const AmmoSelectionResult degraded =
        wfs::sim::select_ammo(AmmoSelectionInput{&rifle, {&ball, &smoke}, true, 25.0, 100.0}, config);
    EXPECT_TRUE(degraded.mismatch);
    AssertGolden("ammo_mismatch_degradation", nlohmann::json(degraded));
}

}  // namespace
