# US2 native 第 1 轮修复 — 第 2 轮复审（只读）

- 审查日期：2026-08-13
- 审查对象：`feature/us2-support` @ `22edf4a1d51ae3fe94ca0e266571f3137aa18e6e`（HEAD）
- 审查方式：仅 `git log` / `git show` / `git diff` / `git grep` 读取对象库，未读取工作区、未修改源码、未 push/merge/approve/checkout
- 前置验证：修复代理报告 Release/Debug CTest 378/378、dotnet 159/159；本复审为静态审查

## 总体结论

**PASS（高置信）**。四条 🟡（FR-010 拆分作用域接线、FR-009 归建中重配、派系 support_kinds 与 entries 矛盾、营级桩路径只跑一次）均已真实闭环，且改动作用于运行时路径（命令链 ProcessDue、支援管线 step_support_pipeline、派系加载、命令 schema 校验），并非仅模型/测试自洽。顺手项（脚本请求 target_unit/for_command_id 解析、AttachRecord id 单调校验）无回归，AttachRecord 旧存档兼容。未发现新 🔴；发现 1 个 🟡（request_type 枚举缺负例测试）与 3 个 💭。

| 项 | 结论 | 置信度 |
| --- | --- | --- |
| FR-010 拆分作用域接线（e2809b3） | PASS | 高 |
| FR-009 归建中重配（b6cecca） | PASS | 高 |
| 派系 support_kinds 对齐（76c915d） | PASS | 高 |
| request_type 枚举（68975de） | PASS（缺负例测试） | 高 |
| 营级桩两次一致（22edf4a） | PASS | 高 |
| 脚本请求字段解析（e44f123） | PASS | 高 |
| AttachRecord id 单调/游标（b6cecca） | PASS | 高 |

## 1. FR-010 拆分作用域接线（e2809b3）

### 核实

- 接线点真实且位于运行时：`command_chain.cpp:454-462` 在 `ProcessDue` 生效裁决前对非 SUPPORT_REQUEST 命令调用 `ResolveCommandScope(command->unit_id)`；拒绝时置 `kRejected` 并走 `LogRejected`（`command_chain.cpp:458-461`），产生可见 `COMMAND_REJECTED ... reason=SPLIT_SQUAD_COMMAND_NOT_ALLOWED` 事件。
- SUPPORT_REQUEST 显式豁免（`command_chain.cpp:456`），与"上下级交互不适用命令作用域"契约一致，避免误伤支援请求。
- 顺序无回归：`TARGET_NOT_FOUND`（`command_chain.cpp:448-452`）与批量父命令跳过（`command_chain.cpp:445-446`）均在新检查之前，非拆分单位走 `ResolveCommandScope` 默认放行分支（`tactical.cpp:218-219`），既有命令路径行为不变。
- 已解散火力组拒绝 `FIRE_TEAM_DISSOLVED`（`tactical.cpp:209-212`，Finding 5）在模型层实现并被 `support_test.cpp:138-146` 覆盖。

### 测试命中

- `command_chain_test.cpp:195-214`：真实走 `Issue → ProcessDue → Find → 事件断言`，整班 MOVE 命令被拒且事件文本精确匹配（reason=SPLIT_SQUAD_COMMAND_NOT_ALLOWED）。
- `command_chain_test.cpp:216-236`：火力组命令放行，断言 `kEffective` + `COMMAND_ACKNOWLEDGED`；火力组单位是测试手工 push 的 `RuntimeUnitState`（见下方 💭）。

### 新问题

无新 🔴/🟡。注意（💭）：生产代码目前没有任何 `SplitSquad` 调用点，也不存在火力组 `RuntimeUnitState` 的生成路径；运行时对火力组 id 会先命中 `TARGET_NOT_FOUND`（`command_chain.cpp:448-452`），因此"火力组放行"与"FIRE_TEAM_DISSOLVED"分支仅测试可达。FR-010 的接线缺口本身已关闭，端到端可达性依赖 US3 拆分命令落地。

## 2. FR-009 归建中重配（b6cecca）

### 核实

- 可抢占候选口径正确：`reclaimable_allocations`（`attach.cpp:384-399`）只收 `kReturning && !immobilized`（瘫痪/需拖运不参与，符合 FR-009/078）。
- 裁决前口径正确：`ResolveRequest` 用 `WithoutReclaimable(ActiveAllocations(), reclaimable)` 计算可用力量（`support_runtime.cpp:153-160`），池余量 0 但有健康归建中单位时不再直接 `INSUFFICIENT_AVAILABLE`。
- 配属时取消原归建并复用记录：按插入顺序确定性选取前 needed 条，`AttachRegistry::Reassign`（`attach.cpp:348-365`）把记录从 RETURNING 转回 ASSIGNED、改写 request_id/assigned_to_node 并清零归建字段；随后输出 `ATTACH_REASSIGNED ... previous_request=...` 可见事件（`support_runtime.cpp:78-102, 176-186`）。不足部分才新 `Attach`（`support_runtime.cpp:180-186`），与 `adjudicate` 的 per-kind×quantity 语义一致（`attach.cpp:127-152`，decision.units 即 request.kinds，不重复计数）。
- 同 tick 竞争仲裁延续 FR-008：due 列表 `stable_sort` 按（优先级降序, 到达序列号升序）（`support_runtime.cpp:282-299`）；脚本请求 `seq=submitted_tick`（`support_runtime.cpp:249-253` 附近），同 tick 同键时保持脚本声明顺序，确定性成立。

### 测试命中

- `support_runtime_test.cpp:74-98`：池 entries quantity=1 被占且 RETURNING，新脚本请求经 `step_support_pipeline` 成功，断言 kExecuting / resolution=assign / att-0 转回 ASSIGNED 且归属 req-2 / registry 大小仍为 1（复用不重复占用）/ `ATTACH_REASSIGNED` 事件存在。
- `support_runtime_test.cpp:100-118`：同 tick 两个请求竞争唯一可抢占单位，高优先级 req-2 胜出并占用 att-0，req-3 被拒且 reason 含 INSUFFICIENT_AVAILABLE——真实命中运行时排序路径。
- `attach_test.cpp:222-238`：可抢占候选过滤（ASSIGNED、瘫痪 RETURNING 均排除）。

### 新问题

💭（低影响）：被抢占的原请求（如 req-1）在 `support_chain` 中保持 `kExecuting`，其 `assigned_unit_ids` 与实际占用不再对应；快照会把 `support_chain` 与 `attach_registry` 原样序列化（`snapshot.cpp:150-157`）。这与既有设计一致（EXECUTING 为终态、归建生命周期挂在配属记录上，即使正常归建完成请求侧也保持 EXECUTING），且 `ATTACH_REASSIGNED` 事件携带 `previous_request` 提供了可追溯性；若后续 UI/复盘要展示"原请求已取消"，建议在该事件之外给原请求一个明确落点（如 resolution 标记或 ATTACH_RETURN_CANCELLED 事件）。

## 3. 派系 support_kinds 与 entries 矛盾（76c915d）

### 核实

- 数据：三派系移除无对应 entries 的 `mortar-82` 声明（faction-china.json:35/53-55、faction-nato.json:28/40、faction-russia.json:28/40）；china 营级保留 `artillery-152` 声明且 entries 存在（faction-china.json:53-54/82）。
- loader 语义校验真实接线：`CheckSupportKindsResolvable`（`faction.cpp:72-88`）在 `LoadFactionInternal` 内统一执行（`faction.cpp:157`），任一声明 kind 在同池 entries 无条目即产出 `SUPPORT_KIND_NOT_CONSUMABLE` 结构化问题；运行时 `initialize_support_state` 走同一加载函数，非法派系会以 `SUPPORT_CONFIG_INVALID` 可见事件呈现。
- 裁决只消费 entries（`attach.cpp:117-147` 的 FindEntry 基于池快照 = entries 扣减占用），因此移除声明不改变运行时可请求集合；china 营级 artillery-152 可消费，NATO/Russia 的 squad-mortar-team 等仍经 entries 可请求。数据矛盾（"声明可用却必然 SCOPE_VIOLATION"）消除。

### 测试命中

- `faction_test.cpp:141-160`：正例真实加载三个内置派系文件并逐池断言声明⊆entries。
- `faction_test.cpp:162-173`：负例写临时派系（support_kinds=["not-an-entry"]）加载，断言 `!ok` 且含 `SUPPORT_KIND_NOT_CONSUMABLE`。

### 新问题

💭（低影响）：NATO/Russia 的 company/battalion `support_kinds` 现为空，而 entries 中 `squad-mortar-team` 仍可请求（faction-nato.json:40-43、faction-russia.json:40-43）。修复选择了"删声明"而非"改声明"，运行时无影响；若后续用 support_kinds 做 UI/准入白名单，会误导。建议将来把可消费条目同步进声明。

## 4. request_type 枚举（68975de）

### 核实

- `command.schema.json:135-146` 将 `support.request_type` 从"非空字符串"收紧为 5 值 enum（reinforce/fire_support/engineer/medical/logistics）。
- 运行时真实生效：`validate_command` 第一层用第三方 draft-07 校验器执行 schema（`command_validation.cpp:346-357`），`wfs_sim_inject_command` / CLI inject 均经该管道（`c_api.cpp:161-183`、`sim_runtime.cpp:330-344`），任意字符串不再通过结构校验（此前 C++ 语义检查只要求非空，`command_validation.cpp:285-290`）。
- 现有数据/测试中的 request_type 取值均在枚举内（scn-support-battalion.json:75/88/100、test_support_chain.jsonl:5-6），无连锁破坏。

### 测试命中

- **无新增负例测试**：全仓搜索未见非法 request_type 被拒的断言（现有测试只用 reinforce/fire_support 正例）。

### 新问题

🟡（低，测试缺口）：`command.schema.json:136-146` 的枚举缺少负例覆盖。建议在 `command_validation_test.cpp` 增加 `request_type: "banana"` → `SCHEMA_INVALID` 断言（或 CLI 负例），防止未来校验器降级/绕过。

## 5. 营级桩路径只跑一次（22edf4a）

### 核实

- `run_support_chain_tests.ps1:185-192` 首次营级运行（threads 1）后取 `--hash` 末行（headless 保证哈希是 stdout 唯一/最后内容，main.cpp 中 hash 在事件落盘后输出），`193-209` 第二次运行（threads 2，同 seed/ticks/script）并比较状态哈希与完整事件序列（`Seq:Tick:Message` 逐条相等）。
- 场景数据确实驱动营级配属/拒绝/转请+归建路径（scn-support-battalion.json:63-108 的 script-req-assign/reject/escalate，后随既有 Assert-Sequence 断言），两次运行比较是对真实裁决路径的确定性回归。

### 测试命中

- 两次运行同一脚本、同 seed、同 ticks，仅 threads 1→2，哈希与事件序列均比较——真实命中此前"只跑一次"的缺口。

## 6. 顺手项（e44f123 + AttachRecord id 单调）

### 核实

- `support.cpp:251-252` 解析 `target_unit`/`for_command_id`，缺省空串；`FromScenario` 的必填校验不涉及这两个字段，既有场景（未声明）无回归。两字段自 e27cf42 起已参与 `SupportRequest` 序列化（support.cpp:117-121/139-143），不改变存档形状；`for_command_id` 解析后即可驱动脚本请求"关联任务完成 → 归建"（`support_runtime.cpp:314-316`）。
- AttachRecord id 单调校验（`attach.cpp:406-435`）：唯一 + `att-<n>` 严格递增；`next_id_` 按最大序号重建（`attach.h:139-148`、`attach.cpp:293-303`）。
- 旧存档兼容：历史实现 id 恒为 `att-<records_.size()>` 且记录只追加、从不删除/重排，任何单份旧存档内 id 必然严格递增、格式合法，新校验可加载；允许缺中间序号（存档清理场景），`AttachResumesCounterAfterArchiveGap` 覆盖（attach_test.cpp:252-266）。

### 测试命中

- `support_test.cpp:294-313`：FromScenario 解析断言（target_unit=squad-a、for_command_id=cmd-7）。
- `attach_test.cpp:240-249`：非单调/非 att-<n> 存档抛 `invalid_argument`；`252-266`：缺中间 id 后游标从最大序号+1 继续。

## 新发现汇总

- 🟡 无阻塞性缺陷，1 个低优先测试缺口：request_type 枚举负例缺失（command.schema.json:136-146）。
- 💭 被抢占原请求在快照中保持 EXECUTING、需靠 ATTACH_REASSIGNED 事件解释（support_runtime.cpp:78-102；snapshot.cpp:150-157）。
- 💭 火力组放行/FIRE_TEAM_DISSOLVED 分支当前仅测试可达，生产无 SplitSquad 调用与火力组单位生成（tactical.cpp:201-218；command_chain.cpp:448-462）。
- 💭 support_kinds 与 entries 呈反向不对称（可消费但未声明），运行时无影响（faction-nato.json:40；faction-russia.json:40）。
- 新 🔴：无。

## 结论

第 1 轮全部 findings 已闭环，顺手项无回归，测试断言均真实命中对应运行时路径。结论 **PASS**，可进入下一阶段（合并或 US3 接线）。建议在后续迭代中补 request_type 枚举负例测试，并在拆分命令/火力组单位生成落地时补端到端用例。
