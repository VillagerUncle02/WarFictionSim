// sim/include/wfs/sim/interaction.h
//
// T051：四种上下级交互类型约束公开接口。
//
// 设计契约（FR-050；contracts/command-schema.md §5）：
// - 上下级交互强制限定为四种：TASK_DISPATCH（上级下发任务）、EXECUTION
//   （下级执行状态上报）、SUMMARY_REPORT（摘要汇报）、SUPPORT_REQUEST
//   （支援请求与配属处置）；契约字段统一携带 interaction_type。
// - 解析强制枚举：未知/缺失类型显式报错（宪法 17），错误列表按固定顺序；
// - 同级经上级转发：sender 与 receiver 同层级时经最近共同上级转发
//   （FR-028/050：同级不直接横向通信），跨层级直接送达；路由为确定性
//   纯函数（宪法第 7 条）。

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "wfs/sim/model/command_node.h"

namespace wfs::sim {

// 四种交互类型（command-schema §5；强制枚举，不可扩展未知值）。
enum class InteractionType : std::uint8_t {
    kTaskDispatch = 0,
    kExecution = 1,
    kSummaryReport = 2,
    kSupportRequest = 3,
};

std::string_view to_string(InteractionType type) noexcept;
// 未知名称抛 std::invalid_argument（强制枚举，宪法 17）。
InteractionType interaction_type_from_string(std::string_view name);

// 交互记录：统一携带 interaction_type + 收发双方节点 + 载荷。
struct InteractionRecord {
    InteractionType type = InteractionType::kTaskDispatch;
    std::string from_node;
    std::string to_node;
    nlohmann::json payload = nlohmann::json::object();

    bool operator==(const InteractionRecord&) const = default;
};

struct InteractionParseResult {
    bool ok() const noexcept { return errors.empty(); }

    InteractionRecord record;
    std::vector<std::string> errors;  // 固定顺序（类型 → 节点）。
};

// 解析交互 JSON：必须携带 interaction_type（四种之一）与收发节点。
InteractionParseResult parse_interaction(const nlohmann::json& json);

struct InteractionRouting {
    bool ok() const noexcept { return error.empty(); }

    std::vector<std::string> hops;  // 含起点与终点，按传递顺序。
    std::string error;
};

// 交互路由：同层级经最近共同上级转发；跨层级直接送达；无共同上级报错。
InteractionRouting route_interaction(const model::CommandTree& tree, const std::string& sender,
                                     const std::string& receiver);

void to_json(nlohmann::json& json, const InteractionRecord& record);
void from_json(const nlohmann::json& json, InteractionRecord& record);

}  // namespace wfs::sim
