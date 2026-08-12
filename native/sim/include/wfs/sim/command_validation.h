// sim/include/wfs/sim/command_validation.h
//
// T014：命令 Schema + 语义双重校验管道公开接口。
//
// 设计契约（contracts/command-schema.md §2；FR-045/069；宪法第 9/17 条）：
// - 玩家与 AI 共用同一命令结构；AI 只能生成命令，输出必须通过规则与格式校验。
// - 管道分两层：先 JSON Schema（结构），再语义校验（引用完整性/权限/可求值性）；
//   结构失败时不再执行语义校验，避免在残缺数据上做引用判断。
// - 语义校验固定顺序：类型已注册 → 目标存在/可指挥 → 完成条件可求值 →
//   弹药覆盖存在；错误列表按该顺序追加，同一输入必然产生同一输出（确定性）。
// - 越权判定：目标的 node_id 必须属于 commander_node_id 的指挥范围
//   （本阶段按"同节点"判定，指挥树展开由 T025 实现）；commander_node_id
//   为空表示调用方不做权限过滤（只查存在性）。
// - 类型注册表：context.registered_types 为空时使用内置 v1 13 种任务类型；
//   后续 T028 可将注册表改为数据驱动，接口已预留注入集合。
// - 弹药覆盖仅对 unit 目标校验（zone/point 目标在命令级无法映射到单兵装备，
//   由执行阶段按目标单位逐个体检，见 command-schema.md §2）。
// - 错误信息确定性：SCHEMA_INVALID 的错误先按 (pointer, message) 排序，
//   语义错误按固定检查顺序，不做任何无序遍历输出。

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "wfs/sim/loader.h"

namespace wfs::sim {

struct ValidationError {
    std::string code;
    std::string message;

    bool operator==(const ValidationError&) const = default;
};

struct CommandValidationResult {
    bool ok() const noexcept { return errors.empty(); }

    std::vector<ValidationError> errors;
};

// 语义校验上下文：从场景（T013）提取的、命令校验所需的纯数据视图。
struct UnitInfo {
    std::string id;
    std::string node_id;
    std::vector<std::string> ammo;
};

struct CommandValidationContext {
    std::string commander_node_id;         // 空 = 不做越权过滤
    std::vector<UnitInfo> units;           // 场景内全部单位（含敌方）
    std::vector<std::string> known_zones;  // 场景内已定义区域
    // 已注册任务类型；空 = 使用内置 v1 类型表（13 种任务）。
    std::vector<std::string> registered_types;
};

// 由场景构建命令校验上下文（c_api 注入通道复用）。
CommandValidationContext make_validation_context(const Scenario& scenario);

// 双重校验：command_json 为原始命令 JSON 文本，schema_path 为
// contracts/schemas/command.schema.json 的显式路径。
CommandValidationResult validate_command(const std::string& command_json, const CommandValidationContext& context,
                                         const std::filesystem::path& schema_path);

// 双重校验：调用方已持有命令与 Schema 的解析结果（单元测试/内部复用）。
CommandValidationResult validate_command(const nlohmann::json& command, const CommandValidationContext& context,
                                         const nlohmann::json& schema);

}  // namespace wfs::sim
