# US3 第一组（T055/T057–T060）代码审查记录 r1

- 审查对象：分支 `feature/us3-command-info`，增量 `git diff feature/us2-integration...feature/us3-command-info`
- 提交：da44308 / 1a1a7ec / 4e46d70 / 11f170a / 129564e / bb82c90 / 8c093bf
- 方式：只读静态审查（仅 git show / git diff / git log 读对象库；未读工作区、未执行构建）
- 依据：spec.md US3、FR-016/028/029/039/051/077、SC-005、CHK154/159/162/165；data-model.md 对应小节与 §18；constitution §2/§7/§12/§17

## 结论

**PASS（置信度 0.8）**。五个任务按 data-model §18 登记的基线整体落地正确，测试断言质量较高（非弱断言），确定性、快照语义、新旧存档兼容的主路径设计谨慎。未发现安全漏洞、数据丢失或确定性破坏；发现 1 个应修的数据加载正确性缺陷（R1）与若干语义/口径与测试覆盖问题。实现代理报告的 Release/Debug CTest 401/401、dotnet 218/218 与本结论一致。

## 逐项核对

### T057 指挥树校验（command_org.cpp / model/command_node.h / model/organization.h）

- 环：`HasParentCycle` 沿 parent 链检测，每个环上节点都会产出 `COMMAND_ORG_NODE_CYCLE`，覆盖正确（重复报一遍是噪音不是错误）。
- 层级链：直接父子 echelon 严格递减（`parsed.echelon >= parent->echelon` 判非法），允许营直接指挥排（跨级）、拒绝同级/反向；传递性由逐边校验保证。
- 编制互反：`BuildOrganizationTree` 对 parent_id 推导与声明 subordinate_ids 做排序后比对，产出 `COMMAND_ORG_ORGANIZATION_MISMATCH`；类型/成环由 `OrganizationTree::SetParent` 的 CanContain/IsDescendantOf 兜底。
- 节点编制错配：存在性 + echelon 一致双检（`COMMAND_ORG_NODE_ORG_UNIT_UNKNOWN` / `NODE_ORG_ECHELON_MISMATCH`）。
- 单位归属：场景单位 node_id 必须命中指挥节点（`COMMAND_ORG_UNIT_NODE_UNKNOWN`），player_node_id 同样校验；squad 节点在 ParseNode 拒绝（FR-001）。
- 直属班基数：班是编制叶子（CanContain 对 kSquad 一律 false），配属/非直属单位天然不进 organizations；`direct_squad_base` 按节点编制锚点子树内的班计数。
- 非法编制不崩溃：validate 固定顺序收集结构化 issue，loader 拒绝；C API 对 init 异常 `catch (...)` 返回 nullptr。

### T058 情报同步（intel_sync.cpp / intel.cpp）

- 间隔 100/100/300/1200 tick（20Hz 下 5s/5s/15s/60s），`sync_ticks_for` 按节点层级取间隔，`tick < last+interval` 跳过——正确。
- 三类信息共用间隔：单个 `INTEL_SYNC` 事件一次携带 enemy（合并敌情数）/own/last_known 计数——正确。
- 直属实时：`last_direct_sync_tick` 每 tick 刷新（test 断言 tick 37 == 37）。
- 来源重标：direct→sync（保留 unit_id/node_id）→relay（仅 level=子节点层级）——与 FR-031 及 §18 登记一致；SC-005 的 100% 来源可见在事件与记录两个层面落实。
- 冻结最后已知：`ChildNodeLost`（节点链路不生效或代表单位失联/摧毁）时跳过合并，父视角保留上次 relay 记录，test 断言 `last_seen_tick == 100`、`enemy=0`——正确。
- T033 F5 字段：观察时填充 `observed_count/type_name/composition` 并随序列化输出，test 断言非空——但 relay 一跳后丢失（见 S2）。

### T059 摘要与裁剪（summary.cpp）

- 字段仅含 missions（active/completed/failed/timed_out）、completion_pct、losses（soldiers/vehicles/squads）、support_requests/needs_support，无单位 id/坐标/武器——符合 FR-051。
- 快照语义：build_summary 读状态后冻结为值对象，`SUMMARY_REPORT` 与 registry 只写一次；test 断言 tick=100 `completed=0` 冻结、tick=600 `completed≥1`。
- 统一裁剪 `build_authorized_view_json`：本节点直属单位完整状态 + 直接下级最新摘要；test 断言下级摘要不含 units/x/soldiers——正确。
- 任务结果累计：mission_exec 在 complete/fail/timeout 三处递增 `mission_outcomes[unit.id]`，接线正确。

### T060 通信（comm.cpp）

- 范围公式四类输入齐全：功率均值×power_scale、保障增益×系数、地形罚值取端点较严者、民用设施端点半径内增益，下限 min_range_km（comm_test 覆盖四项 + 保底）。
- 链路两类：单位↔所属节点、子节点↔父节点，按单位列表/节点插入顺序确定性生成。
- 中断与失联取更严：`link_effective = connected && 两端未摧毁/未失联`；恢复独立（comm 在 step_comm、失联在 step_contact）。
- 到期指令顺延：中断期间 `arrival_tick = tick+1` 逐 tick 门控，恢复后按序到达；comm_test 断言中断 150 tick 不完成、恢复后完成。

### T055 验收测试（run_intel_roles_tests.ps1）

- 断言真实命中：上级摘要无 unit=/x=/weapon=、下级 UNIT_MOVING/MISSION_COMPLETED 完整状态、direct/sync/relay 三级来源、tick=100/200/300/600 间隔、快照 completed=0 与 ≥1——均为强断言。
- threads 1/2：同输入两次运行比较状态哈希与完整事件序列（seq/tick/message）——已接线，但见 N7。

### 快照 / 存档

- 新状态条件序列化/恢复：command_org/intel_sync_state/summaries/mission_outcomes/comm_state 仅在 `configured` 时写入（snapshot.cpp:173-186），加载用 `parsed.contains` 缺省回退（save.cpp:445-462）；旧场景加载→再序列化顶层字节不变。
- 旧存档可加载：所有新字段 from_json 带缺省；CommandOrgState/CommState/SummaryRegistry 均有往返测试。
- golden 哈希：golden-run.hash 未变；scn-smoke-test 蓝红单位相距约 4.2km 无观察记录，序列化增量不落入哈希（见 S3 的潜在风险）。

## Findings

### 🔴 R1 指挥树构建与校验顺序不对称，合法数据被静默拒绝

[command_org.cpp:313](D:/Workbench/Agent/NewProject/native/sim/src/command_org.cpp:313) 用 `CommandTree::from_json` 构建指挥树，而 `CommandTree::AddNode` 要求父节点先于子节点出现（model/command_node.h 内联实现）；[command_org.cpp:241](D:/Workbench/Agent/NewProject/native/sim/src/command_org.cpp:241) 的校验却基于完整 node_ids 集合做父存在/层级链判定，与数组顺序无关。

**Why：** 合法但"子节点声明在前"的场景（例如 nodes 数组把 `node-plt-1` 放在 `node-co-1` 之前）会通过 validate_command_org，随后在 `load_command_org` 构建树时抛 `invalid_argument`，`initialize_runtime_state`（sim_runtime.cpp:218-221）不捕获，最终被 C API `catch (...)` 吞为 nullptr（c_api.cpp:133）——用户只见"加载失败"，没有任何结构化 issue，违反宪法 §17"错误必须被记录或提示"以及本文件头注释"校验已过直接构建"的契约。当前三个场景恰好都父在前，无测试覆盖该顺序。

**建议：** 让 `load_command_org` 与 `OrganizationTree` 同构——先把全部节点入树再统一 SetParent 接线（顺序无关）；或在校验阶段检测"父节点后置"并产出结构化 issue。补一条"子先父后仍成功加载"的单测。

### 🟡 S1 T059 摘要聚合范围仅为直属单位，多级聚合被截断

[summary.cpp:197](D:/Workbench/Agent/NewProject/native/sim/src/summary.cpp:197) 只统计 `unit.node_id == from_node` 的单位：连级→营级摘要只含连 CP 本体的损失/任务结果，其下属排级的损失与完成度不进摘要；而排级摘要只上报给连，营级 authorized view 无法聚合出全营损失/完成度。

**Why：** FR-051/CHK162 要求"无信息遗漏"，T059 任务描述也是"损失/支援聚合"；现状使营级态势缺失孙级损失。现有测试只覆盖叶子节点（排→连），未覆盖连→营的数值聚合，缺口因此未被发现。

**建议：** 明确"下级摘要 = 该节点整棵子树聚合"并在 build_summary 中沿 node 子树累加（或显式在 data-model §18 登记"仅直属"口径）；补"营级摘要包含排级损失"的断言。

### 🟡 S2 T033 F5 识别档位字段 relay 一跳后丢失

[intel_sync.cpp:101-118](D:/Workbench/Agent/NewProject/native/sim/src/intel_sync.cpp:101) 的 `MergeChildIntel` 未复制 `observed_count/type_name/composition`，这些字段只在直属观察时填充（intel.cpp:326-328）。连级（sync）与营级（relay）记录恒为 0/空。

**Why：** data-model §18 登记"T033 F5 已落实（intel.cpp 观察时填充并随快照输出）"，但"随快照输出"只对第一跳成立；更上级看到的敌情只有 tier，丢失数量/类型/构成，与"识别档位核心字段输出"的意图不符。

**建议：** 在 relayed 记录中随 tier 一并复制三字段（若按档位降级输出则在登记中明确档位投影规则），并补营级记录的 F5 字段断言。

### 🟡 S3 IntelRecord 新增三字段无条件输出，旧存档字节不再往返一致

[intel.cpp:227-229](D:/Workbench/Agent/NewProject/native/sim/src/intel.cpp:227) 无条件输出 `observed_count/type_name/composition`（即使为空），而相邻的 `IntelSource.level` 采用"空值省略"保持字节一致（intel.cpp:198-203）。

**Why：** 含 intel_records 的旧存档加载后再次序列化会多出三个键，字节与哈希随之改变，与 save.cpp 注释宣称的"旧存档加载后再次序列化字节不变"不一致。golden-run.hash 目前未受影响，只是因为 scn-smoke-test 蓝红相距 4.2km 从不产生观察记录；一旦旧场景发生目视，哈希即漂移。

**建议：** 三字段空值时省略（与 level 同策略），或作为破坏性变更在 data-model/save 契约中显式登记并走 golden 更新流程。

### 🟡 S4 通信范围公式与 data-model §18 登记不一致

[comm.cpp:249](D:/Workbench/Agent/NewProject/native/sim/src/comm.cpp:249) 实现 `base_range_km × 功率均值 × power_scale + 保障增益 × 系数 − 地形罚值 + 民用增益`；[data-model.md:142](D:/Workbench/Agent/NewProject/specs/001-war-sim-command-battle/data-model.md:142) 登记为"装备功率平均 × power_scale + …"，缺 `base_range_km` 乘项。comm_test 按代码口径断言（2×2→10.0），即测试锁定的是代码而非登记文本。

**Why：** §18 是后续任务与审查的权威基线，公式文本漂移会导致 T061+/数值验收口径混乱。

**建议：** 修正 §18 公式文本或代码，二选一后补一行公式级断言并引用登记行号。

### 🟡 S5 节点通信健康绑定"首个命中单位"，无节点通信资产语义

[comm.cpp:52](D:/Workbench/Agent/NewProject/native/sim/src/comm.cpp:52) `NodeUnit` 取列表内首个 `node_id` 命中的单位作节点代表；单位↔节点与节点↔父链路端点均为该单位（comm.cpp:381-401），intel_sync 的 `ChildNodeLost` 也只看第一个单位。

**Why：** 摧毁/失联一个普通排内单位（恰好是列表首位）会同时断掉该节点全部单位的指挥链路、节点↔父上送链路，并让上级视整个子节点失联。这是可复现的确定性行为，但与 FR-077"通信主要依靠通讯保障部队"的建模不一致——保障部队只影响范围系数，节点连通却绑定单个普通单位。无配置可声明哪个单位是节点 CP/通信载体。

**建议：** 明确节点代表单位语义（例如场景可声明 `node_hq_unit`），或改为节点通信资产抽象；补"代表单位被摧毁导致全节点断链"的行为测试，确认这是预期。

### 🟡 S6 T057 部分校验分支无单元测试

编制互反不一致（`COMMAND_ORG_ORGANIZATION_MISMATCH`，command_org.cpp:140）、编制树成环（`ORGANIZATION_LINK_INVALID`）、节点/编制重复 id、squad 节点拒绝均无用例；现有 7 个测试覆盖了环/层级链/错配/归属，但不覆盖上述分支。

**Why：** 宪法 §2 要求确定性算法有测试；这些分支正是"非法编制不崩溃"的边界，缺测会使后续重构静默破坏校验。

**建议：** 补齐互反不一致、org 成环、重复 id、squad 节点、以及 R1 的"子先父后顺序"用例。

### 💭 N1 FR-016/CHK154 口径

`direct_squad_base` 按节点编制锚点的整棵子树班数计数（command_org.cpp:148-155），营级节点等于"全营班"；CHK154 答复为"直属下级行政编制中的班"，字面上不相等。§18 已选择子树口径，建议在 FR-016/CHK154 文档同步说明，避免后续误解为"仅直接下级编制挂的班"。

### 💭 N2 摘要 id 潜在碰撞

`"sum-" + tick + "-" + from + "-" + to`（summary.cpp:190）在节点 id 含 `-` 时可能碰撞（`a-b→c` 与 `a→b-c` 同 id），from_json 会因重复 id 拒绝加载。建议转义分隔符或改单调序号。

### 💭 N3 同步间隔单调性校验不完整

`IntelSyncConfig::is_valid`（intel_sync.h:45）只约束 company≤battalion、brigade≥battalion，未约束 platoon≤battalion；可配置出"排级间隔 > 营级间隔"，破坏 FR-028"层级越高间隔越长"。建议补 `platoon <= company <= battalion <= brigade`。

### 💭 N4 场景语义校验分叉，错误信息被吞

intel_sync 间隔顺序、comm 数值合法性只在 `initialize_runtime_state` 的 FromScenario 中抛错，C API `catch (...)` 返回 nullptr（c_api.cpp:133）无 message；loader 的结构化 issue 通道未覆盖这些字段。宪法 §17 要求错误可定位，建议把这些校验前移到 loader 产出结构化 issue。

### 💭 N5 step_comm 顺延了已确认命令

comm.cpp:427-433 对 `kIssued` 与 `kAcknowledged` 都写 `arrival_tick = tick+1`；kAcknowledged 已到达，顺延无实际效果。建议收窄到 kIssued，语义更清晰。

### 💭 N6 空 node_id 单位的链路端点错配

`initialize_comm_state` 为 node_id 为空的单位生成 `to_id=""` 的链路，`NodeUnit("")` 返回首个空节点单位；存在多个未挂节点单位时端点错配、距离恒 0。当前已配置场景无此情形，建议显式跳过空 node_id 的链路生成。

### 💭 N7 threads 1/2 断言当前保护价值有限

threads 尚未接入战斗计算路径（headless.h:32 注释"仅作配置保留"），T055 的两线程一致性断言目前无法被实际并行度差异触发。保留作为未来并行化门禁合理，但当下不能证明并行确定性。

## 表扬

- T055 断言质量高：不仅比对哈希，还逐条比对 seq/tick/message 事件序列，快照语义用 `completed=0` 与 `completed≥1` 双向锁定，来源标注三级各自独立断言。
- intel.cpp:273-276 对 register_intel 键字段的写回修复真实消除了"默认构造记录漏 observer_node_id 导致 T058 过滤静默漏项"的隐患，且注释写明了原因。
- 存档新旧兼容处理细致：顶层新字段按 `configured` 条件省略、加载按 `parsed.contains` 缺省、from_json 全部带缺省值，旧场景顶层字节稳定。
- 校验顺序固定、issue code 命名清晰可 grep，符合宪法 §17 不静默吞错。

## 下一步

R1 建议在本组合并前修复（改动小、影响数据健壮性）；S1–S6 可与实现方确认口径后分批处理，S3 若认定为字节兼容破坏需显式登记或走 golden 更新流程。
