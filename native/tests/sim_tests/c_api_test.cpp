// tests/sim_tests/c_api_test.cpp
//
// T012 单元测试：C ABI 边界层。
// 覆盖 ABI 版本号、句柄生命周期（create/destroy/null 安全）、错误码、
// 快照缓冲生命周期与往返（太小→BUFFER_TOO_SMALL→足够→可解析），
// 以及命令注入→队列→step 的快照可见变化与跨句柄确定性。

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

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

std::string ValidCommandJson() {
    return R"({
        "schema_version": 1,
        "type": "SECURE_ZONE",
        "target": {"kind": "unit", "ref": "squad-a"},
        "completion": {"condition": "secure_zone", "params": {"zone": "zone-hill", "duration_ticks": 1200}},
        "intent": "占领高地并坚守",
        "behavior": {"engagement": "aggressive", "ammo_override": "5.56mm", "failure_action": "hold"},
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

std::string SnapshotText(wfs_sim_handle* handle) {
    std::string buffer(4096, '\0');
    std::size_t len = 0;
    const wfs_sim_result result = wfs_sim_get_snapshot(handle, buffer.data(), buffer.size(), &len);
    EXPECT_EQ(result, WFS_SIM_RESULT_OK);
    return buffer.substr(0, len);
}

nlohmann::json SnapshotJson(wfs_sim_handle* handle) {
    return nlohmann::json::parse(SnapshotText(handle));
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
    EXPECT_EQ(wfs_sim_get_state_hash(nullptr, nullptr), WFS_SIM_RESULT_INVALID_ARGUMENT);
    EXPECT_EQ(wfs_sim_save(nullptr, "x.wfs"), WFS_SIM_RESULT_INVALID_ARGUMENT);
    EXPECT_EQ(wfs_sim_load_save(nullptr, "x.wfs"), WFS_SIM_RESULT_INVALID_ARGUMENT);
}

TEST(WfsCApiTest, NullCommandJsonReturnsInvalidArgument) {
    Handle handle;
    ASSERT_NE(handle.get(), nullptr);
    EXPECT_EQ(wfs_sim_inject_command(handle.get(), nullptr), WFS_SIM_RESULT_INVALID_ARGUMENT);
    EXPECT_EQ(wfs_sim_inject_ai_decision(handle.get(), nullptr), WFS_SIM_RESULT_INVALID_ARGUMENT);
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
