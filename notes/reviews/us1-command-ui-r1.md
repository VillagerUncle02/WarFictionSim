# US1 UI 逻辑组审查记录（第 1 轮）— us1-command-ui-r1

> 审查性质：只读（仅 git show/git diff/git log 读对象库，未读工作区、未做任何修改）
> 分支：`feature/us1-command-ui`；范围：`git diff 3856f58...HEAD`
> 提交：edefc5c(ci) + 8562822(测试) + f08d563(T043) + c153417(T039) + 5d2085c(T040) + a56397d(T041) + 6900229(T042) + b420189(集成壳)
> 结论：**FAIL（置信度 0.85）**——纯逻辑层质量高，但集成层未打通，且 T042 事件数据契约缺失。

## 审查范围

对照 `.specify/memory/constitution.md`（§2/§5/§14/§16/§17）、`specs/001-war-sim-command-battle/spec.md`（FR-001/002/027/031/033/034/035/044/045 与 SC-005/006/011）、`contracts/sim-c-api.md`、`command-schema.md`、`save-format.md`、`data-model.md`、`plan.md`，核对：

1. T043 P/Invoke 与 `native/sim/include/wfs/sim/c_api.h`/`src/c_api.cpp` 逐签名一致；ISimClient 只读消费、注入为唯一写路径的结构证据。
2. T039–T042 行为与 FR 语义（迷雾/识别档/来源/最后已知、三级校验、时间加速、事件日志）。
3. 事件日志数据缺口（快照仅计数摘要、无条目正文）。
4. ci.yml CMake 配置重试循环语义。
5. 测试质量与 RED→GREEN、TFM/ProjectReference 影响。
6. 宪法 §5 文件级注释合规。

## Findings 表

| # | 级别 | file:line | 问题 | 修复方向 | 状态 |
|---|---|---|---|---|---|
| F1 | 🔴 | native/sim/src/snapshot.cpp:75-80；app/ui/src/WarFictionSim.Ui/EventLogPanel/EventLogViewModel.cs:6；Interop/Snapshot/EventLogSummaryState.cs:4-5；ViewModels/GameScreenViewModel.cs:113-117 | 快照 `event_log` 只含 `size/capacity/critical_count`，C ABI 无事件查询函数；`EventLogViewModel.Append` 仅测试调用，注释自承"独立供给通道（见最终报告 TODO）"但该通道不存在 → 集成后日志列表恒空，回看/过滤/搜索/置顶形同虚设（FR-044 明确要求） | 在 C ABI 增加 `wfs_sim_query_events`（过滤 + 分页缓冲）或把有界条目并入快照；GameScreenViewModel 消费后 `Append` 进面板 | 实现自报，未修复 |
| F2 | 🔴 | ViewModels/GameScreenViewModel.cs:41；BattleMap/BattleMapViewModel.cs:30,106；CommandPanel/CommandPanelView.xaml:23-29 | 无人订阅 `BattleMap.UnitSelected`，命令面板也没有任何执行单位选择入口 → 集成后 `ExecutorIds` 恒为空，`TARGET_REQUIRED` 恒阻断，玩家无法下达任何命令，US1 指挥闭环断裂 | GameScreenViewModel 订阅 `UnitSelected` → `CommandPanel.SelectExecutor`；面板增加可见单位选择列表 | 未自报，未修复 |
| F3 | 🔴 | Interop/SimNativeBridge.cs:192-199；GameControls/SimulationPump.cs:52-64；ViewModels/GameScreenViewModel.cs:92-100 | `Dispose` 不加 `_gate` 锁直接 `NativeDestroy`，泵线程可能正在 `wfs_sim_step/get_snapshot` 内 → 原生 use-after-free；且泵对 `ObjectDisposedException` 无捕获（`StepOneTick` 只捕 SimNativeException）→ 返回主菜单/关窗时线程池未处理异常可崩溃进程 | Dispose 与在途调用互斥（加锁或先停泵等待回调退出）；捕获 ObjectDisposedException | 未自报，未修复 |
| F4 | 🟡 | BattleMap/MapVisibilityModel.cs:60-67 vs native/sim/include/wfs/sim/intel.h:98 | 来源 kind 词汇跨层不一致：核心为 `direct|sync|relay|expired`，UI 只映射 `echelon|peer`，`sync/relay` 落到兜底显示英文原文，FR-031/SC-005 的来源标注语义丢失；测试断言的是核心从不产生的 `echelon` 值 | 对齐词汇：UI 补 `sync/relay` 文案（同级经上级同步/更上级转发只标层级），或核心改发 UI 词汇，并写入契约 | 未修复 |
| F5 | 🟡 | BattleMap/MapVisibilityModel.cs:119-124；native/sim/src/intel.cpp（intel 记录 to_json 仅 tier） | 识别分档的信息内容残缺：FR-034 要求步兵 T1 显示数量、T2/T3 显示类型/型号/构成；当前 T1 只显示"不明步兵单位"（无数量）、T2/T3 一律 `unit.Type`，且快照没有档位化观察字段（数量/类型名/构成），分档形同虚设 | 快照 intel 记录增加 `observed_count/type_name/composition` 等档位字段，UI 按档渲染 | 未修复 |
| F6 | 🟡 | MainMenu/MainMenuViewModel.cs:171,234-235；ViewModels/MainWindowViewModel.cs:73-80 | 扮演节点选择不生效：菜单把 `SelectedNodeId` 写入 `GameStartRequest.PlayerNodeId`，但应用壳从不读取该字段，`wfs_sim_create` 只读场景 JSON 的 `player_node_id`，C ABI 无节点覆盖参数 → FR-001/002 的"选择指挥节点扮演"是摆设 | C ABI create/load 增加 player_node 参数（或运行时 setter），应用壳消费请求 | 未自报，未修复 |
| F7 | 🟡 | Interop/SimNativeBridge.cs:107-112 | 快照首探（4096B）若返回 OK（快照 ≤4KB），直接 `Parse(ReadOnlyMemory.Empty)` 丢弃真实内容；应解析首探缓冲。现实快照通常 >4KB 不触发，但属正确性缺陷 | OK 分支按 `written` 长度解析首探缓冲 | 未修复 |
| F8 | 🟡 | CommandPanel/CommandPanelViewModel.cs:33；ViewModels/GameScreenViewModel.cs:113 | 只订阅 `Draft.PropertyChanged`，`ExecutorIds`（ObservableCollection）增删不触发重校验——目前靠每帧 `ApplyContext` 全量 Clear+Add 数百个单位掩盖；每帧重建 `CommandableUnit` 与重绑 ComboBox 造成分配与下拉抖动，影响 60fps/UX | 订阅 `CollectionChanged`；ApplyContext 仅在内容变化时更新集合 | 未修复 |
| F9 | 🟡 | ViewModels/GameScreenViewModel.cs:116 | 每 50ms 渲染帧调 `GetStateHash`：核心对全状态（含最多 5000 条事件日志条目）序列化 + SHA-256，且与步进线程争同一把桥锁，8x 档位下可能阻塞步进/渲染 | 降频（如 1Hz）或按需显示哈希 | 未修复 |
| F10 | 🟡 | CommandPanel/CommandJsonBuilder.cs:44；contracts/schemas/command.schema.json（priority minimum:0） | 面板校验不检查优先级 ≥0，负值通过面板预检后被核心以 SCHEMA_INVALID 拒绝，违反 FR-045"错误在面板阻断、警告/建议不阻断"的分级语义 | 面板增加 `PRIORITY_NEGATIVE` 错误级校验 | 未修复 |
| F11 | 🟡 | ViewModels/GameScreenViewModel.cs:83-94 | `OnPresentationFrame` 只捕获 SimNativeException；字段级 ABI 漂移导致的 `SnapshotParseException` 逃逸到 DispatcherTimer → 未处理异常崩溃而非内联提示（宪法 §17） | 捕获 SnapshotParseException（或 Exception）并写入 StatusError | 未修复 |
| F12 | 🟡 | BattleMap/BattleMapView.xaml.cs:37-45（仅点选/拖拽/滚轮）；tasks.md T041 | FR-045/T041 要求"点选与框选结合"，框选（以及区域目标绘制/地图点选目标点）完全未实现，目标指定退化为表单式下拉/文本框 | 地图增加框选矩形交互；点选地图设定 point/zone 目标并联动命令草稿 | 未修复 |

### 💭 Nits

- Interop/ISimClient.cs:3-6：注释"命令注入是唯一修改模拟状态的写路径"表述不精确——`Save/LoadSave` 也经 ABI 改变状态（LoadSave 恢复状态），建议改为"所有状态变更均经核心 ABI 通道"。
- .github/workflows/ci.yml:111-128：重试循环无条件对所有失败重试 3 次（并非仅 503），确定性配置错误会白等约 35 秒；不掩盖失败（最终 throw），建议按退出码/日志区分瞬时与确定性失败。
- CommandPanel/CommandPanelView.xaml:75-86,104-107：数值 TextBox（点坐标/优先级/时限）用默认绑定，非法输入静默不更新源且无内联提示，与 FR-045"内嵌提示"有落差。
- EventLogPanel/EventLogEntryViewModel.cs：`FormatGameTime` 硬编码 20Hz；场景 `tick_hz` ≠ 20 时游戏时间显示错误，应注入 TickHz。
- EventLogPanel/EventLogViewModel.cs：本地环形缓冲与核心 `EventLog` 重复实现（驱逐/过滤语义镜像）；修复 F1 后应移除 UI 侧副本，以核心为单一事实源。

## 做得好的部分

- P/Invoke 与 `c_api.h` 逐签名一致：`cdecl` 调用约定、`LPUTF8Str` 字符串封送、`nuint` 对应 `size_t`、版本串按 `IntPtr` 读静态存储、`wfs_sim_result` 枚举值逐值对应（0–6）、ABI 版本 `0.1.0` 精确匹配；两段式快照读取 + 3 次上限、缓冲生命周期归调用方，符合 sim-c-api.md。
- 只读边界有结构证据：快照 DTO 全部 init-only + `IReadOnly*` 集合，`SnapshotReaderTests.AssertReadOnly` 用反射验证无公开 setter（宪法 §14）。
- 时间模型正确：加速只改变表现层 `StepsPerFrame`，核心 tick 恒 20Hz，暂停为独立状态（宪法 §16）。
- 迷雾/过期/最后已知语义与核心一致：`tick >= memory_until_tick` 删除、`last_seen < tick` 判 stale、最后动向来自情报记录而非敌方实时状态（防泄漏，SC-006）。
- 三级校验顺序与 native 一致（类型→目标→完成条件→弹药覆盖），错误阻断、警告/建议不阻断；核心拒绝回填面板错误，核心为最终权威。
- `SaveHeaderReader` 字段与 `save.cpp BuildHeader` 逐字段一致（magic/版本/header_len/blob_len/hash 偏移核对无误）。
- 宪法 §5：全部新增 `.cs`/`.xaml` 文件（含测试）都有文件级总览注释，公开类型有"为什么"注释。
- ci.yml 重试仅改 Configure 步骤，3 次上限、最终 throw 不掩盖真实失败，YAML/PowerShell 语义正确。

## 测试质量评估

- 覆盖扎实的部分：迷雾判定矩阵、三级校验判定表、命令 JSON 黄金串（与 command.schema.json 逐字段核对一致，含 `kind=units/refs` 批量形态）、快照双入口解析等价、存档头字段、时间控制状态机、事件日志本地环形、ABI 版本。98 项与 `[Fact]/[Theory]` 声明数（94 个，Theory 内联展开后约 98+）吻合。
- 覆盖盲区：无 `GameScreenViewModel` 与 `SimNativeBridge` 的任何测试 → F2/F3/F7/F11 全部漏网；事件日志测试只测本地环形，没有"快照→面板"链路；`EchelonSource_ShowsEchelonLabel` 断言了核心从不产生的词汇（替身自洽但误导）。
- RED→GREEN：提交顺序正确（8562822 测试先行、实现随后）；但对象库中没有 CI 运行结果，98 项是否实际全绿无法从 git 证据确认，须以 CI 为准。
- TFM/ProjectReference：测试工程 `net10.0-windows` + ProjectReference 到 WPF 工程在 windows-2022 + .NET 10 下理论可行（sln 三工程配置完整）；对 `dotnet format`/analyzers 门禁的实际影响未本地验证。

## 未验证猜测

1. 98 项测试在 CI 的实际通过状态（对象库无 CI 结果，且本轮不运行构建）。
2. `net10.0-windows` 测试工程 + ProjectReference 对 sln 构建与 dotnet format 的影响（未本地构建验证）。
3. 核心是否把下属/同级情报聚合进 `player_node_id` 的记录（T033 范围）：UI 假设成立，若核心不聚合，SC-005 也会破。
4. FR-044 后半段（结算页/主菜单回看事件日志、随存档持久化）与 FR-002"允许中途切换"选项均不在 T039–T043 交付清单内，属后续任务。

## 宪法合规要点

- §14：纯数据边界与"UI 不直改模拟状态"结构上成立（只读 DTO + 反射测试）；命令注入是唯一命令写路径成立；但 F3 的 Dispose 竞态破坏了桥接层自述的"同一句柄串行化"安全承诺。
- §16：加速只在表现层，合规。
- §17：大部分错误显式呈现（DLL 缺失、ABI 错配、核心拒绝回填、存档损坏、场景数据 issue）；例外是 F11（SnapshotParseException 崩溃）与 F3（ObjectDisposedException 崩溃）。
- §5：文件级总览注释与"为什么"注释齐全，合规。
- §2：98 项测试覆盖纯逻辑较扎实，但集成装配层零覆盖。

## 整体结论

**FAIL（置信度 0.85）**。三条 🔴 阻断合并：F2 使集成后的应用无法下达任何命令（US1 闭环断裂），F1 使 T042 的"回看/过滤/搜索/置顶"对真实战斗空转（契约级缺口，需在 C ABI/快照暴露事件条目），F3 是返回主菜单/关窗路径上的崩溃级竞态。纯逻辑层（校验、迷雾、JSON、时间模型、存档头）实现质量高、注释与测试规范，问题集中在跨层契约与集成装配。
