// sim/include/wfs/sim/ai/ia_backend.h
//
// T020：AI 后端抽象公开接口。
//
// 设计契约（spec FR-068/069；宪法第 8/9/10 条；research.md §6）：
// - AI 后端是"决策生成器"而非"状态修改器"：decide() 只返回与玩家命令同构的
//   命令 JSON，由注入通道（ai_inject.cpp）经 T014 双重校验后进入确定性命令
//   队列；后端永远不能直接读写模拟状态（宪法第 9 条）。
// - 引擎不持有后端实例、不调用 decide()：决策以异步到达语义通过
//   wfs_sim_inject_ai_decision / inject_ai_decision 注入，模拟主循环不等待
//   AI（宪法第 8 条/FR-026/SC-002）。
// - 决策输入是确定性纯数据（状态快照摘要 + 可见事件，均不含线程数），
//   供决策点记录与回放（宪法第 10 条/CHK052：SC-001 的 AI 输出序列含到达时序）。
// - 后端标识为稳定字符串：script / cloud / none，与无头 CLI --ai-backend
//   取值一致（T019）；云端后端由 T080 实现，本文件只定义抽象。

#pragma once

#include <cstdint>
#include <string>

#include "wfs/sim/clock.h"

namespace wfs::sim {

// 稳定后端标识（CLI --ai-backend 取值同源）。
inline constexpr const char* kAiBackendScript = "script";
inline constexpr const char* kAiBackendCloud = "cloud";
inline constexpr const char* kAiBackendNone = "none";

// 一次 AI 决策的输入：确定性纯数据（宪法第 10 条：决策点记录输入）。
struct AiDecisionInput {
    std::string node_id;           // 请求决策的指挥节点（每节点单一 AI 归属）。
    std::string trigger;           // 触发原因（事件名，如 run_start）。
    GameTick game_tick = 0U;       // 决策请求时的游戏 tick（不读现实时钟）。
    std::string state_summary_json;  // 状态快照摘要（确定性 JSON，不含线程数）。
    std::string events_json;       // 决策点可见事件（JSON 数组，seq 升序）。
};

// 一次 AI 决策的输出：只允许是命令 JSON（或明确错误）。
struct AiDecision {
    bool ok() const noexcept { return error.empty(); }

    std::string command_json;  // 与玩家命令同构的命令 JSON（FR-045/069）。
    std::string error;         // 非空 = 生成失败原因（调用方记录并降级，不注入）。
};

// AI 后端接口：云端（T080）/ 脚本（T021）/ 未来本地模型共用同一抽象。
class IAiBackend {
   public:
    virtual ~IAiBackend() = default;

    // 稳定后端标识（kAiBackendScript / kAiBackendCloud / kAiBackendNone）。
    virtual std::string name() const = 0;

    // 由确定性输入生成命令 JSON；实现必须纯函数化（同一输入同一输出，
    // 不得调用模拟 RNG、不得读取现实时钟、不得依赖线程数）。
    virtual AiDecision decide(const AiDecisionInput& input) = 0;
};

}  // namespace wfs::sim
