# US1 UI 逻辑组审查记录（第 2 轮）— us1-command-ui-r2

> 审查性质：只读（仅 `git log` / `git show` / `git diff` 读对象库，未读工作区代码、未做任何修改、未 push/merge/approve/checkout）
> 分支：`feature/us1-command-ui`（HEAD = ad46129）
> 本轮修复提交：145ae7a / 1ed3b5f / e7255e2（native `wfs_sim_query_events`）+ cecc9b6（事件日志消费）+ f784a2d（命令闭环与框选）+ 28db23b（Dispose/泵生命周期）+ ad46129（建议项）
> 对照基线：第 1 轮记录 notes/reviews/us1-command-ui-r1.md（工作区文件，未提交入分支）
> 结论：**PASS（置信度 0.9）**——第 1 轮 3 条 🔴 全部真实闭环；9 条 🟡 中 6 条闭环、3 条部分闭环（F5/F6/F12 遗留如实标注）；新增 3 条 🟡 与 2 条 💭，均不构成阻断。

## 一、3 条 🔴 逐条核对（file:line 以 HEAD 为准）

### F1 事件日志数据通道 —— ✅ 闭环

- native 侧新增 `wfs_sim_query_events`：声明 [c_api.h](D:/Workbench/Agent/NewProject/native/sim/include/wfs/sim/c_api.h:76)，实现 [c_api.cpp](D:/Workbench/Agent/NewProject/native/sim/src/c_api.cpp:200)。参数校验（空指针→INVALID_ARGUMENT）、坏 JSON/未知名称/字段类型错误/负 limit→INVALID_DATA、缓冲不足→BUFFER_TOO_SMALL 且 `out_len` 含 NUL，两段式读取语义与快照一致。
- 序列化同源：`events_to_json` 与存档/状态哈希的 `serialize_state_json` 共用同一形状（[state_serialization.h](D:/Workbench/Agent/NewProject/native/sim/src/state_serialization.h:32)、[snapshot.cpp](D:/Workbench/Agent/NewProject/native/sim/src/snapshot.cpp:49)）；快照本体仍只携带 `size/capacity/critical_count` 摘要（体积有界），正文经查询通道供给——口径一致且契约已写明。
- P/Invoke 契约一致：`NativeQueryEvents`（[SimNativeBridge.cs](D:/Workbench/Agent/NewProject/app/ui/src/WarFictionSim.Ui/Interop/SimNativeBridge.cs:135)）与 `c_api.h:76` 逐签名对应（cdecl、LPUTF8Str、`nuint`↔`size_t`）；契约文档补齐（specs/.../contracts/sim-c-api.md §事件日志查询）。
- 两段式读取正确：`NativeBufferReader` 首探成功即按实际写出长度解析（F7 同修，[NativeBufferReader.cs](D:/Workbench/Agent/NewProject/app/ui/src/WarFictionSim.Ui/Interop/NativeBufferReader.cs:51)），不再丢弃短响应。
- 真实消费：`GameScreenViewModel` 以生产 client 构造 `EventLogViewModel`（[GameScreenViewModel.cs](D:/Workbench/Agent/NewProject/app/ui/src/WarFictionSim.Ui/ViewModels/GameScreenViewModel.cs:54)），`ApplySummary` 在快照 tick 前进时经 `QueryEvents` 拉取正文（[EventLogViewModel.cs](D:/Workbench/Agent/NewProject/app/ui/src/WarFictionSim.Ui/EventLogPanel/EventLogViewModel.cs:166)），不再是"仅测试调用"。
- critical 置顶：`PinCriticalEvents` → `OrderBy(critical first).ThenBy(seq)`（[EventLogViewModel.cs](D:/Workbench/Agent/NewProject/app/ui/src/WarFictionSim.Ui/EventLogPanel/EventLogViewModel.cs:215)）。
- 过滤/搜索语义：分类/最低严重级/文本下推 native，游戏时间窗口内本地过滤（native 契约无 tick 字段，处理正确）。**但 FR-044"按单位筛选"维度仍缺失**——见新发现 N3。

### F2 命令闭环 —— ✅ 闭环

- 地图→主屏→面板链路真实打通：`BattleMap.UnitSelected`/`UnitsSelected` → `GameScreenViewModel.OnBattleMapUnitSelected/OnBattleMapUnitsSelected`（[GameScreenViewModel.cs](D:/Workbench/Agent/NewProject/app/ui/src/WarFictionSim.Ui/ViewModels/GameScreenViewModel.cs:176)）→ `SyncExecutors` 按 `_commandContext` 过滤己方单位（:183）→ `CommandPanel.SetExecutors`（:200）→ `Draft.ExecutorIds`。
- 点选/框选实现：兵牌按钮命令 + 左键拖拽选择矩形（[BattleMapView.xaml.cs](D:/Workbench/Agent/NewProject/app/ui/src/WarFictionSim.Ui/BattleMap/BattleMapView.xaml.cs:87)）、`SelectUnit` 重复点选取消、`SelectUnitsInScreenRect` 默认只取己方（[BattleMapViewModel.cs](D:/Workbench/Agent/NewProject/app/ui/src/WarFictionSim.Ui/BattleMap/BattleMapViewModel.cs:125)）、Esc 清除；平移改右键拖拽、滚轮缩放不变。
- 空选择错误：`TARGET_REQUIRED`「未选择执行单位：请在地图上点选/框选至少一个己方单位。」（[CommandValidationRules.cs](D:/Workbench/Agent/NewProject/app/ui/src/WarFictionSim.Ui/CommandPanel/CommandValidationRules.cs:40)），`CanSubmit=false`。

### F3 Dispose / 泵生命周期 —— ✅ 闭环

- `SimNativeBridge.Dispose` 与全部 `wfs_sim_*` 共用同一把 `_gate`：锁内置 `_disposed`、置空句柄再 `NativeDestroy`（[SimNativeBridge.cs](D:/Workbench/Agent/NewProject/app/ui/src/WarFictionSim.Ui/Interop/SimNativeBridge.cs:184)），杜绝步进线程在原生函数内被 use-after-free。
- `SimulationPump.Stop` 先停表再 `WaitForPumpExit` 等待在途 step（[SimulationPump.cs](D:/Workbench/Agent/NewProject/app/ui/src/WarFictionSim.Ui/GameControls/SimulationPump.cs:52)）；`Dispose` 复用该顺序后才销毁计时器；重叠定时器回调经 `_running/_pumping` 跳过。锁顺序无环（泵锁在 step 动作外释放，桥锁单独持有），无死锁。
- 关窗顺序正确：`MainWindow_Closed` 先 `_pump?.Dispose()` 再 `ViewModel?.Dispose()`（[MainWindow.xaml.cs](D:/Workbench/Agent/NewProject/app/ui/src/WarFictionSim.Ui/MainWindow.xaml.cs:74)）。
- 异常捕获：`StepOneTick` 捕 `ObjectDisposedException`；`OnPresentationFrame` 捕 `ObjectDisposedException` 与 `SnapshotParseException`（[GameScreenViewModel.cs](D:/Workbench/Agent/NewProject/app/ui/src/WarFictionSim.Ui/ViewModels/GameScreenViewModel.cs:89)）；渲染定时器兜底捕获并停表（[MainWindow.xaml.cs](D:/Workbench/Agent/NewProject/app/ui/src/WarFictionSim.Ui/MainWindow.xaml.cs:48)）。

## 二、9 条 🟡 逐条核对

| # | 状态 | 证据 |
|---|---|---|
| F4 来源词汇 | ✅ 闭环 | `ResolveSourceLabel` 映射 direct/sync/relay/expired 为中文 + 未知值原文兜底（[MapVisibilityModel.cs](D:/Workbench/Agent/NewProject/app/ui/src/WarFictionSim.Ui/BattleMap/MapVisibilityModel.cs:60)）；核心稳定词汇经 native/sim/include/wfs/sim/intel.h 确认一致；测试覆盖 sync/relay/未知值。 |
| F5 识别档位 | 🟡 部分 | UI 侧闭环：`IntelRecordState` 可选 `observed_count/type_name/composition`（[IntelRecordState.cs](D:/Workbench/Agent/NewProject/app/ui/src/WarFictionSim.Ui/Interop/Snapshot/IntelRecordState.cs:31)）+ `SnapshotReader` 可选解析 + `BuildDisplayName` 按档渲染、缺字段不编造（[MapVisibilityModel.cs](D:/Workbench/Agent/NewProject/app/ui/src/WarFictionSim.Ui/BattleMap/MapVisibilityModel.cs:144)）。**遗留**：core 的 intel to_json 仍未输出这三个字段，且 TODO 指向「T058」（层级化情报同步），并非档位字段输出任务（T033 已标完成且未含该字段），跟踪引用不准确——建议在 T033 或新任务登记 core 侧字段输出。 |
| F6 扮演节点 | 🟡 部分 | 假路径已移除（`GameStartRequest` 不再携带 PlayerNodeId）；固定节点场景禁用改选并给出说明、无 `player_node_id` 场景列出节点但注明「v1 由场景数据决定，此选择暂不生效」（[MainMenuViewModel.cs](D:/Workbench/Agent/NewProject/app/ui/src/WarFictionSim.Ui/MainMenu/MainMenuViewModel.cs:98)）。从「静默失效」降级为「透明不支持」是诚实处理，但 FR-001/002 的「选择该规模内一个指挥节点扮演」功能仍未实现（C ABI 无节点覆盖参数），需核心侧参数或登记任务，不能算功能闭环。 |
| F7 首探缓冲 | ✅ 闭环 | `NativeBufferReader` OK 分支按 `required` 解析首探缓冲（[NativeBufferReader.cs](D:/Workbench/Agent/NewProject/app/ui/src/WarFictionSim.Ui/Interop/NativeBufferReader.cs:86)），测试 `FirstProbeSucceeds_ReturnsProbeContent`。 |
| F8 集合通知/每帧重建 | ✅ 闭环 | `CommandPanelViewModel` 订阅 `ExecutorIds.CollectionChanged` 触发重校验；`ApplyContext` 内容不变不重建（`CommandableUnit` 值相等），[CommandPanelViewModel.cs](D:/Workbench/Agent/NewProject/app/ui/src/WarFictionSim.Ui/CommandPanel/CommandPanelViewModel.cs:147)。 |
| F9 状态哈希节流 | ✅ 闭环 | 运行中 1Hz 节流、暂停时实时（[GameScreenViewModel.cs](D:/Workbench/Agent/NewProject/app/ui/src/WarFictionSim.Ui/ViewModels/GameScreenViewModel.cs:204)），`TimeProvider` 注入可测。 |
| F10 负优先级 | ✅ 闭环 | `PRIORITY_NEGATIVE` 面板阻断（[CommandValidationRules.cs](D:/Workbench/Agent/NewProject/app/ui/src/WarFictionSim.Ui/CommandPanel/CommandValidationRules.cs:95)）+ `CommandJsonBuilder.Build` 防御性抛错。 |
| F11 SnapshotParseException 逃逸 | ✅ 闭环 | `OnPresentationFrame` 捕获转中文状态栏；`MainWindow` 渲染定时器兜底捕获停表。 |
| F12 框选/地图目标点选 | 🟡 部分 | 框选交互已实现，但当前语义是「框选己方执行单位」，FR-045 的「区域目标框选/绘制、地图点选目标点」仍未实现——point/zone 目标仍是表单式下拉/坐标文本框（CommandPanelView.xaml 区域/单位目标 ComboBox + PointX/Y TextBox）。后半段遗留。 |

### Nits 核对

- tick_hz：✅ 闭环。`EventLogEntryViewModel` 注入场景 `tickHz`（[EventLogEntryViewModel.cs](D:/Workbench/Agent/NewProject/app/ui/src/WarFictionSim.Ui/EventLogPanel/EventLogEntryViewModel.cs:24)），测试覆盖 20Hz/40Hz 折算。
- UI 侧环形驱逐副本：✅ 已删除（`EventLogFilter.cs` 移除，对象库无遗留引用）。
- ISimClient 注释：❌ 未改。「注入玩家命令——UI 修改模拟状态的唯一写路径」仍与 `LoadSave` 也是状态变更通道的事实不一致（[ISimClient.cs](D:/Workbench/Agent/NewProject/app/ui/src/WarFictionSim.Ui/Interop/ISimClient.cs:30)），建议改为「所有状态变更均经核心 ABI 通道」。
- ci.yml 重试循环：本轮未触及，不在审查清单内，维持 r1 意见。

## 三、本轮新发现

### 🟡 N1 事件日志每帧全量拉取 + 全量重建（性能/滚动 UX）

[EventLogViewModel.cs](D:/Workbench/Agent/NewProject/app/ui/src/WarFictionSim.Ui/EventLogPanel/EventLogViewModel.cs:166)：查询不携带 limit，native 每次序列化至多 5000 条、UI 全部解析后 `TakeLast(500)`；运行中快照 tick 每渲染帧前进即触发一次（[GameScreenViewModel.cs](D:/Workbench/Agent/NewProject/app/ui/src/WarFictionSim.Ui/ViewModels/GameScreenViewModel.cs:138)），且 `Refresh` 对 500 条 `Entries` 全量 Clear+Add（:215）——ListBox 滚动位置每 50ms 复位、GC 抖动，同时该查询持有桥锁与步进线程争锁（8x 档位下放大）。建议：仅在 `summary.Size` 变化时重拉，或给 native 增加「最近 N」/offset 语义，并改为增量更新 Entries。

### 🟡 N2 新增 ABI 入口但版本串未升版（错配时崩溃路径）

`wfs_sim_query_events` 新增后 `WFS_SIM_VERSION_STRING` 仍为 `"0.1.0"`（[c_api.h](D:/Workbench/Agent/NewProject/native/sim/include/wfs/sim/c_api.h:32)），UI `SimAbiVersion.Expected` 同为 `"0.1.0"`。若部署了旧版 sim_core.dll（同版本串、无该入口），版本校验会放行，首次 `QueryEvents` 抛 `EntryPointNotFoundException`——`OnPresentationFrame` 与 `RenderTimer_Tick` 均不捕获该异常，经 DispatcherTimer 逃逸即进程崩溃。建议：升版 ABI 版本（存档头需兼容处理），或至少在桥调用侧捕获 `EntryPointNotFoundException` 转为可操作文案。

### 🟡 N3 FR-044「按单位筛选」维度缺失

FR-044 要求按「分类、单位、游戏时间」筛选；查询契约只有 category/min_severity/text/limit（sim-c-api.md:37-43），`SimEvent` 无结构化 unit 字段，单位筛选只能靠文本搜索间接覆盖。建议给事件增加结构化 unit 维度（或把该缺口显式登记为已知限制），否则 FR-044 语义未完全满足。

### 💭 N4 框选命中用兵牌锚点

`SelectUnitsInScreenRect` 只判 `ScreenX/ScreenY` 单点是否落入矩形（[BattleMapViewModel.cs](D:/Workbench/Agent/NewProject/app/ui/src/WarFictionSim.Ui/BattleMap/BattleMapViewModel.cs:127)），兵牌矩形与框选矩形相交但锚点在外时不会命中，轻微 UX 偏差。

### 💭 N5 返回主菜单不停泵

`MainWindowViewModel.BackToMenu` 释放 Game/句柄但不停 `SimulationPump`（泵归 MainWindow 代码后置持有），泵靠 `ViewModel?.Game ?? 0` 空转至窗口关闭；期间竞态由桥锁 + `ObjectDisposedException` 捕获兜底（不崩溃），但生命周期不干净。建议把泵纳入 ViewModel 或由壳在切换时显式 Stop/Start。

## 四、测试覆盖评估（145 项）

- 静态计数：138 个 `[Fact]` + `MapVisibilityModelTests` 2 个 `[InlineData]` + `SimAbiVersionTests` 5 个 `[InlineData]` = **145 项**，与修复代理报告的 dotnet 145/145 吻合（对象库无 CI 运行结果，未复跑）。
- 修复点覆盖真实：F2 闭环（GameScreenViewModelTests 4 项选择接线/空选择）、F3（SimulationPumpTests Stop/Dispose 等待与恢复、GameScreenViewModelTests 释放/解析异常）、F1（EventLogViewModelTests 重写为查询消费/过滤/置顶、EventQueryReaderTests、NativeBufferReaderTests 首探成功）、F4/F5（MapVisibilityModelTests 词汇/档位/缺字段兜底）、F7/F8/F9/F10/F11、F6 诚实路径（MainMenuViewModelTests 固定节点/说明）均有对应断言。
- 盲区：数值输入只测「整串替换」，未模拟逐键输入中间态（经推演 `SetProperty` 对等值不通知，`"2."` 中间态不会被回写吃掉小数点，实际无缺陷，但缺回归保护）；N1 的滚动/性能语义、N2 的 DLL 错配崩溃路径无测试。
- native 侧 RED 测试（145ae7a）覆盖过滤组合/limit 截断与 count/truncated、seq/tick 顺序、非法 JSON 不改写缓冲、两段式读取、空日志、跨句柄确定性，质量好。

## 五、总体评价

修复质量高：F1 的「count 为截断前总数」设计正确（先全量过滤计数再截断），事件序列化与存档同源避免口径漂移；F2 的己方过滤与「空选择错误」把 FR-045 的预检语义落到了集成层；F3 的锁内销毁 + 泵等待顺序消除了第 1 轮最危险的崩溃级竞态。测试由「纯逻辑」扩展到「装配层」，补上了上轮最大的盲区。

遗留项均不阻断：F5/F6/F12 是如实降级或跨核心侧的数据缺口，N1/N2/N3 属性能与部署/契约健壮性。建议下一轮优先处理 N2（ABI 版本/入口捕获，成本最低、影响面最明确），并为 F5（档位字段输出）、F6（节点覆盖参数）、F12（地图点/区域目标）在 tasks.md 登记去向，避免 TODO 指向不准确的任务编号。
