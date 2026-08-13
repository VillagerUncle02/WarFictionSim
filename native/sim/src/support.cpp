// sim/src/support.cpp
//
// T047：支援请求实体与状态机实现。
//
// 实现策略：
// - 状态机数据表集中登记合法转移：SUBMITTED→EVALUATING；
//   EVALUATING→EXECUTING/REJECTED/ESCALATED；ESCALATED→EVALUATING/REJECTED；
//   终态（EXECUTING/REJECTED）不可再转移。转请后更上级可重新评估，但最终
//   必须落到明确结果（SC-004：玩家始终收到明确响应，FR-008）。
// - Submit 按 id 唯一性校验后按提交顺序追加；Transition 非法转移返回 false
//   且状态不变（宪法第 17 条：显式失败，不静默吞错）；全部查找按固定顺序，
//   同一输入必然产生同一结果（宪法第 7 条）。
// - JSON 序列化覆盖全部字段（存档/快照复用，宪法第 13 条）；反序列化校验
//   id 唯一，重复/非法枚举显式抛 std::invalid_argument。

#include "wfs/sim/support.h"

#include <algorithm>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace wfs::sim {

namespace {

// 合法转移表（固定顺序，唯一权威）。
const std::vector<std::pair<SupportRequestState, SupportRequestState>>& TransitionTable() {
    static const std::vector<std::pair<SupportRequestState, SupportRequestState>> table = {
        {SupportRequestState::kSubmitted, SupportRequestState::kEvaluating},
        {SupportRequestState::kEvaluating, SupportRequestState::kExecuting},
        {SupportRequestState::kEvaluating, SupportRequestState::kRejected},
        {SupportRequestState::kEvaluating, SupportRequestState::kEscalated},
        {SupportRequestState::kEscalated, SupportRequestState::kEvaluating},
        {SupportRequestState::kEscalated, SupportRequestState::kRejected},
    };
    return table;
}

// 状态可达性：从 SUBMITTED 按转移表广度优先遍历（状态有限，≤4 步必达）。
bool StateReachable(const SupportRequestState target) {
    std::set<SupportRequestState> frontier{SupportRequestState::kSubmitted};
    for (int step = 0; step < 4; ++step) {
        if (frontier.contains(target)) {
            return true;
        }
        std::set<SupportRequestState> next;
        for (const SupportRequestState from : frontier) {
            for (const auto& [source, destination] : TransitionTable()) {
                if (source == from) {
                    next.insert(destination);
                }
            }
        }
        frontier = std::move(next);
    }
    return frontier.contains(target);
}

}  // namespace

std::string_view to_string(const SupportRequestState state) noexcept {
    switch (state) {
        case SupportRequestState::kSubmitted:
            return "SUBMITTED";
        case SupportRequestState::kEvaluating:
            return "EVALUATING";
        case SupportRequestState::kExecuting:
            return "EXECUTING";
        case SupportRequestState::kRejected:
            return "REJECTED";
        case SupportRequestState::kEscalated:
            return "ESCALATED";
    }
    return "UNKNOWN";
}

SupportRequestState support_request_state_from_string(const std::string_view name) {
    if (name == "SUBMITTED") {
        return SupportRequestState::kSubmitted;
    }
    if (name == "EVALUATING") {
        return SupportRequestState::kEvaluating;
    }
    if (name == "EXECUTING") {
        return SupportRequestState::kExecuting;
    }
    if (name == "REJECTED") {
        return SupportRequestState::kRejected;
    }
    if (name == "ESCALATED") {
        return SupportRequestState::kEscalated;
    }
    throw std::invalid_argument("未知支援请求状态: " + std::string(name));
}

bool can_transition(const SupportRequestState from, const SupportRequestState to) noexcept {
    for (const auto& [source, target] : TransitionTable()) {
        if (source == from && target == to) {
            return true;
        }
    }
    return false;
}

void to_json(nlohmann::json& json, const SupportRequest& request) {
    json = nlohmann::json{{"id", request.id},
                          {"seq", request.seq},
                          {"priority", request.priority},
                          {"command_id", request.command_id},
                          {"from_node", request.from_node},
                          {"to_node", request.to_node},
                          {"target_unit", request.target_unit},
                          {"request_type", request.request_type},
                          {"kinds", request.kinds},
                          {"quantity", request.quantity},
                          {"for_command_id", request.for_command_id},
                          {"return_after_ticks", request.return_after_ticks},
                          {"submitted_tick", request.submitted_tick},
                          {"evaluating_tick", request.evaluating_tick},
                          {"resolved_tick", request.resolved_tick},
                          {"state", to_string(request.state)},
                          {"resolution", request.resolution},
                          {"resolution_reason", request.resolution_reason},
                          {"assigned_unit_ids", request.assigned_unit_ids}};
}

void from_json(const nlohmann::json& json, SupportRequest& request) {
    request.id = json.at("id").get<std::string>();
    request.seq = json.at("seq").get<std::uint64_t>();
    request.priority = json.at("priority").get<std::int64_t>();
    request.command_id = json.at("command_id").get<std::string>();
    request.from_node = json.at("from_node").get<std::string>();
    request.to_node = json.at("to_node").get<std::string>();
    request.target_unit = json.at("target_unit").get<std::string>();
    request.request_type = json.at("request_type").get<std::string>();
    request.kinds = json.at("kinds").get<std::vector<std::string>>();
    request.quantity = json.at("quantity").get<std::uint64_t>();
    request.for_command_id = json.at("for_command_id").get<std::string>();
    request.return_after_ticks = json.at("return_after_ticks").get<std::uint64_t>();
    request.submitted_tick = json.at("submitted_tick").get<std::uint64_t>();
    request.evaluating_tick = json.at("evaluating_tick").get<std::uint64_t>();
    request.resolved_tick = json.at("resolved_tick").get<std::uint64_t>();
    request.state = support_request_state_from_string(json.at("state").get<std::string>());
    request.resolution = json.at("resolution").get<std::string>();
    request.resolution_reason = json.at("resolution_reason").get<std::string>();
    request.assigned_unit_ids = json.at("assigned_unit_ids").get<std::vector<std::string>>();
    if (request.id.empty() || request.quantity == 0U) {
        throw std::invalid_argument("支援请求字段非法: " + request.id);
    }
}

SupportRequest* SupportChain::Submit(SupportRequest request) {
    if (request.id.empty() || Find(request.id) != nullptr) {
        return nullptr;  // 空 id / 重复 id：显式拒绝，不覆盖既有请求。
    }
    if (request.state != SupportRequestState::kSubmitted) {
        return nullptr;  // 提交必须从 SUBMITTED 起始。
    }
    requests_.push_back(std::move(request));
    return &requests_.back();
}

bool SupportChain::Transition(const std::string& id, const SupportRequestState target) {
    SupportRequest* request = FindMutable(id);
    if (request == nullptr || !can_transition(request->state, target)) {
        return false;  // 非法转移：状态保持不变（宪法 17）。
    }
    request->state = target;
    return true;
}

bool SupportChain::HasCommand(const std::string& command_id) const {
    if (command_id.empty()) {
        return false;
    }
    return std::any_of(requests_.begin(), requests_.end(),
                       [&](const SupportRequest& request) { return request.command_id == command_id; });
}

const SupportRequest* SupportChain::Find(const std::string& id) const {
    for (const SupportRequest& request : requests_) {
        if (request.id == id) {
            return &request;
        }
    }
    return nullptr;
}

SupportRequest* SupportChain::FindMutable(const std::string& id) {
    for (SupportRequest& request : requests_) {
        if (request.id == id) {
            return &request;
        }
    }
    return nullptr;
}

void to_json(nlohmann::json& json, const SupportChain& chain) {
    json = nlohmann::json{{"requests", chain.RequestsInSubmitOrder()}};
}

void from_json(const nlohmann::json& json, SupportChain& chain) {
    SupportChain candidate;
    for (const nlohmann::json& request_json : json.at("requests")) {
        SupportRequest request = request_json.get<SupportRequest>();
        if (request.id.empty() || candidate.Find(request.id) != nullptr) {
            throw std::invalid_argument("支援请求链路包含重复/非法 id: " + request.id);
        }
        // 快照可能处于任意中间状态：直接回填，但状态必须能从 SUBMITTED
        // 逐步转移到达（损坏数据显式拒绝，宪法 17）。
        if (!StateReachable(request.state)) {
            throw std::invalid_argument("支援请求状态不可达（状态机损坏）: " + request.id);
        }
        candidate.requests_.push_back(std::move(request));
    }
    chain = std::move(candidate);
}

}  // namespace wfs::sim
