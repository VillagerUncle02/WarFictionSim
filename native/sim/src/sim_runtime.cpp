// sim/src/sim_runtime.cpp
//
// T019/T020：模拟推进与玩家命令注入实现。
//
// 实现策略：
// - step_sim_state 与 c_api.cpp 原 wfs_sim_step 语义一致（时钟推进 + 队列
//   补发弹出），并新增 COMMAND_PROCESSED 事件，使无头 JSONL 输出可观察
//   命令链路（宪法 15/17）。
// - inject_player_command 与原 InjectCommand 语义一致（T014 → T011 入队），
//   新增 COMMAND_QUEUED 事件；校验失败不改变队列（宪法 17）。

#include "sim_runtime.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "wfs/sim/command_validation.h"
#include "wfs/sim/event_log.h"
#include "wfs/sim/queue.h"

namespace wfs::sim {

void step_sim_state(SimState& state) {
    state.clock.advance(1U);
    QueuedEvent event;
    while (state.queue.try_pop(state.clock.tick(), event)) {
        ++state.processed_events;
        state.event_log.append(state.clock.tick(), EventCategory::kCommand, EventSeverity::kInfo,
                               "COMMAND_PROCESSED seq=" + std::to_string(event.seq) +
                                   " tick=" + std::to_string(state.clock.tick()));
    }
}

PlayerCommandResult inject_player_command(SimState& state, const std::string& command_json,
                                          const std::filesystem::path& schema_path) {
    PlayerCommandResult result;
    const CommandValidationContext context = make_validation_context(state.scenario);
    const CommandValidationResult validation = validate_command(command_json, context, schema_path);
    result.errors = validation.errors;
    if (!validation.ok()) {
        return result;
    }
    result.arrival_tick = state.clock.tick();
    result.arrival_seq = state.queue.enqueue(result.arrival_tick, command_json);
    result.accepted = true;
    state.event_log.append(result.arrival_tick, EventCategory::kCommand, EventSeverity::kInfo,
                           "COMMAND_QUEUED seq=" + std::to_string(result.arrival_seq) +
                               " tick=" + std::to_string(result.arrival_tick));
    return result;
}

}  // namespace wfs::sim
