// sim/src/command_validation.cpp
//
// T014：命令 Schema + 语义双重校验管道实现。
//
// 实现策略：validate_command(string, ...) 负责解析 JSON 与加载 Schema 文件，
// 随后与 validate_command(json, ...) 共享同一管道：
//   1) JSON Schema 结构校验（违规按 (pointer, message) 排序，确定性）；
//   2) schema_version 一致性（T007 数据契约约定）；
//   3) 语义校验按固定顺序：类型注册 → 目标存在/越权 → 完成条件可求值 →
//      弹药覆盖存在。每类语义检查独立成函数，错误仍按该顺序收集后返回，
//      不做无序遍历输出。
// 结构校验失败或版本不匹配时立即返回，不在残缺数据上做引用判断。
// 弹药覆盖仅对 unit 目标可求值；zone/point 目标返回 AMMO_NOT_EVALUABLE，
// 避免"无法校验却静默放行"（宪法第 17 条）。

#include "wfs/sim/command_validation.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "wfs/sim/model/mission.h"

#include "schema_validator.h"

namespace wfs::sim {

namespace {

struct ConditionSpec {
    const char* name;
    std::vector<std::string> required_params;
    std::vector<std::string> optional_params;  // 存在时同样校验形状（M1）。
};

const std::vector<ConditionSpec>& BuiltInConditions() {
    // 与 contracts/command-schema.md §1/§2 同步；T028 任务注册表落地后
    // 可改为数据驱动，此处仍保留确定性内置基线。
    static const std::vector<ConditionSpec> conditions = {
        {"secure_zone", {"zone"}},
        {"destroy_unit", {"target_unit"}},
        {"drive_out", {"zone"}},
        {"clear", {"zone"}},
        {"hold", {"duration_ticks"}},
        {"reach_point", {"point"}},
        // M1：FR-042 全量任务类型可下达——巡逻/构筑/侦察完成条件注册。
        {"patrol", {"cycle_ticks"}, {}},
        {"fortify", {"construction_ticks"}, {}},
        {"recon", {"point"}, {"exit_point"}},
    };
    return conditions;
}

const std::vector<std::string>& BuiltInCommandTypes() {
    // v1 13 种任务类型（data-model.md §11）：默认表直接由 mission_registry
    // 生成，消除双源漂移（F5）；context.registered_types 非空时由调用方
    // 集合完全覆盖（SC-010 可扩展）。
    static const std::vector<std::string> types = [] {
        const std::vector<model::MissionType>& registered = model::registered_mission_types();
        std::vector<std::string> names;
        names.reserve(registered.size());
        for (const model::MissionType type : registered) {
            names.push_back(std::string(model::to_string(type)));
        }
        return names;
    }();
    return types;
}

ValidationError Error(std::string code, std::string message) {
    return ValidationError{std::move(code), std::move(message)};
}

std::string Join(const std::vector<std::string>& parts) {
    std::string joined;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            joined += ", ";
        }
        joined += parts[i];
    }
    return joined;
}

const UnitInfo* FindUnit(const CommandValidationContext& context, const std::string& unit_id) {
    const auto unit_it = std::find_if(context.units.begin(), context.units.end(),
                                      [&](const UnitInfo& unit) { return unit.id == unit_id; });
    return unit_it == context.units.end() ? nullptr : &*unit_it;
}

bool HasUnit(const CommandValidationContext& context, const std::string& unit_id) {
    return FindUnit(context, unit_id) != nullptr;
}

bool HasZone(const CommandValidationContext& context, const std::string& zone_id) {
    return std::find(context.known_zones.begin(), context.known_zones.end(), zone_id) != context.known_zones.end();
}

// T029 契约扩展：撤回/修改元命令（FR-045）不属于 13 种任务类型，单独注册。
bool IsMetaCommandType(const std::string& type) {
    return type == "WITHDRAW_COMMAND" || type == "MODIFY_COMMAND";
}

// ---- 语义检查 1：类型必须已注册（未注册类型拒绝）。 ----
void CheckCommandType(const std::string& type, const CommandValidationContext& context,
                      std::vector<ValidationError>& errors) {
    if (IsMetaCommandType(type)) {
        return;  // 元命令类型在 T029 链路注册（撤回/修改，FR-045）。
    }
    const std::vector<std::string>& registered_types =
        context.registered_types.empty() ? BuiltInCommandTypes() : context.registered_types;
    if (std::find(registered_types.begin(), registered_types.end(), type) == registered_types.end()) {
        errors.push_back(Error("UNREGISTERED_TYPE", "命令类型未注册: " + type));
    }
}

// ---- 语义检查 2：目标必须存在且属于可指挥范围（越权命令拒绝）。 ----
void CheckTarget(const nlohmann::json& command, const CommandValidationContext& context, const UnitInfo*& target_unit,
                 std::vector<ValidationError>& errors) {
    const std::string target_kind = command["target"]["kind"].get<std::string>();
    const std::string target_ref = command["target"].value("ref", std::string());
    if (target_kind == "unit") {
        target_unit = FindUnit(context, target_ref);
        if (target_unit == nullptr) {
            errors.push_back(Error("TARGET_NOT_FOUND", "目标单位不存在: " + target_ref));
            return;
        }
        if (!context.commander_node_id.empty() && target_unit->node_id != context.commander_node_id) {
            errors.push_back(Error("UNAUTHORIZED_TARGET", "目标单位不属于指挥范围: " + target_ref + "（所属节点 " +
                                                              target_unit->node_id + "，指挥节点 " +
                                                              context.commander_node_id + "）"));
        }
    } else if (target_kind == "units") {
        // 批量命令（FR-045）：目标为单位集合；单兵条件（弹药覆盖）延迟到
        // 命令链到达时按单位独立判定（部分接受）。
        if (!command["target"].contains("refs") || !command["target"]["refs"].is_array() ||
            command["target"]["refs"].empty()) {
            errors.push_back(Error("TARGET_NOT_FOUND", "批量目标缺少非空 refs 数组"));
            return;
        }
        for (const nlohmann::json& ref_json : command["target"]["refs"]) {
            const std::string ref = ref_json.get<std::string>();
            const UnitInfo* unit = FindUnit(context, ref);
            if (unit == nullptr) {
                errors.push_back(Error("TARGET_NOT_FOUND", "目标单位不存在: " + ref));
                continue;
            }
            if (!context.commander_node_id.empty() && unit->node_id != context.commander_node_id) {
                errors.push_back(Error("UNAUTHORIZED_TARGET", "目标单位不属于指挥范围: " + ref + "（所属节点 " +
                                                                  unit->node_id + "，指挥节点 " +
                                                                  context.commander_node_id + "）"));
            }
        }
    } else if (target_kind == "zone" && !HasZone(context, target_ref)) {
        errors.push_back(Error("TARGET_NOT_FOUND", "目标区域不存在: " + target_ref));
    }
    // target_kind == "point"：坐标类目标不引用场景实体，T030 机动系统落地前
    // 只做结构校验（Schema 已保证 ref 非空）。
}

// ---- 语义检查 3a：完成条件必需参数完整性。 ----
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
bool CollectMissingConditionParams(const ConditionSpec& spec, const nlohmann::json& params,
                                   std::vector<std::string>& missing) {
    const auto invalid_point = [](const nlohmann::json& value) {
        return !value.is_object() || !value.contains("x") || !value.contains("y") || !value["x"].is_number() ||
               !value["y"].is_number();
    };
    for (const std::string& required : spec.required_params) {
        if (!params.contains(required)) {
            missing.push_back(required);
            continue;
        }
        const nlohmann::json& value = params[required];
        if (required == "duration_ticks" || required == "cycle_ticks" || required == "construction_ticks") {
            if (!value.is_number_integer() || value.get<std::int64_t>() <= 0) {
                missing.push_back(required);
            }
        } else if (required == "point") {
            // T030：reach_point 的 point 参数为坐标对象（x/y，km）。
            if (invalid_point(value)) {
                missing.push_back(required);
            }
        } else if (!value.is_string() || value.get<std::string>().empty()) {
            missing.push_back(required);
        }
    }
    // 可选参数：出现时必须满足形状要求（如 exit_point 为坐标对象）。
    for (const std::string& optional : spec.optional_params) {
        if (!params.contains(optional)) {
            continue;
        }
        const nlohmann::json& value = params[optional];
        if (optional == "exit_point") {
            if (invalid_point(value)) {
                missing.push_back(optional);
            }
        }
    }
    return missing.empty();
}

// ---- 语义检查 3b：条件引用的区域/单位必须存在（条件可求值）。 ----
std::string ConditionReferenceProblem(const std::string& condition, const nlohmann::json& params,
                                      const CommandValidationContext& context) {
    if (condition == "destroy_unit") {
        const std::string unit_id = params["target_unit"].get<std::string>();
        if (!HasUnit(context, unit_id)) {
            return "条件引用的单位不存在: " + unit_id;
        }
    } else if (condition == "secure_zone" || condition == "drive_out" || condition == "clear") {
        const std::string zone_id = params["zone"].get<std::string>();
        if (!HasZone(context, zone_id)) {
            return "条件引用的区域不存在: " + zone_id;
        }
    }
    return {};
}

// ---- 语义检查 3：完成条件必须可求值（未注册/参数不全/引用缺失）。 ----
void CheckCompletionCondition(const nlohmann::json& command, const CommandValidationContext& context,
                              std::vector<ValidationError>& errors) {
    const std::string condition = command["completion"]["condition"].get<std::string>();
    const nlohmann::json params = command["completion"].value("params", nlohmann::json::object());
    const std::vector<ConditionSpec>& conditions = BuiltInConditions();
    const auto condition_it = std::find_if(conditions.begin(), conditions.end(),
                                           [&](const ConditionSpec& spec) { return spec.name == condition; });
    if (condition_it == conditions.end()) {
        errors.push_back(Error("CONDITION_NOT_EVALUABLE", "完成条件未注册: " + condition));
        return;
    }
    std::vector<std::string> missing;
    if (!CollectMissingConditionParams(*condition_it, params, missing)) {
        errors.push_back(
            Error("CONDITION_NOT_EVALUABLE", "完成条件 " + condition + " 缺少可求值参数: " + Join(missing)));
        return;
    }
    const std::string reference_problem = ConditionReferenceProblem(condition, params, context);
    if (!reference_problem.empty()) {
        errors.push_back(Error("CONDITION_NOT_EVALUABLE", reference_problem));
    }
}

// ---- 语义检查 4：弹药覆盖必须存在于单位装备中。 ----
void CheckAmmoOverride(const nlohmann::json& command, const UnitInfo* target_unit,
                       std::vector<ValidationError>& errors) {
    const nlohmann::json& behavior = command["behavior"];
    if (!behavior.contains("ammo_override")) {
        return;
    }
    const std::string ammo = behavior["ammo_override"].get<std::string>();
    const std::string target_kind = command["target"]["kind"].get<std::string>();
    if (target_kind == "units") {
        return;  // 批量单兵条件延迟到命令链到达时按单位判定（FR-045 部分接受）。
    }
    const std::string target_ref = command["target"].value("ref", std::string());
    if (target_kind != "unit") {
        errors.push_back(
            Error("AMMO_NOT_EVALUABLE", "ammo_override 只能对 unit 目标校验（目标 kind=" + target_kind + "）"));
        return;
    }
    if (target_unit != nullptr &&
        std::find(target_unit->ammo.begin(), target_unit->ammo.end(), ammo) == target_unit->ammo.end()) {
        errors.push_back(Error("AMMO_NOT_FOUND", "目标单位装备中不存在弹药 " + ammo + "（单位 " + target_ref + "）"));
    }
}

}  // namespace

CommandValidationContext make_validation_context(const Scenario& scenario) {
    CommandValidationContext context;
    context.commander_node_id = scenario.player_node_id;
    context.known_zones = scenario.zones;
    context.units.reserve(scenario.units.size());
    for (const ScenarioUnit& unit : scenario.units) {
        context.units.push_back(UnitInfo{unit.id, unit.node_id, unit.ammo});
    }
    return context;
}

CommandValidationResult validate_command(const std::string& command_json, const CommandValidationContext& context,
                                         const std::filesystem::path& schema_path) {
    nlohmann::json command;
    try {
        command = nlohmann::json::parse(command_json);
    } catch (const nlohmann::json::parse_error& error) {
        return CommandValidationResult{{Error("INVALID_JSON", std::string("命令不是合法 JSON: ") + error.what())}};
    }

    const detail::SchemaFileResult schema_file = detail::load_schema_file(schema_path);
    if (!schema_file.ok()) {
        return CommandValidationResult{{Error(schema_file.code, schema_file.message)}};
    }
    if (!schema_file.schema.contains("schema_version") || !schema_file.schema["schema_version"].is_number_integer()) {
        return CommandValidationResult{
            {Error("SCHEMA_INVALID", "命令 Schema 缺少整数 schema_version 字段: " + schema_path.string())}};
    }
    return validate_command(command, context, schema_file.schema);
}

CommandValidationResult validate_command(const nlohmann::json& command, const CommandValidationContext& context,
                                         const nlohmann::json& schema) {
    // 第一层：JSON Schema 结构校验（违规已确定性排序）。
    detail::SchemaFileResult schema_file;
    schema_file.schema = schema;
    const std::vector<detail::SchemaViolation> violations = schema_file.validate(command);
    if (!violations.empty()) {
        std::vector<ValidationError> errors;
        errors.reserve(violations.size());
        for (const detail::SchemaViolation& violation : violations) {
            errors.push_back(Error("SCHEMA_INVALID", "[" + violation.pointer + "] " + violation.message));
        }
        return CommandValidationResult{std::move(errors)};
    }

    // 数据契约：命令 schema_version 必须与 Schema 一致（T007 约定）。
    if (command["schema_version"].get<std::int64_t>() != schema["schema_version"].get<std::int64_t>()) {
        return CommandValidationResult{{Error("SCHEMA_VERSION_MISMATCH", "命令 schema_version 与 Schema 不一致")}};
    }

    // 第二层：语义校验（固定顺序：类型 → 目标 → 完成条件 → 弹药，全部收集）。
    std::vector<ValidationError> errors;
    CheckCommandType(command["type"].get<std::string>(), context, errors);
    const UnitInfo* target_unit = nullptr;
    CheckTarget(command, context, target_unit, errors);
    const bool meta = IsMetaCommandType(command["type"].get<std::string>());
    if (!meta) {
        // 任务命令：完成条件可求值 + 弹药覆盖存在（FR-045/058）。
        CheckCompletionCondition(command, context, errors);
        CheckAmmoOverride(command, target_unit, errors);
    }
    if (command["type"].get<std::string>() == "MODIFY_COMMAND") {
        // 修改命令携带完整替代命令：替代命令本身必须通过同一双重校验。
        if (!command.contains("replace_with") || !command["replace_with"].is_object()) {
            errors.push_back(Error("MODIFY_INVALID", "MODIFY_COMMAND 缺少 replace_with 命令对象"));
        } else {
            const CommandValidationResult replacement = validate_command(command["replace_with"], context, schema);
            for (const ValidationError& error : replacement.errors) {
                errors.push_back(ValidationError{error.code, "replace_with: " + error.message});
            }
        }
    }
    return CommandValidationResult{std::move(errors)};
}

}  // namespace wfs::sim
