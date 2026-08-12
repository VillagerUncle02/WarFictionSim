// tests/sim_tests/ai_inject_test.cpp
//
// T020 单元测试：AI 后端抽象与决策注入通道。
// 覆盖合法决策经 T014 校验后入队（T011）、非法输出拒绝且不改变队列、
// 到达 tick+序列号记录（CHK052 回放依据）、决策点记录（宪法 10：
// 状态快照摘要 + 输入 + 返回 + 校验结果）、异步到达语义（注入不推进
// tick、模拟主循环不等待 AI）、决策日志经存档往返恢复，以及玩家命令
// 注入的事件记录（COMMAND_QUEUED/COMMAND_PROCESSED）。

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "test_temp_dir.h"

#include "ai_inject.h"
#include "save.h"
#include "sim_runtime.h"
#include "sim_state.h"
#include "state_serialization.h"
#include "wfs/sim/ai/decision_log.h"
#include "wfs/sim/command_validation.h"
#include "wfs/sim/event_log.h"
#include "wfs/sim/loader.h"
#include "wfs/sim/queue.h"
#include "wfs/sim/rng.h"

namespace {

using wfs::sim::AiDecisionMeta;
using wfs::sim::AiDecisionRecord;
using wfs::sim::AiInjectResult;
using wfs::sim::build_ai_events_json;
using wfs::sim::build_ai_input_summary;
using wfs::sim::compute_state_hash_hex;
using wfs::sim::EventCategory;
using wfs::sim::EventSeverity;
using wfs::sim::GameClock;
using wfs::sim::inject_ai_decision;
using wfs::sim::inject_player_command;
using wfs::sim::load_save_into;
using wfs::sim::load_scenario;
using wfs::sim::Rng;
using wfs::sim::save_to_file;
using wfs::sim::SimState;
using wfs::sim::step_sim_state;

std::filesystem::path RepoRoot() {
    return WFS_SOURCE_ROOT;
}

std::filesystem::path SampleScenario() {
    return RepoRoot() / "data" / "scenarios" / "scn-smoke-test.json";
}

std::filesystem::path CommandSchema() {
    return RepoRoot() / "contracts" / "schemas" / "command.schema.json";
}

// 测试临时目录统一使用共享唯一化设施（F1：pid + 进程内单调序号，防并行冲突）。
using TempDir = wfs::sim::test::TempDir;

// 构造与 wfs_sim_create 等价的最小 SimState（白盒测试：直接驱动内部注入通道）。
SimState MakeState(std::uint64_t seed = 42U, int threads = 1) {
    const auto load = load_scenario(SampleScenario());
    EXPECT_TRUE(load.ok()) << (load.issues.empty() ? "" : load.issues.front().message);
    SimState state;
    state.scenario = load.scenario;
    state.clock = GameClock(load.scenario.tick_hz);
    state.rng = Rng(seed, 0U);
    state.seed = seed;
    state.threads = threads;
    state.scenario_path = SampleScenario();
    return state;
}

std::string ValidCommandJson() {
    return R"({
        "schema_version": 1,
        "type": "SECURE_ZONE",
        "target": {"kind": "unit", "ref": "squad-a"},
        "completion": {"condition": "secure_zone", "params": {"zone": "zone-hill", "duration_ticks": 1200}},
        "intent": "占领高地并坚守",
        "behavior": {"engagement": "aggressive", "ammo_override": "ammo-556", "failure_action": "hold"},
        "priority": 1,
        "deadline": {"game_time": 3600}
    })";
}

AiDecisionMeta Meta(std::string decision_id, std::string node_id = "platoon-alpha",
                    std::string trigger = "test_trigger") {
    return AiDecisionMeta{std::move(decision_id), std::move(node_id), std::move(trigger)};
}

bool HasEvent(const wfs::sim::EventLog& log, const std::string& prefix) {
    for (const wfs::sim::SimEvent& event : log.events()) {
        if (event.message.starts_with(prefix)) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST(WfsAiInjectTest, ValidDecisionAcceptedValidatedAndQueued) {
    SimState state = MakeState();
    const std::string hash_before = compute_state_hash_hex(state);
    const std::string summary_before = build_ai_input_summary(state);
    const std::string events_before = build_ai_events_json(state);

    const AiInjectResult result = inject_ai_decision(state, ValidCommandJson(), Meta("decision-1"), CommandSchema());
    ASSERT_TRUE(result.accepted) << (result.errors.empty() ? "" : result.errors.front().message);
    EXPECT_EQ(result.arrival_tick, 0U);
    EXPECT_EQ(result.decision_id, "decision-1");

    ASSERT_EQ(state.queue.size(), 1U);
    const wfs::sim::QueuedEvent& queued = state.queue.front();
    EXPECT_EQ(queued.tick, 0U);
    EXPECT_EQ(queued.seq, result.arrival_seq);
    EXPECT_EQ(queued.payload, ValidCommandJson());

    ASSERT_EQ(state.decision_log.size(), 1U);
    const AiDecisionRecord& record = state.decision_log.entries().front();
    EXPECT_EQ(record.decision_id, "decision-1");
    EXPECT_EQ(record.node_id, "platoon-alpha");
    EXPECT_EQ(record.trigger, "test_trigger");
    EXPECT_EQ(record.arrival_tick, 0U);
    EXPECT_EQ(record.arrival_seq, queued.seq);
    EXPECT_EQ(record.state_hash, hash_before);  // 决策点快照：注入前状态。
    EXPECT_EQ(record.input_json, summary_before);
    EXPECT_EQ(record.events_json, events_before);
    EXPECT_EQ(record.output_json, ValidCommandJson());
    EXPECT_TRUE(record.validation_ok);
    EXPECT_TRUE(record.validation_errors.empty());
    EXPECT_EQ(state.ai_decision_counter, 1U);

    EXPECT_TRUE(HasEvent(state.event_log, "AI_DECISION_ACCEPTED"));
    EXPECT_FALSE(HasEvent(state.event_log, "AI_DECISION_REJECTED"));
}

TEST(WfsAiInjectTest, InvalidJsonDecisionRejectedWithoutQueueMutation) {
    SimState state = MakeState();
    const std::string before = compute_state_hash_hex(state);

    const AiInjectResult result = inject_ai_decision(state, "{ not json", Meta("decision-bad"), CommandSchema());
    ASSERT_FALSE(result.accepted);
    EXPECT_EQ(state.queue.size(), 0U);
    ASSERT_FALSE(result.errors.empty());
    EXPECT_EQ(result.errors.front().code, "INVALID_JSON");

    ASSERT_EQ(state.decision_log.size(), 1U);
    const AiDecisionRecord& record = state.decision_log.entries().front();
    EXPECT_EQ(record.decision_id, "decision-bad");
    EXPECT_EQ(record.arrival_seq, 0U);  // 拒绝：未入队。
    EXPECT_EQ(record.state_hash, before);
    EXPECT_EQ(record.output_json, "{ not json");
    EXPECT_FALSE(record.validation_ok);
    ASSERT_FALSE(record.validation_errors.empty());
    EXPECT_EQ(record.validation_errors.front().code, "INVALID_JSON");
    EXPECT_EQ(state.ai_decision_counter, 1U);
    EXPECT_TRUE(HasEvent(state.event_log, "AI_DECISION_REJECTED"));
}

TEST(WfsAiInjectTest, SemanticInvalidDecisionRejectedWithNodeAuthority) {
    SimState state = MakeState();
    const std::string unauthorized = R"({
        "schema_version": 1,
        "type": "SECURE_ZONE",
        "target": {"kind": "unit", "ref": "squad-c"},
        "completion": {"condition": "secure_zone", "params": {"zone": "zone-hill", "duration_ticks": 1200}},
        "intent": "越权命令",
        "behavior": {"engagement": "balanced"},
        "priority": 1,
        "deadline": {"game_time": 3600}
    })";
    const AiInjectResult result =
        inject_ai_decision(state, unauthorized, Meta("decision-unauthorized"), CommandSchema());
    ASSERT_FALSE(result.accepted);
    EXPECT_EQ(state.queue.size(), 0U);
    ASSERT_FALSE(result.errors.empty());
    EXPECT_EQ(result.errors.front().code, "UNAUTHORIZED_TARGET");
    EXPECT_FALSE(state.decision_log.entries().front().validation_ok);
}

TEST(WfsAiInjectTest, AiNodeCanCommandItsOwnUnits) {
    SimState state = MakeState();
    const std::string enemy_command = R"({
        "schema_version": 1,
        "type": "SECURE_ZONE",
        "target": {"kind": "unit", "ref": "squad-c"},
        "completion": {"condition": "secure_zone", "params": {"zone": "zone-hill", "duration_ticks": 1200}},
        "intent": "敌方 AI 节点下达命令",
        "behavior": {"engagement": "balanced"},
        "priority": 1,
        "deadline": {"game_time": 3600}
    })";
    const AiInjectResult result =
        inject_ai_decision(state, enemy_command, Meta("decision-enemy", "enemy-command"), CommandSchema());
    ASSERT_TRUE(result.accepted) << (result.errors.empty() ? "" : result.errors.front().message);
    EXPECT_EQ(state.queue.size(), 1U);
    EXPECT_EQ(state.decision_log.entries().front().node_id, "enemy-command");
}

TEST(WfsAiInjectTest, InjectionDoesNotAdvanceClockOrBlockStep) {
    SimState state = MakeState();
    const AiInjectResult result =
        inject_ai_decision(state, ValidCommandJson(), Meta("decision-async"), CommandSchema());
    ASSERT_TRUE(result.accepted);
    // 异步到达语义（宪法 8/FR-026）：注入只入队，不推进 tick、不等待后端。
    EXPECT_EQ(state.clock.tick(), 0U);
    EXPECT_EQ(state.queue.size(), 1U);

    step_sim_state(state);
    EXPECT_EQ(state.clock.tick(), 1U);
    EXPECT_EQ(state.queue.size(), 0U);
    EXPECT_EQ(state.processed_events, 1U);
    EXPECT_TRUE(HasEvent(state.event_log, "COMMAND_PROCESSED"));
}

TEST(WfsAiInjectTest, ArrivalTickAndSequenceRecordedInOrder) {
    SimState state = MakeState();
    const AiInjectResult first = inject_ai_decision(state, ValidCommandJson(), Meta("decision-a"), CommandSchema());
    ASSERT_TRUE(first.accepted);
    EXPECT_EQ(first.arrival_tick, 0U);
    EXPECT_EQ(first.arrival_seq, 0U);
    step_sim_state(state);  // 处理第一条；第二条在下一 tick 到达。

    const AiInjectResult second = inject_ai_decision(state, ValidCommandJson(), Meta("decision-b"), CommandSchema());
    ASSERT_TRUE(second.accepted);
    EXPECT_EQ(second.arrival_tick, 1U);
    EXPECT_EQ(second.arrival_seq, 1U);

    ASSERT_EQ(state.decision_log.size(), 2U);
    EXPECT_EQ(state.decision_log.entries()[0].arrival_tick, 0U);
    EXPECT_EQ(state.decision_log.entries()[0].arrival_seq, 0U);
    EXPECT_EQ(state.decision_log.entries()[1].arrival_tick, 1U);
    EXPECT_EQ(state.decision_log.entries()[1].arrival_seq, 1U);

    // 队列仍保留第二条（tick=1 未到期，当前 tick=1 的 step 已消费第一条）。
    ASSERT_EQ(state.queue.size(), 1U);
    EXPECT_EQ(state.queue.front().tick, 1U);
    EXPECT_EQ(state.queue.front().seq, 1U);
}

TEST(WfsAiInjectTest, PlayerCommandInjectionRecordsQueueEvents) {
    SimState state = MakeState();
    const auto result = inject_player_command(state, ValidCommandJson(), CommandSchema());
    ASSERT_TRUE(result.accepted);
    EXPECT_EQ(result.arrival_tick, 0U);
    EXPECT_EQ(result.arrival_seq, 0U);
    EXPECT_TRUE(HasEvent(state.event_log, "COMMAND_QUEUED"));
    EXPECT_EQ(state.decision_log.size(), 0U);  // 玩家命令不进决策日志。

    step_sim_state(state);
    EXPECT_TRUE(HasEvent(state.event_log, "COMMAND_PROCESSED"));
}

TEST(WfsAiInjectTest, DecisionLogRoundTripsThroughSave) {
    TempDir dir;
    SimState source = MakeState();
    ASSERT_TRUE(inject_ai_decision(source, ValidCommandJson(), Meta("decision-save"), CommandSchema()).accepted);
    step_sim_state(source);
    const std::string hash_before = compute_state_hash_hex(source);
    const std::filesystem::path path = dir.path() / "decisions.wfs";
    EXPECT_EQ(save_to_file(source, path), WFS_SIM_RESULT_OK);

    SimState restored = MakeState();
    EXPECT_EQ(load_save_into(restored, path), WFS_SIM_RESULT_OK);
    EXPECT_EQ(compute_state_hash_hex(restored), hash_before);
    ASSERT_EQ(restored.decision_log.size(), 1U);
    EXPECT_EQ(restored.decision_log.entries().front(), source.decision_log.entries().front());
    EXPECT_EQ(restored.ai_decision_counter, source.ai_decision_counter);
    ASSERT_EQ(restored.queue.size(), 0U);  // 已处理：队列为空但游标已恢复。
    EXPECT_EQ(restored.queue.next_seq(), source.queue.next_seq());
}

TEST(WfsAiInjectTest, AutoDecisionIdIsMonotonic) {
    SimState state = MakeState();
    const AiInjectResult first = inject_ai_decision(state, ValidCommandJson(), Meta({}), CommandSchema());
    const AiInjectResult second = inject_ai_decision(state, ValidCommandJson(), Meta({}), CommandSchema());
    ASSERT_TRUE(first.accepted);
    ASSERT_TRUE(second.accepted);
    EXPECT_EQ(first.decision_id, "ai-0");
    EXPECT_EQ(second.decision_id, "ai-1");
    EXPECT_NE(first.decision_id, second.decision_id);
}

TEST(WfsAiInjectTest, DuplicateExplicitDecisionIdRejectedWithoutLogEntry) {
    SimState state = MakeState();
    const AiInjectResult first = inject_ai_decision(state, ValidCommandJson(), Meta("decision-dup"), CommandSchema());
    ASSERT_TRUE(first.accepted) << (first.errors.empty() ? "" : first.errors.front().message);

    const AiInjectResult second = inject_ai_decision(state, ValidCommandJson(), Meta("decision-dup"), CommandSchema());
    ASSERT_FALSE(second.accepted);
    ASSERT_FALSE(second.errors.empty());
    EXPECT_EQ(second.errors.front().code, "DUPLICATE_DECISION_ID");
    // 日志保持唯一：重复标识不追加记录、不递增游标、不重复入队。
    EXPECT_EQ(state.decision_log.size(), 1U);
    EXPECT_EQ(state.ai_decision_counter, 1U);
    EXPECT_EQ(state.queue.size(), 1U);
    EXPECT_TRUE(HasEvent(state.event_log, "AI_DECISION_REJECTED"));
}

TEST(WfsAiInjectTest, ReservedAutoIdPrefixRejectedAndAutoSpaceIntact) {
    SimState state = MakeState();
    const AiInjectResult reserved = inject_ai_decision(state, ValidCommandJson(), Meta("ai-1"), CommandSchema());
    ASSERT_FALSE(reserved.accepted);
    ASSERT_FALSE(reserved.errors.empty());
    EXPECT_EQ(reserved.errors.front().code, "RESERVED_DECISION_ID");
    // 被拒的保留前缀不占用日志/游标/队列。
    EXPECT_EQ(state.decision_log.size(), 0U);
    EXPECT_EQ(state.ai_decision_counter, 0U);
    EXPECT_EQ(state.queue.size(), 0U);
    EXPECT_TRUE(HasEvent(state.event_log, "AI_DECISION_REJECTED"));

    // 自动分配空间不受影响：counter 从 0 生成 ai-0（显式 ai-1 未撞车）。
    const AiInjectResult automatic = inject_ai_decision(state, ValidCommandJson(), Meta({}), CommandSchema());
    ASSERT_TRUE(automatic.accepted) << (automatic.errors.empty() ? "" : automatic.errors.front().message);
    EXPECT_EQ(automatic.decision_id, "ai-0");
    EXPECT_EQ(state.decision_log.size(), 1U);
}
