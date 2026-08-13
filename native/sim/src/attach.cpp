// sim/src/attach.cpp
//
// T050：配属链仲裁与归建实现。
//
// 实现策略：
// - 仲裁为纯函数（宪法第 7/9 条）：(优先级降序, 到达序列号升序) 稳定排序后
//   逐请求裁决，先满足者先占用资源与分数，后续请求在剩余可用力量上裁决；
//   裁决桩在 US3 营级 AI 接入前以确定性规则替代（配属/拒绝/转请，登记 TODO）。
// - 配属记录生命周期 kAssigned → kReturning → kReturned；归建途中可被
//   重新配属（新请求优先于归建，原归建流程取消，FR-009）；瘫痪单位
//   （需大修/拖运）阻塞配属（FR-009/078）。
// - 途中补给/维修本体属 US3（FR-075/078）：本模块提供确定性 hook，按
//   补给点可达性/抢修可行性给出结构化动作，实际补给/维修执行留接口。

#include "wfs/sim/attach.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace wfs::sim {

namespace {

const ResourcePoolEntry* FindEntry(const PoolSnapshot& pool, const std::string& id) {
    const auto found = std::find_if(pool.available.begin(), pool.available.end(),
                                    [&](const ResourcePoolEntry& entry) { return entry.id == id; });
    return found == pool.available.end() ? nullptr : &*found;
}

void AddAllocations(std::vector<ResourceAllocation>& working, const std::vector<std::string>& kinds,
                    const std::uint64_t quantity) {
    for (const std::string& kind : kinds) {
        const auto found = std::find_if(working.begin(), working.end(), [&](const ResourceAllocation& allocation) {
            return allocation.kind_id == kind;
        });
        if (found == working.end()) {
            working.push_back(ResourceAllocation{kind, quantity});
        } else {
            found->quantity += quantity;
        }
    }
}

bool ParseIdSequence(const std::string& text, std::uint64_t& value) {
    if (text.empty()) {
        return false;
    }
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    return ec == std::errc{} && ptr == end;
}

}  // namespace

std::string_view to_string(const AttachUnitState state) noexcept {
    switch (state) {
        case AttachUnitState::kAssigned:
            return "ASSIGNED";
        case AttachUnitState::kReturning:
            return "RETURNING";
        case AttachUnitState::kReturned:
            return "RETURNED";
    }
    return "UNKNOWN";
}

AttachUnitState attach_unit_state_from_string(const std::string_view name) {
    if (name == "ASSIGNED") {
        return AttachUnitState::kAssigned;
    }
    if (name == "RETURNING") {
        return AttachUnitState::kReturning;
    }
    if (name == "RETURNED") {
        return AttachUnitState::kReturned;
    }
    throw std::invalid_argument("未知配属状态: " + std::string(name));
}

std::string_view to_string(const AttachDecisionKind kind) noexcept {
    switch (kind) {
        case AttachDecisionKind::kAssign:
            return "assign";
        case AttachDecisionKind::kReject:
            return "reject";
        case AttachDecisionKind::kEscalate:
            return "escalate";
    }
    return "unknown";
}

AttachDecisionKind attach_decision_kind_from_string(const std::string_view name) {
    if (name == "assign") {
        return AttachDecisionKind::kAssign;
    }
    if (name == "reject") {
        return AttachDecisionKind::kReject;
    }
    if (name == "escalate") {
        return AttachDecisionKind::kEscalate;
    }
    throw std::invalid_argument("未知裁决类型: " + std::string(name));
}

std::string_view to_string(const TransitLogisticsAction action) noexcept {
    switch (action) {
        case TransitLogisticsAction::kProceed:
            return "proceed";
        case TransitLogisticsAction::kSupplyThenProceed:
            return "supply_then_proceed";
        case TransitLogisticsAction::kRepairThenProceed:
            return "repair_then_proceed";
        case TransitLogisticsAction::kBlockedHeavyRepair:
            return "blocked_heavy_repair";
    }
    return "unknown";
}

// 单请求裁决（确定性规则桩；US3 AI 接入前使用）：
// 范围外 → 拒绝；可用不足 → 连排级拒绝/营级转请（无更上级则拒绝）；
// 连排级再按有限分数裁决（不足拒绝），通过后配属并扣分。
AttachDecision adjudicate(const SupportRequest& request, const PoolSnapshot& pool, const bool limited_score,
                          const bool has_superior) {
    if (request.kinds.empty() || request.quantity == 0U) {
        return AttachDecision{
            AttachDecisionKind::kReject, "SCOPE_VIOLATION kinds 为空或数量为 0", {}, 0U, pool.remaining_score};
    }
    // 范围约束：支援种类必须存在于资源池（快照条目 = 池条目 + 扣减数量）。
    std::vector<std::string> missing;
    for (const std::string& kind : request.kinds) {
        if (FindEntry(pool, kind) == nullptr) {
            missing.push_back(kind);
        }
    }
    if (!missing.empty()) {
        std::string message = "SCOPE_VIOLATION 支援种类不在所属编制资源池内: ";
        for (std::size_t i = 0U; i < missing.size(); ++i) {
            if (i > 0U) {
                message += ",";
            }
            message += missing[i];
        }
        return AttachDecision{AttachDecisionKind::kReject, std::move(message), {}, 0U, pool.remaining_score};
    }

    for (const std::string& kind : request.kinds) {
        const ResourcePoolEntry* entry = FindEntry(pool, kind);
        if (entry == nullptr || entry->quantity < request.quantity) {
            const std::string reason = "INSUFFICIENT_AVAILABLE kind=" + kind;
            if (!limited_score && has_superior) {
                return AttachDecision{AttachDecisionKind::kEscalate, reason, {}, 0U, pool.remaining_score};
            }
            return AttachDecision{AttachDecisionKind::kReject, reason, {}, 0U, pool.remaining_score};
        }
    }

    std::uint64_t cost = 0U;
    std::uint64_t remaining = pool.remaining_score;
    if (limited_score) {
        const ScoreDeductionResult deduction = deduct_score(pool, request.kinds, request.quantity);
        if (!deduction.ok) {
            return AttachDecision{
                AttachDecisionKind::kReject, deduction.error, {}, deduction.cost, deduction.remaining};
        }
        cost = deduction.cost;
        remaining = deduction.remaining;
    }

    return AttachDecision{AttachDecisionKind::kAssign, "", request.kinds, cost, remaining};
}

std::vector<AttachDecision> arbitrate_requests(const std::vector<SupportRequest>& pending,
                                               const EchelonResourcePool& pool,
                                               const std::vector<ResourceAllocation>& assigned,
                                               const bool limited_score, const std::uint64_t remaining_score,
                                               const bool has_superior) {
    // (优先级降序, 到达序列号升序)：stable_sort 保证同键保持提交顺序，
    // seq 是唯一到达序列号，确定性成立（FR-008）。
    std::vector<const SupportRequest*> ordered;
    ordered.reserve(pending.size());
    for (const SupportRequest& request : pending) {
        ordered.push_back(&request);
    }
    std::stable_sort(ordered.begin(), ordered.end(), [](const SupportRequest* left, const SupportRequest* right) {
        if (left->priority != right->priority) {
            return left->priority > right->priority;
        }
        return left->seq < right->seq;
    });

    std::vector<AttachDecision> decisions;
    decisions.reserve(ordered.size());
    std::vector<ResourceAllocation> working = assigned;
    std::uint64_t working_score = remaining_score;
    for (const SupportRequest* request : ordered) {
        PoolSnapshot snapshot = available_force(pool, working);
        snapshot.remaining_score = working_score;
        AttachDecision decision = adjudicate(*request, snapshot, limited_score, has_superior);
        if (decision.kind == AttachDecisionKind::kAssign) {
            AddAllocations(working, request->kinds, request->quantity);
            working_score = decision.score_remaining;
        }
        decisions.push_back(std::move(decision));
    }
    return decisions;
}

TransitLogisticsResult resolve_transit_logistics(const AttachRecord& record, const bool supply_point_reachable,
                                                 const bool field_repair_possible) {
    // 瘫痪单位需要拖运/大修：阻塞配属，不参与重新配属（FR-009/078）。
    // 该判定优先于补给/维修标记：瘫痪本身即需要修理，不得误判为可直行。
    if (record.immobilized) {
        return TransitLogisticsResult{TransitLogisticsAction::kBlockedHeavyRepair, "IMMOBILIZED_REQUIRES_TOW_OR_DEPOT"};
    }
    if (!record.needs_supply && !record.needs_repair) {
        return TransitLogisticsResult{TransitLogisticsAction::kProceed, "无需补给/维修"};
    }
    if (record.needs_repair) {
        if (field_repair_possible) {
            return TransitLogisticsResult{TransitLogisticsAction::kRepairThenProceed, "FIELD_REPAIR_THEN_TRANSFER"};
        }
        return TransitLogisticsResult{TransitLogisticsAction::kBlockedHeavyRepair,
                                      "REPAIR_IMPOSSIBLE_FIELD_REQUIRES_DEPOT"};
    }
    // 补给：可达补给点先补给后出发；不可达走就地申请 hook（US3 送达后续行）。
    const std::string reason = supply_point_reachable ? "SUPPLY_AT_NEAREST_POINT" : "SUPPLY_REQUEST_ON_SITE_US3_HOOK";
    return TransitLogisticsResult{TransitLogisticsAction::kSupplyThenProceed, reason};
}

void to_json(nlohmann::json& json, const AttachDecision& decision) {
    json = nlohmann::json{{"kind", to_string(decision.kind)},
                          {"reason", decision.reason},
                          {"units", decision.units},
                          {"score_cost", decision.score_cost},
                          {"score_remaining", decision.score_remaining}};
}

void from_json(const nlohmann::json& json, AttachDecision& decision) {
    decision.kind = attach_decision_kind_from_string(json.at("kind").get<std::string>());
    decision.reason = json.at("reason").get<std::string>();
    decision.units = json.at("units").get<std::vector<std::string>>();
    decision.score_cost = json.at("score_cost").get<std::uint64_t>();
    decision.score_remaining = json.at("score_remaining").get<std::uint64_t>();
}

void to_json(nlohmann::json& json, const AttachRecord& record) {
    json = nlohmann::json{{"id", record.id},
                          {"request_id", record.request_id},
                          {"kind_id", record.kind_id},
                          {"assigned_to_node", record.assigned_to_node},
                          {"parent_node", record.parent_node},
                          {"state", to_string(record.state)},
                          {"assigned_tick", record.assigned_tick},
                          {"return_start_tick", record.return_start_tick},
                          {"returned_tick", record.returned_tick},
                          {"needs_supply", record.needs_supply},
                          {"needs_repair", record.needs_repair},
                          {"immobilized", record.immobilized}};
}

void from_json(const nlohmann::json& json, AttachRecord& record) {
    record.id = json.at("id").get<std::string>();
    record.request_id = json.at("request_id").get<std::string>();
    record.kind_id = json.at("kind_id").get<std::string>();
    record.assigned_to_node = json.at("assigned_to_node").get<std::string>();
    record.parent_node = json.at("parent_node").get<std::string>();
    record.state = attach_unit_state_from_string(json.at("state").get<std::string>());
    record.assigned_tick = json.at("assigned_tick").get<std::uint64_t>();
    record.return_start_tick = json.at("return_start_tick").get<std::uint64_t>();
    record.returned_tick = json.at("returned_tick").get<std::uint64_t>();
    record.needs_supply = json.at("needs_supply").get<bool>();
    record.needs_repair = json.at("needs_repair").get<bool>();
    record.immobilized = json.at("immobilized").get<bool>();
    if (record.id.empty() || record.kind_id.empty()) {
        throw std::invalid_argument("配属记录字段非法: " + record.id);
    }
}

AttachRecord* AttachRegistry::Attach(const std::string& request_id, const std::string& kind_id,
                                     const std::string& assigned_to_node, const std::string& parent_node,
                                     const std::uint64_t tick) {
    if (request_id.empty() || kind_id.empty() || assigned_to_node.empty() || parent_node.empty()) {
        return nullptr;
    }
    AttachRecord record;
    record.id = "att-" + std::to_string(next_id_);
    record.request_id = request_id;
    record.kind_id = kind_id;
    record.assigned_to_node = assigned_to_node;
    record.parent_node = parent_node;
    record.state = AttachUnitState::kAssigned;
    record.assigned_tick = tick;
    records_.push_back(std::move(record));
    ++next_id_;
    return &records_.back();
}

bool AttachRegistry::StartReturn(const std::string& id, const std::uint64_t tick) {
    AttachRecord* record = FindMutable(id);
    if (record == nullptr || record->state != AttachUnitState::kAssigned) {
        return false;
    }
    record->state = AttachUnitState::kReturning;
    record->return_start_tick = tick;
    return true;
}

bool AttachRegistry::CompleteReturn(const std::string& id, const std::uint64_t tick) {
    AttachRecord* record = FindMutable(id);
    if (record == nullptr || record->state != AttachUnitState::kReturning) {
        return false;
    }
    record->state = AttachUnitState::kReturned;
    record->returned_tick = tick;
    return true;
}

bool AttachRegistry::Reassign(const std::string& id, const std::string& new_request_id,
                              const std::string& new_assigned_to_node, const std::uint64_t tick) {
    AttachRecord* record = FindMutable(id);
    if (record == nullptr || record->state == AttachUnitState::kReturned) {
        return false;
    }
    if (record->immobilized) {
        // 瘫痪（需大修/拖运）阻塞配属；带伤可机动单位（needs_repair 且未瘫痪）
        // 允许前往新接收方后由接收方请求维修（FR-009/078）。
        return false;
    }
    // 新请求优先于归建：原归建流程取消，转为新的配属任务（FR-009）。
    record->request_id = new_request_id;
    record->assigned_to_node = new_assigned_to_node;
    record->state = AttachUnitState::kAssigned;
    record->assigned_tick = tick;
    record->return_start_tick = 0U;
    record->returned_tick = 0U;
    return true;
}

const AttachRecord* AttachRegistry::Find(const std::string& id) const {
    for (const AttachRecord& record : records_) {
        if (record.id == id) {
            return &record;
        }
    }
    return nullptr;
}

AttachRecord* AttachRegistry::FindMutable(const std::string& id) {
    for (AttachRecord& record : records_) {
        if (record.id == id) {
            return &record;
        }
    }
    return nullptr;
}

std::vector<ResourceAllocation> AttachRegistry::ActiveAllocations() const {
    std::vector<ResourceAllocation> allocations;
    for (const AttachRecord& record : records_) {
        if (record.state == AttachUnitState::kReturned) {
            continue;  // 已归建：占用释放，资源回到原属编制。
        }
        const auto found =
            std::find_if(allocations.begin(), allocations.end(),
                         [&](const ResourceAllocation& allocation) { return allocation.kind_id == record.kind_id; });
        if (found == allocations.end()) {
            allocations.push_back(ResourceAllocation{record.kind_id, 1U});
        } else {
            ++found->quantity;
        }
    }
    return allocations;
}

std::vector<ResourceAllocation> reclaimable_allocations(const AttachRegistry& registry) {
    std::vector<ResourceAllocation> reclaimable;
    for (const AttachRecord& record : registry.RecordsInInsertionOrder()) {
        if (record.state != AttachUnitState::kReturning || record.immobilized) {
            continue;  // 仅归建途中且可机动单位可被新请求抢占（FR-009）。
        }
        const auto found =
            std::find_if(reclaimable.begin(), reclaimable.end(),
                         [&](const ResourceAllocation& allocation) { return allocation.kind_id == record.kind_id; });
        if (found == reclaimable.end()) {
            reclaimable.push_back(ResourceAllocation{record.kind_id, 1U});
        } else {
            ++found->quantity;
        }
    }
    return reclaimable;
}

void to_json(nlohmann::json& json, const AttachRegistry& registry) {
    json = nlohmann::json{{"records", registry.RecordsInInsertionOrder()}};
}

void from_json(const nlohmann::json& json, AttachRegistry& registry) {
    AttachRegistry candidate;
    std::set<std::string> seen_ids;
    std::uint64_t previous = 0U;
    bool has_previous = false;
    for (const nlohmann::json& record_json : json.at("records")) {
        AttachRecord record = record_json.get<AttachRecord>();
        if (!seen_ids.insert(record.id).second) {
            throw std::invalid_argument("配属登记表包含重复 id: " + record.id);
        }
        // id 单调契约：格式必须为 att-<n> 且序号严格递增（存档缺中间 id 时
        // 恢复计数器按最大序号重建，避免新 Attach 产生重复 id）。
        constexpr const char* kIdPrefix = "att-";
        constexpr std::size_t kIdPrefixLength = 4U;
        std::uint64_t sequence = 0U;
        if (record.id.size() <= kIdPrefixLength || record.id.compare(0U, kIdPrefixLength, kIdPrefix) != 0U ||
            !ParseIdSequence(record.id.substr(kIdPrefixLength), sequence)) {
            throw std::invalid_argument("配属登记表包含非法 id（必须为 att-<n>）: " + record.id);
        }
        if (has_previous && sequence <= previous) {
            throw std::invalid_argument("配属登记表 id 非严格单调递增: " + record.id);
        }
        previous = sequence;
        has_previous = true;
        if (sequence >= candidate.next_id_) {
            candidate.next_id_ = sequence + 1U;
        }
        candidate.records_.push_back(std::move(record));
    }
    registry = std::move(candidate);
}

}  // namespace wfs::sim
