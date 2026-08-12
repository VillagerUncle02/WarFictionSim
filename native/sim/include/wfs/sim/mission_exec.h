// sim/include/wfs/sim/mission_exec.h
//
// T034：任务判定与事件上报公开接口。
//
// 设计契约（FR-040/043/044/045；宪法第 7/9 条）：
// - 任务完成/失败判定全部由确定性代码负责，AI 不参与（宪法第 7/9 条）；
//   判定只依赖单位状态、任务参数与场景数据，不读现实时钟。
// - 超时处置（FR-044）：超时按 MissionExecConfig.timeout_resolution 处置
//   （fail/cancel/continue），失败后按单位行为参数执行 撤退/坚守/上报。
// - 持续任务（FR-044）：周期完成 → 循环重启（MISSION_LOOP_RESTARTED），
//   直到被新命令取代或 loops 关闭。
// - 任务状态变化（完成/失败/超时）以 interaction=EXECUTION 上报上级节点。

#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace wfs::sim {

struct SimState;          // 内部运行时状态（sim_state.h）。
struct RuntimeUnitState;  // 内部运行时单位（sim_state.h）。

// 任务判定配置（FR-043/044；场景 raw["mission"] 可覆盖）。
struct MissionExecConfig {
    double failure_loss_ratio = 0.5;               // 兵力损失失败阈值。
    std::uint64_t patrol_cycle_ticks = 3600U;      // 巡逻周期时长基线。
    std::uint64_t fortify_ticks = 1800U;           // 构筑工事时长基线。
    std::string timeout_resolution = "fail";       // "fail" | "cancel" | "continue"。
    std::uint64_t timeout_extension_ticks = 600U;  // continue 时的顺延时长。

    static MissionExecConfig FromScenario(const nlohmann::json& raw);

    bool is_valid() const noexcept {
        return failure_loss_ratio >= 0.0 && failure_loss_ratio <= 1.0 && patrol_cycle_ticks > 0U &&
               fortify_ticks > 0U &&
               (timeout_resolution == "fail" || timeout_resolution == "cancel" || timeout_resolution == "continue") &&
               timeout_extension_ticks > 0U;
    }
};

// 单次确定性判定结果（完成/失败互斥；reason 为稳定事件原因）。
struct MissionEvaluation {
    bool completed = false;
    bool failed = false;
    std::string reason;
};

// 判定一个单位当前任务的完成/失败条件（纯确定性；会累计驻留进度）。
MissionEvaluation evaluate_mission(SimState& state, RuntimeUnitState& unit);

// 推进一 tick 的任务判定（侦察类任务判定委托 T035 recon_tasks）。
void step_missions(SimState& state);

// 完成任务：MISSION_COMPLETED + 上报 + 持续任务循环/终止（T035 复用）。
void complete_mission(SimState& state, RuntimeUnitState& unit, const std::string& reason);
// 判定失败：MISSION_FAILED + 上报 + 失败后处置 + 终止（T035 复用）。
void fail_mission(SimState& state, RuntimeUnitState& unit, const std::string& reason);

// 失败后处置（FR-044）：withdraw_to/hold/report，按单位行为参数执行。
void apply_failure_action(SimState& state, RuntimeUnitState& unit);

// 任务状态事件上报上级（FR-050：EXECUTION）。
void report_mission_status(SimState& state, const RuntimeUnitState& unit, const std::string& status,
                           const std::string& reason);

// 超时处置入口（command_chain 委托；按配置 fail/cancel/continue）。
void handle_mission_timeout(SimState& state, const std::string& command_id);

}  // namespace wfs::sim
