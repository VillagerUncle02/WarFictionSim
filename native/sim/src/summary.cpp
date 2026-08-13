// sim/src/summary.cpp
//
// T059：摘要上报与信息权限裁剪实现。
//
// 实现策略：
// - 摘要从状态读取后立即冻结为值对象（快照语义）：SUMMARY_REPORT 事件与
//   SummaryRegistry 都只在生成时刻计算一次，后续状态变化不改写（CHK165）。
// - 任务状态分布 = 命令链（执行中 = issued/acknowledged/effective）+ 每单位
//   结果累计（完成/失败/超时由 mission_exec 在判定处递增）；完成度为
//   完成数 / 已进入判定视野的任务数（四舍五入为整数百分比）。
// - 损失摘要按人员（士兵伤亡）、载具（摧毁的载具单位）、班组（摧毁的非载具
//   单位）三类统计；支援需求取该节点未决请求数（submitted/evaluating/
//   escalated）。
// - 生成节奏 = 上级节点层级同步间隔（与 T058 共用 sync_ticks_for），
//   即"上级按自身同步间隔获取最新摘要"（FR-028/051）。
// - 统一裁剪规则只输出权限内信息：本节点直属单位完整状态 + 直接下级摘要；
//   遍历顺序固定（节点插入顺序 / 摘要生成顺序），保证确定性（宪法第 7 条）。

#include "wfs/sim/summary.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <string>

#include "sim_state.h"
#include "wfs/sim/command_chain.h"
#include "wfs/sim/command_org.h"
#include "wfs/sim/event_log.h"
#include "wfs/sim/intel_sync.h"
#include "wfs/sim/support.h"

namespace wfs::sim {

namespace {

void LogSummary(SimState& state, std::string message) {
    state.event_log.append(state.clock.tick(), EventCategory::kMission, EventSeverity::kInfo, std::move(message));
}

std::string BoolText(const bool value) {
    return value ? "true" : "false";
}

std::string PairKey(const std::string& from_node, const std::string& to_node) {
    return from_node + ":" + to_node;
}

bool IsActiveCommandState(const CommandState command_state) {
    return command_state == CommandState::kIssued || command_state == CommandState::kAcknowledged ||
           command_state == CommandState::kEffective;
}

}  // namespace

SummaryConfig SummaryConfig::FromScenario(
    const nlohmann::json& raw) {  // NOLINT(readability-convert-member-functions-to-static)
    SummaryConfig config;
    if (!raw.contains("summary") || !raw["summary"].is_object()) {
        return config;
    }
    const nlohmann::json& json = raw["summary"];
    if (json.contains("enabled") && !json["enabled"].is_boolean()) {
        throw std::invalid_argument("summary.enabled 必须是布尔值");
    }
    config.enabled = json.value("enabled", config.enabled);
    return config;
}

void to_json(nlohmann::json& json, const SummaryMissionCounts& counts) {
    json = nlohmann::json{{"active", counts.active},
                          {"completed", counts.completed},
                          {"failed", counts.failed},
                          {"timed_out", counts.timed_out}};
}

void from_json(const nlohmann::json& json, SummaryMissionCounts& counts) {
    counts.active = json.at("active").get<std::uint64_t>();
    counts.completed = json.at("completed").get<std::uint64_t>();
    counts.failed = json.at("failed").get<std::uint64_t>();
    counts.timed_out = json.at("timed_out").get<std::uint64_t>();
}

void to_json(nlohmann::json& json, const SummaryLosses& losses) {
    json = nlohmann::json{{"soldiers", losses.soldiers}, {"vehicles", losses.vehicles}, {"squads", losses.squads}};
}

void from_json(const nlohmann::json& json, SummaryLosses& losses) {
    losses.soldiers = json.at("soldiers").get<std::uint64_t>();
    losses.vehicles = json.at("vehicles").get<std::uint64_t>();
    losses.squads = json.at("squads").get<std::uint64_t>();
}

void to_json(nlohmann::json& json, const SummaryReport& report) {
    json = nlohmann::json{{"id", report.id},
                          {"from_node", report.from_node},
                          {"to_node", report.to_node},
                          {"generated_tick", report.generated_tick},
                          {"missions", report.missions},
                          {"completion_pct", report.completion_pct},
                          {"losses", report.losses},
                          {"support_requests", report.support_requests},
                          {"needs_support", report.needs_support}};
}

void from_json(const nlohmann::json& json, SummaryReport& report) {
    report.id = json.at("id").get<std::string>();
    report.from_node = json.at("from_node").get<std::string>();
    report.to_node = json.at("to_node").get<std::string>();
    report.generated_tick = json.at("generated_tick").get<std::uint64_t>();
    report.missions = json.at("missions").get<SummaryMissionCounts>();
    report.completion_pct = json.at("completion_pct").get<std::uint32_t>();
    report.losses = json.at("losses").get<SummaryLosses>();
    report.support_requests = json.at("support_requests").get<std::uint64_t>();
    report.needs_support = json.at("needs_support").get<bool>();
}

void to_json(nlohmann::json& json, const MissionOutcomeCounts& counts) {
    json = nlohmann::json{{"completed", counts.completed}, {"failed", counts.failed}, {"timed_out", counts.timed_out}};
}

void from_json(const nlohmann::json& json, MissionOutcomeCounts& counts) {
    counts.completed = json.at("completed").get<std::uint64_t>();
    counts.failed = json.at("failed").get<std::uint64_t>();
    counts.timed_out = json.at("timed_out").get<std::uint64_t>();
}

SummaryReport* SummaryRegistry::Add(SummaryReport report) {
    reports_.push_back(std::move(report));
    return &reports_.back();
}

const SummaryReport* SummaryRegistry::Find(const std::string& id) const {
    for (const SummaryReport& report : reports_) {
        if (report.id == id) {
            return &report;
        }
    }
    return nullptr;
}

const SummaryReport* SummaryRegistry::LatestFor(const std::string& from_node, const std::string& to_node) const {
    const SummaryReport* latest = nullptr;
    for (const SummaryReport& report : reports_) {
        if (report.from_node == from_node && report.to_node == to_node) {
            latest = &report;
        }
    }
    return latest;
}

std::uint64_t SummaryRegistry::LastReportTick(const std::string& from_node, const std::string& to_node) const {
    const auto iterator = last_report_tick_.find(PairKey(from_node, to_node));
    return iterator == last_report_tick_.end() ? 0U : iterator->second;
}

void SummaryRegistry::SetLastReportTick(const std::string& from_node, const std::string& to_node,
                                        const std::uint64_t tick) {
    last_report_tick_[PairKey(from_node, to_node)] = tick;
}

void SummaryRegistry::Clear() noexcept {
    reports_.clear();
    last_report_tick_.clear();
}

void to_json(nlohmann::json& json, const SummaryRegistry& registry) {
    json = nlohmann::json{{"reports", registry.reports_}, {"last_report_tick", registry.last_report_tick_}};
}

void from_json(const nlohmann::json& json, SummaryRegistry& registry) {
    SummaryRegistry candidate;
    std::set<std::string> ids;
    for (const nlohmann::json& report_json : json.at("reports")) {
        SummaryReport report = report_json.get<SummaryReport>();
        if (!ids.insert(report.id).second) {
            throw std::invalid_argument("摘要登记表包含重复 id: " + report.id);
        }
        candidate.Add(std::move(report));
    }
    candidate.last_report_tick_ = json.value("last_report_tick", std::map<std::string, std::uint64_t>{});
    registry = std::move(candidate);
}

SummaryReport build_summary(const SimState& state, const std::string& from_node, const std::string& to_node,
                            const std::uint64_t generated_tick) {
    SummaryReport report;
    report.id = "sum-" + std::to_string(generated_tick) + "-" + from_node + "-" + to_node;
    report.from_node = from_node;
    report.to_node = to_node;
    report.generated_tick = generated_tick;

    MissionOutcomeCounts outcomes;
    for (const RuntimeUnitState& unit : state.units) {
        if (unit.node_id != from_node) {
            continue;
        }
        for (const model::Soldier& soldier : unit.soldiers) {
            if (soldier.is_casualty()) {
                ++report.losses.soldiers;
            }
        }
        if (unit.destroyed) {
            if (unit.is_vehicle) {
                ++report.losses.vehicles;
            } else {
                ++report.losses.squads;
            }
        }
        const auto outcome = state.mission_outcomes.find(unit.id);
        if (outcome != state.mission_outcomes.end()) {
            outcomes.completed += outcome->second.completed;
            outcomes.failed += outcome->second.failed;
            outcomes.timed_out += outcome->second.timed_out;
        }
    }
    report.missions.completed = outcomes.completed;
    report.missions.failed = outcomes.failed;
    report.missions.timed_out = outcomes.timed_out;
    for (const ChainCommand& command : state.command_chain.CommandsInIssueOrder()) {
        if (command.unit_id.empty() || !IsActiveCommandState(command.state)) {
            continue;
        }
        const auto unit = std::find_if(state.units.begin(), state.units.end(), [&](const RuntimeUnitState& candidate) {
            return candidate.id == command.unit_id;
        });
        if (unit != state.units.end() && unit->node_id == from_node) {
            ++report.missions.active;
        }
    }
    const std::uint64_t denominator =
        report.missions.completed + report.missions.failed + report.missions.timed_out + report.missions.active;
    if (denominator > 0U) {
        report.completion_pct = static_cast<std::uint32_t>(
            std::llround(100.0 * static_cast<double>(report.missions.completed) / static_cast<double>(denominator)));
    }
    for (const SupportRequest& request : state.support_chain.RequestsInSubmitOrder()) {
        if (request.from_node != from_node) {
            continue;
        }
        if (request.state == SupportRequestState::kSubmitted || request.state == SupportRequestState::kEvaluating ||
            request.state == SupportRequestState::kEscalated) {
            ++report.support_requests;
        }
    }
    report.needs_support = report.support_requests > 0U;
    return report;
}

void step_summaries(SimState& state) {
    if (!state.command_org.configured || !state.summary_config.enabled) {
        return;
    }
    const std::uint64_t tick = state.clock.tick();
    for (const std::string& node_id : state.command_org.nodes_in_insertion_order()) {
        const model::CommandNode* node = state.command_org.find_node(node_id);
        if (node == nullptr || node->parent_id.empty()) {
            continue;
        }
        // 上级按自身层级同步间隔获取最新摘要（FR-028/051）。
        const std::uint64_t interval =
            state.intel_sync_config.sync_ticks_for(command_echelon(state.command_org, node->parent_id));
        const std::uint64_t last_tick = state.summaries.LastReportTick(node_id, node->parent_id);
        if (tick < last_tick + interval) {
            continue;
        }
        state.summaries.SetLastReportTick(node_id, node->parent_id, tick);
        const SummaryReport report = build_summary(state, node_id, node->parent_id, tick);
        state.summaries.Add(report);

        std::string message = "SUMMARY_REPORT interaction=SUMMARY_REPORT from=" + report.from_node +
                              " to=" + report.to_node + " generated_tick=" + std::to_string(report.generated_tick) +
                              " missions={active=" + std::to_string(report.missions.active) +
                              ",completed=" + std::to_string(report.missions.completed) +
                              ",failed=" + std::to_string(report.missions.failed) +
                              ",timed_out=" + std::to_string(report.missions.timed_out) +
                              "} completion_pct=" + std::to_string(report.completion_pct) +
                              " losses={soldiers=" + std::to_string(report.losses.soldiers) +
                              ",vehicles=" + std::to_string(report.losses.vehicles) +
                              ",squads=" + std::to_string(report.losses.squads) +
                              "} support_requests=" + std::to_string(report.support_requests) +
                              " needs_support=" + BoolText(report.needs_support);
        LogSummary(state, std::move(message));
    }
}

nlohmann::json build_authorized_view_json(const SimState& state, const std::string& node_id) {
    nlohmann::json own_units = nlohmann::json::array();
    for (const RuntimeUnitState& unit : state.units) {
        if (unit.node_id == node_id) {
            own_units.push_back(unit);
        }
    }
    nlohmann::json subordinates = nlohmann::json::array();
    for (const std::string& child_id : state.command_org.children_of(node_id)) {
        const SummaryReport* latest = state.summaries.LatestFor(child_id, node_id);
        if (latest == nullptr) {
            continue;
        }
        subordinates.push_back(*latest);
    }
    return nlohmann::json{
        {"node_id", node_id}, {"own_units", std::move(own_units)}, {"subordinates", std::move(subordinates)}};
}

}  // namespace wfs::sim
