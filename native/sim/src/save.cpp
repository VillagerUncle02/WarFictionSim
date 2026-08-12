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
//   任何失败都返回错误码且不改写句柄（强保证；宪法第 17 条禁止静默恢复）。
// - 写入用"临时文件 + 替换"避免半写存档；失败时清理临时文件。
// - 迁移链：version < 当前版本时经 migrate_state 逐级迁移；v1 为第一版，
//   无历史迁移，v0/未来版本显式报错（宪法第 13 条）。

#include "save.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

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

// 临时文件 + 替换：写入中断不会留下半写存档；目标已存在时先移除再重命名。
wfs_sim_result WriteFileAtomic(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::path temp = path;
    temp += ".tmp";
    {
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
    }
    std::error_code error;
    if (std::filesystem::exists(path)) {
        std::filesystem::remove(path, error);
        if (error) {
            std::error_code cleanup_error;
            std::filesystem::remove(temp, cleanup_error);
            return WFS_SIM_RESULT_IO_ERROR;
        }
    }
    std::filesystem::rename(temp, path, error);
    if (error) {
        std::error_code cleanup_error;
        std::filesystem::remove(temp, cleanup_error);
        return WFS_SIM_RESULT_IO_ERROR;
    }
    return WFS_SIM_RESULT_OK;
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
    EventQueue queue;
    for (const nlohmann::json& event : root.at("queue").at("events")) {
        queue.enqueue(RequireField<GameTick>(event, "tick"), RequireField<std::uint64_t>(event, "seq"),
                      RequireField<std::string>(event, "payload"));
    }
    return queue;
}

EventLog RestoreEventLog(const nlohmann::json& root) {
    const nlohmann::json& log = root.at("event_log");
    EventLog restored(RequireField<std::size_t>(log, "capacity"));
    for (const nlohmann::json& entry : log.at("entries")) {
        restored.append(RequireField<GameTick>(entry, "tick"),
                        event_category_from_string(RequireField<std::string>(entry, "category")),
                        event_severity_from_string(RequireField<std::string>(entry, "severity")),
                        RequireField<std::string>(entry, "message"), RequireField<std::uint64_t>(entry, "seq"));
    }
    return restored;
}

}  // namespace

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

    // 迁移链骨架：索引 = 源版本，函数把状态从 v 迁移到 v+1。v1 是第一版
    // 格式，表为空；未来破坏性变更按版本号追加步骤并递增
    // kCurrentSaveFormatVersion（宪法第 13 条：逐级迁移，禁止跳级）。
    static const std::vector<std::function<nlohmann::json(nlohmann::json)>> migration_steps;
    nlohmann::json current = state;
    for (std::uint32_t version = from_version; version < to_version; ++version) {
        current = migration_steps.at(version)(std::move(current));
    }
    return current;
}

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
        if (blob_offset + blob_length + kSaveStateHashSize > data.size()) {
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

}  // namespace wfs::sim
