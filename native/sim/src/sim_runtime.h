// sim/src/sim_runtime.h
//
// T019/T020 内部共享：模拟推进与玩家命令注入接口。
//
// C ABI（c_api.cpp）与无头驱动（headless_driver.cpp）复用同一确定性推进/
// 注入路径，避免"无头 CLI 与库内行为分叉"（宪法 15/7：跨运行形态一致）。

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "sim_state.h"
#include "wfs/sim/command_validation.h"

namespace wfs::sim {

struct PlayerCommandResult {
    bool accepted = false;
    std::vector<ValidationError> errors;  // 校验错误（确定性顺序，T014）。
    GameTick arrival_tick = 0U;
    std::uint64_t arrival_seq = 0U;
};

// T029–T031：从场景/数据目录初始化运行期单位、配置与地形库。
// 必须在 scenario/scenario_path 赋值后调用；失败不抛异常（缺失数据目录时
// 按无武器单位初始化，宪法 12 的数据校验由加载器保证）。
void initialize_runtime_state(SimState& state);

// 推进一个离散 tick：时钟 +1，按 (tick, seq) 处理到期队列事件，并记录
// COMMAND_PROCESSED 事件；随后依次执行命令链路（T029）、机动（T030）与
// 战斗（T031）结算（确定性；补发语义由 EventQueue::try_pop 保证）。
void step_sim_state(SimState& state);

// 玩家命令注入：T014 校验后按当前 tick 入队；成功后记录 COMMAND_QUEUED
// 事件，失败不改变队列状态（宪法 17）。
PlayerCommandResult inject_player_command(SimState& state, const std::string& command_json,
                                          const std::filesystem::path& schema_path);

}  // namespace wfs::sim
