// sim/src/command_validation.cpp
//
// T014：命令 Schema + 语义双重校验管道实现。
//
// 实现策略：validate_command(string, ...) 负责解析 JSON 与加载 Schema 文件，
// 随后与 validate_command(json, ...) 共享同一管道：
//   1) JSON Schema 结构校验（违规按 (pointer, message) 排序，确定性）；
//   2) schema_version 一致性（T007 数据契约约定）；
//   3) 语义校验按固定顺序：类型注册 → 目标存在/越权 → 完成条件可求值 →
//      弹药覆盖存在。错误全部收集后按该顺序返回，不做无序遍历输出。
// 结构校验失败或版本不匹配时立即返回，不在残缺数据上做引用判断。
// 弹药覆盖仅对 unit 目标可求值；zone/point 目标返回 AMMO_NOT_EVALUABLE，
// 避免"无法校验却静默放行"（宪法第 17 条）。

#include "wfs/sim/command_validation.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "schema_validator.h"

namespace wfs::sim {

namespace {

struct ConditionSpec {
    const char* name;
    std::vector<std::string> required_params;
};

const std::vector<ConditionSpec>& BuiltInConditions() {
    // 与 contracts/command-schema.md §1/§2 同步；T028 任务注册表落地后
    // 可改为数据驱动，此处仍保留确定性内置基线。
    static const std::vector<ConditionSpec> conditions = {
        {"secure_zone", {"zone"}}, {"destroy_unit", {"target_unit"}}, {"drive_out", {"zone"}},
        {"clear", {"zone"}},       {"hold", {"duration_ticks"}},      {"reach_point", {"point"}},
    };
    return conditions;
}

const std::vector<std::string>& BuiltInCommandTypes() {
    // v1 13 种任务类型（data-model.md §11）；context.registered_types
    // 非空时由调用方集合完全覆盖。
    static const std::vector<std::string> types = {
        "MOVE",
        "PATROL",
        "ATTACK",
        "DEFEND",
        "SECURE_ZONE",
        "CLEAR",
        "DRIVE_OUT",
        "FORTIFY",
        "HIDDEN_RECON",
        "INFILTRATE_RECON",
        "OBSERVATION_POST",
        "FIRE_RECON",
        "SUPPORT_REQUEST",
    };
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

bool HasUnit(const CommandValidationContext& context, const std::string& id) {
    return std::any_of(context.units.begin(), context.units.end(), [&](const UnitInfo& unit) { return unit.id == id; });
}

bool HasZone(const CommandValidationContext& context, const std::string& id) {
    return std::find(context.known_zones.begin(), context.known_zones.end(), id) != context.known_zones.end();
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
    std::vector<ValidationError> errors;
    const std::vector<detail::SchemaViolation> violations = detail::validate_against_schema(command, schema);
    if (!violations.empty()) {
        for (const detail::SchemaViolation& violation : violations) {
            errors.push_back(Error("SCHEMA_INVALID", "[" + violation.pointer + "] " + violation.message));
        }
        return CommandValidationResult{std::move(errors)};
    }

    // 数据契约：命令 schema_version 必须与 Schema 一致（T007 约定）。
    if (command["schema_version"].get<std::int64_t>() != schema["schema_version"].get<std::int64_t>()) {
        return CommandValidationResult{{Error("SCHEMA_VERSION_MISMATCH", "命令 schema_version 与 Schema 不一致")}};
    }

    // 第二层：语义校验（固定顺序，全部收集后返回）。
    const std::string type = command["type"].get<std::string>();
    const std::vector<std::string>& registered_types =
        context.registered_types.empty() ? BuiltInCommandTypes() : context.registered_types;
    if (std::find(registered_types.begin(), registered_types.end(), type) == registered_types.end()) {
        errors.push_back(Error("UNREGISTERED_TYPE", "命令类型未注册: " + type));
    }

    const std::string target_kind = command["target"]["kind"].get<std::string>();
    const std::string target_ref = command["target"]["ref"].get<std::string>();
    const UnitInfo* target_unit = nullptr;

    if (target_kind == "unit") {
        const auto unit_it = std::find_if(context.units.begin(), context.units.end(),
                                          [&](const UnitInfo& unit) { return unit.id == target_ref; });
        if (unit_it == context.units.end()) {
            errors.push_back(Error("TARGET_NOT_FOUND", "目标单位不存在: " + target_ref));
        } else {
            target_unit = &*unit_it;
            if (!context.commander_node_id.empty() && unit_it->node_id != context.commander_node_id) {
                errors.push_back(Error("UNAUTHORIZED_TARGET", "目标单位不属于指挥范围: " + target_ref + "（所属节点 " +
                                                                  unit_it->node_id + "，指挥节点 " +
                                                                  context.commander_node_id + "）"));
            }
        }
    } else if (target_kind == "zone" && !HasZone(context, target_ref)) {
        errors.push_back(Error("TARGET_NOT_FOUND", "目标区域不存在: " + target_ref));
    }
    // target_kind == "point"：坐标类目标不引用场景实体，T030 机动系统落地前
    // 只做结构校验（Schema 已保证 ref 非空）。

    const std::string condition = command["completion"]["condition"].get<std::string>();
    const nlohmann::json params = command["completion"].value("params", nlohmann::json::object());
    const std::vector<ConditionSpec>& conditions = BuiltInConditions();
    const auto condition_it = std::find_if(conditions.begin(), conditions.end(),
                                           [&](const ConditionSpec& spec) { return spec.name == condition; });
    if (condition_it == conditions.end()) {
        errors.push_back(Error("CONDITION_NOT_EVALUABLE", "完成条件未注册: " + condition));
    } else {
        std::vector<std::string> missing;
        for (const std::string& required : condition_it->required_params) {
            if (!params.contains(required)) {
                missing.push_back(required);
                continue;
            }
            const nlohmann::json& value = params[required];
            if (required == "duration_ticks") {
                if (!value.is_number_integer() || value.get<std::int64_t>() <= 0) {
                    missing.push_back(required);
                }
            } else if (!value.is_string() || value.get<std::string>().empty()) {
                missing.push_back(required);
            }
        }
        if (!missing.empty()) {
            errors.push_back(
                Error("CONDITION_NOT_EVALUABLE", "完成条件 " + condition + " 缺少可求值参数: " + Join(missing)));
        } else {
            std::string reference_problem;
            if (condition == "destroy_unit" && !HasUnit(context, params["target_unit"].get<std::string>())) {
                reference_problem = "条件引用的单位不存在: " + params["target_unit"].get<std::string>();
            } else if ((condition == "secure_zone" || condition == "drive_out" || condition == "clear") &&
                       !HasZone(context, params["zone"].get<std::string>())) {
                reference_problem = "条件引用的区域不存在: " + params["zone"].get<std::string>();
            }
            if (!reference_problem.empty()) {
                errors.push_back(Error("CONDITION_NOT_EVALUABLE", reference_problem));
            }
        }
    }

    if (command["behavior"].contains("ammo_override")) {
        const std::string ammo = command["behavior"]["ammo_override"].get<std::string>();
        if (target_kind != "unit") {
            errors.push_back(
                Error("AMMO_NOT_EVALUABLE", "ammo_override 只能对 unit 目标校验（目标 kind=" + target_kind + "）"));
        } else if (target_unit != nullptr &&
                   std::find(target_unit->ammo.begin(), target_unit->ammo.end(), ammo) == target_unit->ammo.end()) {
            errors.push_back(
                Error("AMMO_NOT_FOUND", "目标单位装备中不存在弹药 " + ammo + "（单位 " + target_ref + "）"));
        }
    }

    return CommandValidationResult{std::move(errors)};
}

}  // namespace wfs::sim
