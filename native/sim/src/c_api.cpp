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
// get_state_hash/save/load_save 属 T016/T017，本任务显式返回
// NOT_IMPLEMENTED，禁止静默吞错（宪法第 17 条）。

#include "wfs/sim/c_api.h"

#include <cstring>
#include <new>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "wfs/sim/clock.h"
#include "wfs/sim/command_validation.h"
#include "wfs/sim/loader.h"
#include "wfs/sim/queue.h"
#include "wfs/sim/rng.h"

// 不透明句柄：C 侧只能持有指针，所有成员仅在本翻译单元可见。
struct wfs_sim_handle {
    wfs::sim::Scenario scenario;
    wfs::sim::GameClock clock;
    wfs::sim::Rng rng;
    wfs::sim::EventQueue queue;
    std::uint64_t seed = 0U;
    int threads = 1;
    std::uint64_t processed_events = 0U;
    std::filesystem::path scenario_path;
};

namespace {

// 玩家命令与 AI 决策共用同一校验与入队路径（FR-045/069：AI 只能生成命令，
// 与玩家共用同一结构与队列）。校验失败不改变队列状态。
wfs_sim_result InjectCommand(wfs_sim_handle* handle, const char* command_json) {
    if (handle == nullptr || command_json == nullptr) {
        return WFS_SIM_RESULT_INVALID_ARGUMENT;
    }
    try {
        const std::filesystem::path schema_path =
            wfs::sim::resolve_schema_path(handle->scenario_path, "command.schema.json");
        if (schema_path.empty()) {
            return WFS_SIM_RESULT_INTERNAL_ERROR;
        }
        const wfs::sim::CommandValidationContext context = wfs::sim::make_validation_context(handle->scenario);
        const wfs::sim::CommandValidationResult validation =
            wfs::sim::validate_command(command_json, context, schema_path);
        if (!validation.ok()) {
            return WFS_SIM_RESULT_INVALID_DATA;
        }
        // 到达 tick = 当前 tick；序列号由队列自动分配（T011），
        // 与 AI 注入通道（T020）共用同一确定性排序。
        handle->queue.enqueue(handle->clock.tick(), command_json);
        return WFS_SIM_RESULT_OK;
    } catch (...) {
        return WFS_SIM_RESULT_INTERNAL_ERROR;
    }
}

}  // namespace

extern "C" {

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
        handle->clock.advance(1U);
        // 按 (tick, seq) 顺序处理到期命令：事件 tick 早于当前 tick 时按
        // 补发语义弹出（T011 try_pop 契约），保证注入在暂停/加速边界不丢命令。
        wfs::sim::QueuedEvent event;
        while (handle->queue.try_pop(handle->clock.tick(), event)) {
            ++handle->processed_events;
        }
        return WFS_SIM_RESULT_OK;
    } catch (...) {
        return WFS_SIM_RESULT_INTERNAL_ERROR;
    }
}

wfs_sim_result wfs_sim_inject_command(wfs_sim_handle* handle, const char* command_json) {
    return InjectCommand(handle, command_json);
}

wfs_sim_result wfs_sim_inject_ai_decision(wfs_sim_handle* handle, const char* decision_json) {
    return InjectCommand(handle, decision_json);
}

wfs_sim_result wfs_sim_get_snapshot(wfs_sim_handle* handle, char* out_buf, std::size_t buf_size, std::size_t* out_len) {
    if (handle == nullptr || out_buf == nullptr || out_len == nullptr) {
        return WFS_SIM_RESULT_INVALID_ARGUMENT;
    }
    try {
        const nlohmann::json snapshot = {
            {"abi_version", WFS_SIM_VERSION_STRING},
            {"tick", handle->clock.tick()},
            {"total_us", handle->clock.total_us()},
            {"seed", handle->seed},
            {"threads", handle->threads},
            {"scenario_id", handle->scenario.id},
            {"scenario_name", handle->scenario.name},
            {"player_node_id", handle->scenario.player_node_id},
            {"pending_events", handle->queue.size()},
            {"processed_events", handle->processed_events},
        };
        // dump() 默认紧凑输出且对象键经 std::map 排序，跨调用/跨进程确定。
        const std::string text = snapshot.dump();
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

wfs_sim_result wfs_sim_get_state_hash(wfs_sim_handle* handle, char out_hex[65]) {
    if (handle == nullptr || out_hex == nullptr) {
        return WFS_SIM_RESULT_INVALID_ARGUMENT;
    }
    // T016 实现状态快照与 SHA-256；显式报错，不静默返回假哈希。
    return WFS_SIM_RESULT_NOT_IMPLEMENTED;
}

wfs_sim_result wfs_sim_save(wfs_sim_handle* handle, const char* path) {
    if (handle == nullptr || path == nullptr) {
        return WFS_SIM_RESULT_INVALID_ARGUMENT;
    }
    // T017 实现 WFS-SAVE 存档格式；显式报错。
    return WFS_SIM_RESULT_NOT_IMPLEMENTED;
}

wfs_sim_result wfs_sim_load_save(wfs_sim_handle* handle, const char* path) {
    if (handle == nullptr || path == nullptr) {
        return WFS_SIM_RESULT_INVALID_ARGUMENT;
    }
    // T017 实现存档加载与迁移链；显式报错。
    return WFS_SIM_RESULT_NOT_IMPLEMENTED;
}

}  // extern "C"
