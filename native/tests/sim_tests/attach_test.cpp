// tests/sim_tests/attach_test.cpp
//
// T050 单元测试：配属链仲裁与归建。
// 覆盖（FR-008/009；data-model §14；宪法第 7 条）：
// - 单请求裁决（连排级有限分数 / 营级转请）；
// - 多请求（优先级, 到达序列号）确定性仲裁；
// - 配属 → 归建 → 归还生命周期与占用统计；
// - 归建途中重新配属（新请求优先于归建）与瘫痪阻塞；
// - 途中补给/维修确定性 hook（US3 本体后置）。

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "wfs/sim/attach.h"

namespace {

using wfs::sim::adjudicate;
using wfs::sim::arbitrate_requests;
using wfs::sim::AttachDecision;
using wfs::sim::AttachDecisionKind;
using wfs::sim::AttachRecord;
using wfs::sim::AttachRegistry;
using wfs::sim::AttachUnitState;
using wfs::sim::available_force;
using wfs::sim::EchelonResourcePool;
using wfs::sim::PoolEntryKind;
using wfs::sim::PoolSnapshot;
using wfs::sim::resolve_transit_logistics;
using wfs::sim::ResourceAllocation;
using wfs::sim::ResourcePoolEntry;
using wfs::sim::SupportRequest;
using wfs::sim::TransitLogisticsAction;

EchelonResourcePool MakePool() {
    EchelonResourcePool pool;
    pool.support_score = 60U;
    pool.entries = {
        ResourcePoolEntry{"squad-mortar-team", PoolEntryKind::kUnit, 2U, 30U},
        ResourcePoolEntry{"squad-atgm-team", PoolEntryKind::kUnit, 1U, 25U},
    };
    return pool;
}

SupportRequest MakeRequest(const std::string& id, std::uint64_t seq, std::int64_t priority,
                           const std::vector<std::string>& kinds, std::uint64_t quantity = 1U) {
    SupportRequest request;
    request.id = id;
    request.seq = seq;
    request.priority = priority;
    request.from_node = "node-platoon-1";
    request.to_node = "node-battalion-1";
    request.target_unit = "squad-a";
    request.request_type = "reinforce";
    request.kinds = kinds;
    request.quantity = quantity;
    return request;
}

}  // namespace

TEST(WfsAttachTest, PlatoonLimitedScoreAdjudicatesAssignAndReject) {
    const EchelonResourcePool pool = MakePool();
    const PoolSnapshot snapshot = available_force(pool, {});

    // 分数充足 → 配属并扣分（30，剩余 30 明确可见）。
    const AttachDecision assign = adjudicate(MakeRequest("r1", 1U, 5, {"squad-mortar-team"}), snapshot, true, false);
    EXPECT_EQ(assign.kind, AttachDecisionKind::kAssign);
    EXPECT_EQ(assign.units, (std::vector<std::string>{"squad-mortar-team"}));
    EXPECT_EQ(assign.score_cost, 30U);
    EXPECT_EQ(assign.score_remaining, 30U);

    // 分数不足 → 明确拒绝（INSUFFICIENT_SCORE，用尽即止）。
    PoolSnapshot low = snapshot;
    low.remaining_score = 20U;
    const AttachDecision insufficient =
        adjudicate(MakeRequest("r2", 2U, 5, {"squad-mortar-team"}), low, true, false);
    EXPECT_EQ(insufficient.kind, AttachDecisionKind::kReject);
    EXPECT_EQ(insufficient.reason, "INSUFFICIENT_SCORE");

    // 范围外 → SCOPE_VIOLATION 拒绝（连排级请求范围受营编制约束，FR-008）。
    const AttachDecision out_of_scope =
        adjudicate(MakeRequest("r3", 3U, 5, {"artillery-152"}), snapshot, true, false);
    EXPECT_EQ(out_of_scope.kind, AttachDecisionKind::kReject);
    EXPECT_NE(out_of_scope.reason.find("SCOPE_VIOLATION"), std::string::npos);
}

TEST(WfsAttachTest, BattalionChainEscalatesWhenUnavailableAndRejectsWithoutSuperior) {
    const EchelonResourcePool pool = MakePool();
    const PoolSnapshot snapshot = available_force(pool, {});
    // 请求数量超过池数量：营级完整配属链向上转请。
    const AttachDecision escalated =
        adjudicate(MakeRequest("r1", 1U, 5, {"squad-mortar-team"}, 3U), snapshot, false, true);
    EXPECT_EQ(escalated.kind, AttachDecisionKind::kEscalate);
    EXPECT_NE(escalated.reason.find("INSUFFICIENT_AVAILABLE"), std::string::npos);

    // 无更上级：明确拒绝（SC-004：始终得到明确响应）。
    const AttachDecision rejected =
        adjudicate(MakeRequest("r2", 2U, 5, {"squad-mortar-team"}, 3U), snapshot, false, false);
    EXPECT_EQ(rejected.kind, AttachDecisionKind::kReject);
    EXPECT_NE(rejected.reason.find("INSUFFICIENT_AVAILABLE"), std::string::npos);
}

TEST(WfsAttachTest, ArbitrationOrdersByPriorityThenArrivalSeq) {
    const EchelonResourcePool pool = MakePool();
    // 同一资源三请求竞争：seq=100 但优先级 9 先满足；同优先级（5）按
    // 到达序列号升序（seq=9 先于 seq=10），最后到达且资源耗尽者拒绝。
    const std::vector<SupportRequest> pending = {
        MakeRequest("r-late", 10U, 5, {"squad-mortar-team"}),
        MakeRequest("r-early", 9U, 5, {"squad-mortar-team"}),
        MakeRequest("r-high", 100U, 9, {"squad-mortar-team"}),
    };
    const std::vector<AttachDecision> decisions =
        arbitrate_requests(pending, pool, {}, true, pool.support_score, false);

    ASSERT_EQ(decisions.size(), 3U);
    EXPECT_EQ(decisions[0].kind, AttachDecisionKind::kAssign);  // r-high（优先级 9）。
    EXPECT_EQ(decisions[1].kind, AttachDecisionKind::kAssign);  // r-early（seq 9）。
    EXPECT_EQ(decisions[2].kind, AttachDecisionKind::kReject);  // r-late（seq 10，资源耗尽）。
    EXPECT_NE(decisions[2].reason.find("INSUFFICIENT_AVAILABLE"), std::string::npos);
    // 分数随裁决推进：30 + 30 = 60 → 0。
    EXPECT_EQ(decisions[0].score_remaining, 30U);
    EXPECT_EQ(decisions[1].score_remaining, 0U);
}

TEST(WfsAttachTest, RegistryLifecycleReturnAndReassign) {
    AttachRegistry registry;
    AttachRecord* record = registry.Attach("req-1", "squad-mortar-team", "node-platoon-1", "node-battalion-1", 5U);
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->id, "att-0");
    EXPECT_EQ(record->state, AttachUnitState::kAssigned);
    EXPECT_EQ(registry.ActiveAllocations().size(), 1U);

    // 任务结束 → 归建 → 归还（占用释放，资源回到原属）。
    ASSERT_TRUE(registry.StartReturn("att-0", 100U));
    EXPECT_EQ(record->state, AttachUnitState::kReturning);
    ASSERT_TRUE(registry.CompleteReturn("att-0", 130U));
    EXPECT_EQ(record->state, AttachUnitState::kReturned);
    EXPECT_EQ(record->returned_tick, 130U);
    EXPECT_TRUE(registry.ActiveAllocations().empty());

    // 已归建记录不得再次归建/重新配属（非法转移显式拒绝）。
    EXPECT_FALSE(registry.StartReturn("att-0", 200U));
    EXPECT_FALSE(registry.Reassign("att-0", "req-2", "node-platoon-2", 200U));
}

TEST(WfsAttachTest, ReassignDuringReturnCancelsOriginalReturnFlow) {
    AttachRegistry registry;
    ASSERT_NE(registry.Attach("req-1", "squad-atgm-team", "node-platoon-1", "node-battalion-1", 5U), nullptr);
    ASSERT_TRUE(registry.StartReturn("att-0", 50U));

    // 归建途中新请求优先于归建：原归建流程取消并转为新的配属（FR-009）。
    ASSERT_TRUE(registry.Reassign("att-0", "req-2", "node-platoon-2", 70U));
    const AttachRecord* record = registry.Find("att-0");
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->state, AttachUnitState::kAssigned);
    EXPECT_EQ(record->request_id, "req-2");
    EXPECT_EQ(record->assigned_to_node, "node-platoon-2");
    EXPECT_EQ(record->assigned_tick, 70U);
    EXPECT_EQ(record->return_start_tick, 0U);

    // 瘫痪（需大修/拖运）阻塞配属（FR-009/078）。
    ASSERT_TRUE(registry.StartReturn("att-0", 80U));
    registry.FindMutable("att-0")->immobilized = true;
    EXPECT_FALSE(registry.Reassign("att-0", "req-3", "node-platoon-3", 90U));
}

TEST(WfsAttachTest, TransitLogisticsHookIsDeterministic) {
    AttachRecord record;
    record.id = "att-0";
    record.kind_id = "vehicle-ifv-test";

    // 无需补给/维修：直接前往。
    EXPECT_EQ(resolve_transit_logistics(record, true, true).action, TransitLogisticsAction::kProceed);

    // 瘫痪：需要拖运/大修，阻塞配属。
    record.immobilized = true;
    EXPECT_EQ(resolve_transit_logistics(record, true, true).action,
              TransitLogisticsAction::kBlockedHeavyRepair);

    // 可机动带伤：可抢修则修复后转移，否则阻塞。
    record.immobilized = false;
    record.needs_repair = true;
    EXPECT_EQ(resolve_transit_logistics(record, true, true).action,
              TransitLogisticsAction::kRepairThenProceed);
    EXPECT_EQ(resolve_transit_logistics(record, true, false).action,
              TransitLogisticsAction::kBlockedHeavyRepair);

    // 补给：可达最近补给点先补给；不可达走就地申请 hook（US3 送达后续行）。
    record.needs_repair = false;
    record.needs_supply = true;
    EXPECT_EQ(resolve_transit_logistics(record, true, true).action,
              TransitLogisticsAction::kSupplyThenProceed);
    EXPECT_EQ(resolve_transit_logistics(record, false, true).reason, "SUPPLY_REQUEST_ON_SITE_US3_HOOK");
}

TEST(WfsAttachTest, AttachRegistrySerializeRoundTrip) {
    AttachRegistry registry;
    ASSERT_NE(registry.Attach("req-1", "squad-mortar-team", "node-platoon-1", "node-battalion-1", 5U), nullptr);
    ASSERT_TRUE(registry.StartReturn("att-0", 60U));

    const nlohmann::json json = registry;
    const AttachRegistry restored = json.get<AttachRegistry>();
    EXPECT_EQ(restored.RecordsInInsertionOrder(), registry.RecordsInInsertionOrder());
}
