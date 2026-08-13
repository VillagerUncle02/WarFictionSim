// tests/sim_tests/command_org_test.cpp
//
// T057：营级指挥节点与编制校验单元测试（宪法第 2/7/12 条）。
//
// 覆盖：command_org 构建（层级映射/编制树）、直属班协调能力基数（FR-016）、
// 指挥树无环/层级链/节点编制一致性校验、非法编制的结构化报错与
// CommandOrgState JSON 往返序列化（宪法第 13 条）。

#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "wfs/sim/command_org.h"

namespace {

using wfs::sim::CommandOrgLoadResult;
using wfs::sim::CommandOrgState;
using wfs::sim::DataIssue;
using wfs::sim::direct_squad_base;
using wfs::sim::load_command_org;
using wfs::sim::validate_command_org;
using wfs::sim::model::Echelon;

nlohmann::json NodeJson(const std::string& id, const std::string& name, const std::string& echelon,
                        const std::string& parent_id, const std::string& org_unit_id) {
    return nlohmann::json{{"id", id},
                          {"name", name},
                          {"echelon", echelon},
                          {"parent_id", parent_id},
                          {"org_unit_id", org_unit_id},
                          {"owner", nlohmann::json{{"kind", "ai"}, {"ai_id", id + "-ai"}}},
                          {"command_limit", 4U},
                          {"coordination", 0.8},
                          {"experience", nlohmann::json{{"training", 0.7}, {"service", 0.6}, {"combat", 0.5}}}};
}

nlohmann::json OrgJson(const std::string& id, const std::string& name, const std::string& echelon,
                       const std::string& kind, const std::string& parent_id,
                       const std::vector<std::string>& subordinates) {
    return nlohmann::json{{"id", id},
                          {"name", name},
                          {"echelon", echelon},
                          {"kind", kind},
                          {"parent_id", parent_id},
                          {"soldier_capability", 0.8},
                          {"minimum_soldier_capability", 0.5},
                          {"coordination", 0.8},
                          {"subordinate_ids", subordinates}};
}

nlohmann::json ValidRoot() {
    return nlohmann::json{
        {"command_org",
         nlohmann::json{
             {"nodes", nlohmann::json::array({NodeJson("node-bn-1", "1营", "battalion", "", "org-bn-1"),
                                              NodeJson("node-co-1", "1连", "company", "node-bn-1", "org-co-1"),
                                              NodeJson("node-co-2", "2连", "company", "node-bn-1", "org-co-2"),
                                              NodeJson("node-plt-1", "1排", "platoon", "node-co-1", "org-plt-1"),
                                              NodeJson("node-plt-2", "2排", "platoon", "node-co-1", "org-plt-2"),
                                              NodeJson("node-plt-3", "3排", "platoon", "node-co-2", "org-plt-3")})},
             {"organizations",
              nlohmann::json::array(
                  {OrgJson("org-bn-1", "1营", "battalion", "combined", "", {"org-co-1", "org-co-2"}),
                   OrgJson("org-co-1", "1连", "company", "infantry", "org-bn-1", {"org-plt-1", "org-plt-2"}),
                   OrgJson("org-co-2", "2连", "company", "infantry", "org-bn-1", {"org-plt-3"}),
                   OrgJson("org-plt-1", "1排", "platoon", "infantry", "org-co-1", {"org-sq-1", "org-sq-2", "org-sq-3"}),
                   OrgJson("org-plt-2", "2排", "platoon", "infantry", "org-co-1", {"org-sq-4", "org-sq-5", "org-sq-6"}),
                   OrgJson("org-plt-3", "3排", "platoon", "infantry", "org-co-2", {"org-sq-7", "org-sq-8"}),
                   OrgJson("org-sq-1", "1班", "squad", "infantry", "org-plt-1", {}),
                   OrgJson("org-sq-2", "2班", "squad", "infantry", "org-plt-1", {}),
                   OrgJson("org-sq-3", "3班", "squad", "infantry", "org-plt-1", {}),
                   OrgJson("org-sq-4", "4班", "squad", "infantry", "org-plt-2", {}),
                   OrgJson("org-sq-5", "5班", "squad", "infantry", "org-plt-2", {}),
                   OrgJson("org-sq-6", "6班", "squad", "infantry", "org-plt-2", {}),
                   OrgJson("org-sq-7", "7班", "squad", "infantry", "org-plt-3", {}),
                   OrgJson("org-sq-8", "8班", "squad", "infantry", "org-plt-3", {})})}}}};
}

bool HasIssueCode(const std::vector<DataIssue>& issues, const std::string& code) {
    for (const DataIssue& issue : issues) {
        if (issue.code == code) {
            return true;
        }
    }
    return false;
}

TEST(WfsCommandOrgTest, LoadsValidHierarchyAndMapsEchelons) {
    const CommandOrgLoadResult result = load_command_org(ValidRoot());
    ASSERT_TRUE(result.ok()) << (result.issues.empty()
                                     ? ""
                                     : result.issues.front().code + ": " + result.issues.front().message);
    EXPECT_TRUE(result.state.configured);
    EXPECT_EQ(result.state.node_echelon.at("node-bn-1"), Echelon::kBattalion);
    EXPECT_EQ(result.state.node_echelon.at("node-co-1"), Echelon::kCompany);
    EXPECT_EQ(result.state.node_echelon.at("node-plt-1"), Echelon::kPlatoon);
    EXPECT_EQ(result.state.node_org_unit.at("node-bn-1"), "org-bn-1");
    EXPECT_EQ(result.state.children_of("node-bn-1").size(), 2U);
}

TEST(WfsCommandOrgTest, DirectSquadBaseCountsSquadsNotPersonnel) {
    const CommandOrgLoadResult result = load_command_org(ValidRoot());
    ASSERT_TRUE(result.ok());
    // FR-016：协调能力按班计数（而非人数）；营 = 全部 8 班，连 = 6/2 班，排 = 3/3/2 班。
    EXPECT_EQ(direct_squad_base(result.state, "node-bn-1"), 8U);
    EXPECT_EQ(direct_squad_base(result.state, "node-co-1"), 6U);
    EXPECT_EQ(direct_squad_base(result.state, "node-co-2"), 2U);
    EXPECT_EQ(direct_squad_base(result.state, "node-plt-1"), 3U);
    EXPECT_EQ(direct_squad_base(result.state, "node-plt-3"), 2U);
    EXPECT_EQ(direct_squad_base(result.state, "node-missing"), 0U);
}

TEST(WfsCommandOrgTest, LoadsChildBeforeParentInInsertionOrder) {
    // R1 正例：合法但"子节点声明在前"的顺序必须成功加载（校验与构建顺序无关）。
    nlohmann::json root = ValidRoot();
    const nlohmann::json& nodes = root["command_org"]["nodes"];
    root["command_org"]["nodes"] =
        nlohmann::json::array({nodes[4], nodes[3], nodes[5], nodes[2], nodes[1], nodes[0]});
    const CommandOrgLoadResult result = load_command_org(root);
    ASSERT_TRUE(result.ok()) << (result.issues.empty()
                                     ? ""
                                     : result.issues.front().code + ": " + result.issues.front().message);
    EXPECT_TRUE(result.state.configured);
    EXPECT_EQ(result.state.children_of("node-bn-1").size(), 2U);
    EXPECT_EQ(direct_squad_base(result.state, "node-bn-1"), 8U);
    // 节点插入顺序保持 JSON 声明顺序（宪法第 7 条：确定性）。
    EXPECT_EQ(result.state.nodes_in_insertion_order(),
              (std::vector<std::string>{"node-plt-2", "node-plt-1", "node-plt-3", "node-co-2", "node-co-1",
                                        "node-bn-1"}));
}

TEST(WfsCommandOrgTest, IllegalTreeReportsStructuredIssuesWithoutThrowing) {
    // R1 非法树：校验已产出结构化 issue，构建路径不得再抛异常（宪法第 17 条）。
    nlohmann::json root = ValidRoot();
    root["command_org"]["nodes"].push_back(NodeJson("node-cycle-a", "环A", "platoon", "node-cycle-b", "org-sq-1"));
    root["command_org"]["nodes"].push_back(NodeJson("node-cycle-b", "环B", "company", "node-cycle-a", "org-co-1"));
    const CommandOrgLoadResult result = load_command_org(root);
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(HasIssueCode(result.issues, "COMMAND_ORG_NODE_CYCLE"));
}

TEST(WfsCommandOrgTest, RejectsCycleWithStructuredIssue) {
    nlohmann::json root = ValidRoot();
    root["command_org"]["nodes"].push_back(NodeJson("node-cycle-a", "环A", "platoon", "node-cycle-b", "org-sq-1"));
    root["command_org"]["nodes"].push_back(NodeJson("node-cycle-b", "环B", "company", "node-cycle-a", "org-co-1"));
    std::vector<DataIssue> issues;
    validate_command_org(root, issues);
    EXPECT_TRUE(HasIssueCode(issues, "COMMAND_ORG_NODE_CYCLE"));
}

TEST(WfsCommandOrgTest, RejectsEchelonChainViolation) {
    nlohmann::json root = ValidRoot();
    // 公司节点挂在排节点下：子级 echelon 不低于父级 → 层级链非法。
    root["command_org"]["nodes"].push_back(NodeJson("node-bad", "倒挂连", "company", "node-plt-1", "org-co-1"));
    std::vector<DataIssue> issues;
    validate_command_org(root, issues);
    EXPECT_TRUE(HasIssueCode(issues, "COMMAND_ORG_NODE_ECHELON_CHAIN"));
}

TEST(WfsCommandOrgTest, RejectsNodeOrgEchelonMismatch) {
    nlohmann::json root = ValidRoot();
    // 排级节点锚定营编制单位：echelon 不一致必须报错（宪法 17：不静默）。
    root["command_org"]["nodes"].push_back(NodeJson("node-mis", "错配排", "platoon", "node-co-2", "org-bn-1"));
    std::vector<DataIssue> issues;
    validate_command_org(root, issues);
    EXPECT_TRUE(HasIssueCode(issues, "COMMAND_ORG_NODE_ORG_ECHELON_MISMATCH"));
}

TEST(WfsCommandOrgTest, RejectsOrganizationParentSubordinateMismatch) {
    // S6：编制单位声明的 subordinate_ids 与 parent_id 推导不一致 → 结构化报错。
    nlohmann::json root = ValidRoot();
    for (nlohmann::json& unit : root["command_org"]["organizations"]) {
        if (unit.at("id").get<std::string>() == "org-co-1") {
            unit["subordinate_ids"] = nlohmann::json::array({"org-plt-1"});
        }
    }
    std::vector<DataIssue> issues;
    validate_command_org(root, issues);
    EXPECT_TRUE(HasIssueCode(issues, "COMMAND_ORG_ORGANIZATION_MISMATCH"));
}

TEST(WfsCommandOrgTest, RejectsOrganizationCycle) {
    // S6：编制树成环（同级互为父子触发 CanContain/成环校验）→ 结构化报错。
    nlohmann::json root = ValidRoot();
    for (nlohmann::json& unit : root["command_org"]["organizations"]) {
        const std::string id = unit.at("id").get<std::string>();
        if (id == "org-co-1") {
            unit["parent_id"] = "org-co-2";
        } else if (id == "org-co-2") {
            unit["parent_id"] = "org-co-1";
        }
    }
    std::vector<DataIssue> issues;
    validate_command_org(root, issues);
    EXPECT_TRUE(HasIssueCode(issues, "COMMAND_ORG_ORGANIZATION_LINK_INVALID"));
}

TEST(WfsCommandOrgTest, RejectsDuplicateNodeAndOrganizationIds) {
    // S6：指挥节点与编制单位重复 id 均须产出结构化报错。
    nlohmann::json root = ValidRoot();
    root["command_org"]["nodes"].push_back(NodeJson("node-plt-1", "重复排", "platoon", "node-co-1", "org-plt-1"));
    root["command_org"]["organizations"].push_back(
        OrgJson("org-sq-1", "重复班", "squad", "infantry", "org-plt-1", {}));
    std::vector<DataIssue> issues;
    validate_command_org(root, issues);
    EXPECT_TRUE(HasIssueCode(issues, "COMMAND_ORG_NODE_DUPLICATE_ID"));
    EXPECT_TRUE(HasIssueCode(issues, "COMMAND_ORG_ORGANIZATION_DUPLICATE_ID"));
}

TEST(WfsCommandOrgTest, RejectsSquadCommandNode) {
    // S6：玩家不扮演班一级（FR-001），squad 层级节点必须在解析阶段拒绝。
    nlohmann::json root = ValidRoot();
    root["command_org"]["nodes"].push_back(NodeJson("node-squad", "班节点", "squad", "node-co-1", "org-sq-1"));
    std::vector<DataIssue> issues;
    validate_command_org(root, issues);
    EXPECT_TRUE(HasIssueCode(issues, "COMMAND_ORG_NODE_INVALID"));
}

TEST(WfsCommandOrgTest, RejectsUnitNodeNotInCommandTree) {
    nlohmann::json root = ValidRoot();
    root["units"] = nlohmann::json::array({nlohmann::json{{"id", "squad-x"}, {"node_id", "node-unknown"}}});
    root["player_node_id"] = "node-unknown";
    std::vector<DataIssue> issues;
    validate_command_org(root, issues);
    EXPECT_TRUE(HasIssueCode(issues, "COMMAND_ORG_UNIT_NODE_UNKNOWN"));
    EXPECT_TRUE(HasIssueCode(issues, "COMMAND_ORG_PLAYER_NODE_MISSING"));
}

TEST(WfsCommandOrgTest, RoundTripsThroughJson) {
    const CommandOrgLoadResult result = load_command_org(ValidRoot());
    ASSERT_TRUE(result.ok());
    const CommandOrgState restored = nlohmann::json(result.state).get<CommandOrgState>();
    EXPECT_TRUE(restored.configured);
    EXPECT_EQ(restored.node_echelon, result.state.node_echelon);
    EXPECT_EQ(restored.node_org_unit, result.state.node_org_unit);
    EXPECT_EQ(direct_squad_base(restored, "node-bn-1"), 8U);
    EXPECT_EQ(restored.organizations.size(), result.state.organizations.size());
}

}  // namespace
