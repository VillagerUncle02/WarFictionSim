// tests/sim_tests/command_model_test.cpp
//
// T025 单元测试：指挥节点/编制/最小可指挥单位模型。
// 覆盖：Owner 玩家/AI 归属与每节点单一 AI 归属约束（FR-048）、经验三维
// 有效性、CommandNode 序列化往返、指挥树（单父/无环/重复拒绝/直下级统计/
// 超限判定，FR-015）、OrganizationUnit 编制层级与类型约束（含合成排最低
// 兵员能力）、MinCommandUnit/MinEquippedUnit 构成与序列化。

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "wfs/sim/model/command_node.h"
#include "wfs/sim/model/min_unit.h"
#include "wfs/sim/model/organization.h"

namespace {

namespace model = wfs::sim::model;

using model::CommandNode;
using model::CommandTree;
using model::Echelon;
using model::EquippedUnitKind;
using model::Experience;
using model::MinCommandUnit;
using model::MinEquippedUnit;
using model::MinUnitKind;
using model::NodeOwner;
using model::OrganizationKind;
using model::OrganizationUnit;
using model::OwnerKind;

nlohmann::json RoundTrip(const nlohmann::json& input) {
    return nlohmann::json::parse(input.dump());
}

CommandNode MakePlayerNode(const std::string& id) {
    return CommandNode{id, "Player " + id, "", NodeOwner{OwnerKind::kPlayer, ""}, 4U, 0.8, Experience{0.7, 0.6, 0.5}};
}

CommandNode MakeAiNode(const std::string& id, const std::string& parent) {
    return CommandNode{
        id, "AI " + id, parent, NodeOwner{OwnerKind::kAi, "ai-" + id}, 4U, 0.7, Experience{0.6, 0.5, 0.4}};
}

OrganizationUnit MakeSquad(const std::string& id, OrganizationKind kind, double capability) {
    return OrganizationUnit{id, id, Echelon::kSquad, kind, "", capability, 0.0, 0.5, {}};
}

OrganizationUnit MakePlatoon(const std::string& id, OrganizationKind kind, double minimum_capability) {
    return OrganizationUnit{id, id, Echelon::kPlatoon, kind, "", 0.0, minimum_capability, 0.7, {}};
}

}  // namespace

TEST(WfsCommandNodeModelTest, OwnerKindStringsAndJsonRoundTrip) {
    EXPECT_EQ(model::to_string(OwnerKind::kPlayer), "player");
    EXPECT_EQ(model::to_string(OwnerKind::kAi), "ai");
    EXPECT_EQ(model::owner_kind_from_string("player"), OwnerKind::kPlayer);
    EXPECT_EQ(model::owner_kind_from_string("ai"), OwnerKind::kAi);
    EXPECT_THROW(model::owner_kind_from_string("human"), std::invalid_argument);

    EXPECT_EQ(RoundTrip(nlohmann::json("player")).get<OwnerKind>(), OwnerKind::kPlayer);
    EXPECT_EQ(RoundTrip(nlohmann::json("ai")).get<OwnerKind>(), OwnerKind::kAi);
}

TEST(WfsCommandNodeModelTest, AiOwnerRequiresSingleAiId) {
    const NodeOwner ai{OwnerKind::kAi, "ai-battalion"};
    EXPECT_TRUE(ai.is_valid());
    EXPECT_TRUE(ai.is_ai());
    EXPECT_FALSE(ai.is_player());

    const NodeOwner missing_ai{OwnerKind::kAi, ""};
    EXPECT_FALSE(missing_ai.is_valid());

    const NodeOwner player{OwnerKind::kPlayer, ""};
    EXPECT_TRUE(player.is_valid());
    EXPECT_TRUE(player.is_player());

    // 玩家接管节点不允许同时保留 AI 归属（FR-048：每节点单一归属）。
    const NodeOwner conflicting{OwnerKind::kPlayer, "ai-still-here"};
    EXPECT_FALSE(conflicting.is_valid());
}

TEST(WfsCommandNodeModelTest, ExperienceValidityAndRoundTrip) {
    const Experience valid{0.5, 0.4, 0.9};
    EXPECT_TRUE(valid.is_valid());
    const Experience out_of_range{1.5, 0.0, 0.0};
    EXPECT_FALSE(out_of_range.is_valid());
    const Experience negative{-0.1, 0.0, 0.0};
    EXPECT_FALSE(negative.is_valid());

    const Experience restored = RoundTrip(nlohmann::json(valid)).get<Experience>();
    EXPECT_EQ(restored, valid);
}

TEST(WfsCommandNodeModelTest, CommandNodeRoundTrip) {
    CommandNode node = MakePlayerNode("node-platoon");
    node.command_limit = 8U;
    node.coordination = 0.65;
    const CommandNode restored = RoundTrip(nlohmann::json(node)).get<CommandNode>();
    EXPECT_EQ(restored, node);
    EXPECT_TRUE(restored.is_root());
    EXPECT_EQ(restored.owner.kind, OwnerKind::kPlayer);
    EXPECT_EQ(restored.command_limit, 8u);
    EXPECT_DOUBLE_EQ(restored.coordination, 0.65);
}

TEST(WfsCommandTreeTest, AddNodesAndEnforceSingleParent) {
    CommandTree tree;
    EXPECT_TRUE(tree.AddNode(MakePlayerNode("root")));
    EXPECT_TRUE(tree.AddNode(MakeAiNode("child-1", "root")));
    EXPECT_TRUE(tree.AddNode(MakeAiNode("child-2", "root")));
    EXPECT_FALSE(tree.AddNode(MakePlayerNode("root")));               // id 重复
    EXPECT_FALSE(tree.AddNode(MakeAiNode("ghost", "ghost-parent")));  // 未知父节点
    EXPECT_EQ(tree.size(), 3u);
    EXPECT_EQ(tree.DirectSubordinateCount("root"), 2u);

    const std::vector<std::string> children = tree.ChildrenOf("root");
    ASSERT_EQ(children.size(), 2u);
    EXPECT_EQ(children[0], "child-1");
    EXPECT_EQ(children[1], "child-2");  // 插入顺序确定性
}

TEST(WfsCommandTreeTest, RejectsSelfParentAndCycles) {
    CommandTree tree;
    ASSERT_TRUE(tree.AddNode(MakePlayerNode("root")));
    ASSERT_TRUE(tree.AddNode(MakeAiNode("a", "root")));
    ASSERT_TRUE(tree.AddNode(MakeAiNode("b", "a")));

    EXPECT_FALSE(tree.SetParent("a", "a"));    // 自环
    EXPECT_FALSE(tree.SetParent("a", "b"));    // 祖先挂到后代 → 环
    EXPECT_TRUE(tree.SetParent("b", "root"));  // 正常重挂
    EXPECT_EQ(tree.ChildrenOf("root").size(), 2u);
    EXPECT_EQ(tree.DirectSubordinateCount("a"), 0u);
    EXPECT_EQ(tree.Find("b")->parent_id, "root");
}

TEST(WfsCommandTreeTest, OverLimitDetection) {
    CommandTree tree;
    CommandNode root = MakePlayerNode("root");
    root.command_limit = 2U;
    ASSERT_TRUE(tree.AddNode(root));
    ASSERT_TRUE(tree.AddNode(MakeAiNode("c1", "root")));
    ASSERT_TRUE(tree.AddNode(MakeAiNode("c2", "root")));
    EXPECT_FALSE(tree.IsOverLimit("root"));

    ASSERT_TRUE(tree.AddNode(MakeAiNode("c3", "root")));
    EXPECT_TRUE(tree.IsOverLimit("root"));  // 超限软性降级入口（FR-015）
    EXPECT_FALSE(tree.IsOverLimit("missing"));
}

TEST(WfsCommandTreeTest, JsonRoundTripPreservesTree) {
    CommandTree tree;
    ASSERT_TRUE(tree.AddNode(MakePlayerNode("root")));
    ASSERT_TRUE(tree.AddNode(MakeAiNode("c1", "root")));
    ASSERT_TRUE(tree.AddNode(MakeAiNode("c2", "c1")));

    const CommandTree restored = RoundTrip(nlohmann::json(tree)).get<CommandTree>();
    EXPECT_EQ(restored.size(), tree.size());
    EXPECT_EQ(restored.ChildrenOf("root"), std::vector<std::string>{"c1"});
    EXPECT_EQ(restored.ChildrenOf("c1"), std::vector<std::string>{"c2"});
    EXPECT_EQ(restored.Find("c2")->parent_id, "c1");
}

TEST(WfsCommandTreeTest, InvalidTreeJsonReportsError) {
    const nlohmann::json duplicate = nlohmann::json{
        {"nodes", nlohmann::json::array({nlohmann::json(MakePlayerNode("a")), nlohmann::json(MakePlayerNode("a"))})}};
    EXPECT_THROW(duplicate.get<CommandTree>(), std::invalid_argument);

    const nlohmann::json unknown_parent = nlohmann::json{
        {"nodes", nlohmann::json::array({nlohmann::json(MakePlayerNode("a")), nlohmann::json(MakeAiNode("b", "a")),
                                         nlohmann::json(MakeAiNode("c", "ghost"))})}};
    EXPECT_THROW(unknown_parent.get<CommandTree>(), std::invalid_argument);
}

TEST(WfsOrganizationModelTest, EchelonAndKindStrings) {
    EXPECT_EQ(model::to_string(Echelon::kSquad), "squad");
    EXPECT_EQ(model::to_string(Echelon::kPlatoon), "platoon");
    EXPECT_EQ(model::to_string(Echelon::kCompany), "company");
    EXPECT_EQ(model::to_string(Echelon::kBattalion), "battalion");
    EXPECT_EQ(model::to_string(Echelon::kBrigade), "brigade");
    EXPECT_EQ(model::echelon_from_string("battalion"), Echelon::kBattalion);
    EXPECT_THROW(model::echelon_from_string("corps"), std::invalid_argument);

    EXPECT_EQ(model::to_string(OrganizationKind::kInfantry), "infantry");
    EXPECT_EQ(model::to_string(OrganizationKind::kArmored), "armored");
    EXPECT_EQ(model::to_string(OrganizationKind::kCombined), "combined");
    EXPECT_EQ(model::organization_kind_from_string("support"), OrganizationKind::kSupport);
    EXPECT_THROW(model::organization_kind_from_string("cavalry"), std::invalid_argument);
}

TEST(WfsOrganizationModelTest, PlatoonTypeConstraints) {
    const OrganizationUnit infantry_platoon = MakePlatoon("p-inf", OrganizationKind::kInfantry, 0.0);
    const OrganizationUnit armored_platoon = MakePlatoon("p-arm", OrganizationKind::kArmored, 0.0);
    const OrganizationUnit infantry_squad = MakeSquad("s-inf", OrganizationKind::kInfantry, 0.8);
    const OrganizationUnit armored_squad = MakeSquad("s-arm", OrganizationKind::kArmored, 0.7);

    EXPECT_TRUE(infantry_platoon.CanContain(infantry_squad));
    EXPECT_FALSE(infantry_platoon.CanContain(armored_squad));
    EXPECT_TRUE(armored_platoon.CanContain(armored_squad));
    EXPECT_FALSE(armored_platoon.CanContain(infantry_squad));
}

TEST(WfsOrganizationModelTest, CombinedPlatoonRequiresMinimumCapability) {
    OrganizationUnit combined = MakePlatoon("p-combined", OrganizationKind::kCombined, 0.7);
    EXPECT_TRUE(combined.CanContain(MakeSquad("s-good", OrganizationKind::kInfantry, 0.8)));
    EXPECT_FALSE(combined.CanContain(MakeSquad("s-weak", OrganizationKind::kInfantry, 0.5)));
    // 装甲单元按载具能力判定，不适用步兵兵员能力约束。
    EXPECT_TRUE(combined.CanContain(MakeSquad("s-arm", OrganizationKind::kArmored, 0.0)));
}

TEST(WfsOrganizationModelTest, HigherEchelonsAreRecursive) {
    const OrganizationUnit company =
        OrganizationUnit{"c1", "c1", Echelon::kCompany, OrganizationKind::kInfantry, "", 0.0, 0.0, 0.8, {}};
    const OrganizationUnit battalion =
        OrganizationUnit{"bn1", "bn1", Echelon::kBattalion, OrganizationKind::kArmored, "", 0.0, 0.0, 0.8, {}};
    const OrganizationUnit platoon = MakePlatoon("p1", OrganizationKind::kInfantry, 0.0);
    const OrganizationUnit armored_company =
        OrganizationUnit{"ac1", "ac1", Echelon::kCompany, OrganizationKind::kArmored, "", 0.0, 0.0, 0.8, {}};

    EXPECT_TRUE(company.CanContain(platoon));
    EXPECT_FALSE(company.CanContain(MakeSquad("s1", OrganizationKind::kInfantry, 0.8)));
    EXPECT_TRUE(battalion.CanContain(armored_company));
    EXPECT_FALSE(battalion.CanContain(platoon));
    EXPECT_FALSE(MakeSquad("leaf", OrganizationKind::kInfantry, 0.8)
                     .CanContain(MakeSquad("s2", OrganizationKind::kInfantry, 0.8)));
}

TEST(WfsOrganizationModelTest, OrganizationUnitRoundTrip) {
    OrganizationUnit unit = MakePlatoon("p-combined", OrganizationKind::kCombined, 0.65);
    unit.subordinate_ids = {"s-inf", "s-arm"};
    const OrganizationUnit restored = RoundTrip(nlohmann::json(unit)).get<OrganizationUnit>();
    EXPECT_EQ(restored, unit);
}

TEST(WfsOrganizationTreeTest, BuildsTreeAndRoundTrips) {
    model::OrganizationTree tree;
    const OrganizationUnit company =
        OrganizationUnit{"c1", "c1", Echelon::kCompany, OrganizationKind::kInfantry, "", 0.0, 0.0, 0.8, {}};
    const OrganizationUnit platoon = MakePlatoon("p1", OrganizationKind::kInfantry, 0.0);
    ASSERT_TRUE(tree.AddUnit(company));
    ASSERT_TRUE(tree.AddUnit(MakeSquad("s1", OrganizationKind::kInfantry, 0.8)));
    ASSERT_TRUE(tree.AddUnit(MakeSquad("s2", OrganizationKind::kInfantry, 0.7)));
    ASSERT_TRUE(tree.AddUnit(platoon));
    EXPECT_TRUE(tree.AddSubordinate("c1", "p1"));
    EXPECT_TRUE(tree.AddSubordinate("p1", "s1"));
    EXPECT_TRUE(tree.AddSubordinate("p1", "s2"));

    // 互反一致：parent_id 与 subordinate_ids 双向同步。
    EXPECT_EQ(tree.Find("p1")->parent_id, "c1");
    EXPECT_EQ(tree.SubordinatesOf("c1"), std::vector<std::string>{"p1"});
    EXPECT_EQ(tree.SubordinatesOf("p1"), (std::vector<std::string>{"s1", "s2"}));

    const model::OrganizationTree restored = RoundTrip(nlohmann::json(tree)).get<model::OrganizationTree>();
    EXPECT_EQ(restored.size(), tree.size());
    EXPECT_EQ(restored.SubordinatesOf("p1"), tree.SubordinatesOf("p1"));
    EXPECT_EQ(restored.Find("s1")->parent_id, "p1");
}

TEST(WfsOrganizationTreeTest, AddUnitRejectsDeclaredSubordinates) {
    model::OrganizationTree tree;
    OrganizationUnit parent = MakePlatoon("p1", OrganizationKind::kInfantry, 0.0);
    parent.subordinate_ids = {"s1"};  // 下级接线统一走 AddSubordinate（F6）。
    EXPECT_FALSE(tree.AddUnit(parent));
    EXPECT_TRUE(tree.AddUnit(MakePlatoon("p1", OrganizationKind::kInfantry, 0.0)));
    EXPECT_TRUE(tree.AddUnit(MakeSquad("s1", OrganizationKind::kInfantry, 0.8)));
    EXPECT_TRUE(tree.AddSubordinate("p1", "s1"));
    EXPECT_EQ(tree.SubordinatesOf("p1"), std::vector<std::string>{"s1"});
}

TEST(WfsOrganizationTreeTest, RejectsInconsistentJson) {
    // 非互反：p1 声明下级 s1，但 s1 的 parent_id 缺失。
    OrganizationUnit parent_claims = MakePlatoon("p1", OrganizationKind::kInfantry, 0.0);
    parent_claims.subordinate_ids = {"s1"};
    const nlohmann::json non_mutual = nlohmann::json{
        {"units", nlohmann::json::array({nlohmann::json(parent_claims),
                                         nlohmann::json(MakeSquad("s1", OrganizationKind::kInfantry, 0.8))})}};
    EXPECT_THROW(non_mutual.get<model::OrganizationTree>(), std::invalid_argument);

    // 未知引用：s1 挂到不存在的父节点。
    OrganizationUnit orphan = MakeSquad("s1", OrganizationKind::kInfantry, 0.8);
    orphan.parent_id = "ghost";
    const nlohmann::json unknown_parent = nlohmann::json{{"units", nlohmann::json::array({nlohmann::json(orphan)})}};
    EXPECT_THROW(unknown_parent.get<model::OrganizationTree>(), std::invalid_argument);

    // 类型约束：步兵排不能包含装甲班。
    OrganizationUnit armored_child = MakeSquad("s1", OrganizationKind::kArmored, 0.7);
    armored_child.parent_id = "p1";
    OrganizationUnit infantry_platoon = MakePlatoon("p1", OrganizationKind::kInfantry, 0.0);
    infantry_platoon.subordinate_ids = {"s1"};
    const nlohmann::json kind_mismatch = nlohmann::json{
        {"units", nlohmann::json::array({nlohmann::json(infantry_platoon), nlohmann::json(armored_child)})}};
    EXPECT_THROW(kind_mismatch.get<model::OrganizationTree>(), std::invalid_argument);
}

TEST(WfsOrganizationTreeTest, RejectsCycleOnSetParent) {
    model::OrganizationTree tree;
    ASSERT_TRUE(tree.AddUnit(MakePlatoon("p1", OrganizationKind::kInfantry, 0.0)));
    ASSERT_TRUE(tree.AddUnit(MakeSquad("s1", OrganizationKind::kInfantry, 0.8)));
    ASSERT_TRUE(tree.AddUnit(MakeSquad("s2", OrganizationKind::kInfantry, 0.7)));
    ASSERT_TRUE(tree.AddSubordinate("p1", "s1"));
    ASSERT_TRUE(tree.AddSubordinate("p1", "s2"));
    // 非法重挂：同级互斥与"父节点位于本节点子树"的成环路径均被拒绝（F6）。
    EXPECT_FALSE(tree.SetParent("s2", "s1"));
    EXPECT_FALSE(tree.SetParent("p1", "s2"));
    EXPECT_EQ(tree.Find("p1")->parent_id, "");
}

TEST(WfsCommandNodeModelTest, InvalidNumericRangesRejected) {
    CommandNode node = MakePlayerNode("node-bad");
    node.coordination = 1.5;
    EXPECT_FALSE(node.is_valid());
    EXPECT_THROW(RoundTrip(nlohmann::json(node)).get<CommandNode>(), std::invalid_argument);

    OrganizationUnit unit = MakeSquad("s-bad", OrganizationKind::kInfantry, 0.8);
    unit.coordination = -0.2;
    EXPECT_FALSE(unit.is_valid());
    EXPECT_THROW(RoundTrip(nlohmann::json(unit)).get<OrganizationUnit>(), std::invalid_argument);
}

TEST(WfsCommandNodeModelTest, NaNValuesRejected) {
    const double nan = std::numeric_limits<double>::quiet_NaN();

    const Experience bad_experience{nan, 0.0, 0.0};
    EXPECT_FALSE(bad_experience.is_valid());
    EXPECT_THROW(nlohmann::json(bad_experience).get<Experience>(), std::invalid_argument);

    CommandNode node = MakePlayerNode("node-nan");
    node.coordination = nan;
    EXPECT_FALSE(node.is_valid());
    EXPECT_THROW(nlohmann::json(node).get<CommandNode>(), std::invalid_argument);

    OrganizationUnit unit = MakeSquad("s-nan", OrganizationKind::kInfantry, nan);
    EXPECT_FALSE(unit.is_valid());
    EXPECT_THROW(nlohmann::json(unit).get<OrganizationUnit>(), std::invalid_argument);
}

TEST(WfsMinUnitModelTest, MinCommandUnitRoundTrip) {
    MinCommandUnit unit;
    unit.id = "mc-squad-1";
    unit.kind = MinUnitKind::kSquad;
    unit.organization_id = "org-squad-rifle";
    unit.equipped_unit_ids = {"e-1", "e-2"};
    unit.parent_unit_id = "team-assault";
    const MinCommandUnit restored = RoundTrip(nlohmann::json(unit)).get<MinCommandUnit>();
    EXPECT_EQ(restored, unit);
}

TEST(WfsMinUnitModelTest, MinEquippedUnitCarriesHeavyWeight) {
    MinEquippedUnit heavy;
    heavy.id = "e-hmg";
    heavy.kind = EquippedUnitKind::kSoldier;
    heavy.combat_ref = "soldier-1";
    heavy.weight_kg = 38.0;
    heavy.heavy_equipment = true;
    const MinEquippedUnit restored = RoundTrip(nlohmann::json(heavy)).get<MinEquippedUnit>();
    EXPECT_EQ(restored, heavy);
    EXPECT_TRUE(restored.heavy_equipment);
    EXPECT_DOUBLE_EQ(restored.weight_kg, 38.0);
}
