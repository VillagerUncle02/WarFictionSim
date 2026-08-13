# T053 支援请求 UI 第 3 轮复审（R3）—— feature/us2-support-ui

- 日期：2026-08-13
- 审查方式：只读静态复审；仅通过 `git log/show/diff/grep` 读取对象库，未运行构建/测试，未修改任何源文件（本记录除外）。
- 审查对象：`0ee41d7`（测试先行）+ `449ed43`（实现），HEAD = `449ed43`。
- 范围：R2 三条 🟡/💭 修复是否闭环；8 个新增测试断言是否真实命中；是否有新问题。
- 门禁说明：修复代理已报告 Quick 门禁通过（CTest 379/379、dotnet 215/215）；本文为静态复核，未重复执行。

## 结论：PASS（置信度高）

R2 三条修复全部真实闭环，未发现新的 🔴。发现 1 项新 🟡（规模文案口径不一致，见 §5），不影响本分支合入，建议随后续小修处理。

dotnet 215 项口径已静态对上：HEAD 测试树共有 208 个 `[Fact]` + 2 个 `[Theory]`（含 7 行 `[InlineData]`）= 215 个实际用例；`0ee41d7^` 为 200 + 7 = 207，新增恰好 8 个 `[Fact]`，与两个提交的 diff 完全一致。

## 1. R2-1 FindLatestRequestId 纳入登记失败 —— 已闭环

- 实现：[SupportStatusMapper.cs](app/ui/src/WarFictionSim.Ui/SupportPanel/SupportStatusMapper.cs:131) 将 `SUPPORT_REQUEST_REGISTER_FAILED` 与 `SUPPORT_REQUESTED` 并列为“提交/登记来源”，取事件流中更晚者；`latestAny` 回退语义保留。
- 与 native 对齐：`support_runtime.cpp` 的 `RegisterRequest` 在重复/非法时只发 `REGISTER_FAILED`（不发 `REQUESTED`），因此新请求登记失败不会被旧请求的 `REQUESTED` 过滤隐藏。
- 调用链真实命中：[SupportPanelViewModel.cs](app/ui/src/WarFictionSim.Ui/SupportPanel/SupportPanelViewModel.cs:659) 按 seq 升序排序后调用 `FindLatestRequestId` 再 `MapForRequest`。
- 新增测试 3 项：
  - `FindLatestRequestId_RegisterFailedAfterRequested_TracksFailedRegistration`（SupportStatusMapperTests.cs:184）：旧实现只认 REQUESTED 会返回 `req-cmd-1`，首条断言即失败——红绿命中。
  - `FindLatestRequestId_LaterRequestedWinsOverRegisterFailed`（SupportStatusMapperTests.cs:202）：旧实现也通过，属对称守卫，防止“登记失败永久压过后续 REQUESTED”的回归。
  - `ApplySnapshot_StatusShowsLatestRegisterFailed`（SupportPanelViewModelTests.cs:279）：整链路（FakeSimClient 事件 → ApplySnapshot → 事件查询 → 映射）断言显示 `req-cmd-2` 登记失败且不含“请求评估中”——旧实现下失败，红绿命中。

## 2. R2-2 规模判定改白名单 —— 已闭环

- 实现：`CommandValidationRules.cs:201` 与 `SupportPanelViewModel.cs:242` 均为 `IsNullOrEmpty(scale) || scale == "platoon"`。
- 与 native 对齐：`support.h:104` 缺省 `scale = "platoon"`；`support.cpp:231-233` 以该缺省读取并仅接受 platoon/battalion；`support_runtime.cpp:159` 以 `scale == "platoon"` 决定 `limited_score`。因此“空串 → 有限分数”“未知规模 → 非有限分数”均与 native 白名单一致。
- 新增测试 4 项：
  - `Validate_SupportInsufficientScore_EmptyScaleDefaultsPlatoon_IsWarning`（SupportCommandValidationRulesTests.cs:183）：空串下警告成立；旧黑名单同样通过，属契约守卫。
  - `Validate_SupportInsufficientScore_UnknownScale_NoScoreWarning`（SupportCommandValidationRulesTests.cs:198）：`company` 不得出现分数警告；旧黑名单会误报，红绿命中。
  - `EmptyScaleOptions_DefaultToLimitedScoreSemantics`（SupportPanelViewModelTests.cs:302）：面板空串默认有限分数；守卫。
  - `UnknownScaleOptions_DoNotUseLimitedScoreSemantics`（SupportPanelViewModelTests.cs:323）：未知规模不扣分；旧黑名单下 `IsLimitedScore` 为真，红绿命中。

## 3. R2-3 命令面板方向拒绝回填对称壳层测试 —— 已闭环

- 路由实现（R1 已具备）：[GameScreenViewModel.cs](app/ui/src/WarFictionSim.Ui/ViewModels/GameScreenViewModel.cs:166) 的 `InjectCommand` 按 `sourcePanel` 分发，`case CommandPanelViewModel`（:179）回填命令面板；命令面板 `ShowNativeRejection`（CommandPanelViewModel.cs:253）写入 `NATIVE_REJECTED` 并禁用提交。
- 新增测试 `CommandPanelSubmit_WhenCoreRejects_ShowsErrorOnCommandPanel`（GameScreenViewModelTests.cs:209）：MOVE + 执行单位 + 点目标 → `TrySubmit` 校验通过后触发注入 → FakeSimClient 抛 `SimNativeException` → 真实走 `InjectCommand` 路由并回填命令面板；断言命令面板含 `NATIVE_REJECTED`、支援面板不受影响、`CanSubmit=false`。整链路命中生产代码，且与既有 `SupportPanelSubmit_WhenCoreRejects` 成对，构成方向对称守卫。

## 4. 新增 8 项测试命中汇总

| 测试 | file:line | 命中性质 |
| --- | --- | --- |
| FindLatestRequestId_RegisterFailedAfterRequested | SupportStatusMapperTests.cs:184 | 红绿（旧实现失败） |
| FindLatestRequestId_LaterRequestedWinsOverRegisterFailed | SupportStatusMapperTests.cs:202 | 守卫（旧实现通过） |
| ApplySnapshot_StatusShowsLatestRegisterFailed | SupportPanelViewModelTests.cs:279 | 红绿（旧实现失败） |
| Validate_EmptyScaleDefaultsPlatoon_IsWarning | SupportCommandValidationRulesTests.cs:183 | 守卫（旧实现通过） |
| Validate_UnknownScale_NoScoreWarning | SupportCommandValidationRulesTests.cs:198 | 红绿（旧实现失败） |
| EmptyScaleOptions_DefaultToLimitedScoreSemantics | SupportPanelViewModelTests.cs:302 | 守卫（旧实现通过） |
| UnknownScaleOptions_DoNotUseLimitedScoreSemantics | SupportPanelViewModelTests.cs:323 | 红绿（旧实现失败） |
| CommandPanelSubmit_WhenCoreRejects_ShowsErrorOnCommandPanel | GameScreenViewModelTests.cs:209 | 对称守卫（路由回归时失败） |

8 项测试均调用生产代码路径（映射器、校验规则、ViewModel、应用壳路由），无仅测替身逻辑的空转断言。

## 5. 新问题

🟡 **SupportModeHint 与 IsLimitedScore 规模口径不一致**
[SupportPanelViewModel.cs](app/ui/src/WarFictionSim.Ui/SupportPanel/SupportPanelViewModel.cs:300)

`SupportModeHint` 仍用黑名单（`scale == "battalion"` → 营级，否则连排级），而 `IsLimitedScore`（:242）已改为白名单。对未知规模（如 `company`，R2 新测试明确支持的输入）会出现同一面板自相矛盾：提示“连排级规模：有限分数支援”，而 `EstimatedCostText`/`ScoreRemainingText` 显示“营级配属链不扣减/无分数扣减”。

**Why：** 当前 native 加载会拒绝未知 `support.scale`（support.cpp:232-233），生产路径暂不可达；但 `ScenarioCatalog.ReadSupportMetadata` 对 `support.scale` 不做枚举校验也不告警，且新测试已把该状态定义为合法输入，属于代码级口径不一致，未来新增规模时极易踩坑。

**Suggestion：** 让 `SupportModeHint` 基于 `IsLimitedScore` 表述（例如“连排级（有限分数）/营级或未识别规模（配属链不扣分）”），或为 `support.scale` 增加目录层枚举校验并显式告警。

## 6. 其他说明

- 命令面板的 `SupportScale` 来自快照（GameScreenViewModel.cs:282），支援面板来自场景元数据（:55）；正常加载下 native 已归一化为 platoon/battalion，两源一致，无需处理（仅记录）。
- 提交顺序符合测试先行（0ee41d7 在前、449ed43 在后）；本次未发现性能、安全或数据一致性的新增风险。
