// sim/src/ai_inject.h
//
// T020 内部共享：AI 决策注入通道接口。
//
// 本头文件供 C ABI（c_api.cpp）与无头驱动（headless_driver.cpp）复用同一
// 注入路径：T014 双重校验 → T011 队列入队 → 决策点记录（宪法 8/9/10）。
//
// 线程安全：本通道与 wfs_sim_step 一样非线程安全；注入/step 必须由调用方
// 串行化（queue.h 契约同步；T080 云端异步接入前必须加锁）。
//
// 频率上限预留（FR-052）：按节点限频的强制位置在本通道——T080 触发引擎将
// 基于 AiDecisionMeta / 决策日志实现 interval；本阶段注入只做校验 + 入队，
// 不改变既有确定性契约。

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "sim_state.h"
#include "wfs/sim/command_validation.h"

namespace wfs::sim {

// 注入元数据：决策标识/所属节点/触发原因；decision_id 为空时自动分配。
struct AiDecisionMeta {
    std::string decision_id;
    std::string node_id;  // 非空 = 按该节点做越权校验；空 = 沿用玩家节点上下文。
    std::string trigger;
};

struct AiInjectResult {
    bool accepted = false;
    std::vector<ValidationError> errors;  // 校验错误（确定性顺序，T014）。
    GameTick arrival_tick = 0U;
    std::uint64_t arrival_seq = 0U;  // 0 = 拒绝未入队。
    std::string decision_id;
};

// 注入 AI 决策：经 T014 校验后按当前 tick 入队（异步到达语义：不推进 tick、
// 不等待后端）；无论接受/拒绝都记录决策点（宪法 10），拒绝不改变队列。
AiInjectResult inject_ai_decision(SimState& state, const std::string& command_json, const AiDecisionMeta& meta,
                                  const std::filesystem::path& schema_path);

// 决策输入摘要：确定性 JSON 纯数据（不含线程数，宪法 7：线程数不影响结果）。
std::string build_ai_input_summary(const SimState& state);

// 决策点可见事件：最近 limit 条（seq 升序）的 JSON 数组。
std::string build_ai_events_json(const SimState& state, std::size_t limit = 20U);

}  // namespace wfs::sim
