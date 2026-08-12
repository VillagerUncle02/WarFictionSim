// tests/sim_tests/snapshot_test.cpp
//
// T016 单元测试：状态快照与状态哈希。
// 覆盖 SHA-256 固定实现的标准已知答案向量（KAT）、快照 JSON 内容与确定性、
// 状态哈希的稳定性/敏感性，以及"线程数不影响状态哈希"（宪法第 7 条，
// 与 T018 分区并行框架联动验收）。

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "wfs/sim/c_api.h"
#include "wfs/sim/sha256.h"

namespace {

using wfs::sim::sha256_hex;

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
        "behavior": {"engagement": "aggressive", "ammo_override": "ammo-556", "failure_action": "hold"},
        "priority": 1,
        "deadline": {"game_time": 3600}
    })";
}

class Handle {
   public:
    explicit Handle(std::uint64_t seed = 42u, int threads = 1)
        : handle_(wfs_sim_create(SampleScenario().string().c_str(), seed, threads)) {}

    ~Handle() { wfs_sim_destroy(handle_); }

    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

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

std::string StateHash(wfs_sim_handle* handle) {
    char buffer[WFS_SIM_STATE_HASH_HEX_LEN] = {};
    EXPECT_EQ(wfs_sim_get_state_hash(handle, buffer), WFS_SIM_RESULT_OK);
    return std::string(buffer);
}

}  // namespace

TEST(WfsSha256Test, KnownAnswerVectors) {
    EXPECT_EQ(sha256_hex(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(sha256_hex("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_EQ(sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(WfsSha256Test, MillionLetterAHasStandardDigest) {
    const std::string million_a(1'000'000u, 'a');
    EXPECT_EQ(sha256_hex(million_a), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST(WfsSha256Test, DigestHexOverloadMatchesStringOverload) {
    EXPECT_EQ(sha256_hex(wfs::sim::sha256("abc")), sha256_hex("abc"));
}

TEST(WfsSnapshotTest, SnapshotContainsEventLogSummary) {
    Handle handle;
    ASSERT_NE(handle.get(), nullptr);
    const nlohmann::json snapshot = SnapshotJson(handle.get());
    const nlohmann::json log = snapshot.at("event_log");
    EXPECT_EQ(log.at("size"), 0);
    EXPECT_EQ(log.at("capacity"), 5000);
    EXPECT_EQ(log.at("critical_count"), 0);
}

TEST(WfsSnapshotTest, SnapshotIsDeterministicAcrossCallsAndHandles) {
    Handle first;
    Handle second;
    ASSERT_NE(first.get(), nullptr);
    ASSERT_NE(second.get(), nullptr);
    EXPECT_EQ(SnapshotText(first.get()), SnapshotText(first.get()));
    EXPECT_EQ(SnapshotText(first.get()), SnapshotText(second.get()));
}

TEST(WfsSnapshotTest, ThreadsIsPresentationMetadataOnly) {
    Handle single(42u, 1);
    Handle quad(42u, 4);
    ASSERT_NE(single.get(), nullptr);
    ASSERT_NE(quad.get(), nullptr);
    EXPECT_EQ(SnapshotJson(single.get()).at("threads"), 1);
    EXPECT_EQ(SnapshotJson(quad.get()).at("threads"), 4);
    // 线程数只影响性能：状态哈希必须一致（宪法第 7 条）。
    EXPECT_EQ(StateHash(single.get()), StateHash(quad.get()));
}

TEST(WfsSnapshotTest, StateHashStableForSameState) {
    Handle first;
    Handle second;
    ASSERT_NE(first.get(), nullptr);
    ASSERT_NE(second.get(), nullptr);
    EXPECT_EQ(StateHash(first.get()), StateHash(first.get()));
    EXPECT_EQ(StateHash(first.get()), StateHash(second.get()));
}

TEST(WfsSnapshotTest, StateHashChangesWhenStateChanges) {
    Handle handle;
    ASSERT_NE(handle.get(), nullptr);
    const std::string before = StateHash(handle.get());

    EXPECT_EQ(wfs_sim_step(handle.get()), WFS_SIM_RESULT_OK);
    EXPECT_NE(StateHash(handle.get()), before);

    const std::string after_step = StateHash(handle.get());
    EXPECT_EQ(wfs_sim_inject_command(handle.get(), ValidCommandJson().c_str()), WFS_SIM_RESULT_OK);
    EXPECT_NE(StateHash(handle.get()), after_step);
}

TEST(WfsSnapshotTest, DifferentSeedProducesDifferentHash) {
    Handle seed42(42u, 1);
    Handle seed43(43u, 1);
    ASSERT_NE(seed42.get(), nullptr);
    ASSERT_NE(seed43.get(), nullptr);
    EXPECT_NE(StateHash(seed42.get()), StateHash(seed43.get()));
}

TEST(WfsSnapshotTest, StateHashIndependentOfThreadCountAfterSteps) {
    Handle single(42u, 1);
    Handle quad(42u, 4);
    ASSERT_NE(single.get(), nullptr);
    ASSERT_NE(quad.get(), nullptr);

    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(wfs_sim_step(single.get()), WFS_SIM_RESULT_OK);
        EXPECT_EQ(wfs_sim_step(quad.get()), WFS_SIM_RESULT_OK);
    }
    EXPECT_EQ(wfs_sim_inject_command(single.get(), ValidCommandJson().c_str()), WFS_SIM_RESULT_OK);
    EXPECT_EQ(wfs_sim_inject_command(quad.get(), ValidCommandJson().c_str()), WFS_SIM_RESULT_OK);
    EXPECT_EQ(wfs_sim_step(single.get()), WFS_SIM_RESULT_OK);
    EXPECT_EQ(wfs_sim_step(quad.get()), WFS_SIM_RESULT_OK);

    EXPECT_EQ(StateHash(single.get()), StateHash(quad.get()));
}

TEST(WfsSnapshotTest, StateHashIs64LowercaseHexCharacters) {
    Handle handle;
    ASSERT_NE(handle.get(), nullptr);
    const std::string hash = StateHash(handle.get());
    EXPECT_EQ(hash.size(), 64u);
    for (const char ch : hash) {
        const bool is_digit = ch >= '0' && ch <= '9';
        const bool is_lower_hex = ch >= 'a' && ch <= 'f';
        EXPECT_TRUE(is_digit || is_lower_hex) << "unexpected hash character: " << ch;
    }
}
