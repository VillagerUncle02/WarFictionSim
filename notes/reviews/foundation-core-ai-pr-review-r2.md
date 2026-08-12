# AI PR 审查结论 — PR #109 第 3 轮（r2 重定位复核，foundation-core-ai @ 2ff967f）

- 分支：`feature/foundation-core-ai`
- PR：https://github.com/VillagerUncle02/WarFictionSim/pull/109
- 本轮 head：`2ff967f`（fix(native): adapt golden/CLI test paths to native/ layout）
- 审查方式：只读代码审查（git show / git diff 三点增量），未运行构建、未 push/merge/approve
- 结论：**FAIL（置信度 0.90）**——1 个 🔴（sim_core C ABI DLL 目标被静默删除），其余验证点全部通过

## 1. 审查范围

- 任务：T019（无头 CLI run/inject/save + JSONL）、T020（AI 后端抽象与决策注入通道）、
  T021（无 AI 脚本后端，确定性兜底）、T022（黄金确定性测试框架）。
- 增量：`git diff feature/foundation-core-parallel...feature/foundation-core-ai`
  （merge-base = `fdb8241`，即 parallel 分支 tip；共 33 文件，+2570/-70）。
- 提交链：6ffea15（T020）→ b91c56c（T021）→ a42d89a（T019）→ 6a354fb（T022）→
  dde4b9f（格式）→ 7338806 / a369f77 / e65ec52（审查修复）→ cc16467（前轮记录）→
  7cf094e（散落头文件/main 迁移 native/sim）→ 2ff967f（golden/CLI 路径适配）。
- 本轮重点：最新两提交 7cf094e（纯 R100 移动，0 行变更）与 2ff967f（3 文件路径适配）。

## 2. 对比上一轮（前轮：MCP r1 FAIL 4911780946 → r2 PASS 4911852695 @ 20b74ae）

| 前轮条目 | HEAD 复验结果 |
| --- | --- |
| F1 T080 前置（注入/step 加锁、threads 接入战斗路径） | ✅ tasks.md T080 已登记前置跟踪；代码中线程安全契约注释同步（queue.h / ai_inject.h） |
| F2 --threads 严格解析（拒绝符号、用法错误退出码 2） | ✅ main.cpp:95-114 ParseInt 拒绝 '-'/'+'/非数字；run_cli_tests.ps1 第 10 组断言 -1/+1 退出码 2 |
| r2 ai- 前缀保留、旧存档对称、decision_id 唯一 | ✅ ai_inject.cpp:96-113 + save.cpp 游标/重复校验 + snapshot.cpp 空值省略，均有测试固定 |
| 前轮未发现项 | ❗ 本轮新增：sim_core DLL 目标删除（见 R1），自 6ffea15 即存在，前两轮漏检 |

## 3. Findings 表

| # | 级别 | file:line | 问题 | 修复方向 | 状态 |
| --- | --- | --- | --- | --- | --- |
| R1 | 🔴 | native/sim/CMakeLists.txt:42（6ffea15 删除原 sim_core SHARED 块，现状仅剩 sim_headless） | C ABI 桥接 DLL `sim_core` 不再被构建，而 native/CMakeLists.txt:7、README.md:33、docs/toolchain.md:9/27、app/Directory.Build.targets:2 仍宣称/依赖其产出；CI 无法发现（缺 DLL 时 skip/warn），违反宪法 §14 与 contracts/sim-c-api.md | 恢复 `add_library(sim_core SHARED src/c_api.cpp)`（链接 sim + 运行期 DLL 复制），与 sim_headless 并存；若确属有意暂缓，须在提交/tasks 显式登记并同步文档，禁止静默删除 | 开放 |
| N1 | 💭 | native/sim/headless/main.cpp:1、native/sim/include/wfs/sim/headless.h:1 等 7cf094e 移动的 5 个文件 | 文件级总览注释仍写 `// sim/headless/main.cpp`、`// sim/include/...` 旧路径，与 native/ 布局不符（纯注释，不影响编译） | 更新为 native/sim/... 实际路径 | 开放 |
| N2 | 💭 | specs/001-war-sim-command-battle/tasks.md:59-62（及后续 T023+ 的 tests/、sim/ 路径） | native/ 重定位后任务清单路径未同步，后续按 spec-kit 执行任务会定位到错误目录 | 批量加 native/ 前缀或补充路径约定说明 | 开放 |
| N3 | 💭 | native/sim/src/ai_inject.cpp:96-113 | reserved/duplicate decision_id 的拒绝只写事件日志、不写决策日志，与头文件契约"每个决策点（含拒绝）记录"不一致（§10 复盘/回放会漏这两类尝试）；不影响确定性，测试刻意固定此行为 | 二选一：将这两类拒绝也 append 到决策日志，或修订 ai_inject.h 契约说明该例外 | 开放 |
| N4 | 💭 | .github/workflows/ci.yml:131-151（前序遗留，不在本增量内） | CI "无头黄金/集成"步骤依赖不存在的 data/scenarios/scn-battalion-v1.json 与不存在的 load-save 子命令，恒被跳过；等价覆盖已由 CTest golden/cli/save 承担，无覆盖缺口，但步骤名与行为不符 | 后续改为 scn-smoke-test.json，去掉或实现 load-save | 备注 |

## 4. 特别验证点结论

1. 重构适配 ✅
   - include 指令全部为 `wfs/sim/...`，无旧布局残留（git grep 复核，仅注释行含旧路径）。
   - `add_executable(sim_headless headless/main.cpp)` 与 native/sim/headless/main.cpp 位置一致；
     sim_headless PRIVATE include = native/sim/src，内部头 ai_inject.h/sim_runtime.h/sim_state.h 就位。
   - golden 测试：WFS_SOURCE_ROOT=`${PROJECT_SOURCE_DIR}/..`（=仓库根），场景 `data/scenarios/scn-smoke-test.json`
     与哈希文件 `native/tests/sim_tests/golden/golden-run.hash` 均存在且路径正确（2ff967f 修正）。
   - cli_tests：`${PROJECT_SOURCE_DIR}/../data/scenarios/scn-smoke-test.json`（PROJECT_SOURCE_DIR=native）正确。
   - --threads 严格解析在 rebase 后仍有效（见第 2 节 F2 复验）。
2. AI 边界 ✅：注入经 T014 validate_command 后入队（ai_inject.cpp:121-132）；后端 decide() 为纯函数、
   不碰状态/RNG/现实时钟；script 输出与 LLM 同构且经校验（script_backend_test 固定）；非法拒绝不静默
   （AI_DECISION_REJECTED 事件 + 决策日志）；非法/超时降级与非 AI 兜底中，超时/熔断属 T080 后置（cloud 显式报错），
   script 兜底已实现。
3. 确定性 ✅：SameInputTwice / ThreadCount(1vs4) / CliHashMatchesLibraryDriver / CliHashMatchesCAbi /
   GoldenHashMatchesRecordedFile 五个 TEST 均在 golden_test.cpp，经 gtest_discover_tests(sim_tests) 注册进 CTest；
   状态哈希与决策输入摘要显式排除 threads。
4. 宪法合规 ✅/❌：文件级总览注释齐全（§5，含 N1 旧路径 nit）；错误不静默（§17：退出码 0/1/2、
   校验拒绝记录、cloud 未实现报错）；决策注入记录齐全（§10：input/events/output/validation/state_hash/arrival(tick,seq)，
   入存档与状态哈希）。❌ 唯一例外：§14 的 sim_core DLL 目标被删除（R1）。

## 5. 未验证猜测

- R1 大概率是"native/ 布局 rebase 冲突解决时误删"：merge-base fdb8241 标题即为
  "keep sim_core C ABI DLL target after layout rebase"，而删除它的 6ffea15 提交信息未提及，且删除后无任何替代/登记。
  高置信度猜测，待作者确认意图。
- 本轮未在本机重跑构建/CTest（依赖 vcpkg + clang-cl 工具链）；以绿 CI 双 run（head 2ff967f）作为间接验证。
- golden-run.hash 未手工重算，信任 CTest GoldenHashMatchesRecordedFile 在 CI 通过。
- "175 测试"为前轮记录值，本轮未重新计数，以 CI CTest 全绿为准。

## 6. 宪法合规要点

- §2 测试保障：T019-T022 均带测试（cli_tests、ai_inject/script_backend/save/c_api 单测、golden）✅
- §5 注释：全部新文件有文件级总览注释 ✅（含 N1 旧路径 nit）
- §7 确定性：状态哈希/决策摘要排除 threads；1vs4 黄金测试注册进 CTest ✅
- §8/§9 AI 边界：AI 只产出命令、经校验入队、不直接改状态；非法拒绝不静默；script 非 AI 兜底 ✅；
  事件触发/限频/超时熔断为 T080 后置，已在 tasks.md 登记 ✅
- §10 可复现性：决策点完整记录并进入存档与状态哈希 ✅（N3 为记录范围的小例外）
- §14 语言边界：C ABI 头/错误码契约未破坏，但 **sim_core DLL 构建目标被删除 → 违反** ❌（R1）
- §15 无头可测：sim_headless run/inject/save + JSONL；CMake target 与 main.cpp 位置一致 ✅
- §17 错误处理：参数错误退出码 2、运行错误 1、拒绝记录、cloud 未实现显式报错，无静默吞错 ✅

## 7. 整体结论

**FAIL（置信度 0.90）**

唯一阻断项：R1 —— native/sim/CMakeLists.txt 静默删除 `sim_core` C ABI DLL 目标，违反宪法 §14
（不可妥协）与 contracts/sim-c-api.md，且 CI 无法感知。修复成本低（恢复原 SHARED 块与 sim_headless 并存）。
其余 T019-T022 功能、重定位适配与确定性门禁均正确，无新增 🟡。

## 8. 收敛与轮次检测

- 轮次历史：逻辑组初查 → 3d2c69c → 6bef9f6 → MCP r1 FAIL → 20b74ae → MCP r2 PASS；
  本轮为 r2（记录文件命名 r2，实质第 3 轮）。
- 新变更检测：前轮记录提交 cc16467 之后新增 7cf094e（5 文件 R100 纯移动）与 2ff967f
  （golden/CLI 路径适配，仅路径字符串变更），触发本轮复核；两提交本身无逻辑问题。
- 前轮 head 20b74ae 非当前分支祖先（分支经 native/ 布局 rebase，等价提交为 e65ec52），
  既有记录中的 "final head 20b74ae" 已过期，需以 2ff967f 为准。
- 收敛性：前轮全部 findings 已在 HEAD 复验修复；R1 为前两轮漏检回归（自 6ffea15 存在），
  本轮全增量复核时发现，属"复核补漏"而非新增。
- CI 证据复验（gh 只读）：run 31590590928（push，head 2ff967f）success；
  run 31590594448（pull_request，head 2ff967f）success；PR #109 OPEN、mergeState=CLEAN、MERGEABLE。
- 声明：AI 未批准、未合并；修复 R1 后建议再跑一轮快速复核后由人工 Approve + Merge。
