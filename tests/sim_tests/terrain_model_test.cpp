// tests/sim_tests/terrain_model_test.cpp
//
// T027 单元测试：地形/设施/工事/环境模型。
// 覆盖：统一 passability（速度系数/封锁/水域/需桥梁/两栖，FR-022）、
// TerrainElement 序列化往返、设施可见性规则与生命周期（部署/取消/重布置/
// 侦察残留，FR-014）、工事适用武器类别（FR-067）、EnvironmentState 序列化
// 与静态天气/光照效果。

#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "wfs/sim/model/terrain.h"

namespace {

namespace model = wfs::sim::model;

using model::EnvironmentState;
using model::Facility;
using model::FacilityKind;
using model::FacilityLifecycleState;
using model::FacilityVisibility;
using model::Fortification;
using model::FortificationKind;
using model::LightLevel;
using model::Passability;
using model::TerrainClass;
using model::TerrainElement;
using model::TerrainType;
using model::Weather;

nlohmann::json RoundTrip(const nlohmann::json& input) {
    return nlohmann::json::parse(input.dump());
}

Facility MakeDeployableFacility(const std::string& id) {
    return Facility{id,
                    id,
                    FacilityKind::kDeployable,
                    FacilityVisibility::kHiddenUntilRecon,
                    FacilityLifecycleState::kDeployed,
                    1.0,
                    1.0,
                    false,
                    false,
                    0.0,
                    0.0};
}

}  // namespace

TEST(WfsTerrainModelTest, EnumStringsRoundTrip) {
    EXPECT_EQ(model::to_string(TerrainClass::kBase), "base");
    EXPECT_EQ(model::to_string(TerrainClass::kSurfaceCover), "surface_cover");
    EXPECT_EQ(model::to_string(TerrainClass::kArtificial), "artificial");
    EXPECT_EQ(model::terrain_class_from_string("environment"), TerrainClass::kEnvironment);
    EXPECT_THROW(model::terrain_class_from_string("void"), std::invalid_argument);

    EXPECT_EQ(model::to_string(TerrainType::kRiver), "river");
    EXPECT_EQ(model::terrain_type_from_string("bridge"), TerrainType::kBridge);
    EXPECT_THROW(model::terrain_type_from_string("cliff"), std::invalid_argument);

    EXPECT_EQ(model::to_string(Weather::kFog), "fog");
    EXPECT_EQ(model::weather_from_string("rain"), Weather::kRain);
    EXPECT_EQ(model::to_string(LightLevel::kNight), "night");
    EXPECT_EQ(model::light_level_from_string("dusk_dawn"), LightLevel::kDuskDawn);
}

TEST(WfsTerrainModelTest, UnifiedPassabilityRules) {
    const Passability plain{1.0, false, false, false, true};
    EXPECT_TRUE(plain.can_traverse(false, false));

    const Passability river{0.3, false, true, true, true};
    EXPECT_FALSE(river.can_traverse(false, false));  // 非两栖且无桥梁不可通行
    EXPECT_TRUE(river.can_traverse(false, true));    // 桥梁可通行
    EXPECT_TRUE(river.can_traverse(true, false));    // 两栖单位可浮渡/泅渡

    const Passability blocked{0.0, true, false, false, true};
    EXPECT_FALSE(blocked.can_traverse(true, true));

    const Passability swamp{0.4, false, false, false, true};
    EXPECT_TRUE(swamp.can_traverse(false, false));
    EXPECT_DOUBLE_EQ(swamp.speed_multiplier, 0.4);  // 沼泽减速
}

TEST(WfsTerrainModelTest, TerrainElementRoundTrip) {
    TerrainElement element;
    element.id = "terrain-river";
    element.name = "River";
    element.terrain_class = TerrainClass::kBase;
    element.type = TerrainType::kRiver;
    element.passability = Passability{0.3, false, true, true, true};
    element.concealment = 0.1;
    element.cover = 0.0;

    const TerrainElement restored = RoundTrip(nlohmann::json(element)).get<TerrainElement>();
    EXPECT_EQ(restored, element);
    EXPECT_TRUE(restored.passability.requires_bridge);
}

TEST(WfsFacilityModelTest, VisibilityRules) {
    const Facility fixed{Facility{"fac-hospital", "Hospital", FacilityKind::kFunctional,
                                  FacilityVisibility::kFixedVisible, FacilityLifecycleState::kDeployed, 5.0, 5.0, false,
                                  false, 0.0, 0.0}};
    EXPECT_TRUE(fixed.is_visible());  // 固定设施默认可见（FR-014）

    const Facility coordinate{Facility{"fac-rally", "Rally Point", FacilityKind::kDeployable,
                                       FacilityVisibility::kCoordinateOnly, FacilityLifecycleState::kDeployed, 2.0, 2.0,
                                       true, false, 0.0, 0.0}};
    EXPECT_FALSE(coordinate.is_visible());  // 纯坐标类布置不显示（FR-014）

    Facility deployable = MakeDeployableFacility("fac-hq");
    EXPECT_FALSE(deployable.is_visible());  // 可布置设施初始隐藏
    deployable.ConfirmRecon();
    EXPECT_TRUE(deployable.is_visible());  // 侦察发现后显示
}

TEST(WfsFacilityModelTest, LifecycleDeployCancelRedeploy) {
    Facility facility = MakeDeployableFacility("fac-hq");
    facility.ConfirmRecon();
    EXPECT_TRUE(facility.is_visible());

    facility.Cancel();
    EXPECT_EQ(facility.lifecycle, FacilityLifecycleState::kCancelled);
    EXPECT_TRUE(facility.recon_residue);  // 旧位置侦察信息残留（FR-014）

    facility.Deploy(3.0, 4.0);
    EXPECT_EQ(facility.lifecycle, FacilityLifecycleState::kDeployed);
    EXPECT_DOUBLE_EQ(facility.x, 3.0);
    EXPECT_DOUBLE_EQ(facility.y, 4.0);
    EXPECT_TRUE(facility.recon_residue);  // 重布置后旧位置残留仍存在
    EXPECT_DOUBLE_EQ(facility.old_x, 1.0);
    EXPECT_DOUBLE_EQ(facility.old_y, 1.0);
    EXPECT_FALSE(facility.recon_confirmed);  // 新位置需再次侦察确认
    EXPECT_FALSE(facility.is_visible());

    facility.ConfirmRecon();
    EXPECT_FALSE(facility.recon_residue);  // 再次确认后残留清除
    EXPECT_TRUE(facility.is_visible());
}

TEST(WfsFacilityModelTest, DestroyKeepsResidue) {
    Facility facility = MakeDeployableFacility("fac-supply");
    facility.ConfirmRecon();
    facility.Destroy();
    EXPECT_EQ(facility.lifecycle, FacilityLifecycleState::kDestroyed);
    EXPECT_TRUE(facility.recon_residue);
    EXPECT_FALSE(facility.is_visible());
}

TEST(WfsFacilityModelTest, FacilityRoundTrip) {
    Facility facility = MakeDeployableFacility("fac-medical");
    facility.ConfirmRecon();
    const Facility restored = RoundTrip(nlohmann::json(facility)).get<Facility>();
    EXPECT_EQ(restored, facility);
    EXPECT_TRUE(restored.is_visible());
}

TEST(WfsFortificationModelTest, ApplicableWeaponCategories) {
    Fortification mg_emplacement;
    mg_emplacement.id = "fort-mg";
    mg_emplacement.kind = FortificationKind::kSpecialized;
    mg_emplacement.applicable_weapon_categories = {"mg"};
    EXPECT_TRUE(mg_emplacement.applies_full_bonus("mg"));
    EXPECT_FALSE(mg_emplacement.applies_full_bonus("mortar"));  // 非匹配装备仅基础加成

    Fortification trench;
    trench.id = "fort-trench";
    trench.kind = FortificationKind::kTrench;
    EXPECT_TRUE(trench.applies_full_bonus("rifle"));  // 通用工事对所有类别完整加成
}

TEST(WfsFortificationModelTest, FortificationRoundTrip) {
    Fortification fort;
    fort.id = "fort-camouflage";
    fort.name = "Camouflage Position";
    fort.kind = FortificationKind::kCamouflage;
    fort.concealment_bonus = 0.4;
    fort.cover_bonus = 0.1;
    fort.detection_reduction = 0.5;
    fort.construction_ticks = 1200U;
    const Fortification restored = RoundTrip(nlohmann::json(fort)).get<Fortification>();
    EXPECT_EQ(restored, fort);
}

TEST(WfsEnvironmentModelTest, StaticEnvironmentRoundTrip) {
    EnvironmentState state;
    state.weather = Weather::kFog;
    state.light = LightLevel::kDuskDawn;
    state.visibility_multiplier = 0.4;
    state.mobility_multiplier = 0.8;
    state.accuracy_multiplier = 0.9;
    const EnvironmentState restored = RoundTrip(nlohmann::json(state)).get<EnvironmentState>();
    EXPECT_EQ(restored, state);
    EXPECT_DOUBLE_EQ(restored.visibility_multiplier, 0.4);
}
