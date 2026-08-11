// tests/sim_tests/save_test.cpp
//
// T017 单元测试：存档序列化框架。
// 覆盖保存→加载往返（tick/队列/RNG/哈希一致）、文件布局（magic + 版本 +
// header_json + state_blob + state_hash）、损坏/版本/元数据不匹配拒绝且不改
// 变句柄状态、IO 错误、确定性字节级一致，以及迁移链骨架（宪法第 13/17 条）。

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "wfs/sim/c_api.h"
#include "wfs/sim/save_format.h"
#include "wfs/sim/sha256.h"

namespace {

using wfs::sim::kCurrentSaveFormatVersion;
using wfs::sim::migrate_state;
using wfs::sim::sha256;
using wfs::sim::sha256_hex;
using wfs::sim::Sha256Digest;

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

// 测试专用临时目录：仅创建于系统临时目录下带唯一前缀的路径，析构时递归清理。
class TempDir {
   public:
    TempDir() {
        static int counter = 0;
        path_ = std::filesystem::temp_directory_path() / ("wfs-save-test-" + std::to_string(counter++));
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
    }

    ~TempDir() {
        const std::filesystem::path temp_root = std::filesystem::temp_directory_path();
        const std::filesystem::path normalized = path_.lexically_normal();
        if (normalized.string().starts_with(temp_root.string())) {
            std::error_code ec;
            std::filesystem::remove_all(normalized, ec);
        }
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    std::filesystem::path path() const { return path_; }

    std::filesystem::path Write(const std::string& filename, const std::string& content) const {
        const std::filesystem::path file = path_ / filename;
        std::ofstream out(file, std::ios::binary);
        out << content;
        return file;
    }

   private:
    std::filesystem::path path_;
};

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
    std::string buffer(4096, '\0');
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

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void AppendU32(std::string& out, std::uint32_t value) {
    out.push_back(static_cast<char>(value & 0xFFU));
    out.push_back(static_cast<char>((value >> 8U) & 0xFFU));
    out.push_back(static_cast<char>((value >> 16U) & 0xFFU));
    out.push_back(static_cast<char>((value >> 24U) & 0xFFU));
}

void AppendU64(std::string& out, std::uint64_t value) {
    for (std::size_t i = 0U; i < 8U; ++i) {
        out.push_back(static_cast<char>((value >> (i * 8U)) & 0xFFU));
    }
}

std::string RawHash(const std::string& data) {
    const Sha256Digest digest = sha256(data);
    return std::string(reinterpret_cast<const char*>(digest.data()), digest.size());
}

std::string BuildSaveBytes(std::uint32_t version, const std::string& header, const std::string& blob) {
    std::string bytes;
    bytes.reserve(8U + 4U + 4U + header.size() + 8U + blob.size() + 32U);
    bytes.append("WFS-SAVE");
    AppendU32(bytes, version);
    AppendU32(bytes, static_cast<std::uint32_t>(header.size()));
    bytes.append(header);
    AppendU64(bytes, static_cast<std::uint64_t>(blob.size()));
    bytes.append(blob);
    bytes.append(RawHash(blob));
    return bytes;
}

struct SavedLayout {
    std::uint32_t version = 0U;
    std::string header;
    std::string blob;
    std::string hash;
    std::size_t blob_offset = 0U;
};

SavedLayout ParseLayout(const std::string& bytes) {
    SavedLayout layout;
    if (bytes.size() < 16U + 8U + 32U) {
        return layout;
    }
    layout.version = static_cast<std::uint32_t>(
        static_cast<std::uint8_t>(bytes[8]) | (static_cast<std::uint8_t>(bytes[9]) << 8U) |
        (static_cast<std::uint8_t>(bytes[10]) << 16U) | (static_cast<std::uint8_t>(bytes[11]) << 24U));
    const std::uint32_t header_len = static_cast<std::uint32_t>(
        static_cast<std::uint8_t>(bytes[12]) | (static_cast<std::uint8_t>(bytes[13]) << 8U) |
        (static_cast<std::uint8_t>(bytes[14]) << 16U) | (static_cast<std::uint8_t>(bytes[15]) << 24U));
    if (bytes.size() < 16U + header_len + 8U + 32U) {
        return layout;
    }
    layout.header = bytes.substr(16U, header_len);
    std::uint64_t blob_len = 0U;
    for (std::size_t i = 0U; i < 8U; ++i) {
        blob_len |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(bytes[16U + header_len + i])) << (i * 8U);
    }
    layout.blob_offset = 16U + header_len + 8U;
    if (bytes.size() < layout.blob_offset + blob_len + 32U) {
        return layout;
    }
    layout.blob = bytes.substr(layout.blob_offset, blob_len);
    layout.hash = bytes.substr(layout.blob_offset + blob_len, 32U);
    return layout;
}

}  // namespace

TEST(WfsSaveTest, SaveLoadRoundTripRestoresState) {
    TempDir dir;
    const std::filesystem::path path = dir.path() / "roundtrip.wfs";
    Handle source;
    ASSERT_NE(source.get(), nullptr);
    EXPECT_EQ(wfs_sim_inject_command(source.get(), ValidCommandJson().c_str()), WFS_SIM_RESULT_OK);
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(wfs_sim_step(source.get()), WFS_SIM_RESULT_OK);
    }
    const std::string hash_before = StateHash(source.get());
    EXPECT_EQ(wfs_sim_save(source.get(), path.string().c_str()), WFS_SIM_RESULT_OK);

    Handle restored(42u, 4);
    ASSERT_NE(restored.get(), nullptr);
    EXPECT_EQ(wfs_sim_load_save(restored.get(), path.string().c_str()), WFS_SIM_RESULT_OK);

    const nlohmann::json source_snapshot = SnapshotJson(source.get());
    const nlohmann::json restored_snapshot = SnapshotJson(restored.get());
    EXPECT_EQ(source_snapshot.at("tick"), restored_snapshot.at("tick"));
    EXPECT_EQ(source_snapshot.at("processed_events"), restored_snapshot.at("processed_events"));
    EXPECT_EQ(source_snapshot.at("pending_events"), restored_snapshot.at("pending_events"));
    EXPECT_EQ(StateHash(restored.get()), hash_before);
    // 线程数是运行期配置，不从存档恢复，也不参与状态哈希（宪法第 7 条）。
    EXPECT_EQ(restored_snapshot.at("threads"), 4);
}

TEST(WfsSaveTest, SaveFileLayoutMatchesContract) {
    TempDir dir;
    const std::filesystem::path path = dir.path() / "layout.wfs";
    Handle handle;
    ASSERT_NE(handle.get(), nullptr);
    EXPECT_EQ(wfs_sim_save(handle.get(), path.string().c_str()), WFS_SIM_RESULT_OK);

    const std::string bytes = ReadFile(path);
    EXPECT_EQ(bytes.substr(0, 8), "WFS-SAVE");
    const SavedLayout layout = ParseLayout(bytes);
    EXPECT_EQ(layout.version, kCurrentSaveFormatVersion);

    const nlohmann::json header = nlohmann::json::parse(layout.header);
    EXPECT_EQ(header.at("abi_version"), WFS_SIM_VERSION_STRING);
    EXPECT_EQ(header.at("scenario_id"), "scn-smoke-test");
    EXPECT_EQ(header.at("scenario_name"), "Smoke Test Scenario (Foundational)");
    EXPECT_EQ(header.at("tick"), 0);
    EXPECT_EQ(header.at("seed"), 42);
    EXPECT_EQ(header.at("threads"), 1);
    EXPECT_EQ(header.at("schema_version"), 1);
    EXPECT_EQ(header.at("state_hash_alg"), "SHA-256");
    EXPECT_EQ(header.at("state_size_bytes"), layout.blob.size());

    const nlohmann::json blob = nlohmann::json::parse(layout.blob);
    EXPECT_EQ(blob.at("tick"), 0);
    EXPECT_EQ(blob.at("seed"), 42);
    EXPECT_EQ(blob.at("scenario_id"), "scn-smoke-test");
    EXPECT_TRUE(blob.at("rng").is_object());
    EXPECT_TRUE(blob.at("queue").at("events").is_array());
    EXPECT_EQ(blob.at("queue").at("next_seq"), 0);
    EXPECT_TRUE(blob.at("event_log").at("entries").is_array());
    EXPECT_EQ(blob.at("processed_events"), 0);

    // 内嵌 state_hash == SHA-256(state_blob) == C API 状态哈希。
    EXPECT_EQ(layout.hash, RawHash(layout.blob));
    const Sha256Digest embedded = [&] {
        Sha256Digest digest{};
        std::copy(layout.hash.begin(), layout.hash.end(), digest.begin());
        return digest;
    }();
    EXPECT_EQ(sha256_hex(embedded), StateHash(handle.get()));
}

TEST(WfsSaveTest, SaveIsDeterministicByteIdentical) {
    TempDir dir;
    const std::filesystem::path first_path = dir.path() / "first.wfs";
    const std::filesystem::path second_path = dir.path() / "second.wfs";
    Handle first;
    Handle second;
    ASSERT_NE(first.get(), nullptr);
    ASSERT_NE(second.get(), nullptr);
    EXPECT_EQ(wfs_sim_save(first.get(), first_path.string().c_str()), WFS_SIM_RESULT_OK);
    EXPECT_EQ(wfs_sim_save(second.get(), second_path.string().c_str()), WFS_SIM_RESULT_OK);
    EXPECT_EQ(ReadFile(first_path), ReadFile(second_path));
}

TEST(WfsSaveTest, SaveLoadPreservesPendingCommandQueue) {
    TempDir dir;
    const std::filesystem::path path = dir.path() / "queue.wfs";
    Handle source;
    ASSERT_NE(source.get(), nullptr);
    EXPECT_EQ(wfs_sim_inject_command(source.get(), ValidCommandJson().c_str()), WFS_SIM_RESULT_OK);
    EXPECT_EQ(wfs_sim_inject_command(source.get(), ValidCommandJson().c_str()), WFS_SIM_RESULT_OK);
    EXPECT_EQ(SnapshotJson(source.get()).at("pending_events"), 2);
    EXPECT_EQ(wfs_sim_save(source.get(), path.string().c_str()), WFS_SIM_RESULT_OK);

    Handle restored;
    ASSERT_NE(restored.get(), nullptr);
    EXPECT_EQ(wfs_sim_load_save(restored.get(), path.string().c_str()), WFS_SIM_RESULT_OK);
    EXPECT_EQ(SnapshotJson(restored.get()).at("pending_events"), 2);

    EXPECT_EQ(wfs_sim_step(source.get()), WFS_SIM_RESULT_OK);
    EXPECT_EQ(wfs_sim_step(restored.get()), WFS_SIM_RESULT_OK);
    EXPECT_EQ(StateHash(source.get()), StateHash(restored.get()));
}

TEST(WfsSaveTest, LoadRejectsCorruptMagicWithoutMutatingHandle) {
    TempDir dir;
    const std::filesystem::path path = dir.path() / "corrupt-magic.wfs";
    Handle handle;
    ASSERT_NE(handle.get(), nullptr);
    EXPECT_EQ(wfs_sim_save(handle.get(), path.string().c_str()), WFS_SIM_RESULT_OK);

    std::string bytes = ReadFile(path);
    bytes[0] = 'X';
    const std::filesystem::path corrupt = dir.Write("corrupt-magic-fixed.wfs", bytes);
    const std::string before = SnapshotText(handle.get());
    EXPECT_EQ(wfs_sim_load_save(handle.get(), corrupt.string().c_str()), WFS_SIM_RESULT_INVALID_DATA);
    EXPECT_EQ(SnapshotText(handle.get()), before);
}

TEST(WfsSaveTest, LoadRejectsUnsupportedVersions) {
    TempDir dir;
    const std::filesystem::path path = dir.path() / "versions.wfs";
    Handle handle;
    ASSERT_NE(handle.get(), nullptr);
    EXPECT_EQ(wfs_sim_save(handle.get(), path.string().c_str()), WFS_SIM_RESULT_OK);
    const SavedLayout layout = ParseLayout(ReadFile(path));

    // 未来版本：明确报错（宪法第 13 条：禁止静默恢复）。
    const std::filesystem::path future = dir.Write("future.wfs", BuildSaveBytes(999u, layout.header, layout.blob));
    EXPECT_EQ(wfs_sim_load_save(handle.get(), future.string().c_str()), WFS_SIM_RESULT_INVALID_DATA);

    // 无法迁移的旧版本（v0 早于首个版本）：明确报错。
    const std::filesystem::path ancient = dir.Write("ancient.wfs", BuildSaveBytes(0u, layout.header, layout.blob));
    EXPECT_EQ(wfs_sim_load_save(handle.get(), ancient.string().c_str()), WFS_SIM_RESULT_INVALID_DATA);
}

TEST(WfsSaveTest, LoadRejectsHashMismatch) {
    TempDir dir;
    const std::filesystem::path path = dir.path() / "hash.wfs";
    Handle handle;
    ASSERT_NE(handle.get(), nullptr);
    EXPECT_EQ(wfs_sim_save(handle.get(), path.string().c_str()), WFS_SIM_RESULT_OK);

    std::string bytes = ReadFile(path);
    const SavedLayout layout = ParseLayout(bytes);
    bytes[layout.blob_offset + 3U] ^= 0x01U;  // 翻转 state_blob 内一个字节。
    const std::filesystem::path corrupt = dir.Write("hash-fixed.wfs", bytes);
    EXPECT_EQ(wfs_sim_load_save(handle.get(), corrupt.string().c_str()), WFS_SIM_RESULT_INVALID_DATA);
}

TEST(WfsSaveTest, LoadRejectsHugeBlobLengthWithoutOverflow) {
    TempDir dir;
    Handle handle;
    ASSERT_NE(handle.get(), nullptr);
    const std::string header = "{}";
    std::string bytes = "WFS-SAVE";
    AppendU32(bytes, kCurrentSaveFormatVersion);
    AppendU32(bytes, static_cast<std::uint32_t>(header.size()));
    bytes.append(header);
    const std::size_t blob_offset = 16U + header.size() + 8U;
    // 恶意 blob_length：旧"加法后比较"的边界检查会无符号回绕并放行，
    // 随后 hash_offset 溢出到地址空间之外（UB）。修复后必须直接拒绝。
    const std::uint64_t blob_length =
        std::numeric_limits<std::uint64_t>::max() - static_cast<std::uint64_t>(blob_offset) - 15U;
    AppendU64(bytes, blob_length);
    bytes.append(32U, '\0');  // 占位"blob"字节：满足 header 长度检查的最小文件。
    const std::filesystem::path path = dir.Write("huge-blob-length.wfs", bytes);
    EXPECT_EQ(wfs_sim_load_save(handle.get(), path.string().c_str()), WFS_SIM_RESULT_INVALID_DATA);
}

TEST(WfsSaveTest, LoadRejectsTruncatedFile) {
    TempDir dir;
    const std::filesystem::path path = dir.path() / "truncated.wfs";
    Handle handle;
    ASSERT_NE(handle.get(), nullptr);
    EXPECT_EQ(wfs_sim_save(handle.get(), path.string().c_str()), WFS_SIM_RESULT_OK);
    const std::string bytes = ReadFile(path);
    EXPECT_EQ(
        wfs_sim_load_save(handle.get(), dir.Write("half.wfs", bytes.substr(0, bytes.size() / 2U)).string().c_str()),
        WFS_SIM_RESULT_INVALID_DATA);
    EXPECT_EQ(wfs_sim_load_save(handle.get(), dir.Write("tiny.wfs", "WFS").string().c_str()),
              WFS_SIM_RESULT_INVALID_DATA);
}

TEST(WfsSaveTest, SaveLoadRestoresQueueSequenceCursor) {
    TempDir dir;
    const std::filesystem::path first_path = dir.path() / "cursor-a.wfs";
    Handle first;
    ASSERT_NE(first.get(), nullptr);
    // 两条同 tick 命令入队并处理：队列清空但 auto 游标推进到 2。
    EXPECT_EQ(wfs_sim_inject_command(first.get(), ValidCommandJson().c_str()), WFS_SIM_RESULT_OK);
    EXPECT_EQ(wfs_sim_inject_command(first.get(), ValidCommandJson().c_str()), WFS_SIM_RESULT_OK);
    EXPECT_EQ(wfs_sim_step(first.get()), WFS_SIM_RESULT_OK);
    EXPECT_EQ(SnapshotJson(first.get()).at("pending_events"), 0);
    EXPECT_EQ(wfs_sim_save(first.get(), first_path.string().c_str()), WFS_SIM_RESULT_OK);

    Handle restored;
    ASSERT_NE(restored.get(), nullptr);
    EXPECT_EQ(wfs_sim_load_save(restored.get(), first_path.string().c_str()), WFS_SIM_RESULT_OK);

    // 原始与恢复句柄各自再注入两条同 tick 命令并处理：auto 序列号必须与
    // 原始运行完全一致（T011 全局单调不回收，加载后从 2 继续而非归零）。
    for (int i = 0; i < 2; ++i) {
        EXPECT_EQ(wfs_sim_inject_command(first.get(), ValidCommandJson().c_str()), WFS_SIM_RESULT_OK);
        EXPECT_EQ(wfs_sim_inject_command(restored.get(), ValidCommandJson().c_str()), WFS_SIM_RESULT_OK);
    }
    EXPECT_EQ(wfs_sim_step(first.get()), WFS_SIM_RESULT_OK);
    EXPECT_EQ(wfs_sim_step(restored.get()), WFS_SIM_RESULT_OK);
    EXPECT_EQ(StateHash(first.get()), StateHash(restored.get()));

    const std::filesystem::path second_path = dir.path() / "cursor-b.wfs";
    const std::filesystem::path third_path = dir.path() / "cursor-c.wfs";
    EXPECT_EQ(wfs_sim_save(first.get(), second_path.string().c_str()), WFS_SIM_RESULT_OK);
    EXPECT_EQ(wfs_sim_save(restored.get(), third_path.string().c_str()), WFS_SIM_RESULT_OK);
    // 字节级一致 + 显式断言游标值：两路运行都应为 2 + 2 = 4。
    EXPECT_EQ(ReadFile(second_path), ReadFile(third_path));
    const SavedLayout layout = ParseLayout(ReadFile(second_path));
    EXPECT_EQ(nlohmann::json::parse(layout.blob).at("queue").at("next_seq"), 4);
}

TEST(WfsSaveTest, LoadRejectsEventLogExceedingCapacity) {
    TempDir dir;
    const std::filesystem::path path = dir.path() / "log-capacity.wfs";
    Handle handle;
    ASSERT_NE(handle.get(), nullptr);
    EXPECT_EQ(wfs_sim_save(handle.get(), path.string().c_str()), WFS_SIM_RESULT_OK);

    const SavedLayout layout = ParseLayout(ReadFile(path));
    nlohmann::json header = nlohmann::json::parse(layout.header);
    nlohmann::json blob = nlohmann::json::parse(layout.blob);
    // 2 条事件但环形容量只有 1：合法状态不可能出现，必须拒绝。
    blob["event_log"]["capacity"] = 1;
    blob["event_log"]["entries"] = nlohmann::json::array({
        nlohmann::json{{"seq", 0}, {"tick", 0}, {"category", "command"}, {"severity", "info"}, {"message", "a"}},
        nlohmann::json{{"seq", 1}, {"tick", 0}, {"category", "command"}, {"severity", "info"}, {"message", "b"}},
    });
    const std::string new_blob = blob.dump();
    header["state_size_bytes"] = new_blob.size();
    const std::filesystem::path crafted =
        dir.Write("log-capacity-fixed.wfs", BuildSaveBytes(layout.version, header.dump(), new_blob));
    EXPECT_EQ(wfs_sim_load_save(handle.get(), crafted.string().c_str()), WFS_SIM_RESULT_INVALID_DATA);
}

TEST(WfsSaveTest, LoadRejectsAbiVersionMismatch) {
    TempDir dir;
    const std::filesystem::path path = dir.path() / "abi.wfs";
    Handle handle;
    ASSERT_NE(handle.get(), nullptr);
    EXPECT_EQ(wfs_sim_save(handle.get(), path.string().c_str()), WFS_SIM_RESULT_OK);

    const SavedLayout layout = ParseLayout(ReadFile(path));
    nlohmann::json header = nlohmann::json::parse(layout.header);
    header["abi_version"] = "0.0.0";
    const std::filesystem::path mismatched =
        dir.Write("abi-fixed.wfs", BuildSaveBytes(layout.version, header.dump(), layout.blob));
    EXPECT_EQ(wfs_sim_load_save(handle.get(), mismatched.string().c_str()), WFS_SIM_RESULT_INVALID_DATA);
}

TEST(WfsSaveTest, LoadRejectsGarbageStateBlob) {
    TempDir dir;
    Handle handle;
    ASSERT_NE(handle.get(), nullptr);
    const std::string header = nlohmann::json{{"scenario_id", "scn-smoke-test"}}.dump();
    const std::filesystem::path path =
        dir.Write("garbage.wfs", BuildSaveBytes(kCurrentSaveFormatVersion, header, "not json"));
    EXPECT_EQ(wfs_sim_load_save(handle.get(), path.string().c_str()), WFS_SIM_RESULT_INVALID_DATA);
}

TEST(WfsSaveTest, LoadRejectsScenarioMismatch) {
    TempDir dir;
    const std::filesystem::path path = dir.path() / "scenario.wfs";
    Handle handle;
    ASSERT_NE(handle.get(), nullptr);
    EXPECT_EQ(wfs_sim_save(handle.get(), path.string().c_str()), WFS_SIM_RESULT_OK);

    const SavedLayout layout = ParseLayout(ReadFile(path));
    nlohmann::json header = nlohmann::json::parse(layout.header);
    nlohmann::json blob = nlohmann::json::parse(layout.blob);
    header["scenario_id"] = "other-scenario";
    blob["scenario_id"] = "other-scenario";
    const std::string new_header = header.dump();
    const std::string new_blob = blob.dump();
    header["state_size_bytes"] = new_blob.size();
    const std::filesystem::path mismatched =
        dir.Write("scenario-fixed.wfs", BuildSaveBytes(layout.version, header.dump(), new_blob));
    EXPECT_EQ(wfs_sim_load_save(handle.get(), mismatched.string().c_str()), WFS_SIM_RESULT_INVALID_DATA);
}

TEST(WfsSaveTest, LoadRejectsSeedMismatch) {
    TempDir dir;
    const std::filesystem::path path = dir.path() / "seed.wfs";
    Handle handle;
    ASSERT_NE(handle.get(), nullptr);
    EXPECT_EQ(wfs_sim_save(handle.get(), path.string().c_str()), WFS_SIM_RESULT_OK);

    const SavedLayout layout = ParseLayout(ReadFile(path));
    nlohmann::json header = nlohmann::json::parse(layout.header);
    nlohmann::json blob = nlohmann::json::parse(layout.blob);
    header["seed"] = 43;
    blob["seed"] = 43;
    const std::string new_blob = blob.dump();
    header["state_size_bytes"] = new_blob.size();
    const std::filesystem::path mismatched =
        dir.Write("seed-fixed.wfs", BuildSaveBytes(layout.version, header.dump(), new_blob));
    EXPECT_EQ(wfs_sim_load_save(handle.get(), mismatched.string().c_str()), WFS_SIM_RESULT_INVALID_DATA);
}

TEST(WfsSaveTest, SaveToMissingDirectoryReturnsIoError) {
    TempDir dir;
    Handle handle;
    ASSERT_NE(handle.get(), nullptr);
    const std::filesystem::path missing = dir.path() / "no-such-dir" / "save.wfs";
    EXPECT_EQ(wfs_sim_save(handle.get(), missing.string().c_str()), WFS_SIM_RESULT_IO_ERROR);
}

TEST(WfsSaveTest, SaveTwiceOverwritesExistingFile) {
    TempDir dir;
    const std::filesystem::path path = dir.path() / "overwrite.wfs";
    Handle handle;
    ASSERT_NE(handle.get(), nullptr);
    EXPECT_EQ(wfs_sim_save(handle.get(), path.string().c_str()), WFS_SIM_RESULT_OK);
    EXPECT_EQ(wfs_sim_step(handle.get()), WFS_SIM_RESULT_OK);
    EXPECT_EQ(wfs_sim_save(handle.get(), path.string().c_str()), WFS_SIM_RESULT_OK);

    Handle restored;
    ASSERT_NE(restored.get(), nullptr);
    EXPECT_EQ(wfs_sim_load_save(restored.get(), path.string().c_str()), WFS_SIM_RESULT_OK);
    EXPECT_EQ(SnapshotJson(restored.get()).at("tick"), 1);
}

TEST(WfsSaveTest, ConcurrentSavesToSamePathRemainValid) {
    TempDir dir;
    const std::filesystem::path path = dir.path() / "concurrent.wfs";
    std::atomic<bool> start_flag{false};
    std::vector<wfs_sim_result> results(2U, WFS_SIM_RESULT_INTERNAL_ERROR);
    std::vector<std::thread> threads;
    threads.reserve(results.size());
    for (std::size_t t = 0U; t < results.size(); ++t) {
        threads.emplace_back([&, t]() {
            Handle handle;
            if (handle.get() == nullptr) {
                return;
            }
            start_flag.wait(false);
            for (int i = 0; i < 20; ++i) {
                results[t] = wfs_sim_save(handle.get(), path.string().c_str());
                if (results[t] != WFS_SIM_RESULT_OK) {
                    return;
                }
            }
        });
    }
    start_flag.store(true);
    start_flag.notify_all();
    for (std::thread& thread : threads) {
        thread.join();
    }
    EXPECT_EQ(results[0], WFS_SIM_RESULT_OK);
    EXPECT_EQ(results[1], WFS_SIM_RESULT_OK);

    // 并发写入后文件必须是完整可加载的存档（无半写）。
    Handle restored;
    ASSERT_NE(restored.get(), nullptr);
    EXPECT_EQ(wfs_sim_load_save(restored.get(), path.string().c_str()), WFS_SIM_RESULT_OK);
}

TEST(WfsSaveTest, MigrateStateIdentityForCurrentVersion) {
    const nlohmann::json state = {{"tick", 0}, {"seed", 42}};
    EXPECT_EQ(migrate_state(state, kCurrentSaveFormatVersion, kCurrentSaveFormatVersion), state);
}

TEST(WfsSaveTest, MigrateStateRejectsUnsupportedVersions) {
    const nlohmann::json state = {{"tick", 0}};
    EXPECT_THROW(migrate_state(state, 0u, 1u), std::invalid_argument);
    EXPECT_THROW(migrate_state(state, 2u, 2u), std::invalid_argument);
    EXPECT_THROW(migrate_state(state, 1u, 2u), std::invalid_argument);
    EXPECT_THROW(migrate_state(state, 2u, 1u), std::invalid_argument);
}
