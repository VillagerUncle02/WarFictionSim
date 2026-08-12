// sim/src/snapshot.cpp
//
// T016：状态快照与状态哈希实现。
//
// 实现策略：
// - build_snapshot_json 输出只读快照（c_api wfs_sim_get_snapshot 复用），
//   字段与 T012 保持一致并新增事件日志摘要（data-model.md §17）。
// - serialize_state_json 是唯一权威状态序列化：状态哈希与存档 state_blob
//   共用它，保证 wfs_sim_get_state_hash 与 WFS-SAVE 内嵌 state_hash 一致。
//   序列化显式排除 threads（宪法第 7 条：线程数不影响状态哈希）。
// - 队列只读快照：EventQueue 不暴露遍历接口（T011 保持最小 API），这里
//   通过复制后按 (tick, seq) 顺序弹出，复制不影响原队列且顺序确定。

#include "state_serialization.h"

#include <cstddef>
#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include "wfs/sim/c_api.h"
#include "wfs/sim/event_log.h"
#include "wfs/sim/queue.h"
#include "wfs/sim/sha256.h"

namespace wfs::sim {

namespace {

// 队列按 (tick, seq) 确定性顺序转 JSON（复制后弹出，不改变原队列）。
nlohmann::json QueueToJson(const EventQueue& queue) {
    EventQueue copy = queue;
    nlohmann::json events = nlohmann::json::array();
    while (!copy.empty()) {
        const QueuedEvent& event = copy.front();
        events.push_back(nlohmann::json{
            {"tick", event.tick},
            {"seq", event.seq},
            {"payload", event.payload},
        });
        copy.pop();
    }
    return events;
}

nlohmann::json EventLogToJson(const EventLog& log) {
    nlohmann::json entries = nlohmann::json::array();
    for (const SimEvent& event : log.events()) {
        entries.push_back(nlohmann::json{
            {"seq", event.seq},
            {"tick", event.tick},
            {"category", std::string(to_string(event.category))},
            {"severity", std::string(to_string(event.severity))},
            {"message", event.message},
        });
    }
    return entries;
}

}  // namespace

nlohmann::json build_snapshot_json(const SimState& state) {
    return nlohmann::json{
        {"abi_version", WFS_SIM_VERSION_STRING},
        {"tick", state.clock.tick()},
        {"total_us", state.clock.total_us()},
        {"seed", state.seed},
        {"threads", state.threads},
        {"scenario_id", state.scenario.id},
        {"scenario_name", state.scenario.name},
        {"player_node_id", state.scenario.player_node_id},
        {"pending_events", state.queue.size()},
        {"processed_events", state.processed_events},
        {"event_log",
         nlohmann::json{
             {"size", state.event_log.size()},
             {"capacity", state.event_log.capacity()},
             {"critical_count", state.event_log.critical_count()},
         }},
        // T029–T031：命令链路与单位运行期状态摘要（快照只读消费）。
        {"command_chain",
         nlohmann::json{
             {"commands", state.command_chain.size()},
         }},
        {"units", state.units},
        // T033/T036：情报与胜负/目标进度摘要（快照只读消费）。
        {"intel_records", state.intel_records},
        {"objectives", state.objective_states},
        {"outcome", state.outcome},
    };
}

std::string build_snapshot_text(const SimState& state) {
    return build_snapshot_json(state).dump();
}

nlohmann::json serialize_state_json(const SimState& state) {
    nlohmann::json root{
        {"tick", state.clock.tick()},
        {"seed", state.seed},
        {"scenario_id", state.scenario.id},
        {"rng",
         nlohmann::json{
             {"state", state.rng.state().state},
             {"stream", state.rng.state().stream},
         }},
        {"queue",
         nlohmann::json{
             // next_seq 是队列状态的一部分（T011 全局单调不回收游标）：
             // 存档恢复后 auto 序列号必须与原始运行一致，因此进入哈希。
             {"next_seq", state.queue.next_seq()},
             {"events", QueueToJson(state.queue)},
         }},
        {"processed_events", state.processed_events},
        {"event_log",
         nlohmann::json{
             {"capacity", state.event_log.capacity()},
             {"entries", EventLogToJson(state.event_log)},
         }},
    };
    // T020：AI 决策日志与编号游标是确定性状态的一部分（CHK052 回放依据）。
    // 空值省略字段：旧版存档（无决策日志）加载后再次序列化保持字节一致。
    if (state.ai_decision_counter > 0U) {
        root["ai_decision_counter"] = state.ai_decision_counter;
    }
    if (!state.decision_log.empty()) {
        root["decision_log"] = decision_log_to_json(state.decision_log);
    }
    // T029–T031：命令链路/运行期单位/烟幕是确定性状态的组成部分。
    root["units"] = state.units;
    root["command_chain"] = state.command_chain;
    root["smoke"] = state.smoke_areas;
    root["next_smoke_id"] = state.next_smoke_id;
    // T033/T036：情报记录/关键目标进度/胜负判定是确定性状态的组成部分。
    root["intel_records"] = state.intel_records;
    root["objectives"] = state.objective_states;
    root["outcome"] = state.outcome;
    return root;
}

std::string compute_state_hash_hex(const SimState& state) {
    return sha256_hex(serialize_state_json(state).dump());
}

}  // namespace wfs::sim
