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

struct SchemaViolation {
    std::string pointer;
    std::string message;
};

struct SchemaFileResult {
    bool ok() const noexcept { return code.empty(); }

    std::string code;  // "IO_ERROR" | "INVALID_JSON" | "SCHEMA_INVALID"
    std::string message;
    nlohmann::json schema;

    // 校验文档是否符合本结果携带的 Schema；返回已确定性排序的违规列表。
    // Schema 本身非法（无法构造校验器）或校验器内部失败时抛
    // std::invalid_argument（validate 内部已把 nlohmann JSON_ASSERT 转为异常，
    // Debug/Release 一致不崩溃），由调用方转换为 SCHEMA_INVALID 错误。
    // 校验绑定在结果对象上，避免"实例/Schema 两个同类型参数易互换"的误用面。
    std::vector<SchemaViolation> validate(const nlohmann::json& instance) const;
};

// 读取并解析 Schema 文件（错误结构化返回，不抛异常）。
SchemaFileResult load_schema_file(const std::filesystem::path& schema_path);

}  // namespace wfs::sim::detail
