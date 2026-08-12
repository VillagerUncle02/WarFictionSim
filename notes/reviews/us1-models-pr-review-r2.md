# AI 代码审查记录 — PR #110 第 2 轮（native/ 布局重基后深度复核）

- 分支：`feature/us1-models`
- PR：https://github.com/VillagerUncle02/WarFictionSim/pull/110
- 当前 HEAD：`1695c7f`（fix(native): adapt golden/CLI test paths to native/ layout）
- 审查时间：2026-08-13（Asia/Shanghai）
- 审查方式：只读审查（git show / git diff / git log / gh pr view / gh pr checks / gh run view），未修改源代码、未 push/merge/approve
- 结论：**PASS（置信度 0.86）**——无 🔴；1 🟡（前轮遗漏的模型校验对称性缺口，非重基回归）；4 💭

## 一、审查范围（任务 / commit）

任务：T025（指挥节点/编制/最小可指挥单位）、T026（士兵/班组/载具/武器/弹药）、T027（地形/设施/工事/环境）、T028（任务模型与 13 种类型注册表）、T037（基础数据）、T038（连排级教程场景）。

增量依据：三点 diff `git diff origin/feature/foundation-core-ai...origin/feature/us1-models`，merge-base 为 `7cf094e`。增量提交（7cf094e..1695c7f）：

```text
8021f5b feat(sim): T025 command node/organization/min command unit models
b01809c feat(sim): T026 soldier/squad/vehicle/weapon/ammo combat models
4c72a73 feat(sim): T027 terrain/facility/fortification/environment models
ef74fea feat(sim): T028 mission model + 13-type registry + state machine
c441c2e feat(data): T037 baseline units/terrain catalogs（schema 校验）
0eba587 feat(data): T038 platoon tutorial scenario（独立存档）
71381a5 docs: mark T025-T028/T037/T038 complete
75bd76e fix(sim): address US1 model review findings F1-F9
105381a fix(sim): address US1 model review round 2 findings
6a2fe04 test(sim): unique per-process temp paths for parallel ctest (F1)
b5efb37 docs: PR #110 review record (PASS, round 2)
6b487b3 build(native): keep sim_core C ABI DLL target after layout rebase
1695c7f fix(native): adapt golden/CLI test paths to native/ layout
```

重点复核最后两个重基提交：`6b487b3`（保留 sim_core DLL target）与 `1695c7f`（golden/CLI 测试路径适配）。

## 二、对比上一轮

上一轮记录（分支内 `notes/reviews/us1-models-pr-review.md`）结论为 PASS（第 2 轮），其 head `58a0559` 已被仓库 sanitize + native/ 布局重基改写。经 git 对象比对，前轮修复提交与当前历史的等价映射：

| 前轮提交（旧布局） | 当前等价提交（native/ 布局） | 内容 |
|---|---|---|
| c9f2003 | 75bd76e | F1–F9 逻辑组修复 |
| 8f47343 | 105381a | 第 2 轮 findings 修复 |
| 58a0559 | 6a2fe04 | 并行 ctest 临时路径唯一化（F1） |

逐项复核前轮 F1–F9 均在当前 HEAD 保留并生效：F1 临时路径唯一化（test_temp_dir.h，pid+进程内序号）✓；F2 持续任务循环在状态机表外由 T034 重下发、Mission.continuous 与注册表一致性校验 ✓；F4 data_root 显式三路径 API + 场景单位 type/ammo 数据目录交叉校验 ✓；F5 command_validation 内置类型表由 registered_mission_types() 生成，消除双源漂移 ✓；F6 OrganizationTree parent/subordinate 互反一致 + 非仓库 Schema 不推导数据根（旧 API 兼容）✓；F7 数值域校验 ✓；F8 ConditionExpr 短路分支缺失变量不抛错 ✓；F9 未知枚举经 mission_type_spec 显式抛错 + 回归测试 ✓。黄金哈希变更在 75bd76e 显式登记（1d26d167…→deff7f44…），符合黄金门禁流程。

## 三、Findings 表（行号以当前 HEAD 1695c7f 为准）

| # | 级别 | file:line | 问题 | 修复方向 | 状态 |
|---|---|---|---|---|---|
| F1 | 🟡 | native/sim/include/wfs/sim/model/combat.h:377 | Vehicle::is_valid 仅校验 armor 与乘员/载员容量，未校验 crew/passengers 内部 id 重复、同一士兵同时出现在 crew 与 passengers，也未校验 kModuleDamage/kSeverelyDamaged 与模块状态的一致性（kModuleDamage 却全 functional、kSeverelyDamaged 却全 functional 均判 valid）；与 Squad::is_valid（combat.h:296 起）的重复 id 校验不对称，可让非法花名册数据通过校验 | 仿照 Squad 增加 crew/passengers 重复 id 与跨花名册重叠检查；补充状态-模块一致性规则（kModuleDamage ⇒ 至少一模块 degraded/disabled；kSeverelyDamaged ⇒ 至少一模块 disabled），或显式登记允许的中间态 | 新发现（前两轮未覆盖，非重基回归） |
| F2 | 💭 | native/sim/include/wfs/sim/model/mission.h:259 | ConditionExpr kEq/kNe 对 double 直接 ==/!=，确定性成立但浮点相等语义脆弱（如 0.1+0.2 型累积值永不等于阈值） | 若阈值语义为整数 tick 建议改为整型比较，或注释约定"须使用可精确表示的值/容差" | 观察 |
| F3 | 💭 | native/sim/src/mission_registry.cpp:30 | 13 种任务类型为编译期 C++ 表而非 JSON 数据 | tasks.md T028 已预设 C++ 表，任务类型属引擎固定词汇而非 §12"可自定义内容"（部队/地形/设施已全部 JSON 化）；若 SC-010 要求运行时热增类型（不重编译），需迁移为 JSON 注册表 | 观察 |
| F4 | 💭 | native/tests/cli_tests/CMakeLists.txt:9 | 场景路径以 `"${PROJECT_SOURCE_DIR}/../data/..."` 拼接，依赖 native/ 恰位于仓库根下一层 | 与 sim_tests 一致定义并传入 WFS_SOURCE_ROOT（仓库根）避免 `..` 硬编码 | 观察 |
| F5 | 💭 | native/sim/include/wfs/sim/model/combat.h:1 等（5 个模型头）；native/tests/sim_tests/golden/golden_test.cpp:1 等 | native/ 重基后文件头总览注释中的路径与真实路径不一致（模型头写 `sim/include/...`、测试写 `tests/...`，实际为 `native/sim/...`、`native/tests/...`），两套相对基准混用 | 统一路径注释约定（include 根相对或仓库相对）并批量更新；纯注释问题，宪法 §5 实质已满足 | 观察 |

## 四、未验证猜测（非确定问题）

1. sim_core.dll 的符号导出仅在 CI 构建层面验证；尚无 C# P/Invoke 运行期消费测试（app 侧测试未调用 C ABI），`WINDOWS_EXPORT_ALL_SYMBOLS ON` 在 clang-cl 下导出 C ABI 符号的运行时行为未证实。
2. 黄金哈希 deff7f44… 与库内驱动结果的一致性由 CI golden 测试确认，本次本地未构建重跑。
3. 载具/车辆基线目录尚未建立；loader 的场景单位 type 目前仅与 squads 目录交叉校验，载具类单位接入属后置任务，扩展点未验证。
4. specs/plan.md 仍描述旧布局（sim/、ui/、tools/ 位于仓库根），与 native/、app/ 新布局不一致；文档同步不在本 PR 增量范围内。

## 五、宪法合规要点

- §2 测试：新增 command/combat/terrain/mission_registry/data_library/tutorial 六组测试，覆盖枚举往返、数值域、NaN、引用完整性、设施生命周期、状态机与确定性；既有 golden/loader 回归保留。CI push（31590594435）与 pull_request（31590597165）双 run 均 success（head 1695c7f），native C/C++ 与 app C# job 全绿。
- §5 注释与文档：所有模型头均有文件级总览与"为什么"注释；loader.h 公开接口契约注释完整（F5 仅路径字符串陈旧）。
- §7 确定性：注册表/状态机/错误收集顺序固定；ConditionExpr 为纯函数、固定短路顺序，不依赖随机/时钟/无序容器遍历；黄金哈希变更显式登记。
- §12 数据驱动：units/、terrain/、scenarios/ 全部 JSON 且携带 schema_version；加载执行 JSON Schema + 语义双重校验，非法数据以结构化 issue 报错而非崩溃（loader 层不抛异常、不静默吞错）；跨文件引用（班→武器/弹药、武器→弹药、工事→武器类别、场景单位 type/ammo 兼容）逐项校验。
- §13 存档兼容：全部模型支持 nlohmann::json 往返序列化；scenario schema_version 与 Schema 一致性校验；教程 save_slot 独立存档不变量由 schema（if/then）+ loader（F3）双重保障。
- §17 错误处理：未知枚举/非法数值显式抛 std::invalid_argument 或返回结构化 issue，无静默吞错。
- FR 核对：FR-048 每节点单一 AI 归属（NodeOwner 类型层面互斥，AI 必填 ai_id、玩家禁挂 AI）✓；FR-014 设施部署/取消/重布置/摧毁与侦察残留、纯坐标永不显示（12 项生命周期测试）✓；FR-062 四方向防护（前/侧/上/底，动能+化学能）、模块状态（机动/观瞄/装填）、乘员/载员容量+花名册、can_abandon 弃车入口、浮渡与重装备标志 ✓；FR-042 13 种类型全量注册 + 持续/侦察标志 ✓；FR-044 状态机（下达→执行中→完成/失败/超时/取消，超时后继续/取消/判失败）✓。

## 六、整体结论

**PASS（置信度 0.86）**。无 🔴 阻断项；1 🟡（Vehicle 校验对称性缺口，属前两轮遗漏、非重基回归，建议在载具数据接入（T031/T0xx）前修复）；4 💭（浮点相等语义、任务类型表数据化边界、CLI 场景路径 `..` 拼接、文件头路径注释陈旧）。CI 双 run 绿、mergeState=CLEAN/MERGEABLE、前轮 F1–F9 修复全部在 HEAD 保留。

## 七、收敛与轮次检测

本轮为 PR #110 增量的第 3 轮审查（第 1 轮逻辑组初查+复审 → 第 2 轮 MCP 审查 → 本轮 native/ 布局重基后深度复核）。前轮 findings 已全部闭环（提交映射见 §二）；重基本身核对无误：include 路径（wfs/sim/model/*.h）、WFS_SOURCE_ROOT=仓库根、golden 路径（native/tests/...）、sim_core DLL target、app Directory.Build.targets 与 CMake 均一致，仓库根无残留 tests/ 目录、无旧布局引用。本轮新发现 1 🟡 + 4 💭，无阻断项，且 🟡 为前轮遗漏而非重基回归；问题数量逐轮递减，判定收敛，可进入人工 Approve。
