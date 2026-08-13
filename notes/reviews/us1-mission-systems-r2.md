# Review us1-mission-systems - 第 2 轮（针对已修复内容）

- 审查范围：T032（失联）/T033（迷雾情报）/T034（任务判定）/T035（侦察）/T036（胜负），
  分支 `feature/us1-mission-systems` tip `6d20915`；本轮流读 `git show c3128f9`、
  `3d69f3d`、`6d20915` 与 `git diff feature/us1-command-combat...feature/us1-mission-systems`
  （三点 diff，merge-base `994dbb1`），对照第 1 轮 `notes/reviews/us1-mission-systems-r1.md`。
- 审查方式：只读静态审查（未构建、未跑测试；工作区被其他进程占用，全部经 git show/git diff/git log）。
- 时间：2026-08-13

## 对比第 1 轮

### 已修复（逐条 file:line 核实，行号以 tip `6d20915` 为准）

| # | 结论 | 证据 |
|---|---|---|
| M1 | ✅ | `command_validation.cpp:49-51` 注册 patrol/fortify/recon；形状校验覆盖 `cycle_ticks`/`construction_ticks`（正整数）与 `point`/`exit_point`（坐标对象）；schema 描述与 `contracts/command-schema.md` §1 同步；端到端测试 `mission_command_test.cpp` Patrol/Fortify/HiddenRecon 三例走真实注入路径 |
| M2 | ✅ | `command_chain.cpp:537-549` 撤回按 `was_effective \|\| mission_command_id == target->command_id` 强制取消；`mission_exec.cpp:327-336` 循环重启不再 `MarkCompleted`（非循环才 MarkCompleted+Cancel）；测试 `mission_exec_test.cpp` WithdrawTerminatesLoopingContinuousMission（显式 `mission_command_id="cmd-patrol"`） |
| M3 | ✅ | `sim_state.h:85` side 字段、`sim_state.h:220-222` 存档兜底、`loader.h:40` + `sim_runtime.cpp:101` 场景加载兜底 node_id；判定点 `combat.cpp:774`、`intel.cpp:348`、`mission_exec.cpp:100`、`outcome.cpp:73`、`recon_tasks.cpp:106` 全改 side 比较；`friendly_side()`（sim_state.h）按 player_node_id 派生；`side_faction_test.cpp` 4 例（secure_zone/drive_out/recon/场景加载） |
| M4 | ✅ | `outcome.cpp:152-157` deployment_enabled 必须同时配置 deadline>0 与 zone，非法显式抛错；测试 DeploymentEnabledRequiresDeadlineAndZone |
| M5 | ✅ | `intel.cpp:266-274` 观瞄 kDisabled 阻断观察、kDegraded 观察能力 × `degraded_observation_factor`（默认 0.5，数据驱动）；`intel.h:56` + is_valid；测试 OpticsDisabledPreventsIdentification / OpticsDegradedOrSuppressedLowersTier |
| M6 | ✅ | `intel.cpp:326-341` 每 tick 构建一次地形索引 + `max_range_sq` 距离粗筛；`movement.cpp:220-223,268` 复用索引；`movement.h:108-110` 索引重载。上界正确性已推导：effective_range = base×(floor+clamp(ability)×scale) ≤ base×(floor+scale)，降级因子 ≤1 只减小 |
| M7 | ✅ | `contact.cpp:123-129` 恢复时长改闭区间 [min,max]（span<1e6 时 +1）；测试 RecoveryDurationCoversMaxTickInclusive；RNG 流变化已同步更新 `golden-run.hash`。残留边界见 N2 |
| M9 | ✅ | `contact.h:35` suppression_degrade_threshold、`contact.cpp:89-91` 场景解析、`contact.cpp:207-231` 消费；测试 SuppressionDegradeThresholdIsDataDriven |
| F1 | ✅ | `sim_state.h:220-222` 旧存档无 side 字段按 node_id 兜底（否则 side=="" 使敌我判定恒同阵营）；测试 save_test.cpp LegacyV1SaveWithoutSideKeepsFactionSemantics（状态哈希相等 + 逐单位 side==node_id） |
| F2 | ✅ | `outcome.cpp:158-163` deployment_zone 必须存在于 zone_centers，非法显式抛错（否则 FindZone 返回 nullptr 静默禁用部署超时兜底）；测试 DeploymentEnabledRequiresZoneInZoneCenters |

M8（最后已知快照捕获时机）、M10（intel_test 弱断言）、M11（objective_states 数量不一致）为第 1 轮登记项：本轮核实未误实现、未被新提交回归，维持登记。

### 回归

- 逻辑回归：未发现（`3d69f3d`/`6d20915` 之后无逻辑文件变更；M2 撤回条件有 F4 归属保证 + 双条件防误取消；M6 距离粗筛上界数学成立，不改变观察结果）。
- native/ 布局 rebase：`6d20915` 路径修复正确——`WFS_SOURCE_ROOT=${PROJECT_SOURCE_DIR}/..`（仓库根）、golden 路径 `RepoRoot()/native/tests/sim_tests/golden/golden-run.hash`（该文件在 tip 存在）、CLI 场景路径 `${PROJECT_SOURCE_DIR}/../data/scenarios/*.json`（data/ 位于仓库根，`scn-smoke-test.json`/`scn-tutorial-platoon.json` 均存在）。`native/CMakeLists.txt` → `add_subdirectory(tests)` → sim_tests/cli_tests 注册链完整，无旧布局残留引用（仅文件头注释仍写 "tests/sim_tests"，见 N7）。

### 新增

N1/N2 两条 🟡，N3–N7 五条 💭，详见 Findings 表。

## Findings

| # | 级别 | file:line | 问题 | 修复方向 | 状态 |
|---|---|---|---|---|---|
| N1 | 🟡 | native/sim/CMakeLists.txt:104-109 | sim_core 段为无条件 POST_BUILD 复制，缺 `_sim_core_need_dll_copy` 守卫：静态 triplet 下 `$<TARGET_RUNTIME_DLLS:sim_core>` 空展开导致 copy 命令缺参、构建失败（build 回退）。权威版 `feature/foundation-core-abi:native/sim/CMakeLists.txt` 及兄弟分支提交 `028d833` 均有守卫 | 照抄权威版守卫块（与本文件 sim_headless 段 45-63 同模式：按 IMPORTED_LOCATION 扩展名判动态/静态再决定是否 add_custom_command） | 待修复（可直接 cherry-pick 028d833） |
| N2 | 🟡 | native/sim/src/contact.cpp:124-129 | M7 修复移除了原 `min(span,kMaxBoundedSpan)` 截断：span≥1e6 时闭区间不再成立（仍 [min,max-1]）；span>2^32-1 时 uint64→uint32 截断，span≡0 (mod 2^32) 时 `next_bounded(0)=0` 恒返回 min_ticks，分布失真且可超出 max_ticks。`contact.h:43-46` is_valid 只校验 max≥min、无上界，数据驱动场景可静默产生错误结果（宪法 §12/§17） | is_valid 增加 `max_ticks - min_ticks <= UINT32_MAX-1`（或沿用 kMaxBoundedSpan 截断并保留闭区间语义）；极端配置显式拒绝 | 待修复（极端配置下才触发） |
| N3 | 💭 | side_faction_test.cpp（无 outcome/combat 断言） | M3 改了 5 个判定文件，但测试只覆盖 mission_exec/recon_tasks/loader，未直接覆盖 outcome.cpp `friendly_side`/`EnemyInZone`（关键目标/失败条件）与 combat.cpp side 目标选择的多节点同阵营分支 | 补两例：同阵营其他节点单位不阻止 zone 目标完成、不被选为射击目标 | 登记 |
| N4 | 💭 | native/sim/src/recon_tasks.cpp:93 | M6 未覆盖该调用点：ConcealmentAt 每单位每 tick 重建地形索引；StepObservationPost 经 observe_pair 便捷入口（movement.cpp:134-141）逐敌方重建。侦察单位数量少、影响小 | 侦察步进与 intel 共用预建索引（低成本顺手） | 登记 |
| N5 | 💭 | native/sim/src/movement.cpp:133-141 | 新增 vector 便捷重载位于 `NOLINTEND(bugprone-easily-swappable-parameters)` 之后、未加自己的 NOLINT；若 lint 启用该规则会告警 | 为重载补 NOLINT 或调整注释块范围 | 登记 |
| N6 | 💭 | command_validation.cpp:41-52 | 9 个完成条件 vs 13 种任务类型：OBSERVATION_POST/FIRE_RECON（及 MOVE/ATTACK/DEFEND/SUPPORT_REQUEST 历史遗留）无语义匹配条件，经真实命令路径需凑无关条件（如 OBSERVATION_POST 用 recon+point，但执行器忽略 point）；FR-042「13 类型可用」的语义完整性待收敛 | 为观察哨/火力侦察补条件或显式文档化条件映射（round1 未要求，先登记） | 登记 |
| N7 | 💭 | 文档/元数据 | data-model.md 未记录 unit.side 字段（仅 scenario.schema.json description）；sim_tests 各文件头注释仍写旧布局 "tests/sim_tests"；tasks.md T032–T036 复选框未勾选（按流程待 PR 通过后标记，不属缺陷） | 补充字段文档、顺手改注释 | 登记 |

## 协调注意（非本分支缺陷）

本分支 base `994dbb1` 早于兄弟分支 `feature/us1-command-combat` tip 的三个提交：开 PR 时需并入/继承 `028d833`（sim_core 守卫，即 N1 已在别处修好）与 `e45a9d5`（FR-063 区域压制按命中比例缩放，影响 T032 压制阈值输入）；本分支自带 `6d20915` 与兄弟分支 `43e5fe5` 等价的 native/ 路径修复。

## 未验证猜测

1. 本地未构建、未跑测试（任务要求静态审查；分支未推送、无远端 CI 证据）；M1/M2/M7/F1/F2 回归测试的通过状态为静态推导，历史证据为第 1 轮本地 305/305。
2. `c0333c0`/`89c99f4` RED 提交的具体编译失败形态未逐提交实证（结构上满足测试先于修复）。
3. M6 营级 tick 实际耗时无基准；N2 极端配置（span≥1e6）为静态可复现推导，未运行验证。
4. 云 AI 注入（T080 后置）未端到端验证。

## 宪法合规要点

- §2 测试：T032–T036 各有测试文件并注册 CTest（contact/intel/mission_exec/recon_tasks/outcome/mission_command/side_faction 均在 `native/tests/sim_tests/CMakeLists.txt` 源列表，`gtest_discover_tests(sim_tests)` 注册）；M1–M7/M9 与 F1/F2 各有针对性回归测试。
- §7 确定性：统一 PCG32 RNG（rng.cpp），无 std::rand/random_device；`step_sim_state` 固定顺序（ProcessDue→movement→combat→contact→intel→recon→missions→outcome）；std::map 键序遍历；无可变 static（仅函数级 static const 数据表）；M7 改 RNG 流时同步更新黄金哈希。
- §12 数据驱动：side/suppression_degrade_threshold/degraded_observation_factor/recon/outcome 参数全部场景化；N2 属配置边界校验缺失。
- §15 无头：sim_tests + CLI 测试无头驱动 sim_headless；native/ 布局路径修复核实正确。
- §16 统一时间：全部使用 `state.clock.tick()`，无现实时钟（grep system_clock/steady_clock/time 确认）。
- §17 错误处理：M4/F2/各 FromScenario 非法配置显式抛错；load_save 异常映射为显式错误码，无静默吞错。

## 整体结论

PASS（置信度 0.90）。M1–M7/M9 与 F1/F2 修复真实、逐条可验证，无逻辑回归，无 🔴；两条 🟡 均不阻塞判定——N1 为构建配置守卫遗漏（权威版与兄弟分支已有现成修复，建议开 PR 前并入），N2 仅极端数据配置触发。建议：开 PR 前合并 `028d833`（N1）并顺手收紧 `contact.h` 的 span 上界校验（N2）。

## 收敛与轮次检测

收敛正常：第 1 轮 M 系列全部关闭；本轮新发现从「功能正确性/契约」转移到「构建守卫 + 极端配置边界 + 测试覆盖补全」，问题面收窄、数量下降（11 → 2 🟡 + 5 💭），无同问题反复、无修复引入的实质性逻辑回归。轮次正常（第 2 轮）。
