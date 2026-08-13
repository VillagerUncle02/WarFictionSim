// tests/sim_tests/interaction_test.cpp
//
// T051 单元测试：四种上下级交互类型约束。
// 覆盖（FR-050；contracts/command-schema.md §5）：
// - TASK_DISPATCH/EXECUTION/SUMMARY_REPORT/SUPPORT_REQUEST 强制枚举；
// - 解析对缺失/未知类型与收发节点的显式报错；
// - 同级经上级转发（FR-028/050）与跨层级直接送达；
// - 无共同上级/未知节点/自交互的确定性错误。

#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "wfs/sim/interaction.h"

namespace {

namespace model = wfs::sim::model;

using wfs::sim::InteractionParseResult;
using wfs::sim::InteractionRecord;
using wfs::sim::InteractionType;
using wfs::sim::parse_interaction;
using wfs::sim::route_interaction;

// 营 → 两个连 → 各两个排 的指挥树（营级多节点，用于同级转发用例）。
model::CommandTree MakeTree() {
    model::CommandTree tree;
    tree.AddNode(model::CommandNode{"node-bn", "营", "", model::NodeOwner{}, 8U, 1.0, {}});
    tree.AddNode(model::CommandNode{"node-co-1", "一连", "node-bn", model::NodeOwner{}, 4U, 0.9, {}});
    tree.AddNode(model::CommandNode{"node-co-2", "二连", "node-bn", model::NodeOwner{}, 4U, 0.9, {}});
    tree.AddNode(model::CommandNode{"node-plt-1", "一排", "node-co-1", model::NodeOwner{}, 3U, 0.8, {}});
    tree.AddNode(model::CommandNode{"node-plt-2", "二排", "node-co-1", model::NodeOwner{}, 3U, 0.8, {}});
    tree.AddNode(model::CommandNode{"node-plt-3", "三排", "node-co-2", model::NodeOwner{}, 3U, 0.8, {}});
    return tree;
}

}  // namespace

TEST(WfsInteractionTest, FourInteractionTypesEnforceStrictEnum) {
    EXPECT_EQ(wfs::sim::to_string(InteractionType::kTaskDispatch), "TASK_DISPATCH");
    EXPECT_EQ(wfs::sim::to_string(InteractionType::kExecution), "EXECUTION");
    EXPECT_EQ(wfs::sim::to_string(InteractionType::kSummaryReport), "SUMMARY_REPORT");
    EXPECT_EQ(wfs::sim::to_string(InteractionType::kSupportRequest), "SUPPORT_REQUEST");
    EXPECT_EQ(wfs::sim::interaction_type_from_string("TASK_DISPATCH"), InteractionType::kTaskDispatch);
    EXPECT_EQ(wfs::sim::interaction_type_from_string("SUPPORT_REQUEST"), InteractionType::kSupportRequest);
    // 强制枚举：未知值显式抛错（宪法 17）。
    EXPECT_THROW(wfs::sim::interaction_type_from_string("CHAT"), std::invalid_argument);
    EXPECT_THROW(wfs::sim::interaction_type_from_string("task_dispatch"), std::invalid_argument);
}

TEST(WfsInteractionTest, ParseRequiresInteractionTypeAndEndpoints) {
    const nlohmann::json valid{{"interaction_type", "SUPPORT_REQUEST"},
                               {"from_node", "node-plt-1"},
                               {"to_node", "node-co-1"},
                               {"payload", {{"request_id", "req-1"}}}};
    const InteractionParseResult parsed = parse_interaction(valid);
    EXPECT_TRUE(parsed.ok());
    EXPECT_EQ(parsed.record.type, InteractionType::kSupportRequest);
    EXPECT_EQ(parsed.record.from_node, "node-plt-1");
    EXPECT_EQ(parsed.record.to_node, "node-co-1");
    EXPECT_EQ(parsed.record.payload.at("request_id"), "req-1");

    const InteractionParseResult missing = parse_interaction(nlohmann::json{{"from_node", "a"}, {"to_node", "b"}});
    EXPECT_FALSE(missing.ok());
    EXPECT_NE(missing.errors.front().find("interaction_type"), std::string::npos);

    const InteractionParseResult unknown =
        parse_interaction(nlohmann::json{{"interaction_type", "BROADCAST"}, {"from_node", "a"}, {"to_node", "b"}});
    EXPECT_FALSE(unknown.ok());
    EXPECT_NE(unknown.errors.front().find("未知交互类型"), std::string::npos);

    const InteractionParseResult no_nodes =
        parse_interaction(nlohmann::json{{"interaction_type", "EXECUTION"}});
    EXPECT_FALSE(no_nodes.ok());
    EXPECT_EQ(no_nodes.errors.size(), 2U);
}

TEST(WfsInteractionTest, SameLevelRoutesThroughCommonSuperior) {
    const model::CommandTree tree = MakeTree();

    // 同层级（排↔排）：经共同上级（连）转发，不直接横向通信。
    const auto same_level = route_interaction(tree, "node-plt-1", "node-plt-2");
    ASSERT_TRUE(same_level.ok()) << same_level.error;
    ASSERT_EQ(same_level.hops.size(), 3U);
    EXPECT_EQ(same_level.hops[0], "node-plt-1");
    EXPECT_EQ(same_level.hops[1], "node-co-1");
    EXPECT_EQ(same_level.hops[2], "node-plt-2");

    // 跨连的排↔排：经营转发（最近共同上级）。
    const auto cross_company = route_interaction(tree, "node-plt-1", "node-plt-3");
    ASSERT_TRUE(cross_company.ok()) << cross_company.error;
    ASSERT_EQ(cross_company.hops.size(), 3U);
    EXPECT_EQ(cross_company.hops[1], "node-bn");

    // 跨层级（排↔连、连↔营）：直接送达。
    const auto vertical = route_interaction(tree, "node-plt-1", "node-co-1");
    ASSERT_TRUE(vertical.ok()) << vertical.error;
    ASSERT_EQ(vertical.hops.size(), 2U);
    EXPECT_EQ(vertical.hops[0], "node-plt-1");
    EXPECT_EQ(vertical.hops[1], "node-co-1");
}

TEST(WfsInteractionTest, RoutingRejectsInvalidEndpointsExplicitly) {
    const model::CommandTree tree = MakeTree();

    const auto unknown_sender = route_interaction(tree, "node-missing", "node-plt-1");
    EXPECT_FALSE(unknown_sender.ok());
    EXPECT_NE(unknown_sender.error.find("发送方节点不存在"), std::string::npos);

    const auto self = route_interaction(tree, "node-plt-1", "node-plt-1");
    EXPECT_FALSE(self.ok());

    // 两棵独立树的同层级节点：无共同上级，显式报错。
    model::CommandTree disjoint;
    disjoint.AddNode(model::CommandNode{"root-a", "A", "", model::NodeOwner{}, 1U, 1.0, {}});
    disjoint.AddNode(model::CommandNode{"leaf-a", "a", "root-a", model::NodeOwner{}, 1U, 1.0, {}});
    disjoint.AddNode(model::CommandNode{"root-b", "B", "", model::NodeOwner{}, 1U, 1.0, {}});
    disjoint.AddNode(model::CommandNode{"leaf-b", "b", "root-b", model::NodeOwner{}, 1U, 1.0, {}});
    const auto no_common = route_interaction(disjoint, "leaf-a", "leaf-b");
    EXPECT_FALSE(no_common.ok());
}

TEST(WfsInteractionTest, InteractionRecordSerializeRoundTrip) {
    InteractionRecord record;
    record.type = InteractionType::kExecution;
    record.from_node = "node-plt-1";
    record.to_node = "node-co-1";
    record.payload = {{"status", "COMPLETED"}, {"command_id", "cmd-2"}};

    const nlohmann::json json = record;
    const InteractionRecord restored = json.get<InteractionRecord>();
    EXPECT_EQ(restored, record);
}
