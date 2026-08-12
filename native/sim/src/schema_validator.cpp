// sim/src/schema_validator.cpp
//
// T013/T014 共享：JSON Schema 校验实现（见 schema_validator.h 的设计说明）。
// 校验器本身是第三方确定性组件（无随机、无时钟、无外部状态），
// 本文件只负责调用、收集与排序，不引入任何不确定来源。

// 必须先于任何 nlohmann 头文件定义：nlohmann 默认把 JSON_ASSERT 映射为
// assert()（Debug 下畸形 Schema 直接中止进程，Release 下为未定义行为）。
// 覆盖为抛异常后，畸形 Schema 在 Debug/Release 下统一以异常形式暴露，
// 由调用方转换为结构化 SCHEMA_INVALID（宪法第 12/17 条，PR #107 review F1）。
#include <stdexcept>

#define JSON_ASSERT(x)                                    \
    do {                                                  \
        if (!(x)) {                                       \
            throw std::runtime_error("JSON_ASSERT: " #x); \
        }                                                 \
    } while (false)

#include "schema_validator.h"

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <tuple>
#include <utility>

#include <nlohmann/json-schema.hpp>

namespace wfs::sim::detail {

namespace {

class CollectingErrorHandler final : public nlohmann::json_schema::error_handler {
   public:
    void error(const nlohmann::json::json_pointer& pointer, const nlohmann::json& /*instance*/,
               const std::string& message) override {
        violations_.push_back(SchemaViolation{pointer.to_string(), message});
    }

    std::vector<SchemaViolation> violations_;
};

}  // namespace

SchemaFileResult load_schema_file(const std::filesystem::path& schema_path) {
    std::ifstream input(schema_path, std::ios::binary);
    if (!input) {
        return {"IO_ERROR", "无法打开 Schema 文件: " + schema_path.string(), nlohmann::json{}};
    }
    try {
        nlohmann::json schema = nlohmann::json::parse(input);
        return {"", "", std::move(schema)};
    } catch (const nlohmann::json::parse_error& error) {
        return {"INVALID_JSON", std::string("Schema 不是合法 JSON: ") + error.what(), nlohmann::json{}};
    } catch (const std::exception& error) {
        return {"SCHEMA_INVALID", std::string("Schema 解析失败: ") + error.what(), nlohmann::json{}};
    }
}

std::vector<SchemaViolation> SchemaFileResult::validate(const nlohmann::json& instance) const {
    CollectingErrorHandler handler;
    try {
        nlohmann::json_schema::json_validator validator;
        validator.set_root_schema(schema);
        validator.validate(instance, handler);
    } catch (const std::exception& error) {
        // Schema 本身非法（或校验器内部失败）：映射为可转换的异常，
        // 调用方转为 SCHEMA_INVALID，绝不跨边界抛出/崩溃。
        throw std::invalid_argument(std::string("Schema 校验失败: ") + error.what());
    }

    std::vector<SchemaViolation> violations = std::move(handler.violations_);
    std::sort(violations.begin(), violations.end(), [](const SchemaViolation& lhs, const SchemaViolation& rhs) {
        return std::tie(lhs.pointer, lhs.message) < std::tie(rhs.pointer, rhs.message);
    });
    return violations;
}

}  // namespace wfs::sim::detail
