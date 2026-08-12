// sim/src/sim_state.h
//
// 内部共享：模拟核心可变状态容器（T016/T017 快照与存档模块的单一事实来源）。
//
// 设计说明：C ABI 句柄（c_api.cpp）继承 SimState 作为其全部内容物，快照/
// 存档模块只依赖本结构，不依赖 C ABI 边界类型，从而保持依赖方向
// （边界层 → 核心模块，宪法第 14 条）。threads 与 scenario_path 是运行期
// 配置而非游戏状态：threads 不参与状态哈希（宪法第 7 条），scenario_path
// 仅用于 Schema 路径解析。

#pragma once

#include <cstdint>
#include <filesystem>

#include "wfs/sim/ai/decision_log.h"
#include "wfs/sim/clock.h"
#include "wfs/sim/event_log.h"
#include "wfs/sim/loader.h"
#include "wfs/sim/queue.h"
#include "wfs/sim/rng.h"

namespace wfs::sim {

struct SimState {
    Scenario scenario;        // 场景数据（T013 加载结果，含 raw JSON）。
    GameClock clock;          // 离散 tick 游戏时钟（T010）。
    Rng rng;                  // 统一确定性 RNG（T009）。
    EventQueue queue;         // 事件/命令确定性队列（T011）。
    EventLog event_log;       // 事件日志（T015）。
    std::uint64_t seed = 0U;  // 创建句柄时的显式种子（运行身份标识）。
    int threads = 1;          // 并行度配置（只影响性能，不进哈希/存档状态）。
    std::uint64_t processed_events = 0U;
    DecisionLog decision_log;                  // T020：AI 决策点记录（宪法 10）。
    std::uint64_t ai_decision_counter = 0U;    // T020：决策编号单调游标（接受/拒绝共用）。
    std::filesystem::path scenario_path;
};

}  // namespace wfs::sim
