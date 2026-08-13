// sim/src/save.cpp
//
// T017：存档序列化框架实现。
//
// 实现策略：
// - 布局按 contracts/save-format.md：magic("WFS-SAVE"，8 字节) +
//   format_version(u32 LE) + header_json(长度 u32 LE + 字节) +
//   state_blob(长度 u64 LE + 字节) + state_hash(SHA-256 原始 32 字节)。
//   多字节字段显式按小端写入/读取，不依赖宿主字节序（宪法第 7 条）。
// - state_hash = SHA-256(state_blob)，与 wfs_sim_get_state_hash 一致
//   （blob 即 serialize_state_json 的权威序列化）；header 只承载元数据
//   （含 threads），因此线程数不影响内嵌哈希（宪法第 7 条）。
// - 加载校验顺序：magic → 版本 → header JSON → blob JSON → 哈希 →
//   元数据交叉校验（header/blob/句柄三者一致）→ 组装新状态 → 一次性提交。
//   T020 决策日志与编号游标随 blob 恢复；旧存档缺失字段时按空日志/0 游标
//   兼容（非破坏性演进，不递增 format_version）；恢复后校验
//   log.empty() == (counter == 0)，不一致按损坏数据拒绝。
//   任何失败都返回错误码且不改写句柄（强保证；宪法第 17 条禁止静默恢复）。
// - 写入用"临时文件 + 替换"避免半写存档；失败时清理临时文件。
// - 迁移链：version < 当前版本时经 migrate_state 逐级迁移；v1 为第一版，
//   无历史迁移，v0/未来版本显式报错（宪法第 13 条）。

#include "save.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include <nlohmann/json.hpp>

#include "wfs/sim/c_api.h"
#include "wfs/sim/event_log.h"
#include "wfs/sim/queue.h"
#include "wfs/sim/save_format.h"
#include "wfs/sim/sha256.h"

#include "state_serialization.h"

namespace wfs::sim {

namespace {

constexpr std::size_t kMagicSize = 8U;
constexpr std::size_t kVersionSize = 4U;
constexpr std::size_t kHeaderLengthSize = 4U;
constexpr std::size_t kBlobLengthSize = 8U;
constexpr std::size_t kFixedHeaderBytes = kMagicSize + kVersionSize + kHeaderLengthSize;
constexpr std::uint32_t kByteBits = 8U;     // 每字节位宽（小端序列化移位步长）。
constexpr std::uint32_t kByteMask = 0xFFU;  // 字节掩码。

void AppendU32Le(std::string& out, std::uint32_t value) {
    for (std::size_t i = 0U; i < kVersionSize; ++i) {
        out.push_back(static_cast<char>((value >> (kByteBits * i)) & kByteMask));
    }
}

void AppendU64Le(std::string& out, std::uint64_t value) {
    for (std::size_t i = 0U; i < kBlobLengthSize; ++i) {
        out.push_back(static_cast<char>((value >> (kByteBits * i)) & kByteMask));
    }
}

bool ReadU32Le(const std::string& data, std::size_t offset, std::uint32_t& value) {
    if (offset + kVersionSize > data.size()) {
        return false;
    }
    value = 0U;
    for (std::size_t i = 0U; i < kVersionSize; ++i) {
        value |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(data[offset + i])) << (kByteBits * i);
    }
    return true;
}

bool ReadU64Le(const std::string& data, std::size_t offset, std::uint64_t& value) {
    if (offset + kBlobLengthSize > data.size()) {
        return false;
    }
    value = 0U;
    for (std::size_t i = 0U; i < kBlobLengthSize; ++i) {
        value |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(data[offset + i])) << (kByteBits * i);
    }
    return true;
}

// 解析 JSON 文本；失败统一映射为 invalid_argument（由调用方转 INVALID_DATA）。
nlohmann::json ParseJson(const std::string& text, const char* field_name) {
    try {
        return nlohmann::json::parse(text);
    } catch (const nlohmann::json::exception&) {
        throw std::invalid_argument(std::string("wfs::sim::load_save_into: ") + field_name + " is not valid JSON");
    }
}

bool ReadWholeFile(const std::filesystem::path& path, std::string& content) {
    std::ifstream input_stream(path, std::ios::binary);
    if (!input_stream) {
        return false;
    }
    std::ostringstream buffer;
    buffer << input_stream.rdbuf();
    content = buffer.str();
    return true;
}

// 同一路径的并发写入互斥：按规范化路径持锁，保证"并发写入串行化、
// 不产生半写存档"（contracts/save-format.md；T089 槽位管理同源约束）。
std::mutex& PathLock(const std::filesystem::path& path) {
    static std::mutex registry_mutex;
    static std::map<std::string, std::mutex> locks;
    const std::string key = path.lexically_normal().string();
    std::lock_guard<std::mutex> registry_guard(registry_mutex);
    return locks[key];
}

// 每次写入使用唯一临时名（进程内原子计数 + 线程 id）：并发写同一路径时
// 各线程写各自临时文件，不会交错写同一文件。
std::filesystem::path UniqueTempPath(const std::filesystem::path& path) {
    static std::atomic<std::uint64_t> counter{0U};
    std::filesystem::path temp = path;
    temp += ".tmp." + std::to_string(counter.fetch_add(1U)) + "." +
            std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    return temp;
}

// 写临时文件；失败清理临时文件并返回 IO_ERROR。
wfs_sim_result WriteTempFile(const std::filesystem::path& temp, const std::string& content) {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out) {
        return WFS_SIM_RESULT_IO_ERROR;
    }
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    out.close();
    if (!out) {
        std::error_code cleanup_error;
        std::filesystem::remove(temp, cleanup_error);
        return WFS_SIM_RESULT_IO_ERROR;
    }
    return WFS_SIM_RESULT_OK;
}

// 临时文件 + 原子替换：写入中断不会留下半写存档；目标替换原子完成，
// 不存在"旧存档已删、新存档未就位"的崩溃窗口。
wfs_sim_result WriteFileAtomic(const std::filesystem::path& path, const std::string& content) {
    std::lock_guard<std::mutex> path_guard(PathLock(path));
    const std::filesystem::path temp = UniqueTempPath(path);
    if (WriteTempFile(temp, content) != WFS_SIM_RESULT_OK) {
        return WFS_SIM_RESULT_IO_ERROR;
    }
#ifdef _WIN32
    // MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH)：原子替换目标并落盘。
    if (MoveFileExW(temp.wstring().c_str(), path.wstring().c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
        std::error_code cleanup_error;
        std::filesystem::remove(temp, cleanup_error);
        return WFS_SIM_RESULT_IO_ERROR;
    }
    return WFS_SIM_RESULT_OK;
#else
    // 非 Windows 回退（best-effort）：POSIX rename 本身可原子覆盖目标，
    // 因此先直接 rename；个别平台不支持覆盖时再退化为"删旧 + rename"
    // （该退化存在短暂丢旧档窗口，仅作为不可用环境下的尽力而为；Windows
    // 主目标走上方 MoveFileExW 原子路径）。
    std::error_code error;
    std::filesystem::rename(temp, path, error);
    if (!error) {
        return WFS_SIM_RESULT_OK;
    }
    error.clear();
    std::filesystem::remove(path, error);  // 目标不存在时 remove 会失败，忽略。
    error.clear();
    std::filesystem::rename(temp, path, error);
    if (error) {
        std::error_code cleanup_error;
        std::filesystem::remove(temp, cleanup_error);
        return WFS_SIM_RESULT_IO_ERROR;
    }
    return WFS_SIM_RESULT_OK;
#endif
}

nlohmann::json BuildHeader(const SimState& state, std::size_t blob_size) {
    return nlohmann::json{
        {"abi_version", WFS_SIM_VERSION_STRING},
        {"scenario_id", state.scenario.id},
        {"scenario_name", state.scenario.name},
        {"tick", state.clock.tick()},
        {"seed", state.seed},
        {"threads", state.threads},
        {"schema_version", state.scenario.schema_version},
        {"state_hash_alg", "SHA-256"},
        {"state_size_bytes", blob_size},
    };
}

template <typename T>
T RequireField(const nlohmann::json& document, const char* key) {
    return document.at(key).get<T>();
}

EventQueue RestoreQueue(const nlohmann::json& root) {
    const nlohmann::json& queue_json = root.at("queue");
    EventQueue queue;
    for (const nlohmann::json& event : queue_json.at("events")) {
        queue.enqueue(RequireField<GameTick>(event, "tick"), RequireField<std::uint64_t>(event, "seq"),
                      RequireField<std::string>(event, "payload"));
    }
    // 游标是队列状态的一部分：恢复后 auto 序列号与原始运行完全一致
    // （T011 全局单调不回收）；回退游标视为损坏数据，restore_next_seq
    // 显式报错（宪法第 17 条）。
    queue.restore_next_seq(RequireField<std::uint64_t>(queue_json, "next_seq"));
    return queue;
}

EventLog RestoreEventLog(const nlohmann::json& root) {
    const nlohmann::json& log = root.at("event_log");
    const std::size_t capacity = RequireField<std::size_t>(log, "capacity");
    const nlohmann::json& entries = log.at("entries");
    if (entries.size() > capacity) {
        // 条目数超过环形容量在合法状态下不可能出现：损坏数据显式拒绝。
        throw std::invalid_argument("wfs::sim::load_save_into: event log entries exceed capacity");
    }
    EventLog restored(capacity);
    for (const nlohmann::json& entry : entries) {
        restored.append(RequireField<GameTick>(entry, "tick"),
                        event_category_from_string(RequireField<std::string>(entry, "category")),
                        event_severity_from_string(RequireField<std::string>(entry, "severity")),
                        RequireField<std::string>(entry, "message"), RequireField<std::uint64_t>(entry, "seq"));
    }
    return restored;
}

DecisionLog RestoreDecisionLog(const nlohmann::json& root) {
    if (!root.contains("decision_log")) {
        return DecisionLog{};  // 旧版存档兼容：无决策日志。
    }
    return decision_log_from_json(root.at("decision_log"));
}

// 决策日志内 decision_id 必须唯一（CHK052 回放标识）；重复视为损坏数据。
bool HasDuplicateDecisionIds(const DecisionLog& log) {
    std::unordered_set<std::string> seen_decision_ids;
    for (const AiDecisionRecord& record : log.entries()) {
        if (!seen_decision_ids.insert(record.decision_id).second) {
            return true;
        }
    }
    return false;
}

}  // namespace

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
nlohmann::json migrate_state(const nlohmann::json& state, std::uint32_t from_version, std::uint32_t to_version) {
    if (from_version < kFirstSaveFormatVersion) {
        throw std::invalid_argument("wfs::sim::migrate_state: no migration path from format version " +
                                    std::to_string(from_version));
    }
    if (from_version > kCurrentSaveFormatVersion) {
        throw std::invalid_argument("wfs::sim::migrate_state: format version " + std::to_string(from_version) +
                                    " is newer than supported " + std::to_string(kCurrentSaveFormatVersion));
    }
    if (to_version > kCurrentSaveFormatVersion) {
        throw std::invalid_argument("wfs::sim::migrate_state: target version " + std::to_string(to_version) +
                                    " is not yet supported");
    }
    if (to_version < from_version) {
        throw std::invalid_argument("wfs::sim::migrate_state: target version must not be older than source version");
    }

    // 迁移链骨架：键 = 源版本，函数把状态从 v 迁移到 v+1。v1 是第一版
    // 格式，表为空；未来破坏性变更按版本号登记步骤并递增
    // kCurrentSaveFormatVersion（宪法第 13 条：逐级迁移，禁止跳级；
    // 缺失步骤由 std::map::at 显式报错）。
    static const std::map<std::uint32_t, std::function<nlohmann::json(nlohmann::json)>> migration_steps;
    nlohmann::json current = state;
    for (std::uint32_t version = from_version; version < to_version; ++version) {
        current = migration_steps.at(version)(std::move(current));
    }
    return current;
}
// NOLINTEND(bugprone-easily-swappable-parameters)

wfs_sim_result save_to_file(const SimState& state, const std::filesystem::path& path) {
    try {
        const std::string blob = serialize_state_json(state).dump();
        const std::string header = BuildHeader(state, blob.size()).dump();
        const Sha256Digest digest = sha256(blob);

        std::string file;
        file.reserve(kSaveMagic.size() + kVersionSize + kHeaderLengthSize + header.size() + kBlobLengthSize +
                     blob.size() + digest.size());
        file.append(kSaveMagic.data(), kSaveMagic.size());
        AppendU32Le(file, kCurrentSaveFormatVersion);
        AppendU32Le(file, static_cast<std::uint32_t>(header.size()));
        file.append(header);
        AppendU64Le(file, static_cast<std::uint64_t>(blob.size()));
        file.append(blob);
        file.append(reinterpret_cast<const char*>(digest.data()), digest.size());
        return WriteFileAtomic(path, file);
    } catch (...) {
        return WFS_SIM_RESULT_INTERNAL_ERROR;
    }
}

// 存档加载是固定顺序的校验事务（magic→版本→哈希→元数据→组装→一次性提交），
// 拆分会破坏"失败不改写句柄"的强保证。
// NOLINTBEGIN(readability-function-cognitive-complexity)
wfs_sim_result load_save_into(SimState& state, const std::filesystem::path& path) {
    std::string data;
    if (!ReadWholeFile(path, data)) {
        return WFS_SIM_RESULT_IO_ERROR;
    }
    try {
        if (data.size() < kFixedHeaderBytes + kBlobLengthSize + kSaveStateHashSize) {
            return WFS_SIM_RESULT_INVALID_DATA;
        }
        if (!std::equal(kSaveMagic.begin(), kSaveMagic.end(), data.begin())) {
            return WFS_SIM_RESULT_INVALID_DATA;
        }

        std::uint32_t version = 0U;
        std::uint32_t header_length = 0U;
        if (!ReadU32Le(data, kMagicSize, version) || !ReadU32Le(data, kMagicSize + kVersionSize, header_length)) {
            return WFS_SIM_RESULT_INVALID_DATA;
        }
        const std::size_t header_offset = kFixedHeaderBytes;
        if (header_length > data.size() - header_offset - kBlobLengthSize - kSaveStateHashSize) {
            return WFS_SIM_RESULT_INVALID_DATA;
        }
        const nlohmann::json header = ParseJson(data.substr(header_offset, header_length), "header_json");

        std::uint64_t blob_length = 0U;
        if (!ReadU64Le(data, header_offset + header_length, blob_length)) {
            return WFS_SIM_RESULT_INVALID_DATA;
        }
        const std::size_t blob_offset = header_offset + header_length + kBlobLengthSize;
        // 先减后比：blob_length 是 u64，加法可能无符号回绕；前面的 header
        // 长度检查已保证 data.size() >= blob_offset + kSaveStateHashSize，
        // 因此右侧不会下溢——恶意超大 blob_length 直接拒绝（宪法第 17 条）。
        if (blob_length > data.size() - blob_offset - kSaveStateHashSize) {
            return WFS_SIM_RESULT_INVALID_DATA;
        }
        const std::string blob = data.substr(blob_offset, blob_length);
        const Sha256Digest embedded_hash = [&] {
            Sha256Digest digest{};
            const std::size_t hash_offset = blob_offset + blob_length;
            std::copy(data.begin() + static_cast<std::ptrdiff_t>(hash_offset),
                      data.begin() + static_cast<std::ptrdiff_t>(hash_offset + kSaveStateHashSize), digest.begin());
            return digest;
        }();
        if (sha256(blob) != embedded_hash) {
            return WFS_SIM_RESULT_INVALID_DATA;
        }

        nlohmann::json parsed = ParseJson(blob, "state_blob");
        if (version > kCurrentSaveFormatVersion) {
            return WFS_SIM_RESULT_INVALID_DATA;  // 未来版本：显式拒绝，不猜测结构。
        }
        if (version < kCurrentSaveFormatVersion) {
            parsed = migrate_state(parsed, version, kCurrentSaveFormatVersion);
        }

        // 元数据交叉校验：header / blob / 当前句柄三者必须一致。
        if (RequireField<std::string>(header, "abi_version") != WFS_SIM_VERSION_STRING) {
            // 核心 ABI 与存档 ABI 错配：禁止静默加载（防跨版本损坏）。
            return WFS_SIM_RESULT_INVALID_DATA;
        }
        if (RequireField<std::string>(header, "scenario_id") != state.scenario.id ||
            RequireField<std::string>(parsed, "scenario_id") != state.scenario.id) {
            return WFS_SIM_RESULT_INVALID_DATA;
        }
        if (RequireField<std::uint64_t>(header, "seed") != state.seed ||
            RequireField<std::uint64_t>(parsed, "seed") != state.seed) {
            return WFS_SIM_RESULT_INVALID_DATA;
        }
        if (RequireField<GameTick>(header, "tick") != RequireField<GameTick>(parsed, "tick") ||
            RequireField<std::int64_t>(header, "schema_version") != state.scenario.schema_version ||
            RequireField<std::string>(header, "state_hash_alg") != "SHA-256" ||
            RequireField<std::size_t>(header, "state_size_bytes") != blob.size()) {
            return WFS_SIM_RESULT_INVALID_DATA;
        }

        // 先完整组装新状态，全部成功后再一次性提交，保证失败加载不改写句柄。
        SimState next = state;
        next.clock.reset(RequireField<GameTick>(parsed, "tick"));
        const nlohmann::json& rng_json = parsed.at("rng");
        next.rng.restore(Rng::State{RequireField<std::uint64_t>(rng_json, "state"),
                                    RequireField<std::uint64_t>(rng_json, "stream")});
        next.queue = RestoreQueue(parsed);
        next.processed_events = RequireField<std::uint64_t>(parsed, "processed_events");
        next.event_log = RestoreEventLog(parsed);
        next.decision_log = RestoreDecisionLog(parsed);
        // 旧存档缺失 ai_decision_counter 时显式置 0（与 decision_log 缺失
        // 重置为空对称），保证脏句柄加载旧存档后决策状态完全清空。
        next.ai_decision_counter =
            parsed.contains("ai_decision_counter") ? RequireField<std::uint64_t>(parsed, "ai_decision_counter") : 0U;
        // 决策日志与编号游标必须一一对应：每条记录都递增游标，因此
        // size == counter；不一致说明存档被篡改/损坏（宪法 17）。
        if (next.decision_log.size() != next.ai_decision_counter) {
            return WFS_SIM_RESULT_INVALID_DATA;
        }
        // decision_id 是 CHK052 回放标识，日志内必须唯一；重复按损坏拒绝。
        if (HasDuplicateDecisionIds(next.decision_log)) {
            return WFS_SIM_RESULT_INVALID_DATA;
        }
        // T029–T031：命令链路/运行期单位/烟幕随存档恢复；旧存档缺失时
        // 保持句柄已初始化的场景派生状态（非破坏性演进）。
        if (parsed.contains("units")) {
            next.units = parsed.at("units").get<std::vector<wfs::sim::RuntimeUnitState>>();
        }
        if (parsed.contains("command_chain")) {
            next.command_chain = parsed.at("command_chain").get<wfs::sim::CommandChain>();
        }
        if (parsed.contains("smoke")) {
            next.smoke_areas = parsed.at("smoke").get<std::vector<wfs::sim::SmokeArea>>();
        }
        if (parsed.contains("next_smoke_id")) {
            next.next_smoke_id = parsed.at("next_smoke_id").get<std::uint64_t>();
        }
        // T033/T036：情报记录/关键目标进度/胜负判定随存档恢复；旧存档缺失
        // 时保持句柄初始化产生的默认状态（非破坏性演进，宪法第 13 条）。
        if (parsed.contains("intel_records")) {
            next.intel_records = parsed.at("intel_records").get<std::map<std::string, wfs::sim::IntelRecord>>();
        }
        if (parsed.contains("objectives")) {
            next.objective_states = parsed.at("objectives").get<std::vector<wfs::sim::ObjectiveRuntimeState>>();
        }
        if (parsed.contains("outcome")) {
            next.outcome = parsed.at("outcome").get<wfs::sim::OutcomeState>();
        }
        // T057/T058：指挥组织与层级同步状态随存档恢复；旧存档缺失时保持
        // 句柄初始化产生的场景派生状态（非破坏性演进，宪法第 13 条）。
        if (parsed.contains("command_org")) {
            next.command_org = parsed.at("command_org").get<wfs::sim::CommandOrgState>();
        }
        if (parsed.contains("intel_sync_state")) {
            next.intel_sync_state = parsed.at("intel_sync_state").get<wfs::sim::IntelSyncState>();
        }
        if (parsed.contains("summaries")) {
            next.summaries = parsed.at("summaries").get<wfs::sim::SummaryRegistry>();
        }
        if (parsed.contains("mission_outcomes")) {
            next.mission_outcomes =
                parsed.at("mission_outcomes").get<std::map<std::string, wfs::sim::MissionOutcomeCounts>>();
        }
        if (parsed.contains("comm_state")) {
            next.comm_state = parsed.at("comm_state").get<wfs::sim::CommState>();
        }
        // T047–T050：支援/配属/战术编成随存档恢复；旧存档缺失字段时保持
        // 句柄初始化的场景派生状态（非破坏性演进，宪法第 13 条）。
        if (parsed.contains("support_chain")) {
            next.support_chain = parsed.at("support_chain").get<wfs::sim::SupportChain>();
        }
        if (parsed.contains("attach_registry")) {
            next.attach_registry = parsed.at("attach_registry").get<wfs::sim::AttachRegistry>();
        }
        if (parsed.contains("tactical_registry")) {
            next.tactical_registry = parsed.at("tactical_registry").get<wfs::sim::TacticalRegistry>();
        }
        if (parsed.contains("support_score_remaining")) {
            next.support_score_remaining = parsed.at("support_score_remaining").get<std::uint64_t>();
        }
        state = std::move(next);
        return WFS_SIM_RESULT_OK;
    } catch (const std::invalid_argument&) {
        return WFS_SIM_RESULT_INVALID_DATA;
    } catch (const std::out_of_range&) {
        return WFS_SIM_RESULT_INVALID_DATA;
    } catch (const nlohmann::json::exception&) {
        return WFS_SIM_RESULT_INVALID_DATA;
    } catch (const std::exception&) {
        return WFS_SIM_RESULT_INTERNAL_ERROR;
    }
}
// NOLINTEND(readability-function-cognitive-complexity)

}  // namespace wfs::sim
