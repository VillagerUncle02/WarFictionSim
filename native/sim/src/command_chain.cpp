// sim/src/command_chain.cpp
//
// T029：命令下达与通讯延迟链路实现。
//
// 实现策略：
// - 注入通道（sim_runtime.cpp / ai_inject.cpp）在命令通过 T014 校验后调用
//   Issue：登记链路条目、用统一 RNG 抽取连排级 3–10s 延迟（FR-030），并把
//   命令仍以 (tick+1, seq) 放入事件队列作为"传输到达"信号；真正的通讯延迟
//   生效时间由本模块按 arrival_tick 处理（队列语义保持 T011 不变）。
// - ProcessDue 每 tick 按"同一单位同一到达 tick 的命令集合"做
//   （优先级降序, 序列号升序）裁决（FR-041/045），胜者确认接受并生效，
//   败者产生可见事件；后到的新命令取代同等/更高优先级已生效命令。
// - 撤回/修改（FR-045）：到达时间封顶为目标命令到达时间
//   （withdraw_catches_up），保证"生效前撤回/修改"窗口可测；自身仍计延迟。
// - 批量命令（FR-045）：展开为父命令 + 每单位子命令，按每单位条件独立
//   接受/拒绝，产生 BATCH_PARTIAL_ACCEPT。
// - 所有遍历按下达顺序（= seq 升序），事件文本字段稳定（测试/日志依赖）。

#include "wfs/sim/command_chain.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "sim_state.h"
#include "wfs/sim/event_log.h"
#include "wfs/sim/model/mission.h"
#include "wfs/sim/movement.h"

namespace wfs::sim {

namespace {

bool IsMetaType(const std::string& type) {
    return type == "WITHDRAW_COMMAND" || type == "MODIFY_COMMAND";
}

// 从命令 JSON 提取目标单位集合（unit / units 两种目标形态）。
std::vector<std::string> TargetUnits(const nlohmann::json& command) {
    const nlohmann::json& target = command.at("target");
    const std::string kind = target.at("kind").get<std::string>();
    if (kind == "units") {
        return target.at("refs").get<std::vector<std::string>>();
    }
    return {target.at("ref").get<std::string>()};
}

std::string CommandIdFor(const std::uint64_t seq) {
    return "cmd-" + std::to_string(seq);
}

std::string BatchChildId(const std::string& parent_id, const std::string& unit_id) {
    return parent_id + "." + unit_id;
}

RuntimeUnitState* FindUnit(SimState& state, const std::string& unit_id) {
    for (RuntimeUnitState& unit : state.units) {
        if (unit.id == unit_id) {
            return &unit;
        }
    }
    return nullptr;
}

void Log(SimState& state, const EventCategory category, const EventSeverity severity, std::string message) {
    state.event_log.append(state.clock.tick(), category, severity, std::move(message));
}

// 把行为 JSON 的弹药覆盖映射为任务级弹药策略（FR-058/060：任务级作用域）。
void ApplyAmmoPolicy(RuntimeUnitState& unit, const nlohmann::json& behavior) {
    if (behavior.is_object() && behavior.contains("ammo_override")) {
        unit.ammo_policy = model::AmmoPolicy::kSpecific;
        unit.ammo_override = behavior["ammo_override"].get<std::string>();
    } else {
        unit.ammo_policy = model::AmmoPolicy::kAuto;
        unit.ammo_override.clear();
    }
}

// 生效任务：写入单位任务字段并设置队形目标（切换耗时由机动系统执行）。
void ApplyMission(RuntimeUnitState& unit, const nlohmann::json& payload, const MovementConfig& movement_config) {
    const std::string type_name = payload.at("type").get<std::string>();
    const model::MissionType type = model::mission_type_from_string(type_name);
    unit.mission_active = true;
    unit.mission_command_id = payload.value("command_id", unit.mission_command_id);
    unit.mission_type = type_name;
    unit.mission_priority = payload.at("priority").get<std::int64_t>();
    unit.mission_deadline_ticks = payload["deadline"].value("game_time", 0U);
    unit.mission_condition = payload["completion"].value("condition", std::string(""));
    unit.mission_params = payload["completion"].value("params", nlohmann::json::object());
    const nlohmann::json behavior = payload.value("behavior", nlohmann::json::object());
    ApplyAmmoPolicy(unit, behavior);

    const model::Formation target = formation_for_mission(type, behavior);
    unit.requested_formation = target;
    if (target != unit.formation) {
        unit.formation_switch_remaining = movement_config.formation_switch_ticks;
        unit.moving = false;
    }
    if (unit.mission_condition == "reach_point" && unit.mission_params.is_object() &&
        unit.mission_params.contains("point")) {
        unit.target_x = unit.mission_params["point"].value("x", 0.0);
        unit.target_y = unit.mission_params["point"].value("y", 0.0);
    }
    unit.stuck = false;
}

void CancelMission(RuntimeUnitState& unit) {
    unit.mission_active = false;
    unit.mission_command_id.clear();
    unit.mission_type.clear();
    unit.mission_priority = 0;
    unit.mission_deadline_ticks = 0U;
    unit.mission_condition.clear();
    unit.mission_params = nlohmann::json::object();
    unit.moving = false;
    unit.stuck = false;
    unit.target_x = 0.0;
    unit.target_y = 0.0;
}

// 确认接受事件（FR-046）：接收方确认指令；只有仲裁胜者产生确认。
void LogAcknowledged(SimState& state, const ChainCommand& command) {
    Log(state, EventCategory::kCommand, EventSeverity::kInfo,
        "COMMAND_ACKNOWLEDGED command=" + command.command_id + " unit=" + command.unit_id + " type=" + command.type +
            " seq=" + std::to_string(command.seq) + " issue_tick=" + std::to_string(command.issue_tick) +
            " arrival_tick=" + std::to_string(command.arrival_tick) +
            " delay_ticks=" + std::to_string(command.delay_ticks));
}

void LogRejected(SimState& state, const ChainCommand& command, const std::string& reason) {
    const std::string display_id = command.parent_command_id.empty() ? command.command_id : command.parent_command_id;
    Log(state, EventCategory::kCommand, EventSeverity::kWarning,
        "COMMAND_REJECTED command=" + display_id + " unit=" + command.unit_id + " type=" + command.type +
            " reason=" + reason);
}

}  // namespace

std::uint64_t CommandDelayConfig::min_ticks(const std::uint32_t tick_hz) const {
    return static_cast<std::uint64_t>(std::ceil(min_seconds * static_cast<double>(tick_hz)));
}

std::uint64_t CommandDelayConfig::max_ticks(const std::uint32_t tick_hz) const {
    return static_cast<std::uint64_t>(std::ceil(max_seconds * static_cast<double>(tick_hz)));
}

CommandDelayConfig CommandDelayConfig::FromScenario(
    const nlohmann::json& raw) {  // NOLINT(readability-convert-member-functions-to-static)
    CommandDelayConfig config;
    if (!raw.contains("command_delay") || !raw["command_delay"].is_object()) {
        return config;
    }
    const nlohmann::json& json = raw["command_delay"];
    config.min_seconds = json.value("min_seconds", config.min_seconds);
    config.max_seconds = json.value("max_seconds", config.max_seconds);
    config.withdraw_catches_up = json.value("withdraw_catches_up", config.withdraw_catches_up);
    if (config.min_seconds < 0.0 || config.max_seconds < config.min_seconds) {
        throw std::invalid_argument("command_delay 配置非法（min/max 必须非负且 max>=min）");
    }
    return config;
}

std::string_view to_string(const CommandState state) noexcept {
    switch (state) {
        case CommandState::kIssued:
            return "issued";
        case CommandState::kAcknowledged:
            return "acknowledged";
        case CommandState::kEffective:
            return "effective";
        case CommandState::kCompleted:
            return "completed";
        case CommandState::kWithdrawn:
            return "withdrawn";
        case CommandState::kSuperseded:
            return "superseded";
        case CommandState::kRejected:
            return "rejected";
    }
    return "unknown";
}

CommandState command_state_from_string(const std::string_view name) {
    if (name == "issued") {
        return CommandState::kIssued;
    }
    if (name == "acknowledged") {
        return CommandState::kAcknowledged;
    }
    if (name == "effective") {
        return CommandState::kEffective;
    }
    if (name == "completed") {
        return CommandState::kCompleted;
    }
    if (name == "withdrawn") {
        return CommandState::kWithdrawn;
    }
    if (name == "superseded") {
        return CommandState::kSuperseded;
    }
    if (name == "rejected") {
        return CommandState::kRejected;
    }
    throw std::invalid_argument("未知命令链路状态: " + std::string(name));
}

void to_json(nlohmann::json& json, const ChainCommand& command) {
    json = nlohmann::json{{"command_id", command.command_id},
                          {"seq", command.seq},
                          {"type", command.type},
                          {"unit_id", command.unit_id},
                          {"priority", command.priority},
                          {"issue_tick", command.issue_tick},
                          {"delay_ticks", command.delay_ticks},
                          {"arrival_tick", command.arrival_tick},
                          {"state", to_string(command.state)},
                          {"payload", command.payload},
                          {"parent_command_id", command.parent_command_id},
                          {"target_command_id", command.target_command_id},
                          {"batch", command.batch}};
}

void from_json(const nlohmann::json& json, ChainCommand& command) {
    command.command_id = json.at("command_id").get<std::string>();
    command.seq = json.at("seq").get<std::uint64_t>();
    command.type = json.at("type").get<std::string>();
    command.unit_id = json.at("unit_id").get<std::string>();
    command.priority = json.at("priority").get<std::int64_t>();
    command.issue_tick = json.at("issue_tick").get<std::uint64_t>();
    command.delay_ticks = json.at("delay_ticks").get<std::uint64_t>();
    command.arrival_tick = json.at("arrival_tick").get<std::uint64_t>();
    command.state = command_state_from_string(json.at("state").get<std::string>());
    command.payload = json.at("payload");
    command.parent_command_id = json.at("parent_command_id").get<std::string>();
    command.target_command_id = json.at("target_command_id").get<std::string>();
    command.batch = json.at("batch").get<bool>();
}

void to_json(nlohmann::json& json, const CommandChain& chain) {
    json = nlohmann::json{{"commands", chain.CommandsInIssueOrder()}};
}

void from_json(const nlohmann::json& json, CommandChain& chain) {
    CommandChain candidate;
    for (const nlohmann::json& command_json : json.at("commands")) {
        const ChainCommand command = command_json.get<ChainCommand>();
        // 反序列化校验：command_id 唯一且按 seq 升序（损坏数据显式拒绝）。
        if (candidate.Find(command.command_id) != nullptr) {
            throw std::invalid_argument("命令链路包含重复 command_id: " + command.command_id);
        }
        if (!candidate.empty() && candidate.CommandsInIssueOrder().back().seq >= command.seq) {
            throw std::invalid_argument("命令链路 seq 非严格升序: " + command.command_id);
        }
        candidate.commands_.push_back(command);
    }
    chain = std::move(candidate);
}

// 下达登记按批量/元命令/普通任务三条确定性分支展开，拆分反而破坏可读性。
// NOLINTBEGIN(bugprone-easily-swappable-parameters, readability-function-cognitive-complexity)
CommandChain::IssueResult CommandChain::Issue(const nlohmann::json& command, const std::uint64_t seq,
                                              const GameTick issue_tick, const std::uint32_t tick_hz,
                                              const CommandDelayConfig& config, Rng& rng) {
    IssueResult result;
    result.seq = seq;
    result.command_id = CommandIdFor(seq);
    const std::string type = command.at("type").get<std::string>();
    const std::vector<std::string> units = TargetUnits(command);
    if (units.empty()) {
        result.error = "命令没有目标单位";
        return result;
    }

    // 连排级 3–10s 通讯延迟（FR-030）：统一 RNG 均匀抽样 [min, max]。
    const std::uint64_t min_ticks = config.min_ticks(tick_hz);
    const std::uint64_t max_ticks = config.max_ticks(tick_hz);
    const std::uint32_t span = static_cast<std::uint32_t>(std::min<std::uint64_t>(max_ticks - min_ticks, 1000000U));
    const std::uint64_t delay = min_ticks + static_cast<std::uint64_t>(rng.next_bounded(span));

    const bool is_batch = command["target"]["kind"].get<std::string>() == "units";
    const bool meta = IsMetaType(type);
    ChainCommand parent;
    parent.command_id = result.command_id;
    parent.seq = seq;
    parent.type = type;
    parent.priority = command.at("priority").get<std::int64_t>();
    parent.issue_tick = issue_tick;
    parent.delay_ticks = delay;
    parent.arrival_tick = issue_tick + delay;
    parent.payload = command;
    parent.batch = is_batch;
    if (is_batch) {
        parent.unit_id.clear();
        commands_.push_back(parent);
        for (const std::string& unit_id : units) {
            ChainCommand child = parent;
            child.command_id = BatchChildId(parent.command_id, unit_id);
            child.unit_id = unit_id;
            child.parent_command_id = parent.command_id;
            commands_.push_back(std::move(child));
        }
    } else {
        parent.unit_id = units.front();
        if (meta) {
            // 撤回/修改指向目标命令：到达时间封顶为目标命令到达时间。
            const std::string target_id = command.value("withdraw_command_id", std::string());
            const ChainCommand* target = nullptr;
            if (!target_id.empty()) {
                target = Find(target_id);
            } else {
                for (const ChainCommand& entry : commands_) {
                    if (entry.unit_id == parent.unit_id &&
                        (entry.state == CommandState::kIssued || entry.state == CommandState::kAcknowledged ||
                         entry.state == CommandState::kEffective)) {
                        target = &entry;  // 最后一条在途命令（下达顺序 = seq 升序）。
                    }
                }
            }
            if (target != nullptr && config.withdraw_catches_up) {
                parent.arrival_tick = std::min(parent.arrival_tick, target->arrival_tick);
            }
            parent.target_command_id = target == nullptr ? std::string() : target->command_id;
        }
        commands_.push_back(std::move(parent));
    }

    result.accepted = true;
    result.delay_ticks = delay;
    result.arrival_tick = commands_.back().arrival_tick;
    return result;
}
// NOLINTEND(bugprone-easily-swappable-parameters, readability-function-cognitive-complexity)

const ChainCommand* CommandChain::Find(const std::string& command_id) const {
    for (const ChainCommand& command : commands_) {
        if (command.command_id == command_id) {
            return &command;
        }
    }
    return nullptr;
}

ChainCommand* CommandChain::FindMutable(const std::string& command_id) {
    for (ChainCommand& command : commands_) {
        if (command.command_id == command_id) {
            return &command;
        }
    }
    return nullptr;
}

void CommandChain::MarkCompleted(const std::string& command_id) {
    if (ChainCommand* command = FindMutable(command_id)) {
        command->state = CommandState::kCompleted;
    }
}

void CommandChain::MarkTimedOut(SimState& state, const std::string& command_id) {
    ChainCommand* command = FindMutable(command_id);
    if (command == nullptr) {
        return;
    }
    command->state = CommandState::kCompleted;
    Log(state, EventCategory::kMission, EventSeverity::kWarning,
        "MISSION_TIMED_OUT unit=" + command->unit_id + " command=" + command->command_id + " type=" + command->type);
    if (RuntimeUnitState* unit = FindUnit(state, command->unit_id)) {
        CancelMission(*unit);
    }
}

// 到期处理是固定顺序的事务链（收集→单兵条件→优先级裁决→取代→生效→批量
// 汇总→时限），拆分会破坏同一 tick 内裁决的原子性审查。
// NOLINTBEGIN(readability-function-cognitive-complexity)
void CommandChain::ProcessDue(SimState& state) {
    // 1) 收集本 tick 到期（通讯延迟结束）的命令；按单位分组处理，保证
    //    同一到达 tick 内按（优先级, 序列号）统一裁决（FR-041/045）。
    std::vector<ChainCommand*> arrivals;
    for (ChainCommand& command : commands_) {
        if (command.state == CommandState::kIssued && command.arrival_tick <= state.clock.tick()) {
            arrivals.push_back(&command);
        }
    }
    if (arrivals.empty()) {
        // 2) 时限检查：生效任务超过时限 → 超时终止（FR-045）。
        for (ChainCommand& command : commands_) {
            if (command.state == CommandState::kEffective && command.arrival_tick <= state.clock.tick()) {
                RuntimeUnitState* unit = FindUnit(state, command.unit_id);
                if (unit != nullptr && unit->mission_active && unit->mission_deadline_ticks > 0U &&
                    state.clock.tick() > unit->mission_deadline_ticks) {
                    MarkTimedOut(state, command.command_id);
                }
            }
        }
        return;
    }

    // 批处理：父命令只负责汇总，子命令按单位参与裁决。
    for (ChainCommand* command : arrivals) {
        if (command->batch && command->unit_id.empty()) {
            continue;  // 父命令：在子命令裁决后统一汇总。
        }
        RuntimeUnitState* unit = FindUnit(state, command->unit_id);
        if (unit == nullptr) {
            command->state = CommandState::kRejected;
            LogRejected(state, *command, "TARGET_NOT_FOUND");
            continue;
        }

        // 单兵条件检查（批量部分接受，FR-045）：弹药覆盖必须存在。
        const nlohmann::json behavior = command->payload.value("behavior", nlohmann::json::object());
        if (behavior.is_object() && behavior.contains("ammo_override")) {
            const std::string ammo_id = behavior["ammo_override"].get<std::string>();
            if (!unit->ammo.contains(ammo_id)) {
                command->state = CommandState::kRejected;
                LogRejected(state, *command, "AMMO_NOT_FOUND");
                continue;
            }
        }

        // 当前已生效命令（先前到达）；同 tick 多命令按（优先级, 序列号）取胜者。
        const std::string active_id = unit->mission_active ? unit->mission_command_id : std::string();
        ChainCommand* active = active_id.empty() ? nullptr : FindMutable(active_id);
        if (active != nullptr && active->state != CommandState::kEffective) {
            active = nullptr;
        }

        // 收集同一单位同一到达 tick 的竞争者。
        std::vector<ChainCommand*> competitors;
        for (ChainCommand* candidate : arrivals) {
            if (candidate->unit_id == command->unit_id && !(candidate->batch && candidate->unit_id.empty())) {
                competitors.push_back(candidate);
            }
        }

        // 与已生效命令的优先级比较：更低优先级的新命令直接拒绝。
        for (ChainCommand* candidate : competitors) {
            if (active != nullptr && candidate->priority < active->priority) {
                candidate->state = CommandState::kRejected;
                LogRejected(state, *candidate,
                            "LOWER_PRIORITY 已生效命令优先级更高 (priority=" + std::to_string(active->priority) + ")");
            }
        }

        // 同 tick 竞争裁决：优先级降序、序列号升序取唯一胜者。
        ChainCommand* winner = nullptr;
        for (ChainCommand* candidate : competitors) {
            if (candidate->state != CommandState::kIssued) {
                continue;
            }
            if (winner == nullptr || candidate->priority > winner->priority ||
                (candidate->priority == winner->priority && candidate->seq < winner->seq)) {
                winner = candidate;
            }
        }
        if (winner == nullptr) {
            continue;
        }
        for (ChainCommand* loser : competitors) {
            // 撤回/修改的指向目标由元命令处理（取代/撤回），不在此处判败。
            const bool is_meta_target = IsMetaType(winner->type) && winner->target_command_id == loser->command_id;
            if (loser != winner && loser->state == CommandState::kIssued && !is_meta_target) {
                loser->state = CommandState::kRejected;
                LogRejected(state, *loser, "仲裁落败（优先级/序列号） winner=" + winner->command_id);
            }
        }

        // 已生效命令被新命令取代（FR-041：新命令取代旧命令并给出可见提示）。
        if (active != nullptr && active != winner && active->state == CommandState::kEffective &&
            winner->priority >= active->priority) {
            active->state = CommandState::kSuperseded;
            Log(state, EventCategory::kCommand, EventSeverity::kInfo,
                "COMMAND_SUPERSEDED command=" + active->command_id + " winner=" + winner->command_id +
                    " unit=" + active->unit_id + " reason=新命令取代旧命令");
            if (RuntimeUnitState* active_unit = FindUnit(state, active->unit_id)) {
                CancelMission(*active_unit);
            }
        }

        // 胜者生效。
        if (winner->state == CommandState::kIssued) {
            winner->state = CommandState::kAcknowledged;
            LogAcknowledged(state, *winner);
            if (IsMetaType(winner->type)) {
                ChainCommand* target =
                    winner->target_command_id.empty() ? nullptr : FindMutable(winner->target_command_id);
                if (target == nullptr) {
                    winner->state = CommandState::kRejected;
                    LogRejected(state, *winner, "TARGET_COMMAND_NOT_FOUND");
                    continue;
                }
                if (winner->type == "WITHDRAW_COMMAND") {
                    const bool was_effective = target->state == CommandState::kEffective;
                    target->state = CommandState::kWithdrawn;
                    Log(state, EventCategory::kCommand, EventSeverity::kInfo,
                        "COMMAND_WITHDRAWN command=" + target->command_id + " unit=" + target->unit_id +
                            " type=" + target->type + " priority=" + std::to_string(target->priority));
                    if (was_effective && unit->mission_active) {
                        CancelMission(*unit);
                    }
                    winner->state = CommandState::kCompleted;
                } else {
                    const nlohmann::json replacement = winner->payload.at("replace_with");
                    target->state = CommandState::kSuperseded;
                    Log(state, EventCategory::kCommand, EventSeverity::kInfo,
                        "COMMAND_SUPERSEDED command=" + target->command_id + " winner=" + winner->command_id +
                            " unit=" + target->unit_id + " reason=生效前修改取代原命令");
                    target->payload = replacement;
                    target->type = replacement.at("type").get<std::string>();
                    target->priority = replacement.at("priority").get<std::int64_t>();
                    Log(state, EventCategory::kCommand, EventSeverity::kInfo,
                        "COMMAND_MODIFIED command=" + target->command_id + " unit=" + target->unit_id +
                            " type=" + target->type);
                    nlohmann::json effective = replacement;
                    effective["command_id"] = winner->command_id;
                    ApplyMission(*unit, effective, state.movement_config);
                    winner->state = CommandState::kEffective;
                }
            } else {
                winner->state = CommandState::kEffective;
                nlohmann::json effective = winner->payload;
                effective["command_id"] = winner->command_id;
                ApplyMission(*unit, effective, state.movement_config);
            }
        }
    }

    // 批量父命令汇总：任一子命令接受 → 部分接受；全部拒绝 → 父命令拒绝。
    for (ChainCommand& parent : commands_) {
        if (!parent.batch || !parent.unit_id.empty() || parent.state != CommandState::kIssued) {
            continue;
        }
        if (parent.arrival_tick > state.clock.tick()) {
            continue;
        }
        std::vector<std::string> accepted;
        std::vector<std::string> rejected;
        for (const ChainCommand& child : commands_) {
            if (child.parent_command_id != parent.command_id) {
                continue;
            }
            if (child.state == CommandState::kEffective || child.state == CommandState::kAcknowledged) {
                accepted.push_back(child.unit_id);
            } else if (child.state == CommandState::kRejected) {
                rejected.push_back(child.unit_id);
            }
        }
        if (accepted.empty() && rejected.empty()) {
            parent.state = CommandState::kRejected;
            LogRejected(state, parent, "NO_UNITS");
        } else {
            parent.state = accepted.empty() ? CommandState::kRejected : CommandState::kCompleted;
            std::string accepted_text = "[";
            for (std::size_t i = 0U; i < accepted.size(); ++i) {
                if (i > 0U) {
                    accepted_text += ",";
                }
                accepted_text += accepted[i];
            }
            accepted_text += "]";
            std::string rejected_text = "[";
            for (std::size_t i = 0U; i < rejected.size(); ++i) {
                if (i > 0U) {
                    rejected_text += ",";
                }
                rejected_text += rejected[i];
            }
            rejected_text += "]";
            std::string batch_message = "BATCH_PARTIAL_ACCEPT command=";
            batch_message += parent.command_id;
            batch_message += " accepted=";
            batch_message += accepted_text;
            batch_message += " rejected=";
            batch_message += rejected_text;
            Log(state, EventCategory::kCommand, EventSeverity::kInfo, std::move(batch_message));
        }
    }

    // 时限检查（与空到达路径共用）。
    for (ChainCommand& command : commands_) {
        if (command.state == CommandState::kEffective && command.arrival_tick <= state.clock.tick()) {
            RuntimeUnitState* unit = FindUnit(state, command.unit_id);
            if (unit != nullptr && unit->mission_active && unit->mission_deadline_ticks > 0U &&
                state.clock.tick() > unit->mission_deadline_ticks) {
                MarkTimedOut(state, command.command_id);
            }
        }
    }
}
// NOLINTEND(readability-function-cognitive-complexity)

}  // namespace wfs::sim
