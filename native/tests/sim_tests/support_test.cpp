// tests/sim_tests/support_test.cpp
//
// T046/T047 单元测试：战术分队归建与支援请求状态机（测试先行，RED→GREEN）。
//
// 覆盖（tasks.md T046/T047；FR-010/008；data-model §4/§14）：
// - 任务结束归建无单位丢失：战术分队解散后全部存活成员归还原属；
// - 损失/失联成员处置：仅存活成员归建，伤亡成员进入伤亡记录；
// - 拆分命令作用域：拆分状态下整班命令拒绝、火力组命令放行、恢复行政
//   编制后整班命令恢复（FR-010/045）；
// - SupportRequest 状态机：SUBMITTED→EVALUATING→EXECUTING/REJECTED、
//   转请（ESCALATED）可重新评估、终态不可转移、非法转移显式拒绝；
// - 序列化往返与 id 唯一性（存档复用，宪法第 13 条）。

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "wfs/sim/support.h"
#include "wfs/sim/tactical.h"

namespace {

using wfs::sim::can_transition;
using wfs::sim::CommandScopeResult;
using wfs::sim::compute_return_to_parent;
using wfs::sim::FireTeam;
using wfs::sim::MergeResult;
using wfs::sim::ReturnToParentResult;
using wfs::sim::SplitResult;
using wfs::sim::SupportChain;
using wfs::sim::SupportRequest;
using wfs::sim::SupportRequestState;
using wfs::sim::TacticalKind;
using wfs::sim::TacticalRegistry;
using wfs::sim::TacticalState;
using wfs::sim::TacticalTaskForce;

std::vector<std::string> SoldierIds(std::size_t count) {
    std::vector<std::string> ids;
    ids.reserve(count);
    for (std::size_t i = 0U; i < count; ++i) {
        ids.push_back("soldier-" + std::to_string(i));
    }
    return ids;
}

}  // namespace

// ---- 战术分队：拆分/合并/解除编成 ----

TEST(WfsSupportTest, SplitSquadDistributesDeterministically) {
    TacticalRegistry registry;
    const std::vector<std::string> soldiers = SoldierIds(9U);
    const SplitResult result = registry.SplitSquad("squad-a", soldiers, 2U);
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_EQ(result.teams.size(), 2U);
    // 确定性分配：余数依次补入前面的组 → 9 = 5 + 4。
    EXPECT_EQ(result.teams[0].soldier_ids.size(), 5U);
    EXPECT_EQ(result.teams[1].soldier_ids.size(), 4U);
    EXPECT_EQ(result.teams[0].id, "ft-squad-a-0");
    EXPECT_EQ(result.teams[1].id, "ft-squad-a-1");
    EXPECT_EQ(result.teams[0].admin_squad_id, "squad-a");
    EXPECT_EQ(result.teams[0].kind, TacticalKind::kFireTeam);
    EXPECT_TRUE(registry.IsSplit("squad-a"));

    // 同一输入必然产生同一输出（宪法第 7 条）。
    TacticalRegistry again;
    const SplitResult second = again.SplitSquad("squad-a", soldiers, 2U);
    ASSERT_TRUE(second.ok);
    EXPECT_EQ(second.teams, result.teams);
}

TEST(WfsSupportTest, SplitSquadRejectsInvalidRequests) {
    TacticalRegistry registry;
    // 火力组数量为 0。
    const SplitResult zero_teams = registry.SplitSquad("squad-a", SoldierIds(6U), 0U);
    EXPECT_FALSE(zero_teams.ok);
    // 火力组数量超过成员数。
    const SplitResult too_many = registry.SplitSquad("squad-a", SoldierIds(2U), 3U);
    EXPECT_FALSE(too_many.ok);
    // 空成员清单。
    const SplitResult empty = registry.SplitSquad("squad-a", {}, 2U);
    EXPECT_FALSE(empty.ok);
    // 已拆分班组不得重复拆分（战术编成互斥）。
    const SplitResult first = registry.SplitSquad("squad-b", SoldierIds(4U), 2U);
    ASSERT_TRUE(first.ok);
    const SplitResult duplicate = registry.SplitSquad("squad-b", SoldierIds(4U), 2U);
    EXPECT_FALSE(duplicate.ok);
    EXPECT_TRUE(duplicate.error.find("已拆分") != std::string::npos);
}

TEST(WfsSupportTest, MergeDepletedSquadsThenDissolveRestoresAdminOrganization) {
    TacticalRegistry registry;
    // 两个损失严重的班各剩 2 名幸存者，临时合并为一个火力组继续任务。
    std::vector<std::string> survivors{"squad-a-s0", "squad-a-s1", "squad-b-s0", "squad-b-s1"};
    const MergeResult merged = registry.MergeDepletedSquads("ft-merged-1", "squad-a", survivors);
    ASSERT_TRUE(merged.ok) << merged.error;
    EXPECT_EQ(merged.merged.id, "ft-merged-1");
    EXPECT_EQ(merged.merged.admin_squad_id, "squad-a");
    EXPECT_EQ(merged.merged.soldier_ids, survivors);

    // 解除战术编成、恢复行政编制：合并火力组解散，成员不丢失（FR-010）。
    EXPECT_TRUE(registry.DissolveFireTeams("squad-a"));
    EXPECT_FALSE(registry.IsSplit("squad-a"));
    // 解散记录保留（存档/复盘可见），状态标记为已解散。
    const FireTeam* dissolved_team = registry.FindFireTeam("ft-merged-1");
    ASSERT_NE(dissolved_team, nullptr);
    EXPECT_EQ(dissolved_team->state, TacticalState::kDissolved);
}

// ---- 拆分命令作用域（FR-010/045）----

TEST(WfsSupportTest, SplitStateScopesCommandsToFireTeams) {
    TacticalRegistry registry;
    ASSERT_TRUE(registry.SplitSquad("squad-a", SoldierIds(6U), 2U).ok);

    // 拆分状态下针对整班的命令必须拒绝，且原因明确（宪法 17 不静默）。
    const CommandScopeResult squad_command = registry.ResolveCommandScope("squad-a");
    EXPECT_FALSE(squad_command.valid);
    EXPECT_EQ(squad_command.error, "SPLIT_SQUAD_COMMAND_NOT_ALLOWED");

    // 针对火力组的命令放行，作用域为火力组本身。
    const CommandScopeResult team_command = registry.ResolveCommandScope("ft-squad-a-0");
    EXPECT_TRUE(team_command.valid);
    EXPECT_EQ(team_command.effective_unit_id, "ft-squad-a-0");

    // 恢复行政编制后整班命令恢复。
    ASSERT_TRUE(registry.DissolveFireTeams("squad-a"));
    const CommandScopeResult restored = registry.ResolveCommandScope("squad-a");
    EXPECT_TRUE(restored.valid);
    EXPECT_EQ(restored.effective_unit_id, "squad-a");
}

// ---- 战术分队归建：无单位丢失、损失/失联处置（FR-010/044）----

TEST(WfsSupportTest, TaskForceDissolveReturnsAllMembersWithoutLoss) {
    TacticalRegistry registry;
    TacticalTaskForce force;
    force.id = "tf-1";
    force.task_id = "cmd-0";
    force.member_unit_ids = {"squad-a", "vehicle-1", "ft-squad-b-0"};
    ASSERT_TRUE(registry.FormTaskForce(force));

    const ReturnToParentResult result =
        compute_return_to_parent(*registry.FindTaskForce("tf-1"), force.member_unit_ids);
    ASSERT_TRUE(result.ok);
    // 全部存活：归建清单与成员清单一致（无单位丢失，US2 验收场景 3）。
    EXPECT_EQ(result.returned_unit_ids, force.member_unit_ids);
    EXPECT_TRUE(result.casualty_unit_ids.empty());

    ASSERT_TRUE(registry.DissolveTaskForce("tf-1"));
    const wfs::sim::TacticalTaskForce* dissolved = registry.FindTaskForce("tf-1");
    ASSERT_NE(dissolved, nullptr);
    EXPECT_EQ(dissolved->state, TacticalState::kDissolved);
}

TEST(WfsSupportTest, ReturnToParentKeepsOnlySurvivorsAndRecordsCasualties) {
    TacticalTaskForce force;
    force.id = "tf-2";
    force.task_id = "cmd-1";
    force.member_unit_ids = {"squad-a", "squad-b", "squad-c", "vehicle-1"};

    // squad-b 被消灭、vehicle-1 失联：只归建存活单位，损失/失联进入伤亡记录。
    const ReturnToParentResult result =
        compute_return_to_parent(force, std::vector<std::string>{"squad-a", "squad-c"});
    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.returned_unit_ids.size(), 2U);
    EXPECT_EQ(result.returned_unit_ids[0], "squad-a");
    EXPECT_EQ(result.returned_unit_ids[1], "squad-c");
    ASSERT_EQ(result.casualty_unit_ids.size(), 2U);
    EXPECT_EQ(result.casualty_unit_ids[0], "squad-b");
    EXPECT_EQ(result.casualty_unit_ids[1], "vehicle-1");
}

// ---- 支援请求状态机（T047）----

TEST(WfsSupportTest, SupportRequestStateNamesRoundTrip) {
    EXPECT_EQ(wfs::sim::to_string(SupportRequestState::kSubmitted), "SUBMITTED");
    EXPECT_EQ(wfs::sim::to_string(SupportRequestState::kEvaluating), "EVALUATING");
    EXPECT_EQ(wfs::sim::to_string(SupportRequestState::kExecuting), "EXECUTING");
    EXPECT_EQ(wfs::sim::to_string(SupportRequestState::kRejected), "REJECTED");
    EXPECT_EQ(wfs::sim::to_string(SupportRequestState::kEscalated), "ESCALATED");
    EXPECT_EQ(wfs::sim::support_request_state_from_string("SUBMITTED"), SupportRequestState::kSubmitted);
    EXPECT_EQ(wfs::sim::support_request_state_from_string("REJECTED"), SupportRequestState::kRejected);
    EXPECT_THROW(wfs::sim::support_request_state_from_string("BOGUS"), std::invalid_argument);
}

TEST(WfsSupportTest, SupportRequestStateMachineTransitions) {
    // 核心链路：SUBMITTED → EVALUATING → EXECUTING/REJECTED（data-model §14）。
    EXPECT_TRUE(can_transition(SupportRequestState::kSubmitted, SupportRequestState::kEvaluating));
    EXPECT_TRUE(can_transition(SupportRequestState::kEvaluating, SupportRequestState::kExecuting));
    EXPECT_TRUE(can_transition(SupportRequestState::kEvaluating, SupportRequestState::kRejected));
    // 营级转请：EVALUATING → ESCALATED，更上级重新评估可回到 EVALUATING，
    // 无更上级时明确拒绝（SC-004：始终得到明确响应）。
    EXPECT_TRUE(can_transition(SupportRequestState::kEvaluating, SupportRequestState::kEscalated));
    EXPECT_TRUE(can_transition(SupportRequestState::kEscalated, SupportRequestState::kEvaluating));
    EXPECT_TRUE(can_transition(SupportRequestState::kEscalated, SupportRequestState::kRejected));
    // 终态不可再转移；非法转移一律拒绝。
    EXPECT_FALSE(can_transition(SupportRequestState::kExecuting, SupportRequestState::kRejected));
    EXPECT_FALSE(can_transition(SupportRequestState::kRejected, SupportRequestState::kEvaluating));
    EXPECT_FALSE(can_transition(SupportRequestState::kSubmitted, SupportRequestState::kExecuting));
    EXPECT_FALSE(can_transition(SupportRequestState::kExecuting, SupportRequestState::kEvaluating));
}

TEST(WfsSupportTest, SupportChainEnforcesUniqueIdsAndValidTransitions) {
    SupportChain chain;
    SupportRequest request;
    request.id = "req-1";
    request.seq = 7U;
    request.priority = 3;
    request.from_node = "node-platoon-1";
    request.to_node = "node-battalion-1";
    request.target_unit = "squad-a";
    request.request_type = "reinforce";
    request.kinds = {"squad-mortar-team"};
    request.quantity = 1U;

    ASSERT_NE(chain.Submit(request), nullptr);
    EXPECT_EQ(chain.size(), 1U);
    EXPECT_TRUE(chain.HasCommand("cmd-1") == false);

    // 重复 id 拒绝（不覆盖既有请求）。
    SupportRequest duplicate = request;
    duplicate.id = "req-1";
    EXPECT_EQ(chain.Submit(duplicate), nullptr);
    EXPECT_EQ(chain.size(), 1U);

    // 非法状态转移拒绝且状态不变（宪法 17：显式失败）。
    EXPECT_FALSE(chain.Transition("req-1", SupportRequestState::kExecuting));
    EXPECT_EQ(chain.Find("req-1")->state, SupportRequestState::kSubmitted);
    EXPECT_TRUE(chain.Transition("req-1", SupportRequestState::kEvaluating));
    EXPECT_EQ(chain.Find("req-1")->state, SupportRequestState::kEvaluating);
    EXPECT_TRUE(chain.Transition("req-1", SupportRequestState::kExecuting));

    // 提交顺序即确定性顺序。
    SupportRequest second = request;
    second.id = "req-2";
    ASSERT_NE(chain.Submit(second), nullptr);
    EXPECT_EQ(chain.RequestsInSubmitOrder()[0].id, "req-1");
    EXPECT_EQ(chain.RequestsInSubmitOrder()[1].id, "req-2");
}

TEST(WfsSupportTest, SupportRequestAndChainSerializeRoundTrip) {
    SupportRequest request;
    request.id = "req-9";
    request.seq = 11U;
    request.priority = 4;
    request.command_id = "cmd-3";
    request.from_node = "node-platoon-2";
    request.to_node = "node-battalion-1";
    request.target_unit = "squad-b";
    request.request_type = "fire_support";
    request.kinds = {"mortar-82", "smoke"};
    request.quantity = 2U;
    request.for_command_id = "cmd-2";
    request.return_after_ticks = 60U;
    request.submitted_tick = 5U;
    request.evaluating_tick = 9U;
    request.resolved_tick = 11U;
    request.state = SupportRequestState::kExecuting;
    request.resolution = "assign";
    request.assigned_unit_ids = {"mortar-82"};

    const nlohmann::json json = request;
    const SupportRequest restored = json.get<SupportRequest>();
    EXPECT_EQ(restored, request);

    SupportChain chain;
    SupportRequest submitted = request;
    submitted.state = SupportRequestState::kSubmitted;
    ASSERT_NE(chain.Submit(submitted), nullptr);
    ASSERT_TRUE(chain.Transition(request.id, SupportRequestState::kEvaluating));
    ASSERT_TRUE(chain.Transition(request.id, SupportRequestState::kExecuting));
    const nlohmann::json chain_json = chain;
    const SupportChain restored_chain = chain_json.get<SupportChain>();
    EXPECT_EQ(restored_chain.RequestsInSubmitOrder(), chain.RequestsInSubmitOrder());
}
