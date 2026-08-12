// sim/src/schema_validator.h
//
// 内部共享（T013/T014）：JSON Schema 校验基础设施。
//
// 使用 json-schema-validator（pboettch，基于 nlohmann-json，vcpkg 清单锁定）
// 执行 draft-07 结构校验。本模块承担两件确定性相关职责：
// - 违规列表按 (pointer, message) 排序后输出，屏蔽校验器内部遍历顺序，
//   保证同一输入必然产生同一错误序列（宪法第 7 条）；
// - Schema 文件加载错误以结构化 code/message 返回，调用方直接映射为
//   数据校验错误，非法 Schema 报错而非崩溃（宪法第 12/17 条）。

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace wfs::sim::detail {

struct SchemaFileResult {
    bool ok() const noexcept { return code.empty(); }

    std::string code;  // "IO_ERROR" | "INVALID_JSON" | "SCHEMA_INVALID"
    std::string message;
    nlohmann::json schema;
};

struct SchemaViolation {
    std::string pointer;
    std::string message;
};

// 读取并解析 Schema 文件（错误结构化返回，不抛异常）。
SchemaFileResult load_schema_file(const std::filesystem::path& schema_path);

// 校验 instance 是否符合 schema；返回已确定性排序的违规列表。
// schema 本身非法（无法构造校验器）时抛 std::invalid_argument，
// 由调用方转换为 SCHEMA_INVALID 错误。
std::vector<SchemaViolation> validate_against_schema(const nlohmann::json& instance, const nlohmann::json& schema);

}  // namespace wfs::sim::detail
