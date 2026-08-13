# US1 E2E（T044）第 1 轮审查记录

- 审查对象：分支 `feature/us1-e2e`（基线 `feature/us1-command-ui`）
- 增量提交：`029081a`（E2E 测试）、`3b1be80`（复制 sim_headless.exe + 收窄 DLL 通配）、`f31ae02`（tasks.md 标记 T044）
- 审查方式：全程只读。仅用 `git log/show/diff` 读取对象库，未读工作区文件、未做任何写操作/checkout/push/merge/approve
- 审查范围：`git diff feature/us1-command-ui...feature/us1-e2e`（4 文件，+394/−3）
- 前置信息：实现代理报告本地门禁通过（CTest 333/333、dotnet 159/159、format 全过），本轮为静态审查

## 结论

**PASS（高置信度 ≈ 0.9）**。四条重点核对全部通过，未发现新 🔴；给出 1 条 🟡（进程无超时护栏）与 3 条 💭（见末尾 findings）。

说明：本轮为静态审查，CI 的 App job 尚未实跑；但产物上传/下载路径已按 GitHub Actions 文档语义核对一致（见第 3 节）。

## 1. E2E 断言有效性 —— 通过

核对“UI 桥与 headless 是否真正同构”这一核心问题，证据如下：

| 核对项 | 证据 |
| --- | --- |
| 相同 scenario/seed/threads/命令 | 测试常量 `Seed=42/Threads=1`，两条路径均传入同一 `scenarioPath` 与同一 `commandJson`（`HeadlessParityE2ETests.cs:112,146-151`） |
| tick 0 注入 + 同 tick 数推进 | headless `inject_headless` 先逐行 `inject_player_command`（此时 clock 为 tick 0）再 `StepTicks`（`headless_driver.cpp`）；UI 桥 `wfs_sim_create` 后 `InjectCommand` 再循环 `Step()` 14000 次。`StepTicks` 与 `wfs_sim_step` 共用同一 `step_sim_state`（`headless_driver.cpp` 注释与实现、`c_api.cpp`），操作序列严格对齐 |
| state hash 逐字节一致 | 两侧同用 `compute_state_hash_hex`（`state_serialization.h`，`wfs_sim_get_state_hash` 与 headless `--hash` 同一函数）；`wfs_sim_create` 与 headless `LoadState` 均以 `Rng(seed,0)` + `GameClock(tick_hz)` + `initialize_runtime_state` 构造状态，threads 不进哈希 |
| 事件序列一致 | 两侧事件同源于同一 `EventLog`（容量默认 5000、驱逐策略确定）：UI `wfs_sim_query_events("{}")` limit=0 返回全部保留事件，headless JSONL 写出 `event_log.events()`，seq 均升序，故 `Assert.Equal(headless.Messages, ui.Messages)` 有实义；即便超容量，两侧截断也逐条一致 |
| 命令链路 | `COMMAND_ISSUED → COMMAND_ACKNOWLEDGED → UNIT_MOVING → MISSION_COMPLETED` 前缀与实际 message 文本逐字吻合（`ai_inject.cpp:167`、`command_chain.cpp:152`、`movement.cpp:258`、`mission_exec.cpp:318`，`complete_mission` 的 `reason=REACH_POINT` 来自 `mission_exec.cpp:206`） |
| 战斗结算 | `COMBAT_HIT`/`COMBAT_AREA_HIT` 前缀与 `combat.cpp:886/935` 吻合；教程剧本敌单位在 `(1.15,1.2)/(1.2,1.25)`，MOVE 目标 `(0.95,0.95)`，会进入交战 |
| 通讯延迟 60–200 tick | 场景未覆盖 `command_delay`，核心默认 `min_seconds=3.0/max_seconds=10.0`（`command_chain.h`），20 Hz 下 `ceil(3*20)=60`、`ceil(10*20)=200`，`Assert.InRange(60,200)` 边界正确 |
| 命令 JSON 由生产代码构造 | `BuildMoveCommand` 走 `CommandJsonBuilder.Build(draft)`（`HeadlessParityE2ETests.cs:106`），非手写黄金；键序/UTF-8 与 `command.schema.json` 一致（且已有 `CommandJsonBuilderTests`） |
| 同输入两次重跑一致 | `SameInput_RerunTwice_YieldsSameHashAndEventSequence` 覆盖两侧各自两次运行哈希+事件序列，及跨形态 `headlessFirst == uiFirst` |

另核实无“假通过”路径：`--hash` 是 stdout 唯一内容，测试取最后非空行并用 `^[0-9a-f]{64}$` 校验；headless 非 0 退出码、缺事件文件、非法 JSONL 行均抛中文异常。

## 2. 产物接线 —— 通过

`app/Directory.Build.targets`：

- 通配收窄 `**\*.dll` → `sim\*.dll`（第 17 行）正确。旧通配会同时命中 `vcpkg_installed` 下 release/debug 两个同名 `json-schema-validator.dll`，MSBuild 逐项拷贝到同一 `$(OutDir)` 时“后者覆盖前者”且枚举顺序不保证，混入 Debug CRT 会导致 `wfs_sim_create` 访问冲突；收窄后只取 CMake POST_BUILD 放置了正确 Release 版 validator 的 `sim\` 目录（`native/sim/CMakeLists.txt` 的 `$<TARGET_RUNTIME_DLLS:sim_core>`）。
- 不会漏运行时 DLL：`sim_core.dll` 的非系统依赖只有 validator（nlohmann_json 为头文件、`core_c`/`sim` 为静态链接），MSVC 运行时由系统提供；`sim_headless.exe` 同目录也已由 POST_BUILD 放置 validator，复制到测试输出目录后依赖齐全。
- 缺失产物语义符合要求：构建期仅提示跳过（不阻断 C# 构建），运行期由测试抛出可操作中文错误并 fail（`RequireSimCoreDll`/`LocateHeadlessExe`，`HeadlessParityE2ETests.cs:296-330`），失败语义正确而非静默通过。
- 测试工程显式开启 `WfsCopyHeadlessExe=true`（`WarFictionSim.Ui.Tests.csproj`），exe 与依赖 DLL 落同一输出目录，`ProcessStartInfo` 可直接驱动。

## 3. CI 产物可达性（App job 能否拿到 native 产物）—— 通过

- native job 上传三个 path（`.wfs-artifact`、`**/*.dll`、`**/*.exe`）。按 `actions/upload-artifact` v4+ 文档规则，多路径上传以“所有搜索路径的最小公共祖先”为 artifact 根目录，此处 LCA 为 `native/build/clang-cl-release/`，故条目为 `sim/sim_core.dll`、`sim/sim_headless.exe` 等（相对 LCA）。
- App job `download-artifact@v7` 以 `path: native/build/clang-cl-release` 解压，正好还原到 `native/build/clang-cl-release/sim/…`，与 targets 的 `WfsSimCoreDir=$(WfsNativeBuildDir)\sim`、`WfsHeadlessExePath=…\sim\sim_headless.exe` 逐级吻合；`dotnet build` 在下载之后执行，复制与测试顺序正确；App job 依赖同一 workflow 的 native job（`needs: native-build`），产物同提交、无陈旧风险。
- 测试工程在 `app/WarFictionSim.sln` 内，CI 的 `dotnet build/test` 会执行该 E2E。

遗留观察（out-of-scope，非本分支引入）：`.wfs-artifact` 是隐藏文件，upload-artifact v4+ 默认 `include-hidden-files:false`，骨架期“保证有 artifact”的兜底实际不生效；当前有真实 DLL/exe 产物，不影响本分支。

## 4. 无新问题 / 增量范围 —— 通过

- 增量仅 4 文件、3 提交：测试文件（029081a）、targets+csproj（3b1be80）、tasks.md 勾选（f31ae02），与 T044 范围一致，无夹带修改。
- tasks.md 仅翻转 T044 复选框，任务描述与 quickstart §3.1–3.3 的“同输入哈希一致、命令链路、战斗结算、无头 CLI 与 UI 状态哈希一致”完全对应。

## Findings

新 🔴：无。

新 🟡：

- 🟡 **Process 无超时护栏** — `HeadlessParityE2ETests.cs:162`：`WaitForExit()` 无限期阻塞。若 `sim_headless` 因核心回归卡死（此前 `--ticks -1` 曾可致死循环，见 `main.cpp` 的防御注释），测试将无限挂起，只能靠 CI job 级超时兜底且现场信息少。建议 `WaitForExitAsync(TimeSpan)` + 超时 `Kill(entireProcessTree:true)` 并抛出带退出码/输出的中文异常。

新 💭：

- 💭 **stdout 先于 stderr 顺序读** — `HeadlessParityE2ETests.cs:160-161`：先 `ReadToEnd` stdout 再 stderr，对“大量写 stderr 的子进程”存在管道缓冲死锁的理论风险；当前 CLI stderr 极小，实际风险低，可与上一条一起改为异步读取+超时。
- 💭 **事件序列只比对 message 字符串** — `HeadlessParityE2ETests.cs:64,123,202`：未比对 `(seq,tick)`。若两路径“同一事件文本出现在不同 tick”的时序漂移，message 相等断言发现不了（终态 state hash 只覆盖终态）。命令类 message 已含 `issue_tick/arrival_tick`，覆盖尚可；建议在注释中明确“只比对 message 是有意契约”，或让 `ReadEventMessages` 同时取 `tick/seq` 做三元组比较。
- 💭 **与核心 message 文本硬耦合** — `HeadlessParityE2ETests.cs:224-239`：断言前缀与延迟区间重复了核心内部文案/配置，核心改动会破坏 E2E。作为“契约锁定”可接受；若想降低维护成本，可考虑与 `cli_tests` 的 PowerShell 断言共用事件名前缀常量（当前两处各自维护）。

## 表扬

- 用生产 `CommandJsonBuilder` 构造命令而非手写黄金 JSON，并明确注释“与命令面板同一序列化器”，正确锁定了契约源头。
- 文件头注释把“tick 0 注入 + 同 tick 推进”这一逐字节可比性的前提讲清楚了，避免后人误改。
- DLL 通配收窄的根因注释（Debug CRT 混用 → 访问冲突）与“缺失产物构建期跳过、运行期可操作中文报错并失败”的 fail 语义设计清晰、可执行。
- `AssertStateHash`/`AssertCommandChain`/`AssertCombatSettlement` 拆分得当，断言信息均为可读中文。
