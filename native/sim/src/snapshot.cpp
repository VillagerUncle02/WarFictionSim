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
    };
}

std::string build_snapshot_text(const SimState& state) {
    return build_snapshot_json(state).dump();
}

nlohmann::json serialize_state_json(const SimState& state) {
    return nlohmann::json{
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
             {"events", QueueToJson(state.queue)},
         }},
        {"processed_events", state.processed_events},
        {"event_log",
         nlohmann::json{
             {"capacity", state.event_log.capacity()},
             {"entries", EventLogToJson(state.event_log)},
         }},
    };
}

std::string compute_state_hash_hex(const SimState& state) {
    return sha256_hex(serialize_state_json(state).dump());
}

}  // namespace wfs::sim
