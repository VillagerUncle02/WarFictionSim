// sim/src/c_api.cpp
//
// T012：C ABI 边界层实现。
//
// 实现策略：本文件是 C++ 核心与 C#/工具之间的唯一 P/Invoke 边界
// （contracts/sim-c-api.md，宪法第 14 条）。句柄内部组合已实现的确定性组件
// （loader/clock/rng/queue/command_validation），对外只暴露纯 C 函数与
// 错误码。所有 C++ 异常都在本层边界处被捕获并映射为 wfs_sim_result，
// 绝不跨语言边界抛出；create 失败显式返回 NULL（契约无错误码通道）。
// 快照为只读 JSON 纯数据文本，缓冲生命周期完全由调用方负责；
// 连续读取相同输入产生相同输出（宪法第 7 条）。
// get_state_hash 由 snapshot.cpp（T016）、save/load_save 由 save.cpp（T017）
// 实现；本文件只负责参数校验、错误码映射与缓冲生命周期。

#include "wfs/sim/c_api.h"

#include <cstring>
#include <new>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "ai_inject.h"
#include "sim_runtime.h"
#include "wfs/sim/clock.h"
#include "wfs/sim/loader.h"
#include "wfs/sim/rng.h"

#include "save.h"
#include "sim_state.h"
#include "state_serialization.h"

// 不透明句柄：C 侧只能持有指针，全部内容物为 SimState（sim_state.h，
// T016/T017 快照与存档模块共享的单一状态容器）；本类型仅作为 ABI 外壳。
struct wfs_sim_handle : wfs::sim::SimState {
    // 显式默认构造：SimState 含带默认参数 explicit 构造的成员（EventLog），
    // 聚合初始化不可用，统一走值初始化路径。
    wfs_sim_handle() = default;
};

namespace {

}  // namespace

extern "C" {

// 参数顺序是 contracts/sim-c-api.md 固定的 C ABI 契约
// （scenario_path, seed, threads），C# P/Invoke 按该顺序声明；
// seed/threads 类型相近但语义不同，契约不允许改名/换序。
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
wfs_sim_handle* wfs_sim_create(const char* scenario_path, std::uint64_t seed, int threads) {
    if (scenario_path == nullptr || threads < 1) {
        return nullptr;
    }
    try {
        const std::string path(scenario_path);
        const wfs::sim::ScenarioLoadResult load = wfs::sim::load_scenario(path);
        if (!load.ok()) {
            return nullptr;
        }

        wfs_sim_handle* handle = new (std::nothrow) wfs_sim_handle{};
        if (handle == nullptr) {
            return nullptr;
        }
        handle->scenario = load.scenario;
        handle->clock = wfs::sim::GameClock(handle->scenario.tick_hz);
        handle->rng = wfs::sim::Rng(seed, 0U);
        handle->seed = seed;
        handle->threads = threads;
        handle->scenario_path = path;
        return handle;
    } catch (...) {
        return nullptr;
    }
}

void wfs_sim_destroy(wfs_sim_handle* handle) {
    delete handle;  // NULL 为无操作（delete nullptr 是定义行为）。
}

const char* wfs_sim_version(void) {
    return WFS_SIM_VERSION_STRING;
}

wfs_sim_result wfs_sim_step(wfs_sim_handle* handle) {
    if (handle == nullptr) {
        return WFS_SIM_RESULT_INVALID_ARGUMENT;
    }
    try {
        wfs::sim::step_sim_state(*handle);
        return WFS_SIM_RESULT_OK;
    } catch (...) {
        return WFS_SIM_RESULT_INTERNAL_ERROR;
    }
}

wfs_sim_result wfs_sim_inject_command(wfs_sim_handle* handle, const char* command_json) {
    if (handle == nullptr || command_json == nullptr) {
        return WFS_SIM_RESULT_INVALID_ARGUMENT;
    }
    try {
        const std::filesystem::path schema_path =
            wfs::sim::resolve_schema_path(handle->scenario_path, "command.schema.json");
        if (schema_path.empty()) {
            return WFS_SIM_RESULT_INTERNAL_ERROR;
        }
        const wfs::sim::PlayerCommandResult result =
            wfs::sim::inject_player_command(*handle, command_json, schema_path);
        return result.accepted ? WFS_SIM_RESULT_OK : WFS_SIM_RESULT_INVALID_DATA;
    } catch (...) {
        return WFS_SIM_RESULT_INTERNAL_ERROR;
    }
}

wfs_sim_result wfs_sim_inject_ai_decision(wfs_sim_handle* handle, const char* decision_json) {
    if (handle == nullptr || decision_json == nullptr) {
        return WFS_SIM_RESULT_INVALID_ARGUMENT;
    }
    try {
        const std::filesystem::path schema_path =
            wfs::sim::resolve_schema_path(handle->scenario_path, "command.schema.json");
        if (schema_path.empty()) {
            return WFS_SIM_RESULT_INTERNAL_ERROR;
        }
        // C ABI 无节点元数据通道：沿用玩家节点校验上下文（向后兼容）；
        // 带节点信息的注入（脚本/云端驱动）走内部 inject_ai_decision。
        const wfs::sim::AiInjectResult result =
            wfs::sim::inject_ai_decision(*handle, decision_json, wfs::sim::AiDecisionMeta{}, schema_path);
        return result.accepted ? WFS_SIM_RESULT_OK : WFS_SIM_RESULT_INVALID_DATA;
    } catch (...) {
        return WFS_SIM_RESULT_INTERNAL_ERROR;
    }
}

wfs_sim_result wfs_sim_get_snapshot(wfs_sim_handle* handle, char* out_buf, std::size_t buf_size, std::size_t* out_len) {
    if (handle == nullptr || out_buf == nullptr || out_len == nullptr) {
        return WFS_SIM_RESULT_INVALID_ARGUMENT;
    }
    try {
        // 快照构建与确定性保证见 snapshot.cpp（键序/字段由
        // serialize_state_json 同一规范约束，宪法第 7 条）。
        const std::string text = wfs::sim::build_snapshot_text(*handle);
        const std::size_t required = text.size() + 1U;  // 含 NUL 终止符
        if (buf_size < required) {
            *out_len = required;
            return WFS_SIM_RESULT_BUFFER_TOO_SMALL;
        }
        std::memcpy(out_buf, text.data(), text.size());
        out_buf[text.size()] = '\0';
        *out_len = text.size();
        return WFS_SIM_RESULT_OK;
    } catch (...) {
        return WFS_SIM_RESULT_INTERNAL_ERROR;
    }
}

// out_hex 是输出缓冲，T016 将向其中写入 SHA-256 十六进制文本，
// 非 const 是语义要求。
// NOLINTNEXTLINE(readability-non-const-parameter)
wfs_sim_result wfs_sim_get_state_hash(wfs_sim_handle* handle, char out_hex[WFS_SIM_STATE_HASH_HEX_LEN]) {
    if (handle == nullptr || out_hex == nullptr) {
        return WFS_SIM_RESULT_INVALID_ARGUMENT;
    }
    try {
        // 状态哈希 = SHA-256(权威状态 JSON)；threads 不在序列化范围内，
        // 因此线程数不影响哈希（宪法第 7 条，T018 联动验收）。
        const std::string hex = wfs::sim::compute_state_hash_hex(*handle);
        std::memcpy(out_hex, hex.data(), hex.size());
        out_hex[hex.size()] = '\0';
        return WFS_SIM_RESULT_OK;
    } catch (...) {
        return WFS_SIM_RESULT_INTERNAL_ERROR;
    }
}

wfs_sim_result wfs_sim_save(wfs_sim_handle* handle, const char* path) {
    if (handle == nullptr || path == nullptr) {
        return WFS_SIM_RESULT_INVALID_ARGUMENT;
    }
    return wfs::sim::save_to_file(*handle, path);
}

wfs_sim_result wfs_sim_load_save(wfs_sim_handle* handle, const char* path) {
    if (handle == nullptr || path == nullptr) {
        return WFS_SIM_RESULT_INVALID_ARGUMENT;
    }
    return wfs::sim::load_save_into(*handle, path);
}

}  // extern "C"
