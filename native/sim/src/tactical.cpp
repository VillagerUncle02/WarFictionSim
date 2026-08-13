// sim/src/tactical.cpp
//
// T048：战术分队/火力组模型实现。
//
// 实现策略：
// - 拆分/合并/归建全部为纯确定性计算（宪法第 7 条）：拆分按
//   "基数 = 成员数 / 组数，余数依次补入前面的组"固定分配；不读时钟、不抽
//   随机数。战术编成只登记火力组/分队记录，不改写行政编制（FR-010）。
// - 命令作用域（FR-010/045）：火力组 id 放行；已拆分行政班 id 拒绝
//   （SPLIT_SQUAD_COMMAND_NOT_ALLOWED）；其余原单位放行。解除战术编成后
//   整班命令恢复。
// - 归建结算只归建存活成员，损失/失联成员进入伤亡记录（FR-010/044）；
//   非法输入（重复成员/空 id）以结构化错误返回，不抛异常（与既有
//   "错误码不抛异常"策略一致）。
// - 解散记录保留并标记 kDissolved（存档/复盘可见），IsSplit/ActiveFireTeamIds
//   只统计活跃编成，保证"恢复行政编制"语义与可观测历史兼得。

#include "wfs/sim/tactical.h"

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

bool HasDuplicate(const std::vector<std::string>& ids) {
    std::set<std::string> seen;
    for (const std::string& id : ids) {
        if (!seen.insert(id).second) {
            return true;
        }
    }
    return false;
}

bool IsActive(const FireTeam& team) {
    return team.state == TacticalState::kActive;
}
bool IsActive(const TacticalTaskForce& force) {
    return force.state == TacticalState::kActive;
}

}  // namespace

std::string_view to_string(const TacticalKind kind) noexcept {
    switch (kind) {
        case TacticalKind::kFireTeam:
            return "fire_team";
        case TacticalKind::kBreachTeam:
            return "breach_team";
        case TacticalKind::kAssaultTeam:
            return "assault_team";
        case TacticalKind::kSupportTeam:
            return "support_team";
    }
    return "unknown";
}

TacticalKind tactical_kind_from_string(const std::string_view name) {
    if (name == "fire_team") {
        return TacticalKind::kFireTeam;
    }
    if (name == "breach_team") {
        return TacticalKind::kBreachTeam;
    }
    if (name == "assault_team") {
        return TacticalKind::kAssaultTeam;
    }
    if (name == "support_team") {
        return TacticalKind::kSupportTeam;
    }
    throw std::invalid_argument("未知功能组类型: " + std::string(name));
}

std::string_view to_string(const TacticalState state) noexcept {
    switch (state) {
        case TacticalState::kActive:
            return "active";
        case TacticalState::kDissolved:
            return "dissolved";
    }
    return "unknown";
}

TacticalState tactical_state_from_string(const std::string_view name) {
    if (name == "active") {
        return TacticalState::kActive;
    }
    if (name == "dissolved") {
        return TacticalState::kDissolved;
    }
    throw std::invalid_argument("未知战术编成状态: " + std::string(name));
}

SplitResult TacticalRegistry::SplitSquad(const std::string& squad_id, const std::vector<std::string>& soldier_ids,
                                         const std::size_t team_count) {
    if (squad_id.empty()) {
        return SplitResult{false, {}, "班组 id 为空"};
    }
    if (soldier_ids.empty() || HasDuplicate(soldier_ids)) {
        return SplitResult{false, {}, "成员清单为空或包含重复成员"};
    }
    if (team_count == 0U || team_count > soldier_ids.size()) {
        return SplitResult{false, {}, "火力组数量必须介于 1 与成员数之间"};
    }
    if (IsSplit(squad_id)) {
        return SplitResult{false, {}, "班组已拆分，不得重复拆分: " + squad_id};
    }

    // 确定性分配：基数 + 余数依次补入前面的组（同输入必然同输出）。
    const std::size_t base = soldier_ids.size() / team_count;
    const std::size_t remainder = soldier_ids.size() % team_count;
    SplitResult result;
    result.ok = true;
    std::size_t offset = 0U;
    for (std::size_t team_index = 0U; team_index < team_count; ++team_index) {
        const std::size_t team_size = base + (team_index < remainder ? 1U : 0U);
        FireTeam team;
        team.id = "ft-" + squad_id + "-" + std::to_string(team_index);
        team.admin_squad_id = squad_id;
        team.kind = TacticalKind::kFireTeam;
        team.soldier_ids.assign(soldier_ids.begin() + static_cast<std::ptrdiff_t>(offset),
                                soldier_ids.begin() + static_cast<std::ptrdiff_t>(offset + team_size));
        result.teams.push_back(std::move(team));
        offset += team_size;
    }
    for (const FireTeam& team : result.teams) {
        fire_teams_.push_back(team);
    }
    return result;
}

MergeResult TacticalRegistry::MergeDepletedSquads(const std::string& merged_id, const std::string& admin_squad_id,
                                                  const std::vector<std::string>& soldier_ids) {
    if (merged_id.empty() || admin_squad_id.empty()) {
        return MergeResult{false, {}, "合并 id/行政班 id 为空"};
    }
    if (soldier_ids.empty() || HasDuplicate(soldier_ids)) {
        return MergeResult{false, {}, "合并成员清单为空或包含重复成员"};
    }
    if (FindFireTeam(merged_id) != nullptr) {
        return MergeResult{false, {}, "火力组 id 已存在: " + merged_id};
    }

    FireTeam merged;
    merged.id = merged_id;
    merged.admin_squad_id = admin_squad_id;
    merged.kind = TacticalKind::kFireTeam;
    merged.soldier_ids = soldier_ids;
    MergeResult result;
    result.ok = true;
    result.merged = merged;
    fire_teams_.push_back(std::move(merged));
    return result;
}

bool TacticalRegistry::DissolveFireTeams(const std::string& squad_id) {
    if (squad_id.empty()) {
        return false;
    }
    // 解除战术编成、恢复行政编制：全部活跃火力组标记解散（保留记录）。
    for (FireTeam& team : fire_teams_) {
        if (team.admin_squad_id == squad_id && IsActive(team)) {
            team.state = TacticalState::kDissolved;
        }
    }
    return true;
}

bool TacticalRegistry::FormTaskForce(TacticalTaskForce force) {
    if (force.id.empty() || force.task_id.empty() || force.member_unit_ids.empty()) {
        return false;
    }
    if (FindTaskForce(force.id) != nullptr) {
        return false;
    }
    if (HasDuplicate(force.member_unit_ids)) {
        return false;
    }
    task_forces_.push_back(std::move(force));
    return true;
}

bool TacticalRegistry::DissolveTaskForce(const std::string& task_force_id) {
    for (TacticalTaskForce& force : task_forces_) {
        if (force.id == task_force_id && IsActive(force)) {
            force.state = TacticalState::kDissolved;
            return true;
        }
    }
    return false;
}

CommandScopeResult TacticalRegistry::ResolveCommandScope(const std::string& unit_id) const {
    if (unit_id.empty()) {
        return CommandScopeResult{false, "", "目标单位为空"};
    }
    if (FindFireTeam(unit_id) != nullptr) {
        // 火力组是拆分状态下的合法命令作用域（FR-010）。
        return CommandScopeResult{true, unit_id, ""};
    }
    if (IsSplit(unit_id)) {
        // 拆分状态下不对行政班组整体下发命令（FR-010/045，宪法 17 显式反馈）。
        return CommandScopeResult{false, "", "SPLIT_SQUAD_COMMAND_NOT_ALLOWED"};
    }
    return CommandScopeResult{true, unit_id, ""};
}

bool TacticalRegistry::IsSplit(const std::string& squad_id) const {
    return std::any_of(fire_teams_.begin(), fire_teams_.end(),
                       [&](const FireTeam& team) { return team.admin_squad_id == squad_id && IsActive(team); });
}

const FireTeam* TacticalRegistry::FindFireTeam(const std::string& id) const {
    for (const FireTeam& team : fire_teams_) {
        if (team.id == id) {
            return &team;
        }
    }
    return nullptr;
}

const TacticalTaskForce* TacticalRegistry::FindTaskForce(const std::string& id) const {
    for (const TacticalTaskForce& force : task_forces_) {
        if (force.id == id) {
            return &force;
        }
    }
    return nullptr;
}

std::vector<std::string> TacticalRegistry::ActiveFireTeamIds(const std::string& squad_id) const {
    std::vector<std::string> ids;
    for (const FireTeam& team : fire_teams_) {
        if (team.admin_squad_id == squad_id && IsActive(team)) {
            ids.push_back(team.id);
        }
    }
    return ids;
}

void TacticalRegistry::Clear() noexcept {
    fire_teams_.clear();
    task_forces_.clear();
}

ReturnToParentResult compute_return_to_parent(const TacticalTaskForce& force,
                                              const std::vector<std::string>& active_member_ids) {
    if (force.id.empty()) {
        return ReturnToParentResult{false, {}, {}, "战术分队 id 为空"};
    }
    if (HasDuplicate(active_member_ids)) {
        return ReturnToParentResult{false, {}, {}, "存活成员清单包含重复成员"};
    }
    const std::set<std::string> alive(active_member_ids.begin(), active_member_ids.end());
    ReturnToParentResult result;
    result.ok = true;
    for (const std::string& member : force.member_unit_ids) {
        // 仅存活成员归建；损失/失联成员进入伤亡记录（FR-010/044）。
        if (alive.contains(member)) {
            result.returned_unit_ids.push_back(member);
        } else {
            result.casualty_unit_ids.push_back(member);
        }
    }
    return result;
}

void to_json(nlohmann::json& json, const FireTeam& team) {
    json = nlohmann::json{{"id", team.id},
                          {"admin_squad_id", team.admin_squad_id},
                          {"soldier_ids", team.soldier_ids},
                          {"kind", to_string(team.kind)},
                          {"state", to_string(team.state)}};
}

void from_json(const nlohmann::json& json, FireTeam& team) {
    team.id = json.at("id").get<std::string>();
    team.admin_squad_id = json.at("admin_squad_id").get<std::string>();
    team.soldier_ids = json.at("soldier_ids").get<std::vector<std::string>>();
    team.kind = tactical_kind_from_string(json.at("kind").get<std::string>());
    team.state = tactical_state_from_string(json.at("state").get<std::string>());
    if (team.id.empty() || HasDuplicate(team.soldier_ids)) {
        throw std::invalid_argument("火力组字段非法: " + team.id);
    }
}

void to_json(nlohmann::json& json, const TacticalTaskForce& force) {
    json = nlohmann::json{{"id", force.id},
                          {"task_id", force.task_id},
                          {"member_unit_ids", force.member_unit_ids},
                          {"state", to_string(force.state)}};
}

void from_json(const nlohmann::json& json, TacticalTaskForce& force) {
    force.id = json.at("id").get<std::string>();
    force.task_id = json.at("task_id").get<std::string>();
    force.member_unit_ids = json.at("member_unit_ids").get<std::vector<std::string>>();
    force.state = tactical_state_from_string(json.at("state").get<std::string>());
    if (force.id.empty() || HasDuplicate(force.member_unit_ids)) {
        throw std::invalid_argument("战术分队字段非法: " + force.id);
    }
}

void to_json(nlohmann::json& json, const TacticalRegistry& registry) {
    json = nlohmann::json{{"fire_teams", registry.FireTeamsInInsertionOrder()},
                          {"task_forces", registry.TaskForcesInInsertionOrder()}};
}

void from_json(const nlohmann::json& json, TacticalRegistry& registry) {
    TacticalRegistry candidate;
    for (const nlohmann::json& team_json : json.at("fire_teams")) {
        const FireTeam team = team_json.get<FireTeam>();
        if (candidate.FindFireTeam(team.id) != nullptr) {
            throw std::invalid_argument("战术登记表包含重复火力组 id: " + team.id);
        }
        candidate.fire_teams_.push_back(team);
    }
    for (const nlohmann::json& force_json : json.at("task_forces")) {
        const TacticalTaskForce force = force_json.get<TacticalTaskForce>();
        if (candidate.FindTaskForce(force.id) != nullptr) {
            throw std::invalid_argument("战术登记表包含重复分队 id: " + force.id);
        }
        candidate.task_forces_.push_back(force);
    }
    registry = std::move(candidate);
}

}  // namespace wfs::sim
