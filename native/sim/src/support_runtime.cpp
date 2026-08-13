// sim/src/support_runtime.cpp
//
// T047–T050：支援请求管线的运行期推进实现。
//
// 实现策略（FR-005/008/009/046；SC-004；宪法第 7/9 条）：
// - 每 tick 固定顺序推进：脚本裁决桩请求登记 → 命令来源请求登记 →
//   到期评估按（优先级降序, 到达序列号升序）裁决 → 归建/重新配属。
//   全部随机性来源是既有统一 RNG（命令通讯延迟），本模块不额外抽随机数。
// - 连排级：有限分数请求（扣分明确可见，用尽即止）；营级：确定性裁决桩
//   的配属/拒绝/转请（US3 营级 AI 决策接入前以规则替代，登记 TODO），
//   转请后 v1 无旅级裁决器 → 明确拒绝（SC-004：始终得到明确响应）。
// - 归建：连排级按关联任务命令完成触发，营级裁决桩按 return_after_ticks
//   触发（下属任务上报接入后移除，登记 TODO）；归建途中可被重新配属：
//   裁决前把健康 RETURNING 记录视为可抢占候选，配属时取消原归建并复用
//   （新请求优先于归建，FR-009）。
// - 派系加载失败/配置非法时记录结构化可见事件并保持未配置（宪法 17：
//   不静默吞错；场景数据校验失败由加载器在更早层报告）。

#include "sim_runtime.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "sim_state.h"
#include "wfs/sim/attach.h"
#include "wfs/sim/command_chain.h"
#include "wfs/sim/event_log.h"
#include "wfs/sim/faction.h"
#include "wfs/sim/resource_pool.h"
#include "wfs/sim/support.h"

namespace wfs::sim {

namespace {

void LogSupport(SimState& state, std::string message) {
    state.event_log.append(state.clock.tick(), EventCategory::kCommand, EventSeverity::kInfo, std::move(message));
}

const EchelonResourcePool* ActivePool(const SimState& state) {
    const auto found = state.support_faction.pools.find(state.support_pool_echelon);
    return found == state.support_faction.pools.end() ? nullptr : &found->second;
}

std::string JoinIds(const std::vector<std::string>& ids) {
    std::string joined = "[";
    for (std::size_t i = 0U; i < ids.size(); ++i) {
        if (i > 0U) {
            joined += ",";
        }
        joined += ids[i];
    }
    joined += "]";
    return joined;
}

// 可抢占的归建中单位不阻塞新请求：从活动占用中扣除后计算可用力量（FR-009）。
std::vector<ResourceAllocation> WithoutReclaimable(std::vector<ResourceAllocation> active,
                                                   const std::vector<ResourceAllocation>& reclaimable) {
    for (const ResourceAllocation& reclaim : reclaimable) {
        const auto found = std::find_if(active.begin(), active.end(), [&](const ResourceAllocation& allocation) {
            return allocation.kind_id == reclaim.kind_id;
        });
        if (found == active.end()) {
            continue;
        }
        found->quantity = found->quantity > reclaim.quantity ? found->quantity - reclaim.quantity : 0U;
    }
    return active;
}

// 取消原归建并重新配属：新请求优先于归建（FR-009）。按插入顺序确定性选取
// 前 needed 条健康 RETURNING 记录，返回实际重新配属数量。
std::uint64_t ReassignReturningUnits(SimState& state, const std::string& request_id, const std::string& to_node,
                                     const std::string& kind, const std::uint64_t needed) {
    std::uint64_t reassigned = 0U;
    for (const AttachRecord& snapshot : state.attach_registry.RecordsInInsertionOrder()) {
        if (reassigned >= needed) {
            break;
        }
        const AttachRecord* record = state.attach_registry.Find(snapshot.id);
        if (record == nullptr || record->kind_id != kind || record->state != AttachUnitState::kReturning ||
            record->immobilized) {
            continue;
        }
        const std::string previous_request_id = record->request_id;
        if (state.attach_registry.Reassign(record->id, request_id, to_node, state.clock.tick())) {
            LogSupport(state, "ATTACH_REASSIGNED request=" + request_id + " unit=" + kind +
                                  " previous_request=" + previous_request_id + " to=" + to_node +
                                  " tick=" + std::to_string(state.clock.tick()));
            ++reassigned;
        }
    }
    return reassigned;
}

// 由命令链路生效的 SUPPORT_REQUEST 命令构建请求（T047/046）。
SupportRequest RequestFromCommand(const SimState& state, const ChainCommand& command) {
    const nlohmann::json& support = command.payload.at("support");
    SupportRequest request;
    request.id = "req-" + command.command_id;
    request.seq = command.seq;
    request.priority = command.priority;
    request.command_id = command.command_id;
    request.from_node = state.support_config.player_node_id;
    request.to_node = support.value("to_node", state.support_config.superior_node_id);
    request.target_unit = command.unit_id;
    request.request_type = support.at("request_type").get<std::string>();
    request.kinds = support.at("kinds").get<std::vector<std::string>>();
    request.quantity = support.value("quantity", 1U);
    request.for_command_id = support.value("for_command_id", std::string());
    request.submitted_tick = state.clock.tick();
    return request;
}

// 登记请求：SUBMITTED 事件 → EVALUATING 事件 + 评估/裁决时刻（确定性）。
void RegisterRequest(SimState& state, SupportRequest request) {
    const std::string id = request.id;
    if (state.support_chain.Find(id) != nullptr || state.support_chain.Submit(request) == nullptr) {
        LogSupport(state, "SUPPORT_REQUEST_REGISTER_FAILED request=" + id + " reason=DUPLICATE_OR_INVALID");
        return;
    }
    LogSupport(state,
               "SUPPORT_REQUESTED request=" + id + " interaction=SUPPORT_REQUEST from_node=" + request.from_node +
                   " to_node=" + request.to_node + " request_type=" + request.request_type +
                   " priority=" + std::to_string(request.priority) + " quantity=" + std::to_string(request.quantity));
    state.support_chain.Transition(id, SupportRequestState::kEvaluating);
    SupportRequest* registered = state.support_chain.FindMutable(id);
    registered->evaluating_tick = state.clock.tick();
    // 审批层级基线影响评估延迟（0/1/2 级转发，FR-006 落地基线）。
    const std::uint64_t approval_delay =
        static_cast<std::uint64_t>(state.support_faction.approval_level) * state.support_config.approval_step_ticks;
    registered->resolved_tick = state.clock.tick() + state.support_config.evaluation_delay_ticks + approval_delay;
    LogSupport(state, "SUPPORT_EVALUATING request=" + id + " interaction=SUPPORT_REQUEST evaluating_tick=" +
                          std::to_string(registered->evaluating_tick) +
                          " resolve_tick=" + std::to_string(registered->resolved_tick));
}

// 单请求裁决执行：配属（扣分/占用）/拒绝/转请（v1 无更上级 → 明确拒绝）。
void ResolveRequest(SimState& state, SupportRequest& request) {
    const EchelonResourcePool* pool = ActivePool(state);
    if (pool == nullptr) {
        state.support_chain.Transition(request.id, SupportRequestState::kRejected);
        LogSupport(state,
                   "SUPPORT_REJECTED request=" + request.id + " interaction=SUPPORT_REQUEST reason=POOL_NOT_FOUND");
        return;
    }

    // 归建途中且可重新配属的记录不阻塞新请求：裁决按抢占后可用力量计算，
    // 配属时再取消原归建（新请求优先于归建，FR-009）。
    const std::vector<ResourceAllocation> reclaimable = reclaimable_allocations(state.attach_registry);
    PoolSnapshot snapshot =
        available_force(*pool, WithoutReclaimable(state.attach_registry.ActiveAllocations(), reclaimable));
    snapshot.remaining_score = state.support_score_remaining;
    const bool limited_score = state.support_config.scale == "platoon";
    const bool has_superior = !state.support_config.superior_node_id.empty();
    const AttachDecision decision = adjudicate(request, snapshot, limited_score, has_superior);

    switch (decision.kind) {
        case AttachDecisionKind::kAssign: {
            state.support_chain.Transition(request.id, SupportRequestState::kExecuting);
            SupportRequest* live = state.support_chain.FindMutable(request.id);
            live->resolution = "assign";
            live->resolved_tick = state.clock.tick();
            live->assigned_unit_ids = decision.units;
            if (limited_score) {
                state.support_score_remaining = decision.score_remaining;
            }
            LogSupport(state, "SUPPORT_ASSIGNED request=" + request.id +
                                  " interaction=SUPPORT_REQUEST score_cost=" + std::to_string(decision.score_cost) +
                                  " score_remaining=" + std::to_string(decision.score_remaining) +
                                  " units=" + JoinIds(decision.units));
            for (const std::string& kind : decision.units) {
                // 优先抢占归建中单位（取消原归建），不足部分再从池内新配属。
                const std::uint64_t reclaimed =
                    ReassignReturningUnits(state, request.id, request.from_node, kind, request.quantity);
                for (std::uint64_t i = reclaimed; i < request.quantity; ++i) {
                    state.attach_registry.Attach(request.id, kind, request.from_node, request.to_node,
                                                 state.clock.tick());
                }
            }
            break;
        }
        case AttachDecisionKind::kReject: {
            state.support_chain.Transition(request.id, SupportRequestState::kRejected);
            SupportRequest* live = state.support_chain.FindMutable(request.id);
            live->resolution = "reject";
            live->resolution_reason = decision.reason;
            live->resolved_tick = state.clock.tick();
            LogSupport(state, "SUPPORT_REJECTED request=" + request.id +
                                  " interaction=SUPPORT_REQUEST reason=" + decision.reason);
            break;
        }
        case AttachDecisionKind::kEscalate: {
            state.support_chain.Transition(request.id, SupportRequestState::kEscalated);
            SupportRequest* live = state.support_chain.FindMutable(request.id);
            live->resolution = "escalate";
            live->resolved_tick = state.clock.tick();
            LogSupport(state, "SUPPORT_ESCALATED request=" + request.id + " interaction=SUPPORT_REQUEST to_node=" +
                                  state.support_config.superior_node_id + " reason=" + decision.reason);
            // v1 旅级后置：无更上级裁决器，确定性桩明确拒绝（SC-004/FR-008
            // 边缘情形"逐级上转后仍力量不足：最终拒绝"；US3/旅级接入后替换）。
            state.support_chain.Transition(request.id, SupportRequestState::kRejected);
            live = state.support_chain.FindMutable(request.id);
            live->resolution = "reject";
            live->resolution_reason = "NO_SUPERIOR_ESCALATION";
            LogSupport(state, "SUPPORT_REJECTED request=" + request.id +
                                  " interaction=SUPPORT_REQUEST reason=NO_SUPERIOR_ESCALATION escalated=true");
            break;
        }
    }
}

}  // namespace

void initialize_support_state(SimState& state) {
    state.support_config = SupportConfig::FromScenario(state.scenario.raw);
    state.support_chain.Clear();
    state.attach_registry.Clear();
    state.tactical_registry.Clear();
    state.support_faction = FactionTemplate{};
    state.support_score_remaining = 0U;
    state.support_pool_echelon = "battalion";
    state.support_configured = false;

    if (state.support_config.faction_id.empty()) {
        return;
    }
    const std::filesystem::path data_root = find_scenario_data_root(state.scenario_path);
    if (data_root.empty()) {
        LogSupport(state, "SUPPORT_CONFIG_INVALID reason=DATA_ROOT_NOT_FOUND");
        return;
    }
    const FactionLoadResult loaded = load_faction(data_root / "factions" / (state.support_config.faction_id + ".json"));
    if (!loaded.ok()) {
        const std::string message = loaded.issues.empty() ? "UNKNOWN" : loaded.issues.front().message;
        LogSupport(state, "SUPPORT_CONFIG_INVALID faction=" + state.support_config.faction_id + " reason=" + message);
        return;
    }
    state.support_faction = loaded.faction;
    const auto pool = state.support_faction.pools.find(state.support_pool_echelon);
    if (pool == state.support_faction.pools.end()) {
        LogSupport(state, "SUPPORT_CONFIG_INVALID reason=POOL_ECHELON_NOT_FOUND:" + state.support_pool_echelon);
        return;
    }
    // 连排级有限分数总额由所属营编制资源池确定（FR-008）。
    state.support_score_remaining = pool->second.support_score;
    state.support_configured = true;
}

void step_support_pipeline(SimState& state) {
    if (!state.support_configured) {
        return;
    }
    const std::uint64_t tick = state.clock.tick();

    // 1) 场景裁决桩请求（US3 营级 AI 接入前以数据声明确定性替代，TODO）。
    for (const SupportRequest& scripted : state.support_config.scripted_requests) {
        if (tick < scripted.submitted_tick || state.support_chain.Find(scripted.id) != nullptr) {
            continue;
        }
        SupportRequest request = scripted;
        request.seq = scripted.submitted_tick;  // 裁决桩到达序列号（确定性）。
        RegisterRequest(state, std::move(request));
    }

    // 2) 命令来源请求：命令链生效的 SUPPORT_REQUEST 命令（FR-046 共用通道）。
    for (const ChainCommand& command : state.command_chain.CommandsInIssueOrder()) {
        if (command.type != "SUPPORT_REQUEST" || command.state != CommandState::kEffective) {
            continue;
        }
        if (state.support_chain.HasCommand(command.command_id)) {
            continue;
        }
        RegisterRequest(state, RequestFromCommand(state, command));
    }

    // 3) 到期评估：同 tick 竞争按（优先级降序, 到达序列号升序）裁决（FR-008）。
    std::vector<SupportRequest*> due;
    for (const SupportRequest& snapshot : state.support_chain.RequestsInSubmitOrder()) {
        SupportRequest* request = state.support_chain.FindMutable(snapshot.id);
        if (request != nullptr && request->state == SupportRequestState::kEvaluating &&
            tick >= request->resolved_tick) {
            due.push_back(request);
        }
    }
    std::stable_sort(due.begin(), due.end(), [](const SupportRequest* left, const SupportRequest* right) {
        if (left->priority != right->priority) {
            return left->priority > right->priority;
        }
        return left->seq < right->seq;
    });
    for (SupportRequest* request : due) {
        ResolveRequest(state, *request);
    }

    // 4) 归建/重新配属：任务结束 → RETURNING → RETURNED（FR-009/010）。
    for (const AttachRecord& record_snapshot : state.attach_registry.RecordsInInsertionOrder()) {
        const AttachRecord* record = state.attach_registry.Find(record_snapshot.id);
        if (record == nullptr) {
            continue;
        }
        if (record->state == AttachUnitState::kAssigned) {
            const SupportRequest* request = state.support_chain.Find(record->request_id);
            bool trigger_return = false;
            if (request != nullptr) {
                if (request->return_after_ticks > 0U) {
                    // 裁决桩的任务结束确定性替代（US3 接入后移除，TODO）。
                    trigger_return = tick >= request->resolved_tick + request->return_after_ticks;
                } else if (!request->for_command_id.empty()) {
                    const ChainCommand* linked = state.command_chain.Find(request->for_command_id);
                    trigger_return = linked != nullptr && linked->state == CommandState::kCompleted;
                }
            }
            if (trigger_return && state.attach_registry.StartReturn(record->id, tick)) {
                LogSupport(state, "ATTACH_RETURNING request=" + record->request_id + " unit=" + record->kind_id +
                                      " to=" + record->parent_node + " tick=" + std::to_string(tick));
            }
        } else if (record->state == AttachUnitState::kReturning &&
                   tick >= record->return_start_tick + state.support_config.return_delay_ticks &&
                   state.attach_registry.CompleteReturn(record->id, tick)) {
            LogSupport(state, "ATTACH_RETURNED request=" + record->request_id + " unit=" + record->kind_id +
                                  " tick=" + std::to_string(tick));
        }
    }
}

}  // namespace wfs::sim
