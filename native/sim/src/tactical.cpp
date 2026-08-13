// sim/src/tactical.cpp
//
// T048 RED 脚手架：接口契约已定（tactical.h），实现待 T048 提交替换。
// 当前全部显式失败（宪法第 17 条：不静默吞错），支撑 T046 测试先行。

#include "wfs/sim/tactical.h"

#include <stdexcept>

namespace wfs::sim {

std::string_view to_string(const TacticalKind) noexcept { return "UNKNOWN"; }
TacticalKind tactical_kind_from_string(const std::string_view) {
    throw std::invalid_argument("T048 RED 脚手架：功能组类型解析未实现");
}
std::string_view to_string(const TacticalState) noexcept { return "UNKNOWN"; }
TacticalState tactical_state_from_string(const std::string_view) {
    throw std::invalid_argument("T048 RED 脚手架：战术状态解析未实现");
}

SplitResult TacticalRegistry::SplitSquad(const std::string&, const std::vector<std::string>&, std::size_t) {
    return SplitResult{false, {}, "T048 RED 脚手架：拆分未实现"};
}
MergeResult TacticalRegistry::MergeDepletedSquads(const std::string&, const std::string&,
                                                  const std::vector<std::string>&) {
    return MergeResult{false, {}, "T048 RED 脚手架：合并未实现"};
}
bool TacticalRegistry::DissolveFireTeams(const std::string&) { return false; }
bool TacticalRegistry::FormTaskForce(TacticalTaskForce) { return false; }
bool TacticalRegistry::DissolveTaskForce(const std::string&) { return false; }
CommandScopeResult TacticalRegistry::ResolveCommandScope(const std::string&) const {
    return CommandScopeResult{false, "", "T048 RED 脚手架：作用域裁决未实现"};
}
bool TacticalRegistry::IsSplit(const std::string&) const { return false; }
const FireTeam* TacticalRegistry::FindFireTeam(const std::string&) const { return nullptr; }
const TacticalTaskForce* TacticalRegistry::FindTaskForce(const std::string&) const { return nullptr; }
std::vector<std::string> TacticalRegistry::ActiveFireTeamIds(const std::string&) const { return {}; }
void TacticalRegistry::Clear() noexcept {
    fire_teams_.clear();
    task_forces_.clear();
}

ReturnToParentResult compute_return_to_parent(const TacticalTaskForce&, const std::vector<std::string>&) {
    return ReturnToParentResult{false, {}, {}, "T048 RED 脚手架：归建结算未实现"};
}

void to_json(nlohmann::json&, const FireTeam&) {}
void from_json(const nlohmann::json&, FireTeam&) {}
void to_json(nlohmann::json&, const TacticalTaskForce&) {}
void from_json(const nlohmann::json&, TacticalTaskForce&) {}
void to_json(nlohmann::json&, const TacticalRegistry&) {}
void from_json(const nlohmann::json&, TacticalRegistry&) {}

}  // namespace wfs::sim
