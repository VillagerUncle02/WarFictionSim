// tests/sim_tests/c_api_test.cpp
//
// T012 单元测试：C ABI 边界层。
// 覆盖 ABI 版本号、句柄生命周期（create/destroy/null 安全）、错误码、
// 快照缓冲生命周期与往返（太小→BUFFER_TOO_SMALL→足够→可解析），
// 以及命令注入→队列→step 的快照可见变化与跨句柄确定性。

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "wfs/sim/c_api.h"

namespace {

std::filesystem::path RepoRoot() {
    return WFS_SOURCE_ROOT;
}

std::filesystem::path SampleScenario() {
    return RepoRoot() / "data" / "scenarios" / "scn-smoke-test.json";
}

std::filesystem::path RuntimeCombatScenario() {
    return RepoRoot() / "data" / "scenarios" / "scn-runtime-combat-test.json";
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

// scn-runtime-combat-test.json 的玩家单位/目标区，供事件日志查询测试
// 注入有效命令（command/info 事件源）。
std::string RuntimeCommandJson() {
    return R"({
        "schema_version": 1,
        "type": "SECURE_ZONE",
        "target": {"kind": "unit", "ref": "squad-friend"},
        "completion": {"condition": "secure_zone", "params": {"zone": "zone-a", "duration_ticks": 1200}},
        "intent": "占领 A 区并坚守",
        "behavior": {"engagement": "aggressive", "ammo_override": "ammo-556", "failure_action": "hold"},
        "priority": 1,
        "deadline": {"game_time": 3600}
    })";
}

class Handle {
   public:
    Handle() : handle_(wfs_sim_create(SampleScenario().string().c_str(), 42u, 1)) {}

    ~Handle() { wfs_sim_destroy(handle_); }

    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

    wfs_sim_handle* get() const { return handle_; }

   private:
    wfs_sim_handle* handle_;
};

class RuntimeCombatHandle {
   public:
    RuntimeCombatHandle() : handle_(wfs_sim_create(RuntimeCombatScenario().string().c_str(), 42u, 1)) {}

    ~RuntimeCombatHandle() { wfs_sim_destroy(handle_); }

    RuntimeCombatHandle(const RuntimeCombatHandle&) = delete;
    RuntimeCombatHandle& operator=(const RuntimeCombatHandle&) = delete;

    wfs_sim_handle* get() const { return handle_; }

   private:
    wfs_sim_handle* handle_;
};

std::string SnapshotText(wfs_sim_handle* handle) {
    std::string buffer(1 << 20, '\0');
    std::size_t len = 0;
    const wfs_sim_result result = wfs_sim_get_snapshot(handle, buffer.data(), buffer.size(), &len);
    EXPECT_EQ(result, WFS_SIM_RESULT_OK);
    return buffer.substr(0, len);
}

nlohmann::json SnapshotJson(wfs_sim_handle* handle) {
    return nlohmann::json::parse(SnapshotText(handle));
}

// 注入有效命令（command/info）并推进 6 tick：确定性产生 intel/info、
// combat/info 与 combat/warning 事件（scn-runtime-combat-test.json，seed=42）。
void ProduceMixedEvents(wfs_sim_handle* handle) {
    EXPECT_EQ(wfs_sim_inject_command(handle, RuntimeCommandJson().c_str()), WFS_SIM_RESULT_OK);
    for (int i = 0; i < 6; ++i) {
        EXPECT_EQ(wfs_sim_step(handle), WFS_SIM_RESULT_OK);
    }
}

std::string QueryEventsText(wfs_sim_handle* handle, const std::string& query_json) {
    std::string buffer(1 << 20, '\0');
    std::size_t len = 0;
    const wfs_sim_result result = wfs_sim_query_events(handle, query_json.c_str(), buffer.data(), buffer.size(), &len);
    EXPECT_EQ(result, WFS_SIM_RESULT_OK);
    return buffer.substr(0, len);
}

nlohmann::json QueryEventsJson(wfs_sim_handle* handle, const std::string& query_json) {
    return nlohmann::json::parse(QueryEventsText(handle, query_json));
}

}  // namespace

TEST(WfsCApiTest, VersionMatchesAbiMacroAndIsStable) {
    const char* first = wfs_sim_version();
    const char* second = wfs_sim_version();
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(std::string(first), WFS_SIM_VERSION_STRING);
    EXPECT_EQ(first, second);  // 静态存储：两次调用返回同一字符串。
}

TEST(WfsCApiTest, CreateFailsForMissingScenario) {
    const std::filesystem::path missing = RepoRoot() / "data" / "scenarios" / "does-not-exist.json";
    EXPECT_EQ(wfs_sim_create(missing.string().c_str(), 42u, 1), nullptr);
}

TEST(WfsCApiTest, CreateRejectsInvalidThreads) {
    EXPECT_EQ(wfs_sim_create(SampleScenario().string().c_str(), 42u, 0), nullptr);
    EXPECT_EQ(wfs_sim_create(SampleScenario().string().c_str(), 42u, -1), nullptr);
}

TEST(WfsCApiTest, HandleLifecycle) {
    Handle handle;
    ASSERT_NE(handle.get(), nullptr);
    EXPECT_EQ(wfs_sim_step(handle.get()), WFS_SIM_RESULT_OK);

    // destroy 后不再使用该句柄；NULL destroy 为无操作。
    wfs_sim_destroy(nullptr);
}

TEST(WfsCApiTest, NullHandleReturnsInvalidArgument) {
    const char* command = ValidCommandJson().c_str();
    EXPECT_EQ(wfs_sim_step(nullptr), WFS_SIM_RESULT_INVALID_ARGUMENT);
    EXPECT_EQ(wfs_sim_inject_command(nullptr, command), WFS_SIM_RESULT_INVALID_ARGUMENT);
    EXPECT_EQ(wfs_sim_inject_ai_decision(nullptr, command), WFS_SIM_RESULT_INVALID_ARGUMENT);
    EXPECT_EQ(wfs_sim_get_snapshot(nullptr, nullptr, 0u, nullptr), WFS_SIM_RESULT_INVALID_ARGUMENT);
    EXPECT_EQ(wfs_sim_query_events(nullptr, "{}", nullptr, 0u, nullptr), WFS_SIM_RESULT_INVALID_ARGUMENT);
    EXPECT_EQ(wfs_sim_get_state_hash(nullptr, nullptr), WFS_SIM_RESULT_INVALID_ARGUMENT);
    EXPECT_EQ(wfs_sim_save(nullptr, "x.wfs"), WFS_SIM_RESULT_INVALID_ARGUMENT);
    EXPECT_EQ(wfs_sim_load_save(nullptr, "x.wfs"), WFS_SIM_RESULT_INVALID_ARGUMENT);
}

TEST(WfsCApiTest, NullCommandJsonReturnsInvalidArgument) {
    Handle handle;
    ASSERT_NE(handle.get(), nullptr);
    EXPECT_EQ(wfs_sim_inject_command(handle.get(), nullptr), WFS_SIM_RESULT_INVALID_ARGUMENT);
    EXPECT_EQ(wfs_sim_inject_ai_decision(handle.get(), nullptr), WFS_SIM_RESULT_INVALID_ARGUMENT);
    EXPECT_EQ(wfs_sim_query_events(handle.get(), nullptr, nullptr, 0u, nullptr), WFS_SIM_RESULT_INVALID_ARGUMENT);
}

TEST(WfsCApiTest, StateHashImplementedAndWellFormed) {
    Handle handle;
    ASSERT_NE(handle.get(), nullptr);
    char hash[WFS_SIM_STATE_HASH_HEX_LEN] = {};
    EXPECT_EQ(wfs_sim_get_state_hash(handle.get(), hash), WFS_SIM_RESULT_OK);
    EXPECT_EQ(std::string(hash).size(), 64u);
}

TEST(WfsCApiTest, SnapshotRoundTripAndFields) {
    Handle handle;
    ASSERT_NE(handle.get(), nullptr);

    const std::string first = SnapshotText(handle.get());
    const std::string second = SnapshotText(handle.get());
    EXPECT_EQ(first, second);  // 只读快照：连续读取内容一致。

    const nlohmann::json snapshot = nlohmann::json::parse(first);
    EXPECT_EQ(snapshot.at("abi_version"), WFS_SIM_VERSION_STRING);
    EXPECT_EQ(snapshot.at("tick"), 0);
    EXPECT_EQ(snapshot.at("seed"), 42);
    EXPECT_EQ(snapshot.at("threads"), 1);
    EXPECT_EQ(snapshot.at("scenario_id"), "scn-smoke-test");
    EXPECT_EQ(snapshot.at("pending_events"), 0);
    EXPECT_EQ(snapshot.at("processed_events"), 0);
}

TEST(WfsCApiTest, SnapshotBufferTooSmallReportsRequiredSize) {
    Handle handle;
    ASSERT_NE(handle.get(), nullptr);

    char small[8] = {};
    std::size_t required = 0;
    const wfs_sim_result too_small = wfs_sim_get_snapshot(handle.get(), small, sizeof(small), &required);
    EXPECT_EQ(too_small, WFS_SIM_RESULT_BUFFER_TOO_SMALL);
    EXPECT_GT(required, sizeof(small));

    std::string buffer(required + 1, '\0');
    std::size_t written = 0;
    const wfs_sim_result ok = wfs_sim_get_snapshot(handle.get(), buffer.data(), buffer.size(), &written);
    EXPECT_EQ(ok, WFS_SIM_RESULT_OK);
    EXPECT_EQ(written, required - 1);  // required 含 NUL，written 为文本字节数。
}

TEST(WfsCApiTest, SnapshotReflectsCommandInjectionAndStep) {
    Handle handle;
    ASSERT_NE(handle.get(), nullptr);

    EXPECT_EQ(wfs_sim_inject_command(handle.get(), ValidCommandJson().c_str()), WFS_SIM_RESULT_OK);
    EXPECT_EQ(SnapshotJson(handle.get()).at("pending_events"), 1);

    EXPECT_EQ(wfs_sim_step(handle.get()), WFS_SIM_RESULT_OK);
    const nlohmann::json after = SnapshotJson(handle.get());
    EXPECT_EQ(after.at("tick"), 1);
    EXPECT_EQ(after.at("pending_events"), 0);
    EXPECT_EQ(after.at("processed_events"), 1);
}

TEST(WfsCApiTest, AiDecisionValidCommandAcceptedAndProcessed) {
    Handle handle;
    ASSERT_NE(handle.get(), nullptr);

    EXPECT_EQ(wfs_sim_inject_ai_decision(handle.get(), ValidCommandJson().c_str()), WFS_SIM_RESULT_OK);
    EXPECT_EQ(SnapshotJson(handle.get()).at("pending_events"), 1);

    EXPECT_EQ(wfs_sim_step(handle.get()), WFS_SIM_RESULT_OK);
    const nlohmann::json after = SnapshotJson(handle.get());
    EXPECT_EQ(after.at("tick"), 1);
    EXPECT_EQ(after.at("pending_events"), 0);
    EXPECT_EQ(after.at("processed_events"), 1);
}

TEST(WfsCApiTest, InvalidCommandRejectedWithoutQueueMutation) {
    Handle handle;
    ASSERT_NE(handle.get(), nullptr);

    EXPECT_EQ(wfs_sim_inject_command(handle.get(), "{ not json"), WFS_SIM_RESULT_INVALID_DATA);
    EXPECT_EQ(SnapshotJson(handle.get()).at("pending_events"), 0);

    // 语义非法（越权目标）：同样拒绝且不改变队列。
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
    EXPECT_EQ(wfs_sim_inject_ai_decision(handle.get(), unauthorized.c_str()), WFS_SIM_RESULT_INVALID_DATA);
    EXPECT_EQ(SnapshotJson(handle.get()).at("pending_events"), 0);
}

TEST(WfsCApiTest, SnapshotsAreDeterministicAcrossHandles) {
    Handle first;
    Handle second;
    ASSERT_NE(first.get(), nullptr);
    ASSERT_NE(second.get(), nullptr);
    EXPECT_EQ(SnapshotText(first.get()), SnapshotText(second.get()));
}

TEST(WfsCApiTest, QueryEventsFiltersCategorySeverityTextAndLimit) {
    RuntimeCombatHandle handle;
    ASSERT_NE(handle.get(), nullptr);
    ProduceMixedEvents(handle.get());

    // 空过滤返回全部事件，count 与 events 一致且不截断。
    const nlohmann::json all = QueryEventsJson(handle.get(), "{}");
    const nlohmann::json& all_events = all.at("events");
    ASSERT_TRUE(all_events.is_array());
    ASSERT_GT(all_events.size(), 0U);
    EXPECT_EQ(all.at("count").get<std::size_t>(), all_events.size());
    EXPECT_FALSE(all.at("truncated").get<bool>());

    // 响应结构：每事件含 seq/tick/category/severity/message；
    // seq 严格升序（append 顺序），tick 非降序（游戏时间顺序）。
    std::uint64_t prev_seq = 0U;
    std::uint64_t prev_tick = 0U;
    bool first_event = true;
    for (const nlohmann::json& event : all_events) {
        ASSERT_TRUE(event.contains("seq") && event.contains("tick") && event.contains("category") &&
                    event.contains("severity") && event.contains("message"));
        EXPECT_TRUE(event.at("seq").is_number_unsigned());
        EXPECT_TRUE(event.at("tick").is_number_unsigned());
        const std::uint64_t seq = event.at("seq").get<std::uint64_t>();
        const std::uint64_t tick = event.at("tick").get<std::uint64_t>();
        if (!first_event) {
            EXPECT_GT(seq, prev_seq);
            EXPECT_GE(tick, prev_tick);
        }
        prev_seq = seq;
        prev_tick = tick;
        first_event = false;
    }

    // 按 category 过滤：只返回对应分类。
    const nlohmann::json combat = QueryEventsJson(handle.get(), R"({"category":"combat"})");
    ASSERT_GT(combat.at("events").size(), 0U);
    for (const nlohmann::json& event : combat.at("events")) {
        EXPECT_EQ(event.at("category").get<std::string>(), "combat");
    }
    EXPECT_EQ(combat.at("count").get<std::size_t>(), combat.at("events").size());

    // 按 min_severity 过滤：warning 及以上（warning/critical）。
    const nlohmann::json warnings = QueryEventsJson(handle.get(), R"({"min_severity":"warning"})");
    ASSERT_GT(warnings.at("events").size(), 0U);
    for (const nlohmann::json& event : warnings.at("events")) {
        const std::string severity = event.at("severity").get<std::string>();
        EXPECT_TRUE(severity == "warning" || severity == "critical");
    }

    // 按 text 子串搜索（区分大小写）。
    const nlohmann::json hits = QueryEventsJson(handle.get(), R"({"text":"attacker="})");
    ASSERT_GT(hits.at("events").size(), 0U);
    for (const nlohmann::json& event : hits.at("events")) {
        EXPECT_NE(event.at("message").get<std::string>().find("attacker="), std::string::npos);
    }

    // limit 截断：count 为截断前总数，truncated 仅在总数超过 limit 时为 true。
    const std::size_t total = hits.at("count").get<std::size_t>();
    ASSERT_GT(total, 3U) << "前置条件：截断分支必须被实际覆盖";
    const nlohmann::json limited = QueryEventsJson(handle.get(), R"({"text":"attacker=","limit":3})");
    EXPECT_EQ(limited.at("count").get<std::size_t>(), total);
    EXPECT_EQ(limited.at("events").size(), 3U);
    EXPECT_TRUE(limited.at("truncated").get<bool>());
    // 截断结果保留 seq 升序前缀（前 3 条与全量查询一致）。
    for (std::size_t i = 0U; i < limited.at("events").size(); ++i) {
        EXPECT_EQ(limited.at("events").at(i).at("seq"), hits.at("events").at(i).at("seq"));
    }

    // 过滤条件可组合（category + min_severity + text）。
    const nlohmann::json composed =
        QueryEventsJson(handle.get(), R"({"category":"combat","min_severity":"warning","text":"="})");
    for (const nlohmann::json& event : composed.at("events")) {
        EXPECT_EQ(event.at("category").get<std::string>(), "combat");
        const std::string severity = event.at("severity").get<std::string>();
        EXPECT_TRUE(severity == "warning" || severity == "critical");
        EXPECT_NE(event.at("message").get<std::string>().find("="), std::string::npos);
    }
}

TEST(WfsCApiTest, QueryEventsInvalidQueryJsonReturnsInvalidDataWithoutMutation) {
    RuntimeCombatHandle handle;
    ASSERT_NE(handle.get(), nullptr);
    ProduceMixedEvents(handle.get());

    // 坏 JSON / 非对象 / 未知名称 / 字段类型错误 / 负 limit 一律返回
    // INVALID_DATA（结构化报错，不崩溃），且不改写输出缓冲与 out_len。
    const std::vector<std::string> invalid_queries = {
        "{ not json",
        "[]",
        "\"text\"",
        "null",
        "42",
        R"({"category":"bogus"})",
        R"({"min_severity":"fatal"})",
        R"({"category":3})",
        R"({"min_severity":[]})",
        R"({"text":7})",
        R"({"limit":-1})",
        R"({"limit":"3"})",
        R"({"limit":2.5})",
    };
    for (const std::string& query : invalid_queries) {
        char out[32] = {};
        std::size_t required = 12345U;
        const wfs_sim_result result = wfs_sim_query_events(handle.get(), query.c_str(), out, sizeof(out), &required);
        EXPECT_EQ(result, WFS_SIM_RESULT_INVALID_DATA) << "query=" << query;
        EXPECT_EQ(required, 12345U) << "非法查询不得改写 out_len: " << query;
        EXPECT_EQ(out[0], '\0') << "非法查询不得改写输出缓冲: " << query;
    }

    // 非法查询不破坏句柄：后续合法查询仍可用。
    const nlohmann::json after = QueryEventsJson(handle.get(), "{}");
    EXPECT_GT(after.at("events").size(), 0U);
}

TEST(WfsCApiTest, QueryEventsBufferTooSmallSupportsTwoPhaseRead) {
    RuntimeCombatHandle handle;
    ASSERT_NE(handle.get(), nullptr);
    ProduceMixedEvents(handle.get());

    // 第一阶段：小缓冲 → BUFFER_TOO_SMALL 并写出所需字节数（含 NUL）。
    char small[16] = {};
    std::size_t required = 0;
    const wfs_sim_result too_small =
        wfs_sim_query_events(handle.get(), R"({"category":"combat"})", small, sizeof(small), &required);
    EXPECT_EQ(too_small, WFS_SIM_RESULT_BUFFER_TOO_SMALL);
    EXPECT_GT(required, sizeof(small));

    // 第二阶段：按 required 分配后重试 → OK，written 为文本字节数。
    std::string buffer(required + 1, '\0');
    std::size_t written = 0;
    const wfs_sim_result ok =
        wfs_sim_query_events(handle.get(), R"({"category":"combat"})", buffer.data(), buffer.size(), &written);
    EXPECT_EQ(ok, WFS_SIM_RESULT_OK);
    EXPECT_EQ(written, required - 1);
    const nlohmann::json parsed = nlohmann::json::parse(buffer.substr(0, written));
    for (const nlohmann::json& event : parsed.at("events")) {
        EXPECT_EQ(event.at("category").get<std::string>(), "combat");
    }
}

TEST(WfsCApiTest, QueryEventsEmptyLogReturnsEmptyArray) {
    Handle handle;  // 未注入/未 step：事件日志为空。
    ASSERT_NE(handle.get(), nullptr);

    const nlohmann::json empty = QueryEventsJson(handle.get(), "{}");
    EXPECT_TRUE(empty.at("events").is_array());
    EXPECT_EQ(empty.at("events").size(), 0U);
    EXPECT_EQ(empty.at("count").get<std::size_t>(), 0U);
    EXPECT_FALSE(empty.at("truncated").get<bool>());

    const nlohmann::json filtered = QueryEventsJson(handle.get(), R"({"category":"combat","limit":10})");
    EXPECT_EQ(filtered.at("events").size(), 0U);
    EXPECT_EQ(filtered.at("count").get<std::size_t>(), 0U);
    EXPECT_FALSE(filtered.at("truncated").get<bool>());
}

TEST(WfsCApiTest, QueryEventsNullArgumentsReturnInvalidArgument) {
    RuntimeCombatHandle handle;
    ASSERT_NE(handle.get(), nullptr);
    char buffer[64] = {};
    std::size_t len = 0;

    EXPECT_EQ(wfs_sim_query_events(handle.get(), nullptr, buffer, sizeof(buffer), &len),
              WFS_SIM_RESULT_INVALID_ARGUMENT);
    EXPECT_EQ(wfs_sim_query_events(handle.get(), "{}", nullptr, sizeof(buffer), &len), WFS_SIM_RESULT_INVALID_ARGUMENT);
    EXPECT_EQ(wfs_sim_query_events(handle.get(), "{}", buffer, sizeof(buffer), nullptr),
              WFS_SIM_RESULT_INVALID_ARGUMENT);
}

TEST(WfsCApiTest, QueryEventsDeterministicAcrossHandles) {
    RuntimeCombatHandle first;
    RuntimeCombatHandle second;
    ASSERT_NE(first.get(), nullptr);
    ASSERT_NE(second.get(), nullptr);
    ProduceMixedEvents(first.get());
    ProduceMixedEvents(second.get());

    const std::string query = R"({"category":"combat","min_severity":"info","text":"attacker="})";
    EXPECT_EQ(QueryEventsText(first.get(), query), QueryEventsText(second.get(), query));
}
