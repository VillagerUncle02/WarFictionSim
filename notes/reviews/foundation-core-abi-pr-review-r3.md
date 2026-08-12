# PR #107 复审记录（第 3 轮 · 轻量只读）

- 审查对象：PR #107（feature/foundation-core-abi），新 tip `9d79d8a`，本轮修复提交 `b7f5c69`
- 审查方式：仅使用 `git show` / `git diff` / `git log` 读取 git 对象，未读取工作区文件、未修改任何文件、未 push/merge/approve/checkout、未等待 CI
- 范围：逐条核对 round 2 三条 findings 是否闭环 + 4 个新增回归测试是否真实覆盖 + 是否引入新问题

## 结论：FAIL（置信度 0.90）

F1（TU 内）与 F3 已闭环；F2 只闭环了「字段存在但类型非法」分支，「字段缺失」分支仍是未定义行为（Debug 下 abort），且对应回归测试在 Release 门禁下靠 UB 侥幸变绿。存在 1 个 🔴、1 个 🟡，建议修复后再合入。

## 1. Round 2 findings 修复核对

| 项 | 要求 | 实现位置 | 核对结果 |
|---|---|---|---|
| F1 bad_alloc 重抛 | 在通用 catch 前加 `catch(const std::bad_alloc&){throw;}` | `native/sim/src/schema_validator.cpp:60-62`（load_schema_file）、`:74-76`（validate） | ✅ TU 内闭环：catch 顺序正确（parse_error → bad_alloc → exception），`<new>` 已引入（:27），普通 Schema 错误归类不变；❌ 端到端未闭环，见 R3-2 |
| F2 schema_version 类型防护 | 两侧先 `is_number_integer()`，不满足返回结构化 SCHEMA_INVALID、不抛异常 | `native/sim/src/command_validation.cpp:267-270` | ⚠️ 存在键类型防护成立；缺失键分支是 UB/断言，见 R3-1 |
| F3 注释归因修正 | 改注释措辞、不改行为 | `native/sim/src/schema_validator.cpp:7-13` | ✅ 闭环：归因改为「DLL 解析器抛 type_error + 下方 catch 转换」，宏定义与行为未变 |

未发现错误码误改：整数不一致仍返回 `SCHEMA_VERSION_MISMATCH`（command_validation.cpp:271-272）；类型非法返回 `SCHEMA_INVALID` 符合本轮要求。主链路（string 重载 + 严格 `contracts/schemas/command.schema.json`，`required: schema_version` + `type:integer` + `minimum:1`）不受影响，缺失/非法版本在 Schema 层即被拦截，不会到达语义层。

## 2. 新增回归测试核对（native/tests/sim_tests/command_validation_test.cpp）

- `MissingSchemaVersionReturnsStructuredErrorWithoutThrow`（:270-282）❌ **不可信**：`command.erase("schema_version")` 后调用 const 重载，`command["schema_version"]` 本身是 UB（见 R3-1）。Release 下 NDEBUG 屏蔽 JSON_ASSERT，读越界内存侥幸返回 SCHEMA_INVALID 而通过；Debug 下 assert 直接 abort，测试进程崩溃。断言表面有效，实际未验证到安全行为。
- `StringSchemaVersionReturnsStructuredErrorWithoutThrow`（:284-294）✅ 真实覆盖：键存在、值 `"v1"`，守卫命中，结构化 SCHEMA_INVALID，不抛异常。
- `FloatSchemaVersionReturnsStructuredErrorWithoutThrow`（:296-307）✅ 真实覆盖：`1.5` 为 number_float，`is_number_integer()` 为 false，命中守卫。
- `NonIntegerSchemaVersionInSchemaReturnsStructuredErrorWithoutThrow`（:309-320）✅ 真实覆盖：命令侧整数 1、Schema 侧 `"v1"`，短路的第二个条件命中守卫。

不误伤正常路径：`ValidCommandPasses` 使用严格 Schema，行为不变；测试 2-4 的 LaxSchema（`properties:{}`、无 required）在 Schema 层通过后由守卫早退，不触及语义检查，无误报。另：LaxSchema() 恒含 schema_version 键，缺少「Schema 侧完全缺失该键」的用例（💭 R3-4）。

## 3. 新发现

### 🔴 R3-1 — `command_validation.cpp:267`（测试 `command_validation_test.cpp:270-282`）

守卫用 const `operator[]` 取数：`validate_command(const nlohmann::json& command, ...)` 中 `command["schema_version"]` 在键缺失时，按 nlohmann 官方文档属于未定义行为并触发运行时断言（JSON_ASSERT）。该编译单元未覆盖 JSON_ASSERT，因此 Debug（clang-cl-debug）下 `assert` 直接中止进程，Release（CI/gates 均用 Release）下断言被 NDEBUG 移除、解引用 `end()` 为 UB。

修复提交的信息与错误消息都声称处理「缺失或类型非法」，但缺失分支实际既不返回结构化错误也不安全；其回归测试断言 `EXPECT_NO_THROW` 仅因 Release 下 UB 侥幸通过，Debug/ASan 运行会暴露问题。F2 的「缺失」分支未闭环。

建议：先判存在再取数，例如：

```cpp
const auto cmd_ver = command.find("schema_version");
const auto sch_ver = schema.find("schema_version");
if (cmd_ver == command.end() || sch_ver == schema.end() ||
    !cmd_ver->is_number_integer() || !sch_ver->is_number_integer()) {
    return CommandValidationResult{{Error("SCHEMA_INVALID", "...")}};
}
```

（或使用 `contains()` / `value(key, nlohmann::json{})`；Schema 侧同样存在该问题，string 重载因 `:247` 已有 contains 预检而安全，直接调用 json 重载的调用方不受保护。）

### 🟡 R3-2 — F1 端到端未闭环（`loader.cpp:203`、`command_validation.cpp:251`）

schema_validator.cpp 重抛的 `bad_alloc` 会在边界处被 `catch(const std::exception&)` 重新捕获并折叠为 `SCHEMA_INVALID`；只有逃逸到 c_api 的 `catch(...)` 才映射为 `INTERNAL_ERROR`。因此「内存耗尽不得伪装成非法数据」的目标在 loader/command_validation 边界仍未达成（round 2 F1 修复方向明确提到该边界通道）。建议在两处通用 catch 前加 `catch(const std::bad_alloc&){throw;}`（或映射为 INTERNAL_ERROR）。

### 💭 R3-3 — 超大 unsigned schema_version（既有角落）

`is_number_integer()` 对 number_unsigned 返回 true，`get<std::int64_t>()` 在值 > INT64_MAX 时仍抛 `type_error(302)` 并逃逸。严格 Schema 主链路下不可达；建议后续取数用 `get<std::uint64_t>` 或先做范围判断。

### 💭 R3-4 — 缺少 Schema 侧缺失键测试

LaxSchema() 恒含 `schema_version`，4 个用例未覆盖「Schema 完全不含该键」时 R3-1 的 schema 侧分支。

## 4. 其他核对

- 编译/类型：`#include <new>`、catch 顺序、`is_number_integer` 用法均合法；与 90/90 通过一致（未独立编译验证）。
- CI 说明：CI 与 `scripts/gates.ps1` 默认均只跑 Release（`native-build` 仅 `clang-cl-release`），因此 R3-1 的 Debug abort 不会被当前门禁发现；建议合入前补跑一次 `clang-cl-debug` 或 ASan。

## 5. 建议

1. 修 R3-1（阻塞）：改为 find/contains 后再取数，两侧同查；修后 4 个用例中缺失类用例才真实生效。
2. 建议一并处理 R3-2（bad_alloc 边界通道），F1 才算完整闭环。
3. 合入前补跑 Debug/ASan 门禁。
