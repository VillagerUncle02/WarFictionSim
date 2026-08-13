// sim/src/support.cpp
//
// T047 RED 脚手架：接口契约已定（support.h），实现待 T047 提交替换。
// 当前全部显式失败（宪法第 17 条：不静默吞错），支撑 T046 测试先行。

#include "wfs/sim/support.h"

#include <stdexcept>

namespace wfs::sim {

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
    throw std::invalid_argument("T047 RED 脚手架：状态名解析未实现: " + std::string(name));
}

bool can_transition(const SupportRequestState, const SupportRequestState) noexcept {
    return false;  // RED 脚手架：全部转移拒绝。
}

SupportRequest* SupportChain::Submit(SupportRequest) { return nullptr; }
bool SupportChain::Transition(const std::string&, const SupportRequestState) { return false; }
bool SupportChain::HasCommand(const std::string&) const { return false; }
const SupportRequest* SupportChain::Find(const std::string&) const { return nullptr; }
SupportRequest* SupportChain::FindMutable(const std::string&) { return nullptr; }

void to_json(nlohmann::json&, const SupportRequest&) {}
void from_json(const nlohmann::json&, SupportRequest&) {}
void to_json(nlohmann::json&, const SupportChain&) {}
void from_json(const nlohmann::json&, SupportChain&) {}

}  // namespace wfs::sim
