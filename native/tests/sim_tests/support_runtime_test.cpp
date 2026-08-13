// tests/sim_tests/support_runtime_test.cpp
//
// T047–T050 支援管线运行期测试（FR-009 归建途中重新配属接线）。
// 覆盖：
// - 池余量 0 但存在归建中（RETURNING）单位时，新请求取消原归建并重新配属
//   成功（FR-009），而不是直接 INSUFFICIENT_AVAILABLE；
// - 同 tick 多请求竞争唯一可抢占单位时仍按（优先级降序, 到达序列号升序）
//   确定性仲裁（FR-008）；
// - 场景裁决桩请求经 step_support_pipeline 确定性登记（数据驱动）。

#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "sim_runtime.h"
#include "sim_state.h"
#include "wfs/sim/support.h"

namespace {

using wfs::sim::AttachRecord;
using wfs::sim::AttachUnitState;
using wfs::sim::EchelonResourcePool;
using wfs::sim::PoolEntryKind;
using wfs::sim::ResourcePoolEntry;
using wfs::sim::SimState;
using wfs::sim::SupportRequest;
using wfs::sim::SupportRequestState;

bool HasEvent(const wfs::sim::EventLog& log, const std::string& text) {
    for (const wfs::sim::SimEvent& event : log.events()) {
        if (event.message.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

SimState MakeSupportState() {
    SimState state;
    state.support_configured = true;
    state.support_pool_echelon = "battalion";
    state.support_faction.id = "faction-test";
    state.support_faction.approval_level = 0U;
    EchelonResourcePool pool;
    pool.support_score = 60U;
    pool.entries = {ResourcePoolEntry{"squad-mortar-team", PoolEntryKind::kUnit, 1U, 30U}};
    state.support_faction.pools["battalion"] = pool;
    state.support_config.scale = "battalion";
    state.support_config.evaluation_delay_ticks = 0U;
    state.support_config.return_delay_ticks = 20U;
    state.support_config.approval_step_ticks = 0U;
    return state;
}

SupportRequest MakeScriptedRequest(const std::string& id, std::int64_t priority) {
    SupportRequest request;
    request.id = id;
    request.priority = priority;
    request.from_node = "node-platoon-2";
    request.to_node = "node-battalion-1";
    request.request_type = "reinforce";
    request.kinds = {"squad-mortar-team"};
    request.quantity = 1U;
    request.submitted_tick = 0U;
    request.state = SupportRequestState::kSubmitted;
    return request;
}

}  // namespace

TEST(WfsSupportRuntimeTest, ReassignInTransitSatisfiesNewRequestWhenPoolExhausted) {
    SimState state = MakeSupportState();
    // 池余量 1 且已被既有配属占用；该单位进入归建途中（仍计占用，但可抢占）。
    ASSERT_NE(state.attach_registry.Attach("req-1", "squad-mortar-team", "node-platoon-1", "node-battalion-1", 0U),
              nullptr);
    ASSERT_TRUE(state.attach_registry.StartReturn("att-0", 5U));
    state.support_config.scripted_requests = {MakeScriptedRequest("req-2", 5)};

    wfs::sim::step_support_pipeline(state);

    // 新请求不得以 INSUFFICIENT_AVAILABLE 拒绝：取消原归建并重新配属（FR-009）。
    const SupportRequest* live = state.support_chain.Find("req-2");
    ASSERT_NE(live, nullptr);
    EXPECT_EQ(live->state, SupportRequestState::kExecuting);
    EXPECT_EQ(live->resolution, "assign");
    const AttachRecord* record = state.attach_registry.Find("att-0");
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->state, AttachUnitState::kAssigned);
    EXPECT_EQ(record->request_id, "req-2");
    EXPECT_EQ(record->assigned_to_node, "node-platoon-2");
    EXPECT_EQ(state.attach_registry.size(), 1U);  // 抢占复用记录，不重复占用。
    EXPECT_TRUE(
        HasEvent(state.event_log, "ATTACH_REASSIGNED request=req-2 unit=squad-mortar-team previous_request=req-1"))
        << "重新配属必须产生可见事件";
}

TEST(WfsSupportRuntimeTest, ReassignHonorsPriorityArbitrationInSameTick) {
    SimState state = MakeSupportState();
    ASSERT_NE(state.attach_registry.Attach("req-1", "squad-mortar-team", "node-platoon-1", "node-battalion-1", 0U),
              nullptr);
    ASSERT_TRUE(state.attach_registry.StartReturn("att-0", 5U));
    // 同一到达 tick 的两个请求竞争唯一可抢占单位：优先级仲裁一致（FR-008）。
    state.support_config.scripted_requests = {MakeScriptedRequest("req-2", 5), MakeScriptedRequest("req-3", 4)};

    wfs::sim::step_support_pipeline(state);

    const SupportRequest* winner = state.support_chain.Find("req-2");
    ASSERT_NE(winner, nullptr);
    EXPECT_EQ(winner->state, SupportRequestState::kExecuting);
    EXPECT_EQ(state.attach_registry.Find("att-0")->request_id, "req-2");
    const SupportRequest* loser = state.support_chain.Find("req-3");
    ASSERT_NE(loser, nullptr);
    EXPECT_EQ(loser->state, SupportRequestState::kRejected);
    EXPECT_NE(loser->resolution_reason.find("INSUFFICIENT_AVAILABLE"), std::string::npos);
}
