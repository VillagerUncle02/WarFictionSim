// sim/src/mission_registry.cpp
//
// T028：任务模型注册表与状态机数据表实现。
//
// 实现策略：
// - 13 种任务类型按固定顺序登记（data-model.md §11，与 command_validation
//   内置类型表一致）；注册表是命令校验、任务判定（T034）与侦察判定（T035）
//   的单一事实来源，后续可扩展为数据驱动（SC-010：新增类型不改判定逻辑）。
// - MissionTypeSpec 数据表承载 持续任务/侦察类 标识；任务状态机按 FR-044
//   登记 下达→执行→完成/失败/超时/取消 与超时后的 继续/取消/判失败。
// - 名称映射使用稳定字符串（MOVE/PATROL/...、ISSUED/EXECUTING/...），
//   未知名称显式抛 std::invalid_argument（宪法第 17 条，不静默吞错）；
//   所有查找顺序固定，同一输入必然产生同一结果（宪法第 7 条）。

#include "wfs/sim/model/mission.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace wfs::sim::model {

namespace {

// 13 种任务类型注册表（固定顺序 = 序列化与判定的权威顺序）。
const std::array<MissionTypeSpec, 13> kMissionTypeSpecs = {{
    {MissionType::kMove, "MOVE", false, false},
    {MissionType::kPatrol, "PATROL", true, false},
    {MissionType::kAttack, "ATTACK", false, false},
    {MissionType::kDefend, "DEFEND", false, false},
    {MissionType::kSecureZone, "SECURE_ZONE", false, false},
    {MissionType::kClear, "CLEAR", false, false},
    {MissionType::kDriveOut, "DRIVE_OUT", false, false},
    {MissionType::kFortify, "FORTIFY", false, false},
    {MissionType::kHiddenRecon, "HIDDEN_RECON", false, true},
    {MissionType::kInfiltrateRecon, "INFILTRATE_RECON", false, true},
    {MissionType::kObservationPost, "OBSERVATION_POST", true, true},
    {MissionType::kFireRecon, "FIRE_RECON", false, true},
    {MissionType::kSupportRequest, "SUPPORT_REQUEST", false, false},
}};

template <typename EnumType>
EnumType MatchEnum(const std::string_view name, const std::vector<std::pair<std::string_view, EnumType>>& table,
                   const char* kind) {
    for (const auto& [stable_name, value] : table) {
        if (stable_name == name) {
            return value;
        }
    }
    throw std::invalid_argument(std::string(kind) + " 名称未知: " + std::string(name));
}

}  // namespace

std::string_view to_string(const MissionType type) noexcept {
    const auto found = std::find_if(kMissionTypeSpecs.begin(), kMissionTypeSpecs.end(),
                                    [type](const MissionTypeSpec& spec) { return spec.type == type; });
    return found == kMissionTypeSpecs.end() ? "UNKNOWN" : found->name;
}

MissionType mission_type_from_string(const std::string_view name) {
    for (const MissionTypeSpec& spec : kMissionTypeSpecs) {
        if (spec.name == name) {
            return spec.type;
        }
    }
    throw std::invalid_argument("未知任务类型: " + std::string(name));
}

std::string_view to_string(const MissionState state) noexcept {
    switch (state) {
        case MissionState::kIssued:
            return "ISSUED";
        case MissionState::kExecuting:
            return "EXECUTING";
        case MissionState::kCompleted:
            return "COMPLETED";
        case MissionState::kFailed:
            return "FAILED";
        case MissionState::kTimedOut:
            return "TIMED_OUT";
        case MissionState::kCancelled:
            return "CANCELLED";
    }
    return "UNKNOWN";
}

MissionState mission_state_from_string(const std::string_view name) {
    static const std::vector<std::pair<std::string_view, MissionState>> table = {
        {"ISSUED", MissionState::kIssued},       {"EXECUTING", MissionState::kExecuting},
        {"COMPLETED", MissionState::kCompleted}, {"FAILED", MissionState::kFailed},
        {"TIMED_OUT", MissionState::kTimedOut},  {"CANCELLED", MissionState::kCancelled},
    };
    return MatchEnum(name, table, "任务状态");
}

std::string_view to_string(const EngagementPolicy policy) noexcept {
    switch (policy) {
        case EngagementPolicy::kAggressive:
            return "aggressive";
        case EngagementPolicy::kBalanced:
            return "balanced";
        case EngagementPolicy::kCautious:
            return "cautious";
    }
    return "unknown";
}

EngagementPolicy engagement_policy_from_string(const std::string_view name) {
    static const std::vector<std::pair<std::string_view, EngagementPolicy>> table = {
        {"aggressive", EngagementPolicy::kAggressive},
        {"balanced", EngagementPolicy::kBalanced},
        {"cautious", EngagementPolicy::kCautious},
    };
    return MatchEnum(name, table, "接敌策略");
}

std::string_view to_string(const CautionLevel level) noexcept {
    switch (level) {
        case CautionLevel::kAggressive:
            return "aggressive";
        case CautionLevel::kStandard:
            return "standard";
        case CautionLevel::kConservative:
            return "conservative";
    }
    return "unknown";
}

CautionLevel caution_level_from_string(const std::string_view name) {
    static const std::vector<std::pair<std::string_view, CautionLevel>> table = {
        {"aggressive", CautionLevel::kAggressive},
        {"standard", CautionLevel::kStandard},
        {"conservative", CautionLevel::kConservative},
    };
    return MatchEnum(name, table, "谨慎程度");
}

std::string_view to_string(const AmmoPolicy policy) noexcept {
    switch (policy) {
        case AmmoPolicy::kAuto:
            return "auto";
        case AmmoPolicy::kSpecific:
            return "specific";
    }
    return "unknown";
}

AmmoPolicy ammo_policy_from_string(const std::string_view name) {
    static const std::vector<std::pair<std::string_view, AmmoPolicy>> table = {
        {"auto", AmmoPolicy::kAuto},
        {"specific", AmmoPolicy::kSpecific},
    };
    return MatchEnum(name, table, "弹药选择策略");
}

std::string_view to_string(const FailureAction action) noexcept {
    switch (action) {
        case FailureAction::kWithdrawTo:
            return "withdraw_to";
        case FailureAction::kHold:
            return "hold";
        case FailureAction::kReport:
            return "report";
    }
    return "unknown";
}

FailureAction failure_action_from_string(const std::string_view name) {
    static const std::vector<std::pair<std::string_view, FailureAction>> table = {
        {"withdraw_to", FailureAction::kWithdrawTo},
        {"hold", FailureAction::kHold},
        {"report", FailureAction::kReport},
    };
    return MatchEnum(name, table, "失败后处置");
}

void to_json(nlohmann::json& json, const MissionType type) {
    json = to_string(type);
}

void from_json(const nlohmann::json& json, MissionType& type) {
    type = mission_type_from_string(json.get<std::string>());
}

void to_json(nlohmann::json& json, const MissionState state) {
    json = to_string(state);
}

void from_json(const nlohmann::json& json, MissionState& state) {
    state = mission_state_from_string(json.get<std::string>());
}

void to_json(nlohmann::json& json, const EngagementPolicy policy) {
    json = to_string(policy);
}

void from_json(const nlohmann::json& json, EngagementPolicy& policy) {
    policy = engagement_policy_from_string(json.get<std::string>());
}

void to_json(nlohmann::json& json, const CautionLevel level) {
    json = to_string(level);
}

void from_json(const nlohmann::json& json, CautionLevel& level) {
    level = caution_level_from_string(json.get<std::string>());
}

void to_json(nlohmann::json& json, const AmmoPolicy policy) {
    json = to_string(policy);
}

void from_json(const nlohmann::json& json, AmmoPolicy& policy) {
    policy = ammo_policy_from_string(json.get<std::string>());
}

void to_json(nlohmann::json& json, const FailureAction action) {
    json = to_string(action);
}

void from_json(const nlohmann::json& json, FailureAction& action) {
    action = failure_action_from_string(json.get<std::string>());
}

void to_json(nlohmann::json& json, const MissionTarget& target) {
    json = nlohmann::json{{"kind", target.kind}, {"ref", target.ref}};
}

void from_json(const nlohmann::json& json, MissionTarget& target) {
    target.kind = json.at("kind").get<std::string>();
    target.ref = json.at("ref").get<std::string>();
}

void to_json(nlohmann::json& json, const MissionBehavior& behavior) {
    json = nlohmann::json{{"engagement", behavior.engagement},         {"caution", behavior.caution},
                          {"ammo_policy", behavior.ammo_policy},       {"ammo_override", behavior.ammo_override},
                          {"failure_action", behavior.failure_action}, {"failure_target", behavior.failure_target}};
}

void from_json(const nlohmann::json& json, MissionBehavior& behavior) {
    behavior.engagement = json.at("engagement").get<EngagementPolicy>();
    behavior.caution = json.at("caution").get<CautionLevel>();
    behavior.ammo_policy = json.at("ammo_policy").get<AmmoPolicy>();
    behavior.ammo_override = json.at("ammo_override").get<std::string>();
    behavior.failure_action = json.at("failure_action").get<FailureAction>();
    behavior.failure_target = json.at("failure_target").get<std::string>();
}

void to_json(nlohmann::json& json, const Mission& mission) {
    json = nlohmann::json{{"id", mission.id},
                          {"type", mission.type},
                          {"target", mission.target},
                          {"completion_condition", mission.completion_condition},
                          {"completion_params", mission.completion_params},
                          {"intent", mission.intent},
                          {"behavior", mission.behavior},
                          {"priority", mission.priority},
                          {"deadline_ticks", mission.deadline_ticks},
                          {"state", mission.state},
                          {"continuous", mission.continuous},
                          {"loops", mission.loops}};
}

void from_json(const nlohmann::json& json, Mission& mission) {
    mission.id = json.at("id").get<std::string>();
    mission.type = json.at("type").get<MissionType>();
    mission.target = json.at("target").get<MissionTarget>();
    mission.completion_condition = json.at("completion_condition").get<std::string>();
    mission.completion_params = json.at("completion_params");
    mission.intent = json.at("intent").get<std::string>();
    mission.behavior = json.at("behavior").get<MissionBehavior>();
    mission.priority = json.at("priority").get<std::int64_t>();
    mission.deadline_ticks = json.at("deadline_ticks").get<std::uint64_t>();
    mission.state = json.at("state").get<MissionState>();
    mission.continuous = json.at("continuous").get<bool>();
    mission.loops = json.at("loops").get<bool>();
    if (!mission.is_valid()) {
        throw std::invalid_argument("任务 continuous 与注册表不一致: " + mission.id);
    }
}

const std::vector<MissionType>& registered_mission_types() {
    static const std::vector<MissionType> types = [] {
        std::vector<MissionType> result;
        result.reserve(kMissionTypeSpecs.size());
        for (const MissionTypeSpec& spec : kMissionTypeSpecs) {
            result.push_back(spec.type);
        }
        return result;
    }();
    return types;
}

bool is_registered_mission_type(const std::string_view name) {
    return std::ranges::any_of(kMissionTypeSpecs, [name](const MissionTypeSpec& spec) { return spec.name == name; });
}

const MissionTypeSpec& mission_type_spec(const MissionType type) {
    const auto found = std::find_if(kMissionTypeSpecs.begin(), kMissionTypeSpecs.end(),
                                    [type](const MissionTypeSpec& spec) { return spec.type == type; });
    if (found == kMissionTypeSpecs.end()) {
        throw std::invalid_argument("任务类型未注册");
    }
    return *found;
}

bool is_continuous_mission(const MissionType type) {
    return mission_type_spec(type).continuous;
}

bool is_recon_mission(const MissionType type) {
    return mission_type_spec(type).recon;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
bool can_transition(const MissionState from_state, const MissionState target_state) noexcept {
    const std::span<const MissionState> allowed = transitions_from(from_state);
    return std::find(allowed.begin(), allowed.end(), target_state) != allowed.end();
}

std::span<const MissionState> transitions_from(const MissionState state) noexcept {
    // FR-044：下达后进入执行或取消；执行中可完成/失败/超时/取消；
    // 超时后由指挥官决定 继续（回执行）/判失败/取消；终态不可再转移。
    // 持续任务循环（COMPLETED→EXECUTING）不进入本状态机表：完成是终态，
    // 循环由 T034 执行器按 Mission.loops 在表外重新下发（F2）。
    static constexpr std::array kIssued = {MissionState::kExecuting, MissionState::kCancelled};
    static constexpr std::array kExecuting = {MissionState::kCompleted, MissionState::kFailed, MissionState::kTimedOut,
                                              MissionState::kCancelled};
    static constexpr std::array kTimedOut = {MissionState::kExecuting, MissionState::kFailed, MissionState::kCancelled};
    static constexpr std::array<MissionState, 0> kTerminal = {};
    switch (state) {
        case MissionState::kIssued:
            return kIssued;
        case MissionState::kExecuting:
            return kExecuting;
        case MissionState::kTimedOut:
            return kTimedOut;
        case MissionState::kCompleted:
        case MissionState::kFailed:
        case MissionState::kCancelled:
            return kTerminal;
    }
    return kTerminal;
}

bool is_terminal_mission_state(const MissionState state) noexcept {
    return state == MissionState::kCompleted || state == MissionState::kFailed || state == MissionState::kCancelled;
}

}  // namespace wfs::sim::model
