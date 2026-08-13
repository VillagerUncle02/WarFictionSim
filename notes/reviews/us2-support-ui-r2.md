# T053 支援请求 UI —— R1 修复第 2 轮只读复审记录（R2）

审查人：Code Reviewer（只读审查，未修改任何项目源码）

审查日期：2026-08-13

审查方式：仅读取 git 对象库（`git show` / `git diff` / `git log` 为主，辅以
`git grep` / `git ls-tree` 定位），未读取工作区文件，未构建/运行测试；门禁
Quick 通过（CTest 379/379、dotnet 207/207）为修复代理报告，本轮未复跑。

审查对象：分支 `feature/us2-support-ui`，HEAD = `0c9ee40`。
修复提交：`53908a0`（测试先行）+ `0c9ee40`（实现）。

说明：`notes/reviews` 中未收录 T053 UI 第 1 轮审查记录文件（仅有 native 组
`us2-support-r1.md`），本条审查以任务给定的 4 条 🟡 定义为基准。

## 1. 四条 R1 🟡 逐条核对（以 HEAD 为准）

### R1-1 拒绝按提交来源面板路由 —— PASS（置信度 0.95）

- GameScreenViewModel.cs:63-65：两个面板的 `SubmitRequested` 均把 `sender`
  传给 `InjectCommand(json, sender)`；原 38588db 版本两个订阅都写
  `(_, json) => InjectCommand(json)`，导致 `sourcePanel` 恒为 null，
  `SupportPanelViewModel` / `CommandPanelViewModel` 两个 switch 分支均为
  死代码，拒绝一律落入 default 回填命令面板。修复后两条分支均可达。
- GameScreenViewModel.cs:166-185：`InjectCommand` 按 `sourcePanel` 类型回填
  支援面板或命令面板，default 仍回填命令面板，原命令面板路径语义不变
  （命令面板提交时 sender 即 CommandPanelViewModel 实例）。
- 新测试 GameScreenViewModelTests.cs:187-206 用 `NextInjectError` 注入
  `INSUFFICIENT_SCORE` 拒绝，断言 NATIVE_REJECTED 落在支援面板、命令面板无
  该错误、`CanSubmit=false`；修复前该测试必然失败（拒绝被回填到命令面板）。

### R1-2 营级分数/扣减/警告语义 —— PASS（置信度 0.90）

- native/sim/src/support_runtime.cpp：`limited_score = state.support_config.scale
  == "platoon"`，仅连排级调用 `deduct_score` 并以 `INSUFFICIENT_SCORE` 拒绝；
  营级在可用力量不足时走 escalate/reject（`INSUFFICIENT_AVAILABLE`），不扣分。
- CommandValidationRules.cs:197-222：有限分数预检加 `context.SupportScale !=
  "battalion"` 守卫，营级不再产生 SUPPORT_INSUFFICIENT_SCORE 警告；连排级
  （platoon）及未配置（空 scale）行为与修复前一致，未误伤连排级。
- SupportPanelViewModel.cs:237-264：`ScoreInsufficient` 增加 `IsLimitedScore`
  门控；`IsLimitedScore` 取 `_options.Scale != "battalion"`；营级文案改为
  “分数额度（营级配属链不扣减）/ 无分数扣减 / 分数不足不构成拒绝原因”。
- SupportSummaryState.cs:17-18 注释与 native 一致：营级快照 `score_remaining`
  仍携带池额度（`initialize_support_state` 对两档都赋 `pool.support_score`，
  `ResolveRequest` 仅在 limited_score 时回写），注释准确。
- SupportPanelView.xaml:46 标题去掉“连排级有限分数”限定，与两档语义匹配。
- 双源一致性：选项 scale 来自场景 JSON（ScenarioCatalog.cs:151），快照 scale
  来自同一场景 JSON（snapshot.cpp:95 → SnapshotReader.cs:209），当前仅
  platoon/battalion 两档，无分歧。

### R1-3 状态查询墙钟节流 250ms（跨 tick、到期补拉） —— PASS（置信度 0.90）

- SupportPanelViewModel.cs:28-30：`StatusRefreshThrottle` 由 500ms 改为 250ms，
  与 EventLogViewModel.cs:40 `RefreshThrottle` 一致（N1 先例）。
- SupportPanelViewModel.cs:610-640：重构为“同一 tick 不查；tick 前进但未到
  期 → `_pullPending = true` 挂起；到期后任一新 tick 补拉一次”，跨 tick 生效
  且有 catch-up，不再像旧实现那样在 tick 前进时无视节流直接查询。
- PullSupportStatus（642-673）每次仍是全量事件窗口查询（SUPPORT_ +
  ATTACH_RETURN 两连查，按 seq 合并），因此节流只增加最多约 250ms 的展示
  延迟，不丢事件、不吞关键状态；查询异常保留上次状态并显示中文错误。
- 新测试 SupportPanelViewModelTests.cs:227-245 使用 `MutableTimeProvider`
  精确验证：首帧 2 次查询 → 3 个新 tick 内仍 2 次 → 推进 250ms 后补拉为
  4 次。旧实现（tick 前进即查）在 tick 2 就应增至 4 次，断言必失败。

### R1-4 按请求 id 过滤状态链 —— PASS（置信度 0.85，见新发现 🟡-1）

- SupportStatusMapper.cs:89-106 `MapForRequest`：按 `request=` 字段过滤出单
  请求状态链，文案前缀 `请求 {id}：`，`RequestId` 回填；无 id 时回退 `Map`
  （与原行为一致）。
- SupportStatusMapper.cs:113-133 `FindLatestRequestId`：取最近一次
  `SUPPORT_REQUESTED` 的 id，无 REQUESTED 时回退最后一个带 `request=` 的
  事件，无则 null。
- SupportPanelViewModel.cs:655-661：查询结果先按 seq 排序，再经
  FindLatestRequestId + MapForRequest 映射；旧请求的 ASSIGNED/RETURNING/
  RETURNED 不会覆盖新请求的评估/结果展示。
- native 侧核对：SUPPORT_REQUESTED/EVALUATING/ASSIGNED/REJECTED/ESCALATED/
  REGISTER_FAILED 与 ATTACH_RETURNING/RETURNED 均带 `request=`（
  support_runtime.cpp），过滤前提成立。
- 新测试 SupportStatusMapperTests.cs:127-181 与 SupportPanelViewModelTests.cs:
  247-276 均真实命中（交错请求各取各链、无 id 回退、取最近提交）。

## 2. 新发现

### 🟡-1 FindLatestRequestId 会隐藏“新请求登记失败”状态（中置信度 0.7）

- SupportStatusMapper.cs:113-133（尤其 126-132）：函数永远优先返回最近一次
  `SUPPORT_REQUESTED` 的 id（`latestSubmitted`），即使其后更新的一条
  `SUPPORT_REQUEST_REGISTER_FAILED request=<新id>` 才是用户最新提交的结果。
- 复现：事件流为 `REQUESTED req-A` → `EVALUATING req-A` →
  `REQUEST_REGISTER_FAILED req-B`。FindLatestRequestId 返回 req-A，面板继续
  显示“请求 req-A：请求评估中”，新提交 req-B 的“登记失败”被过滤隐藏；
  修复前的 `Map` 会显示最后一条 REGISTER_FAILED。
- 影响评估：UI 正常路径 command_id 由 native 按 seq 生成（CommandIdFor(seq)），
  重复登记几乎不可达，故非 blocker；但这是 R1-4“请求过滤不回退错误”语义下
  的真实缺口，且新测试（FindLatestRequestId_ReturnsLastSubmittedRequest，
  SupportStatusMapperTests.cs:168-181）未覆盖该交错场景。
- 建议：当最新携带 `request=` 的事件为 SUPPORT_REQUEST_REGISTER_FAILED 时
  优先返回其 id（或对 latestSubmitted/latestAny 按 seq 取更晚者并特判失败
  事件），并补一条交错测试。

### 💭-1 规模判定为“非营级”黑名单，与 native“仅 platoon”白名单不对称

- CommandValidationRules.cs:200 与 SupportPanelViewModel.cs:241 都用
  `!= "battalion"` 表达有限分数语义，而 native 是 `scale == "platoon"`。
  当前数据仅 platoon/battalion 两档（scn-support-platoon.json /
  scn-support-battalion.json），无实际影响；未来若新增 company 等规模，
  UI 会继续按有限分数提示而 native 不扣分，产生漂移。建议与 native 同式
  （`== "platoon"`）或抽公共判定。

### 💭-2 缺命令面板方向的壳层拒绝回填回归测试

- GameScreenViewModelTests.cs:187-206 只覆盖支援面板方向；命令面板原有回填
  仅由 CommandPanelViewModelTests.cs:145-151 的 `ShowNativeRejection` 单元
  测试覆盖（不经壳层路由）。建议对称补一条“命令面板提交被核心拒绝 → 回填
  命令面板且支援面板无 NATIVE_REJECTED”的测试，防止后续路由改动回归。

### 💭-3 节流“同 tick 到期补拉”分支实际不可达（无害）

- SupportPanelViewModel.cs:619-626：`_pullPending` 只在“tick 前进且未到期”
  分支（631-635）置位，置位时 `_lastSyncedTick` 必然小于当前 tick；tick 单调
  递增下不会再有 `tick == _lastSyncedTick` 且 pending 为真的帧，补拉实际由
  tick 前进分支（631-639）完成。该分支与 EventLogViewModel.cs:205-217 N1
  先例同款，属防御性冗余，不影响行为；仅建议注释说明或后续与事件日志一并
  收敛。

## 3. 新测试真实性核对（8 项全部真实命中）

| 测试 | 位置 | 为什么能命中 |
|------|------|--------------|
| Validate_SupportInsufficientScore_BattalionScale_NoScoreWarning | SupportCommandValidationRulesTests.cs:169-180 | 修复前无条件产生 SUPPORT_INSUFFICIENT_SCORE 警告，断言必失败 |
| BattalionOptions_DoNotShowScoreDeductionSemantics | SupportPanelViewModelTests.cs:200-225 | 修复前 ScoreInsufficient=true、文案无“无分数扣减”，断言必失败 |
| ApplySnapshot_ConsecutiveTickAdvances_AreThrottledByWallClock | SupportPanelViewModelTests.cs:227-245 | 修复前跨 tick 不节流，查询数在 tick 2 即增长，断言必失败 |
| ApplySnapshot_StatusTracksLatestSubmittedRequest | SupportPanelViewModelTests.cs:247-276 | 修复前 Map 文案无 request id，`Contains("req-cmd-2")` 必失败 |
| MapForRequest_InterleavedRequests_FiltersEachChainAndPrefixesRequestId | SupportStatusMapperTests.cs:127-150 | 直接覆盖新 API 交错链语义 |
| MapForRequest_WithoutRequestId_FallsBackToGlobalLatest | SupportStatusMapperTests.cs:152-166 | 直接覆盖新 API 回退路径 |
| FindLatestRequestId_ReturnsLastSubmittedRequest | SupportStatusMapperTests.cs:168-181 | 直接覆盖新 API 取最近提交语义 |
| SupportPanelSubmit_WhenCoreRejects_ShowsErrorOnSupportPanel | GameScreenViewModelTests.cs:187-206 | 修复前拒绝回填命令面板，两条断言必失败 |

测试用例总数静态核对：`[Fact]` 200 项 + 2 个 `[Theory]` 的 `[InlineData]`
7 例 = 207，与修复代理报告的 dotnet 207/207 口径一致（新增 8 项均为 [Fact]）。

## 4. 整体结论

**PASS（置信度 0.85）**：四条 R1 🟡 全部真实闭环，修复与 native 语义一致，
测试先行且 8 项新断言均能区分修复前后行为；未发现 🔴。建议后续轮次处理
🟡-1（登记失败事件被隐藏）并补命令面板方向壳层测试；💭 项不阻塞合并。

