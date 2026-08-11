// sim/src/ai_inject.cpp
//
// T020：AI 决策注入通道实现。
//
// 实现策略（宪法 8/9/10；CHK052 回放）：
// - 注入是纯同步的校验 + 入队：不调用任何后端、不推进 tick、不等待 AI，
//   模拟主循环（wfs_sim_step）随后按 (tick, seq) 确定性处理（FR-026/SC-002）。
// - 校验复用 T014 双重管道；meta.node_id 非空时按该节点做越权过滤，空时
//   沿用玩家节点上下文（C ABI 向后兼容），保证 AI 只能命令自己节点内的单位。
// - 每个决策点（含拒绝）记录：注入前状态哈希/输入摘要/可见事件/AI 返回/
//   校验结果（宪法 10）；到达 tick + 队列序列号进记录，构成 CHK052 回放依据。
// - 事件日志同步追加 AI_DECISION_ACCEPTED / AI_DECISION_REJECTED（宪法 17：
//   拒绝不静默），且顺序固定（先队列、后事件、再决策日志），确定性成立。

#include "ai_inject.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "state_serialization.h"
#include "wfs/sim/command_validation.h"
#include "wfs/sim/event_log.h"
#include "wfs/sim/queue.h"

namespace wfs::sim {

namespace {

// 决策点输入摘要：确定性 JSON（键序由 nlohmann 字典序保证），显式排除
// threads/scenario_path（宪法 7：线程数与调度不参与结果）。
nlohmann::json BuildAiInputSummaryJson(const SimState& state) {
    nlohmann::json units = nlohmann::json::array();
    for (const ScenarioUnit& unit : state.scenario.units) {
        units.push_back(nlohmann::json{{"id", unit.id},
                                       {"type", unit.type},
                                       {"node_id", unit.node_id},
                                       {"x", unit.x},
                                       {"y", unit.y},
                                       {"ammo", unit.ammo}});
    }
    return nlohmann::json{
        {"tick", state.clock.tick()},
        {"seed", state.seed},
        {"scenario_id", state.scenario.id},
        {"zones", state.scenario.zones},
        {"units", std::move(units)},
        {"pending_events", state.queue.size()},
        {"processed_events", state.processed_events},
        {"event_log",
         nlohmann::json{
             {"size", state.event_log.size()},
             {"capacity", state.event_log.capacity()},
             {"critical_count", state.event_log.critical_count()},
         }},
        {"decision_log_size", state.decision_log.size()},
    };
}

}  // namespace

std::string build_ai_input_summary(const SimState& state) {
    return BuildAiInputSummaryJson(state).dump();
}

std::string build_ai_events_json(const SimState& state, std::size_t limit) {
    const std::vector<SimEvent> events = state.event_log.events();
    nlohmann::json output = nlohmann::json::array();
    const std::size_t start = events.size() > limit ? events.size() - limit : 0U;
    for (std::size_t i = start; i < events.size(); ++i) {
        const SimEvent& event = events[i];
        output.push_back(nlohmann::json{{"seq", event.seq},
                                        {"tick", event.tick},
                                        {"category", std::string(to_string(event.category))},
                                        {"severity", std::string(to_string(event.severity))},
                                        {"message", event.message}});
    }
    return output.dump();
}

AiInjectResult inject_ai_decision(SimState& state, const std::string& command_json, const AiDecisionMeta& meta,
                                  const std::filesystem::path& schema_path) {
    AiInjectResult result;
    result.decision_id =
        meta.decision_id.empty() ? "ai-" + std::to_string(state.ai_decision_counter) : meta.decision_id;

    // 显式 decision_id 必须全局唯一（CHK052 回放标识）：重复拒绝且不追加
    // 记录/不递增游标，保持决策日志唯一（宪法 10/17）。
    if (!meta.decision_id.empty()) {
        for (const AiDecisionRecord& record : state.decision_log.entries()) {
            if (record.decision_id == result.decision_id) {
                result.errors.push_back(
                    ValidationError{"DUPLICATE_DECISION_ID", "决策标识已存在: " + result.decision_id});
                state.event_log.append(
                    state.clock.tick(), EventCategory::kCommand, EventSeverity::kWarning,
                    "AI_DECISION_REJECTED decision_id=" + result.decision_id + " code=DUPLICATE_DECISION_ID");
                return result;
            }
        }
    }

    // 决策点快照：全部在注入前采集（宪法 10：记录"决策点"而非"入队后"）。
    const std::string state_hash = compute_state_hash_hex(state);
    const std::string input_json = build_ai_input_summary(state);
    const std::string events_json = build_ai_events_json(state);

    CommandValidationContext context = make_validation_context(state.scenario);
    if (!meta.node_id.empty()) {
        // 按决策节点做越权校验：AI 只能命令本节点指挥范围内的单位。
        context.commander_node_id = meta.node_id;
    }
    const CommandValidationResult validation = validate_command(command_json, context, schema_path);
    result.errors = validation.errors;

    const GameTick arrival_tick = state.clock.tick();
    if (validation.ok()) {
        result.arrival_tick = arrival_tick;
        result.arrival_seq = state.queue.enqueue(arrival_tick, command_json);
        result.accepted = true;
        state.event_log.append(arrival_tick, EventCategory::kCommand, EventSeverity::kInfo,
                               "AI_DECISION_ACCEPTED decision_id=" + result.decision_id + " tick=" +
                                   std::to_string(arrival_tick) + " seq=" + std::to_string(result.arrival_seq));
    } else {
        const std::string code = validation.errors.empty() ? "UNKNOWN" : validation.errors.front().code;
        state.event_log.append(arrival_tick, EventCategory::kCommand, EventSeverity::kWarning,
                               "AI_DECISION_REJECTED decision_id=" + result.decision_id + " code=" + code);
    }

    AiDecisionRecord record;
    record.decision_id = result.decision_id;
    record.node_id = meta.node_id;
    record.trigger = meta.trigger;
    record.arrival_tick = arrival_tick;
    record.arrival_seq = result.arrival_seq;
    record.state_hash = state_hash;
    record.input_json = input_json;
    record.events_json = events_json;
    record.output_json = command_json;
    record.validation_ok = validation.ok();
    record.validation_errors = validation.errors;
    state.decision_log.append(std::move(record));
    ++state.ai_decision_counter;
    return result;
}

}  // namespace wfs::sim
