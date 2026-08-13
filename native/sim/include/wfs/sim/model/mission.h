// sim/include/wfs/sim/model/mission.h
//
// T028：任务模型（Mission/MissionType/ConditionExpr）与 13 种任务类型注册。
//
// 设计契约（data-model.md §11；FR-041/042/043/044/045）：
// - MissionType 全量 13 种：MOVE/PATROL/ATTACK/DEFEND/SECURE_ZONE/CLEAR/
//   DRIVE_OUT/FORTIFY/HIDDEN_RECON/INFILTRATE_RECON/OBSERVATION_POST/
//   FIRE_RECON/SUPPORT_REQUEST（FR-042）；注册表按固定顺序提供，可扩展
//   （SC-010：新增类型不修改既有判定逻辑）。
// - Mission 携带类型/目标/完成条件/意图/结构化行为参数/优先级/时限/状态
//   （FR-045），与 contracts/command-schema.md §1 字段一一对应；模型字段
//   将随存档/快照序列化，命名保持一致（宪法第 13 条）。
// - 状态机数据表（FR-044）：下达 → 执行中 → 完成/失败/超时/取消；超时后
//   由指挥官决定 继续/取消/判失败；持续任务循环直到被新命令取代或终止。
// - ConditionExpr 是确定性条件表达式（AND/OR/NOT + 比较），Evaluate 只
//   依赖输入上下文，无随机/时钟/无序容器，短路顺序固定（宪法第 7 条）；
//   缺失变量显式抛错（宪法第 17 条，不静默吞错）——但只在被求值路径上
//   抛错：短路分支不会被访问，因此未求值分支的缺失变量不会触发异常。
//
// 本头文件声明注册表与状态机接口；实现集中在 sim/src/mission_registry.cpp。

#pragma once

#include <cstdint>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace wfs::sim::model {

// 13 种任务类型（data-model.md §11；与 command_validation 内置类型表一致）。
enum class MissionType : std::uint8_t {
    kMove = 0,
    kPatrol = 1,
    kAttack = 2,
    kDefend = 3,
    kSecureZone = 4,
    kClear = 5,
    kDriveOut = 6,
    kFortify = 7,
    kHiddenRecon = 8,
    kInfiltrateRecon = 9,
    kObservationPost = 10,
    kFireRecon = 11,
    kSupportRequest = 12,
    kCount = 13,  // 枚举数量哨兵。
};

// 任务状态机（FR-044）。
enum class MissionState : std::uint8_t {
    kIssued = 0,
    kExecuting = 1,
    kCompleted = 2,
    kFailed = 3,
    kTimedOut = 4,
    kCancelled = 5,
};

// 接敌策略（FR-045：交火/规避/伺机）。
enum class EngagementPolicy : std::uint8_t {
    kAggressive = 0,
    kBalanced = 1,
    kCautious = 2,
};

// 谨慎程度（FR-045：激进/标准/保守）。
enum class CautionLevel : std::uint8_t {
    kAggressive = 0,
    kStandard = 1,
    kConservative = 2,
};

// 弹药选择（FR-045：自动/指定类型）。
enum class AmmoPolicy : std::uint8_t {
    kAuto = 0,
    kSpecific = 1,
};

// 失败后处置（contracts/command-schema.md：撤退至指定区域/转入防守/上报）。
enum class FailureAction : std::uint8_t {
    kWithdrawTo = 0,
    kHold = 1,
    kReport = 2,
};

std::string_view to_string(MissionType type) noexcept;
MissionType mission_type_from_string(std::string_view name);
std::string_view to_string(MissionState state) noexcept;
MissionState mission_state_from_string(std::string_view name);
std::string_view to_string(EngagementPolicy policy) noexcept;
EngagementPolicy engagement_policy_from_string(std::string_view name);
std::string_view to_string(CautionLevel level) noexcept;
CautionLevel caution_level_from_string(std::string_view name);
std::string_view to_string(AmmoPolicy policy) noexcept;
AmmoPolicy ammo_policy_from_string(std::string_view name);
std::string_view to_string(FailureAction action) noexcept;
FailureAction failure_action_from_string(std::string_view name);

void to_json(nlohmann::json& json, MissionType type);
void from_json(const nlohmann::json& json, MissionType& type);
void to_json(nlohmann::json& json, MissionState state);
void from_json(const nlohmann::json& json, MissionState& state);
void to_json(nlohmann::json& json, EngagementPolicy policy);
void from_json(const nlohmann::json& json, EngagementPolicy& policy);
void to_json(nlohmann::json& json, CautionLevel level);
void from_json(const nlohmann::json& json, CautionLevel& level);
void to_json(nlohmann::json& json, AmmoPolicy policy);
void from_json(const nlohmann::json& json, AmmoPolicy& policy);
void to_json(nlohmann::json& json, FailureAction action);
void from_json(const nlohmann::json& json, FailureAction& action);

// 目标（contracts/command-schema.md：unit|zone|point）。
struct MissionTarget {
    std::string kind;  // "unit" | "zone" | "point"
    std::string ref;

    bool operator==(const MissionTarget&) const = default;
};

// 结构化行为参数（FR-045；可扩展，新增参数不改变执行框架）。
struct MissionBehavior {
    EngagementPolicy engagement = EngagementPolicy::kBalanced;
    CautionLevel caution = CautionLevel::kStandard;
    AmmoPolicy ammo_policy = AmmoPolicy::kAuto;
    std::string ammo_override;  // ammo_policy == kSpecific 时指定弹药 id。
    FailureAction failure_action = FailureAction::kReport;
    std::string failure_target;  // kWithdrawTo 时指定撤退区域/集结点。

    bool operator==(const MissionBehavior&) const = default;
};

// 任务（FR-045 字段全集；completion_params 保持原始 JSON 以承载任意参数）。
struct Mission {
    std::string id;
    MissionType type = MissionType::kMove;
    MissionTarget target;
    std::string completion_condition;  // "secure_zone"/"destroy_unit"/...
    nlohmann::json completion_params = nlohmann::json::object();
    std::string intent;  // 自由文本：展示与 AI 理解背景（FR-045）。
    MissionBehavior behavior;
    std::int64_t priority = 0;
    std::uint64_t deadline_ticks = 0U;  // 0 = 未指定（FR-045 时限）。
    MissionState state = MissionState::kIssued;
    bool continuous = false;  // 持续任务：循环执行直到被取代/终止（FR-044）。
    bool loops = true;        // 持续任务循环开关。

    // 一致性校验（F2/F7）：continuous 必须与注册表一致；priority 非负。
    // 未知 MissionType 时经 mission_type_spec 显式抛错（F9，与注册表一致）。
    bool is_valid() const;

    bool operator==(const Mission&) const = default;
};

// 任务类型注册表条目（数据表，mission_registry.cpp 提供 v1 全量）。
struct MissionTypeSpec {
    MissionType type;
    std::string_view name;
    bool continuous;  // 持续任务（观察哨/巡逻等）。
    bool recon;       // 侦察类任务（T035 判定机制）。
};

// 已注册任务类型（固定顺序：v1 13 种，SC-010 可扩展）。
const std::vector<MissionType>& registered_mission_types();
bool is_registered_mission_type(std::string_view name);
const MissionTypeSpec& mission_type_spec(MissionType type);
// 未知枚举与 mission_type_spec 一致显式抛 std::invalid_argument（F9）。
bool is_continuous_mission(MissionType type);
bool is_recon_mission(MissionType type);

// 状态机数据表（FR-044）。
bool can_transition(MissionState from_state, MissionState target_state) noexcept;
std::span<const MissionState> transitions_from(MissionState state) noexcept;
bool is_terminal_mission_state(MissionState state) noexcept;

// ---- 条件表达式（确定性求值器）----

enum class ConditionOp : std::uint8_t {
    kTrue = 0,
    kEq = 1,
    kNe = 2,
    kGt = 3,
    kGe = 4,
    kLt = 5,
    kLe = 6,
    kAnd = 7,
    kOr = 8,
    kNot = 9,
};

std::string_view to_string(ConditionOp op) noexcept;
ConditionOp condition_op_from_string(std::string_view name);

// 确定性条件表达式树：叶子 = 比较运算，复合 = AND/OR/NOT。
// 缺失变量仅在"实际被求值的路径"上报错：AND/OR 短路后未访问的分支
// 不读取变量，因此不会因短路分支缺失变量而抛异常（F8）。
struct ConditionExpr {
    ConditionOp op = ConditionOp::kTrue;
    std::string variable;                 // 叶子变量名。
    double threshold = 0.0;               // 叶子阈值。
    std::vector<ConditionExpr> children;  // 复合节点子条件（固定顺序）。

    // 求值：只读取上下文、按固定顺序短路；被求值路径上的缺失变量抛
    // std::invalid_argument（短路分支不访问，不报错）。
    bool Evaluate(const std::map<std::string, double>& context) const;

    nlohmann::json ToJson() const;
    // 解析 JSON（非法 op/缺字段/NOT 子条件数错误抛 std::invalid_argument）。
    static ConditionExpr Parse(const nlohmann::json& json);
    static ConditionExpr True() { return ConditionExpr{}; }
    static ConditionExpr Compare(ConditionOp op, std::string variable, double threshold);
    static ConditionExpr Combine(ConditionOp op, std::vector<ConditionExpr> children);

    bool operator==(const ConditionExpr&) const = default;
};

void to_json(nlohmann::json& json, const MissionTarget& target);
void from_json(const nlohmann::json& json, MissionTarget& target);
void to_json(nlohmann::json& json, const MissionBehavior& behavior);
void from_json(const nlohmann::json& json, MissionBehavior& behavior);
void to_json(nlohmann::json& json, const Mission& mission);
void from_json(const nlohmann::json& json, Mission& mission);
void to_json(nlohmann::json& json, const ConditionExpr& expr);
void from_json(const nlohmann::json& json, ConditionExpr& expr);

// ---- 内联实现（条件表达式为纯值类型，随头文件提供）----

inline ConditionExpr ConditionExpr::Compare(const ConditionOp op, std::string variable, const double threshold) {
    return ConditionExpr{op, std::move(variable), threshold, {}};
}

inline ConditionExpr ConditionExpr::Combine(const ConditionOp op, std::vector<ConditionExpr> children) {
    return ConditionExpr{op, "", 0.0, std::move(children)};
}

inline bool Mission::is_valid() const {
    if (priority < 0) {
        return false;
    }
    return is_continuous_mission(type) == continuous;
}

inline bool ConditionExpr::Evaluate(const std::map<std::string, double>& context) const {
    const auto value = [&](const std::string& name) {
        const auto it = context.find(name);
        if (it == context.end()) {
            throw std::invalid_argument("条件变量不存在: " + name);
        }
        return it->second;
    };
    switch (op) {
        case ConditionOp::kTrue:
            return true;
        case ConditionOp::kEq:
            return value(variable) == threshold;
        case ConditionOp::kNe:
            return value(variable) != threshold;
        case ConditionOp::kGt:
            return value(variable) > threshold;
        case ConditionOp::kGe:
            return value(variable) >= threshold;
        case ConditionOp::kLt:
            return value(variable) < threshold;
        case ConditionOp::kLe:
            return value(variable) <= threshold;
        case ConditionOp::kAnd:
            for (const ConditionExpr& child : children) {
                if (!child.Evaluate(context)) {
                    return false;
                }
            }
            return true;
        case ConditionOp::kOr:
            for (const ConditionExpr& child : children) {
                if (child.Evaluate(context)) {
                    return true;
                }
            }
            return false;
        case ConditionOp::kNot:
            if (children.size() != 1U) {
                throw std::invalid_argument("NOT 条件必须恰好包含一个子条件");
            }
            return !children.front().Evaluate(context);
    }
    throw std::invalid_argument("未知条件操作符");
}

inline nlohmann::json ConditionExpr::ToJson() const {
    switch (op) {
        case ConditionOp::kAnd:
        case ConditionOp::kOr:
        case ConditionOp::kNot:
            return nlohmann::json{{"op", to_string(op)}, {"children", children}};
        case ConditionOp::kTrue:
            return nlohmann::json{{"op", to_string(op)}};
        default:
            return nlohmann::json{{"op", to_string(op)}, {"variable", variable}, {"threshold", threshold}};
    }
}

inline ConditionExpr ConditionExpr::Parse(const nlohmann::json& json) {
    try {
        const ConditionOp op = condition_op_from_string(json.at("op").get<std::string>());
        if (op == ConditionOp::kTrue) {
            return ConditionExpr{};
        }
        if (op == ConditionOp::kAnd || op == ConditionOp::kOr || op == ConditionOp::kNot) {
            std::vector<ConditionExpr> children;
            for (const nlohmann::json& child : json.at("children")) {
                children.push_back(Parse(child));
            }
            if (op == ConditionOp::kNot && children.size() != 1U) {
                throw std::invalid_argument("NOT 条件必须恰好包含一个子条件");
            }
            return ConditionExpr{op, "", 0.0, std::move(children)};
        }
        return ConditionExpr{op, json.at("variable").get<std::string>(), json.at("threshold").get<double>(), {}};
    } catch (const std::invalid_argument&) {
        throw;  // 语义错误原样抛出。
    } catch (const nlohmann::json::exception& error) {
        throw std::invalid_argument(std::string("条件表达式 JSON 非法: ") + error.what());
    }
}

// ConditionOp 名称映射（供 ToJson/Parse 使用，稳定字符串）。
inline std::string_view to_string(const ConditionOp op) noexcept {
    switch (op) {
        case ConditionOp::kTrue:
            return "true";
        case ConditionOp::kEq:
            return "eq";
        case ConditionOp::kNe:
            return "ne";
        case ConditionOp::kGt:
            return "gt";
        case ConditionOp::kGe:
            return "ge";
        case ConditionOp::kLt:
            return "lt";
        case ConditionOp::kLe:
            return "le";
        case ConditionOp::kAnd:
            return "and";
        case ConditionOp::kOr:
            return "or";
        case ConditionOp::kNot:
            return "not";
    }
    return "unknown";
}

inline ConditionOp condition_op_from_string(const std::string_view name) {
    if (name == "true") {
        return ConditionOp::kTrue;
    }
    if (name == "eq") {
        return ConditionOp::kEq;
    }
    if (name == "ne") {
        return ConditionOp::kNe;
    }
    if (name == "gt") {
        return ConditionOp::kGt;
    }
    if (name == "ge") {
        return ConditionOp::kGe;
    }
    if (name == "lt") {
        return ConditionOp::kLt;
    }
    if (name == "le") {
        return ConditionOp::kLe;
    }
    if (name == "and") {
        return ConditionOp::kAnd;
    }
    if (name == "or") {
        return ConditionOp::kOr;
    }
    if (name == "not") {
        return ConditionOp::kNot;
    }
    throw std::invalid_argument("未知条件操作符: " + std::string(name));
}

inline void to_json(nlohmann::json& json, const ConditionExpr& expr) {
    json = expr.ToJson();
}

inline void from_json(const nlohmann::json& json, ConditionExpr& expr) {
    expr = ConditionExpr::Parse(json);
}

}  // namespace wfs::sim::model
