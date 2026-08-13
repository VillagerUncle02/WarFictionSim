# T053 支援请求 UI 审查记录（R1）

审查人：Code Reviewer（只读审查，未修改代码文件）

审查日期：2026-08-13

审查方式：仅通过 `git show` / `git diff` / `git log` / `git grep` / `git ls-tree`
读取对象库（HEAD = 1aee867），未读取工作区文件，未构建/运行测试
（门禁结果采信实现代理报告：CTest 379/379、dotnet 199/199、format 全过）。
除本审查记录外未修改任何文件。

## 1. 审查范围

分支：`feature/us2-support-ui`，增量：`git diff feature/us2-support...feature/us2-support-ui`
（ca94bd2 test / 38588db feat / 1aee867 docs，29 文件，+2290/-11）。

覆盖任务：T053（支援请求 UI：目标/需求类型/支援种类选择、分数显示与扣减反馈、
请求状态与结果提示；SUPPORT_REQUEST 三级校验与统一命令注入闭环）。

必读上下文核对：
- spec.md US2 全文与 FR-005/008（连排级有限分数、范围约束、明确结果）：已核对；
- tasks.md T053（标记完成，含"三级校验与统一命令注入闭环"）：已核对；
- data-model.md §14（支援请求/配属记录）与 §18（US3 裁决桩登记）：已核对；
- contracts/command-schema.md §5（四种交互类型）与 §1.1（SUPPORT_REQUEST 负载）：已核对；
- contracts/schemas/command.schema.json（support 负载段，129–173 行）：已核对；
- native/sim/src/snapshot.cpp（support 摘要键）、support_runtime.cpp（事件名/扣分/拒绝）、
  attach.cpp（adjudicate 原因码）、resource_pool.cpp（deduct_score/check_request_scope）、
  command_validation.cpp（SUPPORT_REQUEST 语义校验）：已核对；
- .specify/memory/constitution.md §14（语言边界与分层调用：UI 不得直改模拟状态）、
  §17（禁止静默吞错）：已核对；
- 既有 CommandPanel/CommandJsonBuilder/CommandValidationRules/GameScreenViewModel/
  EventLogViewModel/EventQueryReader/ScenarioCatalog 及 FakeSimClient：已核对。

## 2. Findings 表

| # | 级别 | file:line | 问题 | 修复方向 | 状态 |
|---|------|-----------|------|----------|------|
| 1 | 🟡 | app/ui/src/WarFictionSim.Ui/ViewModels/GameScreenViewModel.cs:64（事件接线）；165–180（InjectCommand 的 sourcePanel 分支） | 支援面板提交被核心拒绝时，错误回填到命令面板而非支援面板：`SupportPanel.SubmitRequested += (_, json) => InjectCommand(json);` 未传 sourcePanel，switch 默认分支调用 CommandPanel.ShowNativeRejection；SupportPanel.CanSubmit 保持 true，面板侧 NATIVE_REJECTED 内嵌错误永远不会出现 | 接线改为 `InjectCommand(json, this)`（或 SupportPanel 实例）；补 FakeSimClient.NextInjectError 的回填测试 | 待修复 |
| 2 | 🟡 | app/ui/src/WarFictionSim.Ui/SupportPanel/SupportPanelViewModel.cs:245–254（分数展示）；CommandValidationRules.cs:213–217（SUPPORT_INSUFFICIENT_SCORE 警告）；SupportPanelView.xaml（"数量与分数（连排级有限分数）"分组无条件展示） | 分数/扣减语义无条件按连排级呈现：native attach.cpp:167–178 仅在 `scale == "platoon"`（limited_score）时 deduct_score/INSUFFICIENT_SCORE，营级（scn-support-battalion，faction-nato battalion support_score=55）不扣分也不会因分数拒绝；营级面板仍显示"剩余分数：55""扣减后剩余：不足（核心将按 INSUFFICIENT_SCORE 拒绝）"，文案与真实裁决不符 | 按 SupportPanelOptions.Scale 隐藏/改写分数区与警告文案：仅 platoon 展示预计扣减与 INSUFFICIENT_SCORE 提示，营级显示"配属链模式，无分数扣减" | 待修复 |
| 3 | 🟡 | app/ui/src/WarFictionSim.Ui/SupportPanel/SupportPanelViewModel.cs:599–618（RefreshStatusFromEvents/节流/两连查） | 状态查询节流只在同 tick 内生效：tick 前进即立即查询（20Hz 时每 tick 2 次 QueryEvents：SUPPORT_ 与 ATTACH_RETURN，约 40 次/秒且无 limit）；EventLogViewModel 采用跨 tick 墙钟节流（250ms，N1 先例），此处注释称"与事件日志面板同策略"但实现不同 | 采用墙钟节流 + tick 前进挂起补拉（同 EventLogViewModel.ApplySummary 模式），必要时合并查询或加 limit | 建议 |
| 4 | 🟡 | app/ui/src/WarFictionSim.Ui/SupportPanel/SupportStatusMapper.cs:68–77（Map 取全局最新事件） | 状态是"全部 SUPPORT_*/ATTACH_* 事件的全局最新"，不按请求过滤：旧请求的 ATTACH_RETURNED 可覆盖新请求的 EVALUATING/REJECTED 展示，且状态文案不含 RequestId，多请求并发时玩家无法判断状态属于哪一笔请求 | 按最新请求（如最大 request id 或最近 SUPPORT_REQUESTED）过滤展示，或状态文案附 RequestId | 建议 |
| 5 | 💭 | app/ui/src/WarFictionSim.Ui/Interop/Snapshot/SupportSummaryState.cs:17 | 注释"营级恒为 0"与 native 不符：initialize_support_state（support_runtime.cpp:251）无条件写入 pool.support_score，营级（faction-nato battalion=55）非零；与 Finding 2 一并修正 | 注释改为"连排级剩余分数；营级不参与扣减但快照仍携带池额度" | 建议 |
| 6 | 💭 | app/tests/ui_tests/WarFictionSim.Ui.Tests/SupportPanel/*（计数口径） | 静态统计新增 40 个 Fact/Theory 特性（分支内共 194 个特性），与实现代理报告"199 项/新增 42 项"存在 ±2 差异（可能为 Theory 数据行或断言计数口径）；另缺 Finding 1/2 的回归测试（核心拒绝回填路径、营级分数展示） | 以 dotnet 实际执行为准校核计数；补上述两个场景的测试 | 建议 |

## 3. 核对结论（按任务要求逐项）

### 3.1 SUPPORT_REQUEST JSON 契约（核对点 1）：通过

- CommandJsonBuilder 键序固定：schema_version → type → target → completion → support →
  intent → behavior → priority → deadline，黄金字符串测试锁定；support 负载键序
  request_type/kinds/quantity/to_node/for_command_id 与 command.schema.json 逐字段一致。
- request_type 5 枚举（reinforce/fire_support/engineer/medical/logistics）与
  command-schema §1.1、schema enum 完全一致；kinds 非空、quantity ≥1（缺省 1）、
  to_node/for_command_id 为空时省略——与 native RequestFromCommand 的 value() 缺省语义一致。
- 三级校验判定表与 native 对齐：未选目标/需求类型/种类、数量非法、错误完成条件、
  池外种类 = Error（对应 native TARGET_REQUIRED/SUPPORT_PAYLOAD_INVALID/
  CONDITION_NOT_EVALUABLE/SCOPE_VIOLATION）；分数不足 = Warning（连排级 native 以
  INSUFFICIENT_SCORE 拒绝，面板提示"核心可能拒绝"、核心拒绝时回填）；默认优先级 = Warning、
  空池 = Suggestion（表现层语义）。唯一偏差见 Finding 2（营级仍提示 INSUFFICIENT_SCORE）。

### 3.2 分数显示与扣减反馈（核对点 2）：通过（含 Finding 2）

- ScoreRemaining/PendingRequests/Attaches 全部来自快照 support 摘要（snapshot.cpp
  score_remaining/pending_requests/attaches），非静态文案；预计扣减为面板预估值，
  实际扣减反馈来自 SUPPORT_ASSIGNED 事件的 score_cost/score_remaining 字段。
- 拒绝原因映射正确：INSUFFICIENT_SCORE/SCOPE_VIOLATION/INSUFFICIENT_AVAILABLE/
  POOL_NOT_FOUND/NO_SUPERIOR_ESCALATION 与 native 事件文本一致；未知码原样显示（不静默）。
- 营级场景的"分数展示/警告语义"与 native 不一致（Finding 2），其余为通过。

### 3.3 提交路径与目标联动（核对点 3）：通过（含 Finding 1）

- 唯一写路径：SupportPanel.TrySubmit → SubmitRequested → GameScreenViewModel.InjectCommand →
  ISimClient.InjectCommand；SupportPanel 持有的 ISimClient 仅用于 QueryEvents（只读），
  符合宪法 §14"一切状态变更必须通过模拟层命令接口"。
- 目标池过滤 side == friendly && node == playerNodeId（己方可指挥单位）；
  BattleMap 单选同步支援目标、多选/空选清除；CommandValidationRules 另有
  UNAUTHORIZED_TARGET 兜底，敌方/越权目标不会进入载荷。
- Finding 1：核心拒绝时的"回填对应面板"接线实际未生效（sourcePanel 恒为 null）。

### 3.4 测试真实性（核对点 4）：通过（附口径与缺口）

- 新增测试断言真实命中实现：黄金 JSON 字符串（键序/省略/缺省）、三级校验判定表
  （13 项）、ViewModel 快照驱动分数/目标池/内联输入错误/提交事件载荷、状态映射
  （SUPPORT_REQUESTED→EVALUATING→ASSIGNED/REJECTED、归建、转请、拒绝原因码）、
  场景目录派系池投影（Configured/Scale/SuperiorNodeId/Kinds/Cost）、应用壳注入
  （FakeSimClient.InjectedCommands 载荷含 type/request_type/kinds/to_node）。
- 注入断言验证的是真实序列化输出而非替身自洽：FakeSimClient 仅镜像查询过滤，
  载荷经 CommandJsonBuilder 生成；native 侧语义由 T045/T047 集成测试覆盖。
- 缺口：无"核心拒绝 → 支援面板回填"测试（正是 Finding 1 的盲区）；无营级分数展示
  测试（Finding 2）。
- 计数：静态新增 40 个 Fact/Theory 特性，共 194 个；与报告"199/新增 42"的 ±2 差异
  建议以 dotnet 实际执行口径为准（不影响真实性结论）。

### 3.5 无新问题（核对点 5）：通过

- 快照解析容错：native build_snapshot_json 恒定输出 support 摘要（configured=false 也输出
  全键）；UI Require* 缺字段时抛 SnapshotParseException 并在状态栏显示，不崩溃。
- 空池/未配置：ScenarioCatalog 记录结构化 LoadIssues（宪法 §17 不静默），面板给出
  SUPPORT_POOL_UNAVAILABLE 建议与"未配置支援"提示；SUPPORT_CONFIG_INVALID 事件经
  "SUPPORT_" 文本过滤进入状态映射。
- 线程/Dispatcher：ApplySnapshot 与事件查询均在 UI 线程（视图定时器驱动）；查询异常与
  ObjectDisposedException 被捕获并保留旧状态；目标/单位集合仅内容变化时重建
  （CommandableUnit 实现值相等，SequenceEqual 生效，符合 F8 防抖）。

## 4. 结论

**PASS（有条件）——置信度：高。**

- 🔴 阻断级：无。
- 🟡 建议修复后合并：Finding 1（拒绝回填错位，最直接影响 T053"请求状态与结果提示"闭环）、
  Finding 2（营级分数/警告语义误导）、Finding 3（状态查询节流失效）、
  Finding 4（状态跨请求串扰）。
- 未发现数据损坏、注入绕过、契约违背或宪法 §14 违规；JSON 契约、快照驱动、
  三级校验、拒绝原因映射与测试真实性均通过静态核对。
