# PR #107 第 5 轮轻量复审记录（tip b4e69f7）

日期：2026-08-13（Asia/Shanghai）

审查范围：仅核对第 4 轮 finding（loader.cpp 的 schema_version 裸 get 无范围防护）在 b4e69f7 的修复是否闭环、是否引入新问题。上一 tip 为 47edfd3，本轮唯一新增提交为 b4e69f7。

方法：只读静态审查。仅使用 git show / git diff / git log / git grep 读取提交树，未读工作区文件，未修改任何文件，未执行 push / merge / approve / checkout。

变更范围：b4e69f7 共改动 4 个文件，+58/-4，与第 4 轮修复要求一一对应：

- native/sim/src/loader.cpp
- native/tests/sim_tests/loader_test.cpp
- contracts/schemas/scenario.schema.json
- contracts/schemas/command.schema.json

## 结论

PASS（置信度：高）

第 4 轮 finding 已闭环，未发现本轮新增问题。修复代理已报告 Release 94/94、Debug 94/94 通过；远端 CI 在 push 后触发中，本次为静态审查结论。

## 逐条核对

### 1. HasIntegerSchemaVersion 最终形态（loader.cpp:39-52）

符合要求：

- 先 contains("schema_version")，再 is_number_integer()，任一失败即返回 false。
- 对超大 unsigned 的范围守卫为「先判 is_number_unsigned()，再 get<uint64_t>() 与 static_cast<uint64_t>(INT64_MAX) 比较」。
- 无异常路径：get<uint64_t>() 只在 is_number_unsigned() 为 true 时经短路求值执行，类型必然匹配，不会抛 type_error；signed 整数直接通过守卫。

### 2. ExtractScenarioData 两处守卫（loader.cpp:76-82）

符合要求：

- 在两次裸 get<std::int64_t>()（loader.cpp:81-82）之前，对 root 与 schema_file.schema 都调用 HasIntegerSchemaVersion。
- 守卫失败路径 push 结构化 SCHEMA_INVALID 并 return false，不触碰后续裸 get。

### 3. Schema JSON 补 maximum

符合要求：

- scenario.schema.json:19 与 command.schema.json:19 均为 `"maximum": 9223372036854775807`（= INT64_MAX），数值正确，JSON 语法完整。

### 4. 新增越界测试（loader_test.cpp:145-171）

符合要求，两条路径真实覆盖：

- OutOfRangeScenarioSchemaVersionReturnsStructuredErrorWithoutThrow：标准 schema（含 maximum），越界值 9223372036854775808ULL 由第一层 JSON Schema 校验拦截，返回 SCHEMA_INVALID。
- OutOfRangeScenarioSchemaVersionWithLaxSchemaReturnsStructuredErrorWithoutThrow：宽松 schema `{"schema_version":1,"type":"object"}`（无 maximum 约束），JSON Schema 层放行后由 ExtractScenarioData 的 C++ 守卫拦截，返回 SCHEMA_INVALID 且 message 含 "schema_version"。
- 两条测试均使用 EXPECT_NO_THROW 包裹 load_scenario 调用，能有效检测异常逃逸。

### 5. 无未防护的 schema_version 裸 get

git grep 核对通过：

- loader.cpp:81-82 的两次 get<std::int64_t>() 均在守卫之后，属合法取数。
- command_validation.cpp:288 的 get<std::int64_t>() 前有 exceeds_int64 + is_number_integer 双重守卫（第 3 轮 7e0595f 修复）。

### 6. 无回归

- LoadsValidSampleScenario（schema_version=1）：守卫放行，正常加载路径不变。
- SchemaVersionMismatchRejected（999，在 int64 范围内）：守卫放行后仍走 SCHEMA_VERSION_MISMATCH 路径。

## Findings

本轮无新增 🔴 或 🟡。

范围外观察（非 b4e69f7 引入、不计入本轮 finding）：command_validation.cpp:139 的 duration_ticks 校验 `is_number_integer() + get<std::int64_t>()` 与 loader 第 4 轮问题同构，超大 unsigned 理论上可抛 type_error；该代码在第 3 轮 7e0595f 已存在且不在本轮修复范围内。是否需要另行跟进由主控决定。

## 下一步

等待远端 CI 通过后即可认为 PR #107 第 4 轮修复验收完成；无需追加修复提交。
