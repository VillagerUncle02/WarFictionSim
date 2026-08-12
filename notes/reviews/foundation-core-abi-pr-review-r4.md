# PR #107 复审记录（第 4 轮 · 轻量只读）

- 审查对象：PR #107（feature/foundation-core-abi），tip `47edfd3`，本轮修复提交 `7e0595f`
- 审查方式：仅使用 `git show` / `git diff` / `git log` 读取 git 对象；未读取工作区文件、未修改任何源码文件、未 push/merge/approve/checkout、未等待远端 CI
- 范围：逐条核对第 3 轮 R3-1/R3-2/R3-3/R3-4 是否闭环 + 新增回归测试是否真实覆盖 + Debug 下无 abort 的理由 + 是否引入新问题

## 结论：PASS（置信度 0.92）

第 3 轮 4 条 findings 全部闭环：R3-1 的 const `operator[]` UB 已消除，R3-2 两处边界 bad_alloc 均先重抛，R3-3 的超大 unsigned 已做范围防护并有真实测试，R3-4 的 Schema 侧缺键测试已补。错误码与主路径语义未漂移。

另识别 1 个 🟡（loader 语义层同类超大 unsigned 遗留，代码为既有、`7e0595f` 未触碰、不在第 3 轮 findings 内，不阻塞本轮闭环结论，建议下轮跟进）与 2 个 💭。

## 1. 第 3 轮 findings 修复核对（file:line 以 tip `47edfd3` 为准）

| 项 | 要求 | 实现位置 | 核对结果 |
|---|---|---|---|
| R3-1 缺键 UB | 先 `find()/contains()` 判键存在再取数；缺失/非整数/超大 unsigned（> INT64_MAX）均返回结构化 `SCHEMA_INVALID`；不触碰 const `operator[]`、不抛异常 | `native/sim/src/command_validation.cpp:274-287` | ✅ 闭环：两侧先 `find()`，任一 `end()` 即返回 `SCHEMA_INVALID`（:276-279）；`exceeds_int64` lambda 仅在 `is_number_unsigned()` 为真时取 `get<std::uint64_t>()`（:280-282），范围/类型守卫（:283-287）后才做 `get<std::int64_t>()` 比较（:288-289）。全程无 const `operator[]`，也无可能抛 `type_error` 的取数路径 |
| R3-2 bad_alloc 边界重抛 | 在通用 catch 前 `catch(const std::bad_alloc&){throw;}` | `command_validation.cpp:252-254`、`loader.cpp:204-206` | ✅ 两处均闭环：catch 顺序正确（bad_alloc 在 `catch(const std::exception&)` 之前），`<new>` 均已引入；schema_validator.cpp 内部同样只重抛 bad_alloc（此前已验证），因此内存耗尽不会在边界被折叠为 `SCHEMA_INVALID` |
| R3-3 超大 unsigned 防护 | `is_number_integer()` 对 number_unsigned 为 true，`get<int64_t>()` 会抛 type_error；先做范围判断 | `command_validation.cpp:280-287` + 测试 `command_validation_test.cpp:337-350` | ✅ 闭环（命令/校验命令侧）：> INT64_MAX 的 unsigned 返回 `SCHEMA_INVALID`，不再触达 `get<int64_t>()`；严格 Schema 主链路之外（公开 json 重载 + LaxSchema）也安全 |
| R3-4 Schema 侧缺键测试 | 新增「Schema 完全不含该键」的用例 | `command_validation_test.cpp:323-335` | ✅ 闭环：`lax_schema.erase("schema_version")` 后命中 schema 侧 `find()==end()` 分支 |

错误码/主路径语义核对：

- 缺失 → `SCHEMA_INVALID`，消息「schema_version 缺失（命令与 Schema 均须包含该字段）」（:276-279）；
- 越界/类型非法 → `SCHEMA_INVALID`，消息「schema_version 越界或类型非法（命令与 Schema 均须为 int64 范围内整数）」（:283-287）；
- 整数不一致 → 仍为 `SCHEMA_VERSION_MISMATCH`（:288-289），未漂移；
- 消息由 round 2 的单条合并文案拆分为两条更精确的文案，错误码不变，无测试依赖旧文案（测试仅断言 code 与 `schema_version` 子串）；
- string 重载预检 `command_validation.cpp:237` 用 `contains()` 短路后取数，原本就安全，行为未变；string 重载传入超大 unsigned 的 Schema 会落入 json 重载的范围守卫，同样结构化报错；
- `ValidCommandPasses` 等主路径测试未受影响。

## 2. 新增回归测试真实覆盖核对（native/tests/sim_tests/command_validation_test.cpp）

- `MissingSchemaVersionReturnsStructuredErrorWithoutThrow`（:270-283）✅ 修复后真实覆盖：`command.erase("schema_version")` 后命中 find 守卫，`EXPECT_NO_THROW` + `SCHEMA_INVALID` + 消息子串断言现在验证的是安全行为，不再是 round 3 指出的「Release 下靠 UB 侥幸变绿」。测试体本身无需改动（断言形式本来就正确），配合源码修复即完成「改真实安全断言」的要求。
- `MissingSchemaVersionKeyInSchemaReturnsStructuredErrorWithoutThrow`（:323-335）✅ 新增且真实：命令侧整数 1、Schema 侧完全缺键，LaxSchema（无 required、properties 为空）在 Schema 层放行后命中 schema 侧 `find()==end()` 分支。
- `HugeUnsignedSchemaVersionReturnsStructuredErrorWithoutThrow`（:337-350）✅ 新增且真实：命令侧 `9223372036854775808ULL`（INT64_MAX+1）为 number_unsigned，`is_number_integer()` 为 true、无法被旧守卫拦截，必须命中 `exceeds_int64` 才返回 `SCHEMA_INVALID`；断言验证了范围防护而非被其他分支短路。
- 既有 `String/`Float/`NonIntegerSchemaVersionInSchema` 三个用例不受影响，仍真实覆盖类型非法分支。

测试总数核对：`12 + 12 + 23 + 13 + 5 + 16 + 11 = 92`，与修复代理报告的 Release 92/92、Debug 92/92 一致。

## 3. Debug 下无 abort 的理由（成立）

- `command_validation.cpp` 编译单元未覆盖 `JSON_ASSERT`（仅 `schema_validator.cpp` 将其覆盖为抛 `runtime_error`），因此本文件内 `JSON_ASSERT` 默认映射为 `assert()`，Debug 下会直接中止进程；
- 修复后的 schema_version 路径完全不调用 const `operator[]`：`find()` 返回迭代器后直接解引用，缺失键走 `end()` 早退；`nlohmann::json::find()` 在非 object 上安全返回 `end()`（不触发 305/JSON_ASSERT）；
- `exceeds_int64` 的 `get<std::uint64_t>()` 由 `is_number_unsigned()` 短路保护，无越界取数；
- 最终 `get<std::int64_t>()`（:288-289）只在「两侧均为整数且无符号值 ≤ INT64_MAX」之后执行，不会抛 `type_error/out_of_range`；
- 三个新增/修正用例在 Debug 下均可走完结构化返回路径，不再触碰 `JSON_ASSERT` 的断言路径。结论：Debug 不 abort 的理由成立。

## 4. 新发现

### 🟡 loader 语义层超大 unsigned 遗留（`native/sim/src/loader.cpp:63-64`）

`ExtractScenarioData` 中 `root["schema_version"].get<std::int64_t>()` 与 `schema_file.schema["schema_version"].get<std::int64_t>()` 仍直接取 int64，无范围防护。`contracts/schemas/scenario.schema.json` 对 `schema_version` 仅约束 `type: integer + minimum: 1`（无 maximum），因此超大 unsigned（> INT64_MAX）能通过 JSON Schema 层，随后 `get<int64_t>()` 抛异常逃出 `load_scenario`；经 C API 会被 `catch(...)` 映射为 `INTERNAL_ERROR`，把「非法数据」误报为「内部故障」——与 R3-3 同一类问题在 loader 侧的镜像。

定性：非本轮引入（`7e0595f` 未触碰该行），不在第 3 轮 findings 内，故不阻塞本轮 PASS；建议下一轮对 loader 侧同样做 `find + is_number_integer + ≤ INT64_MAX` 守卫（或取 `get<std::uint64_t>` 后比较），并补一个 loader 侧超大 unsigned 用例。

### 💭 超大 unsigned 仅覆盖命令侧

`HugeUnsignedSchemaVersionReturnsStructuredErrorWithoutThrow` 只覆盖命令侧；Schema 侧超大 unsigned 未单测。两侧守卫由同一 `exceeds_int64` lambda 实现、逻辑对称，覆盖风险低，但补一条 Schema 侧用例更完备。

### 💭 语义层 const operator[]（既有，非本轮引入）

`command_validation.cpp:109-110/169-170/294-297` 的 `command["target"]`、`command["completion"]`、`command["behavior"]` 等仍是 const `operator[]`。主链路（严格 `command.schema.json`，required 字段齐全）由 Schema 层保证键存在，安全；仅当调用方通过公开 json 重载传入完全宽松且缺 required 的 Schema 时才可能命中缺键断言。此为第 3 轮之前已存在的 API 契约假设，本轮修复未触碰、也未重新引入，仅记录备查。

## 5. 验证与范围说明

- 运行验证以修复代理报告为准：Release 92/92、Debug 92/92 均通过；测试总数静态核对为 92，吻合。远端 CI 由 push 触发、仍在运行，本轮按要求未等待。
- 未独立编译/运行测试（只读复审）；上述 Debug 无 abort 结论为对 `find()`/`JSON_ASSERT` 语义的静态论证。
- 全程未修改工作区源码、未做任何 git 写操作。

## 6. 建议

1. 本轮闭环成立，可继续走合入流程；
2. 🟡 loader.cpp:63-64 建议下轮随手一并修复（改动很小、与 R3-3 同型）；
3. 若下轮处理 🟡，顺带补 loader 侧超大 unsigned 测试与命令校验的 Schema 侧超大 unsigned 测试。
