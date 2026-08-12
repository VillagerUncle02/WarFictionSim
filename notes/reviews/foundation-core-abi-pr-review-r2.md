# AI 代码审查记录（r2）— PR #107 增量复核

- 仓库：VillagerUncle02/WarFictionSim（本地根 `D:\Workbench\Agent\NewProject`）
- PR：#107，分支 `feature/foundation-core-abi`，审查基线 HEAD `42c1700`
- 审查时间：2026-08-13
- 审查方式：只读静态审查（git show / git diff / 依赖源码核对），未修改任何源代码，未 push/merge/approve
- CI 证据：run `31613998031`（push，head 42c1700）success；run `31614002518`（pull_request，head 42c1700）success；mergeable=CLEAN
- 声明：AI 审查通过不代表已批准/已合并，等待人工 Approve + Merge。

## 1. 审查范围

本文件只覆盖 PR #107 的增量：T012（C ABI 边界层 c_api.cpp / c_api.h / c_api_test.cpp）、T013（场景加载与 Schema 校验 loader.cpp / schema_validator.cpp / loader_test.cpp）、T014（命令双重校验 command_validation.cpp / command_validation_test.cpp）及对应测试、CMake/测试路径的 native/ 布局适配。

三点差集提交：`git diff feature/foundation-core...feature/foundation-core-abi`

```text
42c1700 fix(sim): malformed JSON Schema must return structured SCHEMA_INVALID instead of aborting (PR #107 review F1)
54fe3b2 fix(review): sync PR #106 review fixes (RNG single impl, clang-format, ci lint header-filter/log)
8760c0b build(native): adapt tests to native/ layout and add sim_core C ABI DLL target
528bcea docs: AI PR review record for PR #107 (PASS, round 2, via pr-ai-reviewer MCP)
d68476f fix(tools): support -BaseRef for chained-PR Closes diff
ecb097c fix(sim): resolve clang-tidy warnings in T012-T014
67ef89c feat(sim): C ABI boundary layer (T012)
ca0d208 feat(sim): command validation pipeline (T014)
78cce30 feat(sim): scenario loader with schema validation (T013)
```

本次重点核对：42c1700（畸形 Schema 的 JSON_ASSERT 修复）与 8760c0b/54fe3b2（native/ 目录重构适配）。

排除项（按任务要求不计入本轮新 findings）：`rng.h/rng.cpp` 的改动来自 54fe3b2 同步前序 PR #106 已审查内容；`scripts/*.ps1` 与 `d68476f` 属工具链改进；`528bcea` 为归档文档。

## 2. 与上一轮对比

分支内归档记录 `notes/reviews/foundation-core-abi-pr-review.md` 为 MCP 流程第 2 轮 PASS（head 3808b5c），其 F1 是 json-schema-validator 许可证/维护状态核实（已闭环）。该记录归档后又落地三个提交，构成本轮复核：

- **已修复（验证通过）**
  - native/ 布局适配（8760c0b）：`native/sim/CMakeLists.txt`、`native/tests/sim_tests/CMakeLists.txt` 的 include/目标/测试路径已一致，无旧编译路径残留；`sim_core`（C ABI DLL）目标、`WINDOWS_EXPORT_ALL_SYMBOLS` 过渡导出、静态 triplet 下 DLL 复制防护均已接线。
  - 畸形 Schema 结构化报错（42c1700）：`schema_validator.cpp:61-71` 的 validate() 捕获异常并重抛 `std::invalid_argument`；`loader.cpp:201-205` 与 `command_validation.cpp:249-253` 映射为 `SCHEMA_INVALID`；新增两条回归测试（见 §3 F3 的机制备注）。
  - c_api_test 悬垂 `c_str()`、`BUFFER_TOO_SMALL` 的 out_len 语义、`INVALID_DATA` 折叠语义、静态 triplet 的 DLL 复制防护（42c1700）。
- **回归**：未发现。42c1700 仅新增 try/catch 与测试，不改变既有违规排序（(pointer, message) 排序仍在 catch 之后执行）与错误码；CMake 适配保持 sim/sim_core 目标结构与 CI 一致。
- **新增发现**：三条 🟡 见 §3，均为 T013/T014 增量中的健壮性/注释问题，不构成阻断；其中 F3 是本轮对 F1 修复机制的深度核对结论。

## 3. Findings 表

| # | 级别 | file:line | 问题 | 修复方向 | 状态 |
|---|---|---|---|---|---|
| F1 | 🟡 | native/sim/src/schema_validator.cpp:56,67 | `catch (const std::exception&)` 把 `std::bad_alloc` 及校验器内部故障一并归为 `SCHEMA_INVALID`，内存耗尽/内部缺陷被伪装成“非法数据”，违反 §17“损坏状态可定位”。 | 只捕获可预期异常（type_error/parse_error/invalid_argument/runtime_error），`bad_alloc` 等重抛；在 loader/command_validation 边界保留 `INTERNAL_ERROR` 通道区分内部故障与坏数据。 | 待修复（建议） |
| F2 | 🟡 | native/sim/src/command_validation.cpp:264 | 公开 json 重载中 `command["schema_version"].get<int64_t>() != schema["schema_version"].get<int64_t>()` 无类型防护；若直接调用方传入未用 `type:integer` 约束的 Schema 或非整数版本号（如 1.5），nlohmann `type_error` 会逃逸，最终被 c_api 折叠为 `INTERNAL_ERROR` 而非结构化 `INVALID_DATA`/`SCHEMA_VERSION_MISMATCH`。主链路（string 重载 + 运行时 Schema）不受影响。 | 将该读取纳入 try/catch，或先 `is_number_integer()` 检查再取数；语义层保持“结构化错误而非异常”。 | 待修复（建议） |
| F3 | 🟡 | native/sim/src/schema_validator.cpp:7-18（及 42c1700 提交信息） | JSON_ASSERT 覆盖与注释把“畸形 Schema 不崩溃”归因于宏覆盖，但锁定的 json-schema-validator 2.4.0（vcpkg baseline c4d9956）parser 位于预编译 DLL（json-validator.cpp，含于 nlohmann_json_schema_validator.dll），其源码不含 JSON_ASSERT/assert；`required:"type"` 实际由 `get<vector<string>>()` 抛 type_error，真正兜底的是新增的 catch(std::exception&)。宏只影响本 TU 内 nlohmann/json.hpp 内部断言。行为正确、测试绿，但机制归因不准确，易误导后续维护。 | 修正注释与提交信息的归因描述（说明真实失败路径为 DLL 抛异常 + catch 兜底；宏覆盖仅保护本 TU 的 json.hpp 内部断言）；或补充针对真实 abort 场景（如未来 header-only 版本）的 Debug 测试。 | 待修正（文档/注释） |

💭 其他（不计入阻断）：

1. native/sim/CMakeLists.txt:15,36 — `src/c_api.cpp` 同时编入 `sim`（STATIC）与 `sim_core`（SHARED），同一 TU 重复编译；静态库中该 TU 当前未被引用、链接期不会重复定义，但两份编译产物后续易出现职责/版本漂移。可考虑仅放入 `sim_core` 或拆出独立 C ABI 目标。
2. 旧路径残留（文档/注释层，非编译路径）：`specs/.../contracts/sim-c-api.md:29` 仍写 `sim/src/c_api.cpp`；`specs/.../research.md:13` 仍写 `sim/src/parallel.cpp`；`specs/.../tasks.md:55-62` 中 T015–T022 仍写 `sim/src/...`；各新源文件头部注释第 1 行仍写 `// sim/src/...`（实际在 `native/sim/...`）。建议批量改为 `native/sim/...`。
3. native/sim/src/c_api.cpp:66-68,101-103 — `INTERNAL_ERROR` 无日志/错误详情；`wfs_sim_create` 失败仅返回 nullptr（契约无错误码通道，已在 c_api.h 文档化）。属 T015 事件日志落地前的阶段限制，但当前是定位注入/创建失败原因的主要盲区。
4. 轮次追踪断链：归档记录中 F1 是“json-schema-validator 许可证核实”，而 42c1700 提交信息中“PR #107 review F1”指畸形 Schema 问题，两者编号不匹配；第三轮发现未回写归档记录，建议补记。

## 4. 未验证猜测（未经运行证实，不视为确定问题）

1. **Debug 配置未由 CI 覆盖**：CI 仅构建/测试 `clang-cl-release`，无 Debug job。本地存在 `native/build/clang-cl-debug` 构建目录，但按只读审查约束未运行。基于依赖源码静态分析（2.4.0 parser 无 assert，`required` 非数组走 `get<vector<string>>()` 抛异常路径），推断 Debug 下同样返回结构化 SCHEMA_INVALID 而非 abort，但未实证。
2. **JSON_ASSERT 覆盖的剩余风险边界**：该宏只覆盖 schema_validator.cpp 这个 TU 的 nlohmann/json.hpp；loader.cpp/command_validation.cpp/c_api.cpp 等其他 TU 的 json.hpp 仍使用默认 `assert()`。当前这些 TU 的取数路径均有 Schema 前置约束，未发现可触发的断言点，但“跨 TU 断言语义不一致”的长期影响未评估。
3. **C# 侧端到端**：`WINDOWS_EXPORT_ALL_SYMBOLS` 过渡方案与 P/Invoke 运行期调用未在本审查运行验证（CI 有 dotnet build/test，但未复跑 UI 进程间调用）。

## 5. 宪法合规要点

- **§2 测试保障**：T012–T014 各有 GoogleTest 套件（c_api_test / loader_test / command_validation_test），含新增畸形 Schema 回归测试（loader_test.cpp:240-246、command_validation_test.cpp:237-257），覆盖真实语义畸形（`required` 为字符串）而非仅 JSON 语法错误。✔
- **§7 确定性**：Schema 违规按 (pointer, message) 排序；语义错误按固定顺序收集；快照 dump 对象键经 std::map 排序，跨调用确定。✔
- **§9 AI 边界**：`wfs_sim_inject_ai_decision` 与玩家命令共用同一结构/校验/队列（c_api.cpp:44-69,132-138）。✔
- **§12 数据驱动**：场景/命令均为 JSON + 运行时 Schema 校验；非法数据返回结构化错误不崩溃（loader.cpp:199-211、command_validation.cpp:245-261）。✔（机制归因见 F3，行为本身合规）
- **§14 语言边界**：C ABI 只暴露纯 C 函数、不透明句柄、纯数据缓冲；快照只读、注入为唯一写路径（c_api.h 文件级注释）。✔
- **§17 错误处理**：无静默吞错——NOT_IMPLEMENTED 显式报错，校验失败结构化返回。保留项：错误分类粒度偏粗（F1）、create 无错误码通道（c_api.h:11 已文档化，T015 日志待落地）。✔（附 🟡）
- **§5 注释**：全部新文件均有文件级总览注释并说明“为什么”。✔（路径字样需同步 native/，见 💭2）

## 6. 整体结论

**结论：PASS（置信度 0.90）**

无 🔴 阻断项。F1 行为闭环成立（结构化 SCHEMA_INVALID + 回归测试 + CI 绿），native/ 布局重构适配完整。三条 🟡 均为健壮性/注释问题：错误分类粒度（F1）、公开 json 重载缺少类型防护（F2）、F1 修复机制归因不准确（F3），均不改变当前正确行为，可随后续任务（T015 日志 / 代码清理）一并处理。

## 7. 收敛与轮次检测

- 本文件为用户侧 r2 复核记录；分支内归档记录为 MCP 流程第 2 轮 PASS（head 3808b5c），其后 8760c0b/54fe3b2/42c1700 为新一轮修复提交，即本轮基线。
- 收敛信号：无新增 🔴；F1 已闭环；本轮三条 🟡 中 F2/F3 属首轮未覆盖的增量内容（T014 公开重载、F1 机制核对），F1 为既有 catch 粒度问题；未出现与上轮矛盾或反复横跳的判定。
- 停止条件：若三条 🟡 全部为可选整改，则本 PR 可进入人工 Approve + Merge；建议至少修正 F3 的注释归因，避免后续维护者依赖不存在的安全机制。
