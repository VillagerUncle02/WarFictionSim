# US2 native 逻辑组 T045–T052 审查记录（R1）

审查人：Code Reviewer（只读审查，未修改任何文件）

审查日期：2026-08-13

审查方式：仅通过 `git show` / `git diff` / `git log` / `git grep` 读取对象库
（HEAD = 9793dae），未读取工作区文件，未构建/运行测试。

## 1. 审查范围

分支：`feature/us2-support`，增量：`git diff feature/us1-e2e...feature/us2-support`
（c799d63/d7e6f40/e27cf42/81b6f3b/e68e23d/69915c2/c1f4d20/b04de7d/44357e1/18c3257/7357913/9793dae，42 文件，+4403/-33）。

覆盖任务：T045（CLI 集成）、T046（战术分队归建测试）、T047（支援请求状态机）、
T048（战术分队模型）、T049（资源池/有限分数）、T050（配属链仲裁与归建）、
T051（四种交互类型）、T052（派系模板）。

必读上下文核对：
- spec.md US2 全文与 FR-005/006/007/008/009/010、SC-004：已逐条核对；
- data-model.md §14（支援请求/配属记录）、§18（US3 裁决桩登记）：已核对；
- contracts/command-schema.md §5（四种交互类型）及 §1.1（SUPPORT_REQUEST 负载）：已核对；
- .specify/memory/constitution.md §2（测试保障）/§7（确定性裁决）/§12（数据驱动）/§17（错误处理）及 §5（文件级总览注释）：已核对。

## 2. Findings 表

| # | 级别 | file:line | 问题 | 修复方向 | 状态 |
|---|------|-----------|------|----------|------|
| 1 | 🟡 | native/sim/src/tactical.cpp:201（ResolveCommandScope）；native/sim/src/command_chain.cpp:419（ProcessDue 无调用） | FR-010 拆分命令作用域只在模型层实现：`git grep` 确认运行时命令链从不调用 ResolveCommandScope，拆分态下向整班下令实际会被正常接受，不会拒绝 | 在 ProcessDue/ApplyMission 生效前按 unit_id 调用 ResolveCommandScope，拒绝时产生可见事件（SPLIT_SQUAD_COMMAND_NOT_ALLOWED） | 待确认：建议在 T054 集成闭环或 US3 补齐 |
| 2 | 🟡 | native/sim/src/attach.cpp:314（Reassign）；native/sim/src/support_runtime.cpp:207（step_support_pipeline 无调用） | FR-009 归建途中重新配属未接入运行时：`git grep` 确认 Reassign 只有 registry API 与单元测试，管线从不调用；归建中单位仍计占用（ActiveAllocations），池余量 0 时新请求走 INSUFFICIENT_AVAILABLE 拒绝而非"新请求优先于归建、取消原归建"；可复现（池 quantity=1：请求 A 配属 → RETURNING → 请求 B 同 kind 到达即拒绝） | Assign 前将同 kind 的 RETURNING 记录视为可回收候选，调用 Reassign 取消原归建；或在 data-model §18 显式登记"重新配属由桩后置" | 待确认：T050 自述已完成"归建/重新配属"，但运行时未接线；建议本组或 US3 补 |
| 3 | 🟡 | data/factions/faction-china.json（battalion.support_kinds=["mortar-82","artillery-152"]）；native/sim/src/attach.cpp:97-134（adjudicate 只查 entries） | support_kinds 声明"可用支援种类"但请求校验只读 entries，mortar-82 无对应 entry，声明可用却必然 SCOPE_VIOLATION，数据自相矛盾；support_kinds 字段在裁决中完全未消费 | 请求校验合并 support_kinds+entries（如把 mortar-82 作为 fire_support entry 加入），或删除未消费字段并在文档说明 entries 为唯一权威 | 待修复 |
| 4 | 🟡 | native/tests/cli_tests/run_support_chain_tests.ps1:112-134（连排级两次运行）；营级路径 143-187 仅一次 | T045"确定性两次一致"只覆盖连排级，营级配属/拒绝/转请桩路径未做同输入两次比较 | 营级场景同样运行两次（threads 1/2）并比较状态哈希与事件序列 | 建议 |
| 5 | 💭 | native/sim/src/tactical.cpp:205（FindFireTeam(unit_id) != nullptr） | 已解散（kDissolved）火力组 id 仍会被 ResolveCommandScope 放行，因为 FindFireTeam 不区分状态；当前无运行时调用者暂不显现，一旦按 Finding 1 接线即生效 | 放行条件增加 team.state == kActive | 建议 |
| 6 | 💭 | native/sim/src/attach.cpp:283（id = "att-" + records_.size()）；from_json attach.cpp:394-406 仅查重复 | AttachRecord id 依赖 records_.size() 单调分配；反序列化只校验唯一不校验单调，存档缺中间 id 时新 Attach 可能产生重复 id | 序列化/恢复 next_id 游标，或恢复时按最大序号重建计数器 | 建议 |
| 7 | 💭 | native/sim/src/attach.cpp:169（arbitrate_requests）；native/sim/src/support_runtime.cpp:238-256（runtime 自行排序+单请求裁决） | 两套仲裁实现并存：纯函数 arbitrate_requests 仅测试使用，runtime 用 due 排序 + adjudicate 顺序裁决；语义当前一致，但双路径有漂移风险 | runtime 复用 arbitrate_requests（把占用回写 attach registry），或删除未使用路径 | 建议 |
| 8 | 💭 | native/sim/src/support.cpp:250-279（SupportConfig::FromScenario） | 脚本请求只解析 id/priority/from/to/request_type/kinds/quantity/return_after_ticks/submit_tick，target_unit/seq/for_command_id 不读，脚本请求 target_unit 恒空，data-model §14"请求含目标"未完整落地（裁决不依赖 target，功能不受影响） | FromScenario 补齐 target_unit/for_command_id 解析 | 建议 |
| 9 | 💭 | contracts/schemas/command.schema.json:136-140（request_type 仅 minLength）；native/sim/src/command_validation.cpp:280-285（只查非空） | 契约文档列出 5 种需求类型（reinforce/fire_support/engineer/medical/logistics）但 schema/语义校验均不枚举，任意字符串可通过 | schema 加 enum 或语义校验枚举并报 CONTRACT_VIOLATION | 建议 |
| 10 | 💭 | native/sim/src/support.cpp:222-232（延迟/节点读取） | evaluation_delay_ticks/return_delay_ticks/approval_step_ticks 不校验非零，player_node_id/superior_node_id 不校验非空，配置错误时行为退化为立即结算或静默降级 | FromScenario 显式校验并抛 invalid_argument | 建议 |
| 11 | 💭 | native/sim/src/tactical.cpp:164-175（DissolveFireTeams） | 对不存在/无活跃火力组的 squad 也返回 true，错误语义模糊 | 无匹配活跃记录时返回 false | 建议 |
| 12 | 💭 | native/sim/src/snapshot.cpp:98（"pending_requests" = support_chain.size()） | 快照字段名 pending_requests 实为全部登记请求数（终态 REJECTED/EXECUTING 也计入），消费方易误读 | 改名 registered_requests 或只计非终态 | 建议 |
| 13 | 💭 | native/sim/include/wfs/sim/support.h:70（assigned_unit_ids） | 字段名 unit_ids 但存的是资源类型（kind）清单，与 attach.h AttachDecision.units 一致但命名易误解 | 改名 assigned_kind_ids 或统一命名 | 建议 |

## 3. 核对结论（按任务要求逐项）

1. 支援请求状态机（SUBMITTED→EVALUATING→EXECUTING/REJECTED，ESCALATED 可回 EVALUATING/REJECTED）
   与归建（ASSIGNED→RETURNING→RETURNED）按 data-model §14 正确；连排级有限分数
   （额度=营编制池 support_score、扣分可见、INSUFFICIENT_SCORE 用尽即止、范围约束
   SCOPE_VIOLATION）与营级（优先级,到达序列号）仲裁确定性正确。缺：Finding 2（重配未接线）。
2. 战术分队：拆分确定性（基数+余数）、合并、解除编成恢复行政编制、归建仅存活成员
   均正确并有测试；缺：Finding 1（作用域运行时未接线）、Finding 5（已解散组放行）。
3. 派系模板：三份 JSON 结构完整（各层级池+审批层级 0/1/2），schema 校验、语义校验、
   data/units 交叉引用、非法数据结构化报错不崩溃；指挥逻辑无派系特判（approval_level
   数字驱动延迟）。缺：Finding 3（support_kinds 未消费）。
4. 集成：step_support_pipeline 在命令链之后、机动/战斗之前（sim_runtime.cpp:253）；
   SUPPORT_REQUEST 不占目标任务槽（command_chain.cpp:454-461 排除时限/竞争者/任务写入）；
   快照/存档往返：serialize_state_json 仅在 support_configured 时写支援字段，
   未配置场景旧存档字节不变，save/load 恢复 support_chain/attach_registry/
   tactical_registry/support_score_remaining；命令 schema/语义校验与契约一致。
5. US3 裁决桩：配属/拒绝/转请与归建由确定性桩替代已在 data-model §18 与 tasks.md T045
   显式登记，代码注释 TODO 齐全；转请无上级时 NO_SUPERIOR_ESCALATION 明确拒绝
   （support_runtime.cpp:170-180）。非"测试造假跳过"。
6. 测试真实性：T045 CLI 断言具体（score_cost=30/score_remaining=30/units=[squad-mortar-team]/
   INSUFFICIENT_SCORE/SCOPE_VIOLATION/NO_SUPERIOR_ESCALATION/事件序列+归建），连排级
   两次运行 hash+事件一致；T046 单元测试命中实现；存档往返测试强断言（状态哈希一致+
   支援摘要一致）。缺：Finding 4（营级无两次运行）。
7. 宪法：确定性（统一 RNG、无现实时钟、固定遍历顺序、threads 不进哈希）；数据驱动
   （派系/场景/延迟全数据化）；错误不静默（非法转移 false、加载失败结构化 issue、
   SUPPORT_CONFIG_INVALID 事件）；新增文件均有文件级总览注释。

## 4. 未验证猜测

- 未实际构建/运行测试（只读审查限制）：CLI 测试、sim_tests、存档往返是否全绿未验证；
  断言与实现静态核对一致，但"测试通过"为未验证假设。
- faction 默认加载路径 resolve_schema_path（loader.cpp:269）依赖仓库布局
  （data/factions → 向上找 contracts/schemas），静态核对成立但未运行验证。
- T045 连排级归建链路静态确认成立：MOVE 为 non-continuous（mission_registry.cpp:31），
  mission_exec.cpp:335 会 MarkCompleted → command_chain kCompleted → support_runtime.cpp:270
  触发 RETURNING；未运行验证。
- 营级 CLI 测试中 script-req-reject 以 SCOPE_VIOLATION 拒绝（artillery-152 不在 NATO 池），
  与测试断言一致；"拒绝"演示的是范围外而非力量不足，场景设计意图需确认。

## 5. 宪法合规要点

- §2 测试保障：每功能带测试，确定性算法（仲裁/分数/拆分/归建/状态机）有单测 ✓
- §7 确定性裁决：纯函数+统一 RNG；threads 不进状态哈希 ✓
- §12 数据驱动：派系/场景/延迟数据化，非法数据报错不崩溃 ✓
- §17 错误处理：非法状态转移显式 false、加载校验抛 invalid_argument、运行时配置失败
  记录 SUPPORT_CONFIG_INVALID 事件 ✓
- §5 文件级总览注释：support/attach/tactical/resource_pool/faction/interaction/
  support_runtime 及全部测试均有文件头注释 ✓
- §13 存档兼容：旧存档缺支援字段时保持场景派生状态，非破坏性演进 ✓

## 6. 整体结论

**PASS（置信度 0.80）**。

核心状态机、确定性仲裁、有限分数、派系数据与校验、快照/存档往返、测试真实性与
US3 桩登记均达标；FR-009 归建途中重新配属与 FR-010 拆分命令作用域在模型层就绪但
运行时未接线（Finding 1/2），若验收判据要求这两条全链路运行时生效，应升级为
blocker 并在 T054/US3 内补齐后再合并。

## 7. R1 修复登记（Backend Architect，2026-08-13）

本轮按"测试先行、最小改动"修复 4 条 🟡 与 4 条顺手 💭，其余 💭 维持登记：

### 已修复

| # | 修复内容 | 位置/测试 |
|---|----------|-----------|
| 1 🟡 | FR-010 运行时接线：CommandChain::ProcessDue 在生效前按 unit_id 调用 ResolveCommandScope，拆分态整班命令以 SPLIT_SQUAD_COMMAND_NOT_ALLOWED 拒绝并产生可见事件；火力组命令放行 | native/sim/src/command_chain.cpp；native/tests/sim_tests/command_chain_test.cpp（整班拒绝 + 火力组放行两条运行时测试） |
| 2 🟡 | FR-009 运行时接线：裁决前把健康 RETURNING 记录视为可抢占候选（reclaimable_allocations），池余量不足时取消原归建并重新配属，不再直接 INSUFFICIENT_AVAILABLE；同 tick 仍按（优先级, 到达序列号）仲裁 | native/sim/src/support_runtime.cpp、attach.cpp/h；support_test.cpp（池余量 0 + 归建中 → 重配成功；同 tick 优先级仲裁）与 attach_test.cpp（可抢占候选筛选） |
| 3 🟡 | 统一 support_kinds 与 entries 语义：三派系移除无条目支撑的 mortar-82 声明（china 营级保留 artillery-152），loader 新增语义校验 SUPPORT_KIND_NOT_CONSUMABLE | data/factions/*.json；native/sim/src/faction.cpp；faction_test.cpp（三派系可消费断言 + 负例） |
| 4 🟡 | 营级配属/拒绝/转请桩路径同输入两次运行（threads 1/2），比较状态哈希与事件序列 | native/tests/cli_tests/run_support_chain_tests.ps1 |
| 5 💭 | 已解散火力组 id 在 ResolveCommandScope 中拒绝（FIRE_TEAM_DISSOLVED） | native/sim/src/tactical.cpp；support_test.cpp |
| 6 💭 | AttachRecord 反序列化校验 id 单调（att-<n> 严格递增），恢复时按最大序号重建 next_id 游标，避免存档缺中间 id 后重复 | native/sim/src/attach.cpp/h；attach_test.cpp |
| 8 💭 | SupportConfig::FromScenario 补解析 scripted_requests 的 target_unit/for_command_id | native/sim/src/support.cpp；support_test.cpp |
| 9 💭 | command.schema.json 的 support.request_type 枚举 5 种需求类型 | contracts/schemas/command.schema.json |

### 仍登记未修（后续轮次）

| # | 内容 |
|---|------|
| 7 💭 | arbitrate_requests 与 runtime 两套仲裁路径并存，语义当前一致但存在漂移风险；建议 runtime 复用 arbitrate_requests 或删除未使用路径 |
| 10 💭 | FromScenario 不校验 evaluation/return/approval 延迟非零与节点非空，配置错误退化为立即结算/静默降级 |
| 11 💭 | DissolveFireTeams 对不存在/无活跃火力组 squad 仍返回 true，错误语义模糊 |
| 12 💭 | snapshot.cpp pending_requests 实为全部登记请求数，命名易误读 |
| 13 💭 | SupportRequest.assigned_unit_ids 存的是资源类型清单，命名易误解 |
