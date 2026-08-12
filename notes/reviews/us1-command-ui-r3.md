# US1 命令 UI 第 3 轮轻量复审记录（us1-command-ui-r3）

- 审查对象：`feature/us1-command-ui`，HEAD = `86e5626`（N4/N5 修复提交）
- 复审范围：第 2 轮 findings N1–N5 与 F5/F6/F12 登记对应的 8 个修复提交（native `b0ac403`/`0efad77`/`0360e1a`/`4f0aa98`，UI `455f578`/`0bb0ec5`/`a9a2e5b`/`86e5626`）
- 方式：只读静态复审。仅用 `git show`/`git diff`/`git log`/`git grep` 读对象库（以 HEAD 树为准），未读工作区文件，未做任何修改、push/merge/approve/checkout
- 门禁：修复代理报告 CTest 333/333、dotnet 157/157、format 全过；本轮未复跑，仅静态核对

## 结论

**PASS（置信度：高）**

六项逐条核实全部通过：N2 版本三处一致且精确匹配/启动拒绝语义未变；N3 的 native 过滤语义与契约一致、UI 输入真实写入 `query_json.unit_id`、测试真实覆盖；N1 已实现 250ms 节流 + seq 增量合并去重 + 锚点恢复 + 重试一次中文提示，不再每 50ms 全量重建；N4/N5 按兵牌矩形相交命中、泵随战斗主屏启停且先 Stop 再释放；F5/F6/F12 登记语句与任务号（T033/T103/T041）正确、未动勾选状态。

新发现问题：无 🔴；1 个 🟡（结构化 unit 字段后续事项只在契约里声称“已登记”，tasks.md 无对应登记）；3 个 💭（见下）。

## 逐条核实

### 1. N2 — ABI 0.2.0 同步与精确匹配语义（PASS）

- native `native/sim/include/wfs/sim/c_api.h:34-36`：`WFS_SIM_ABI_VERSION_MINOR=2`、`WFS_SIM_VERSION_STRING="0.2.0"`。
- UI `app/ui/src/WarFictionSim.Ui/Interop/SimAbiVersion.cs:13`：`Expected="0.2.0"`。
- 契约 `specs/001-war-sim-command-battle/contracts/sim-c-api.md:30-33` 同步为 0.2.0，并写明小版本 +1 不破坏既有符号/布局。
- 精确匹配语义未变：`SimAbiVersion.IsCompatible` 仍为 `StringComparison.Ordinal` 全等（`SimAbiVersion.cs:20-21`），`Verify` 仍抛 `SimAbiVersionMismatchException`；启动路径 `SimNativeBridge.cs:86` 与读档路径 `MainMenuViewModel.cs:235` 的调用点未改。
- 测试同步：`SimAbiVersionTests.cs:17/21/37/40/46` 改断言 0.2.0；native `c_api_test.cpp:141-143` 断言 `wfs_sim_version()` == `"0.2.0"`、MINOR==2、PATCH==0。
- `git grep "0.1.0"` 残留核销：`SimAbiVersionTests.cs:22`（`InlineData("0.1.0", false)`）为有意保留的负例；`native/vcpkg.json:3` 是 vcpkg 清单版本号，与 ABI 无关，均属“无关系的地方”。`SnapshotReaderTests.cs:19/110/220` 与 `SaveHeaderReaderTests.cs:16/26` 的 0.1.0 是解析器测试夹具，见 💭1。

### 2. N3 — unit_id 过滤（契约/实现/UI/测试一致，PASS；另见 🟡1）

- native 实现 `native/sim/src/c_api.cpp:91-101`（`ParseUnitId`）：非字符串抛 `std::invalid_argument` → `wfs_sim_query_events` 的 `catch (const std::invalid_argument&)` 映射为 `WFS_SIM_RESULT_INVALID_DATA`；空串/null 视同不过滤。
- `c_api.cpp:229-247`：先按 `EventFilter`（category/min_severity/text）过滤，再叠加 `message.find(unit_id) != npos`（区分大小写子串），`text` 与 `unit_id` 同时给定取交集；erase-remove 保持 seq 升序；`count` 为截断前总数、`truncated` 由 `limit` 判定。
- UI：`EventLogPanelView.xaml:36-37` 有 `AutomationProperties.Name="按单位筛选"` 的输入框，绑定 `UnitIdText`；`EventLogViewModel.cs:140-149` setter 触发 `InvalidateAndPull`（立即重查）；`BuildNativeQuery`（`EventLogViewModel.cs:388-391`）真实写出 `unit_id`。
- 测试真实覆盖：native `c_api_test.cpp:367`（`QueryEventsFiltersByUnitIdSubstring`：与全集按子串过滤精确相等、大小写不命中、与 category/text 交集、空/null 等价、未知 unit_id 零命中但不污染其它维度；非法类型 `7/[]/{}` 在 `c_api_test.cpp:470-472` 断言 `INVALID_DATA` 且不改写 out_len/缓冲）；UI `EventLogViewModelTests.cs` 新增 `UnitIdFilter_IsSentToNativeQuery`、`UnitIdAndTextFilters_AreCombinedInOneQuery`、`UnitIdAndTextFilters_IntersectNativeResults`；`FakeSimClient.cs:121-124` 镜像 native 的 Ordinal 子串语义。

### 3. N1 — 事件日志刷新节流/增量合并/锚点/重试（PASS）

- 节流：`EventLogViewModel.cs:40` `RefreshThrottle=250ms`；`ApplySummary`（`:196-228`）tick 前进且距上次拉取不足 250ms 时记 `_pullPending` 挂起，到期补拉，不再每帧（50ms）全量查询。
- 增量合并去重：`MergeIncrementally`（`:316-342`）按 seq 做目标集合去重、尾部追加、头部驱逐、既有条目实例复用（不 Clear+Add）；出现历史 seq 回插时退化为带锚点全量重建。
- 过滤变化立即重查：`InvalidateAndPull`（`:231-238`）置 `_fullRebuild` 并立即 `PullEvents`；分类/严重级/文本/单位四个 setter 均走此路径。
- 滚动锚点：`RebuildEntries`（`:345-352`）重建前外发 `ScrollAnchorChanged`；视图 `EventLogPanelView.xaml.cs:36-58` 捕获最后可见 seq、`ScrollIntoView` 恢复。
- 失败处理：`QueryWindowWithRetry`（`:272-287`）失败重试一次，仍失败则 `PullEvents` 的 catch（`:254-267`）显示中文 `查询事件日志失败：…（已重试一次）`，保留上次成功窗口，不向渲染循环抛异常；`ObjectDisposedException` 有专门中文提示。
- 测试：`EventLogViewModelTests.cs:72`（节流合并 + 到期补拉）、`:93`（增量合并不重复 + 实例复用）、`:110`（500 窗口头部驱逐）、`:238`（过滤变化带锚点重建）、`:257`/`:271`（重试恢复/双重失败中文提示保留窗口），并引入 `MutableTimeProvider` 使节流断言确定性。

### 4. N4/N5 — 框选命中与步进泵生命周期（PASS）

- N4：`BattleMapViewModel.cs:130-137` 命中判定改为兵牌矩形（`ScreenX/Y ± ScreenHitHalfWidth/Height`，常量在 `UnitMarkerViewModel.cs:16/19`）与框选矩形相交，锚点在外但兵牌相交仍命中；测试 `BattleMapViewModelTests.cs:134` 覆盖“锚点在矩形外仍命中”。
- N5：泵移入 `MainWindowViewModel`：进入战斗 `StartPump()`（`:100-108`）启动、返回主菜单 `DisposeGame()`（`:109-115`）先 `_pump?.Stop()`（`SimulationPump.Stop` 先停表再等待在途 step 退出）再 `Game?.Dispose()`（进而释放 `ISimClient`）；`Dispose()`（`:61-75`）同顺序；`MainWindow.xaml.cs` 只保留 50ms 渲染定时器。测试 `MainWindowViewModelTests.cs:36/56` 分别断言返回主菜单/Dispose 后步数不再增长且客户端已释放。

### 5. F5/F6/F12 登记（PASS）

- `specs/001-war-sim-command-battle/tasks.md:91`（T033，F5：intel to_json 补 observed_count/type_name/composition）、`:99`（T041，F12：FR-045 地图点选/区域框选）、`:248`（T103，F6：v1 开局扮演节点覆盖参数）。
- 任务号正确（T033/T103/T041），仅追加“待办登记”文案，勾选状态未动（T033 保持 `[x]`、T041/T103 保持 `[ ]`）；`MapVisibilityModel.cs:146-149` 的 TODO 引用同步由 T058 修正为 T033。

## 新发现问题

### 🟡 追踪性：结构化 unit 字段后续事项“已登记”但 tasks.md 无登记

- 位置：`specs/001-war-sim-command-battle/contracts/sim-c-api.md:50-52` 声称“给 `SimEvent` 增加结构化 unit 字段并改为精确单位匹配**已登记为后续事项**”，但 `git grep` 在 `tasks.md`（及其它文档）中找不到对应登记（`c_api.cpp:89` 注释也只回指契约，未回指任务）。
- 影响：r2 的 N3 只解决了 v1 子串匹配；“结构化精确单位筛选”这一部分依赖一条不存在的登记，存在后续丢失风险；契约表述与任务跟踪事实不符。
- 建议：仿照 F5/F6/F12 的“待办登记”格式，在 T042（或 T015）描述中追加结构化 unit 字段与精确单位匹配的待办，并让 `c_api.cpp:89`/契约回指该任务号。合并前补齐即可，不阻塞本轮功能验收。

### 💭 其它提示（不阻塞）

1. 解析器测试夹具仍用 `abi_version:"0.1.0"`：`SnapshotReaderTests.cs:19/110/220`、`SaveHeaderReaderTests.cs:16/26`。二者只测解析、任意样本值即可通过，但 `SnapshotReaderTests.cs` 文件头注释声称样例“与 native snapshot.cpp 对齐”，而 native 现输出 0.2.0，声明已失真。建议把夹具同步为 0.2.0 或注明“任意样本值”。
2. `EventLogViewModel.ApplySummary`（`:202-214`）中“同一 tick 到期补拉”分支（`_lastSyncedTick == snapshotTick && _pullPending`）实际不可达：`_pullPending` 仅在 `_lastSyncedTick != snapshotTick` 的节流分支置位，而 `_lastSyncedTick` 只在成功拉取时更新（同时清 pending）。到期补拉实际由 tick 前进分支完成，功能正确；建议删除该死分支或修正注释，避免后续维护者误读。测试 `EventLogViewModelTests.cs:72` 的“即使 tick 未再前进”注释与实际触发路径不符（属同一问题）。
3. ABI 升 0.2.0 后，`save.cpp:200` 写头的 `abi_version` 变为 0.2.0，`save.cpp:381` 与 `MainMenuViewModel.cs:235` 会精确匹配拒绝 0.1.0 旧存档。这是“精确匹配、拒绝跨版本”策略的预期后果（预发布阶段无存量存档风险），记录备查，不视为缺陷。
