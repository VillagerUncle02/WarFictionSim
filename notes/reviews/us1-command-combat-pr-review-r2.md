# Review us1-command-combat（PR #111）— 第 2 轮 PR 审查（native/ 布局 rebase 后）

- 审查对象：PR #111，分支 `feature/us1-command-combat`，当前 HEAD `43e5fe5`
  （`fix(native): adapt golden/CLI test paths to native/ layout`）
- 审查方式：Code Reviewer 子代理，只读（git log/show/diff 等），未修改任何源代码
- 时间：2026-08-13

## 审查范围（任务 / commit）

- 任务：T023（命令链路集成测试）、T024（战斗结算黄金测试）、T029（命令下达与通讯延迟）、
  T030（机动系统）、T031（战斗结算：动能/化学能穿深、过穿、班组区域结算、压制、模块损伤、弃车与乘员/载员结算）。
- 增量提交（三点 diff `feature/us1-models...feature/us1-command-combat`，merge-base `6b487b3`）：
  共 8 个提交、40 文件，+4794/−43：

  1. `7bc7f08` test(cli): add T023 command chain integration test（RED 基线）
  2. `1c50b9e` feat(sim): implement US1 command/movement/combat systems（T029–T031）
  3. `47a2518` test(sim): add US1 review regression tests（F1/F4/F5/F6/F8/F9/F11，RED 基线）
  4. `b57d727` fix(sim): address US1 review findings F1–F9/F11/F15
  5. `e5e7d9d` fix(sim): load legacy v1 saves without crew_count field（N1）
  6. `887e61e` docs: mark T023/T024/T029–T031 complete + review records
  7. `994dbb1` docs: AI PR review record for PR #111（PASS，round 1）
  8. `43e5fe5` fix(native): adapt golden/CLI test paths to native/ layout（本轮新增，重点审查）

核心文件：`native/sim/src/{combat,movement,command_chain,sim_runtime}.cpp`、
`native/sim/src/sim_state.h`、`native/sim/include/wfs/sim/{combat,movement,command_chain}.h`、
`native/tests/sim_tests/{command_chain_test,runtime_combat_test}.cpp`、
`native/tests/sim_tests/golden/combat_golden.cpp`、
`native/tests/cli_tests/{run_command_chain_tests.ps1,test_command_chain.jsonl}`、
`contracts/schemas/{command,vehicles}.schema.json`、`data/units/vehicles.json`、
`data/scenarios/scn-runtime-combat-test.json`。

## 对比上一轮

- 上一轮（PR 级第 1 轮）为 PASS（head `c666b6c`）；本轮唯一实质变化是分支被
  rebase 到 native/ 布局（`1d5815c`）之上，并新增 `43e5fe5` 做路径适配。
- 逐文件核对 `c666b6c` 与当前 HEAD：combat/movement/command_chain/sim_runtime/
  sim_state/loader/command_validation/save/snapshot/ai_inject 及全部新增测试、
  schema、data 文件均逐字节一致；唯一差异是
  - `golden_test.cpp`：黄金哈希路径 `tests/sim_tests/...` → `native/tests/sim_tests/...`（1 行）；
  - `cli_tests/CMakeLists.txt`：场景路径 `${PROJECT_SOURCE_DIR}/data/...` → `${PROJECT_SOURCE_DIR}/../data/...`（2 行）。
  即 rebase 未丢失任何测试数量或断言语义，仅做布局路径修正。
- 前序已修复项复核（当前 HEAD 无回归）：F1 开火冷却（`combat.cpp:898` 命中判定前落账，
  测试 `HitConsumesFireCooldown` 通过）；F2 弃车悬垂引用（`combat.cpp:660/691-693` 先拷贝
  id/node/坐标再 push_back）；F3 弃车清空名单+双班组（`combat.cpp:668-700`，
  测试断言 `soldiers.empty()` 与 `-dismounted-crew/-dismounted-passengers`）；F4 跨单位
  撤回/修改拒绝（`command_chain.cpp:296-313`，测试覆盖）；F5 车辆目录加载（`loader.cpp`
  七类目录 + `sim_runtime.cpp` MakeRuntimeUnit）；F6 选弹适配入评分（`combat.cpp:488-495`）；
  F7 批量 ACK 子命令匹配与战斗中期存档往返；F8 两栖数据驱动；F9 配置范围校验
  （`CombatConfig::FromScenario` 显式抛错）；F11 deadline `>=` 边界；F15 拒绝记录 0；
  N1 `crew_count` 默认值（`sim_state.h:180` value() 回退 + LegacyV1 回归测试）。
- 上轮登记的开放项经复核仍然存在且未被误实现（F12 烟幕死参数、F13 meta schema/递归深度、
  F14 每 tick 全量拷贝、F16 上/底防护不可达、N2 批量 meta 命令、N3 选弹仅正向加成），
  均属不阻塞项，保持登记状态。

## Findings

| # | 级别 | file:line | 问题 | 修复方向 | 状态 |
|---|---|---|---|---|---|
| R3-1 | 🟡 | combat.cpp:945-953（对照 425-427） | 单位级压制实际施加值未按命中比例缩放：`resolve_suppression(..., area_hit=true)` 用完整的 `suppression_area_gain × lethality` 更新 `suppression`，而 `resolve_area_engagement` 返回并在 `COMBAT_AREA_HIT` 日志中记录的 `suppression_added` 是按 `hit_count/count` 缩放后的值；日志数值与真实状态增量不一致（FR-063 语义被稀释） | 实际施加值改用 `area.suppression_added`（或让日志输出真实 delta），保持注释"按命中比例 × 杀伤力"与状态一致 | 新发现，登记 |
| R3-2 | 💭 | command_chain.cpp:316-320 | 通讯延迟抽样区间为 `[min_ticks, max_ticks-1]`：`delay = min + next_bounded(span)` 上界不含 max，注释却写 `[min, max]`；默认配置下 10s（200 tick）永不被抽到（实际 60–199 tick） | 注释与实现统一（`span = max-min+1` 或改注释为 `[min, max)`）；确定性不受影响 | 新发现 |
| R3-3 | 💭 | combat.cpp:398, 419 | `resolve_area_engagement` 局部变量 `casualties` 只 `++` 从不读取，属死代码（clang-tidy 未报但语义误导） | 删除该局部变量，仅保留 `outcome.casualty` | 新发现 |
| R3-4 | 💭 | c_api.cpp:61-74 | `wfs_sim_create` 在 `new` 之后调用 `initialize_runtime_state`（71 行），若场景 combat/movement/terrain 配置非法抛 `std::invalid_argument`，catch 直接返回 nullptr，`handle` 泄漏（仅非法配置错误路径、单次分配） | catch 前 delete handle，或先完成全部可抛初始化再分配 | 新发现 |
| R3-5 | 💭 | combat.cpp:671-676, 986-987 | FR-062"伤害远超摧毁所需→超额传递伤亡"用"单次命中伤害/总 HP"近似，未按"超出摧毁剩余部分"结算；定性方向正确（一击致命比磨血死伤亡高），属基线简化 | 在 data-model/任务登记中显式记录该近似口径，后续 T031 精化时对齐 FR-062 原文 | 新发现（登记） |
| R3-6 | 💭 | combat.cpp:984-996 | 非击穿（跳弹）的 tick 仍会调用 `ApplyVehicleModuleDamage`/严重受损弃车掷骰，按其累计损伤消耗 RNG 与触发状态推进；结果确定但语义上"跳弹也推进模块/弃车结算" | 将模块损伤/弃车掷骰收紧到 `damage.penetrated` 分支内 | 新发现 |
| F12 | 💭 | movement.h:37；movement.cpp:199-214 | `MovementConfig::smoke_concealment`（0.8）死参数，`smoke_concealment_at` 命中返回常量 1.0 | 删除或改返回配置值 | 沿用上轮登记，仍开放 |
| F13 | 💭 | command.schema.json:34-56；command_validation.cpp:311-320 | meta 命令仍强制 completion/intent/behavior/deadline；MODIFY 的 `replace_with` 递归校验无深度上限 | schema if-then 放宽；递归深度上限 | 沿用上轮登记，仍开放 |
| F14 | 💭 | combat.cpp:784；sim_runtime.cpp | 每 tick 全量拷贝 `units` + 按 id 线性查指针（O(N²)） | T093 性能预算前暂缓 | 沿用上轮登记，仍开放 |
| F16 | 💭 | combat.cpp:355-365, 912 | 四方向防护已实现，但 `hit_direction` 只产出 front/side，上/底方向在直接火力下不可达（间接火力散布圈未接入） | 间接火力（T031 后置部分）接入时补 top/bottom 路径 | 沿用上轮登记，仍开放 |
| N2 | 💭 | command_chain.cpp:296-317, 477-496 | 批量 meta 命令绕过 F4 归属校验，最终以 TARGET_COMMAND_NOT_FOUND 拒绝，事件名误导 | 明确拒绝批量 meta 或实现批量撤回 | 沿用上轮登记，仍开放 |
| N3 | 💭 | combat.cpp:494-512 | 弹药适配因子仅正向加成，全劣配时退化纯威胁/距离排序 | 可选降权/提示 | 沿用上轮登记，仍开放 |

## 特别验证点结论

1. 重构适配（✅）：CMake 工程根为 `native/CMakeLists.txt`（`project()` 在此声明），
   故 `PROJECT_SOURCE_DIR=native/`；`43e5fe5` 的 `../data/...` 解析到仓库根 `data/`、
   `WFS_SOURCE_ROOT="${PROJECT_SOURCE_DIR}/.."` 解析到仓库根、黄金哈希路径 `native/tests/...`
   均正确。CMake 测试注册完整（sim_tests 含 runtime_combat_test/command_chain_test/combat_golden，
   cli_tests 含 command_chain_tests add_test）。rebase 内容与原 PR 逐字节一致（见上），
   无测试数量/断言丢失。
2. 确定性（✅）：新模块无 `steady_clock/system_clock/time()/rand()/random_device`、
   无可变 static 状态；随机性只来自统一 `SimState::rng`（PCG32），遍历固定顺序
   （士兵名单序、候选输入序、排序带距离/id 二次键、`std::map` 键序）。T024 黄金样例、
   T023 双运行哈希/事件序列一致、golden-run.hash 门禁共同锁定。golden-run.hash
   本轮由 `deff7f44…` 变为 `9317fde2…`（战斗系统接入运行期后的语义变化，非确定性回归；
   与原 PR 提交一致）。
3. 关键逻辑抽查（✅，除 R3-1/R3-5/R3-6 的口径问题）：动能穿深 `pen × max(0, 1 - decay×range/ref)`
   随距离衰减、化学能固定（FR-056）；过穿 `ratio>1` 后指数衰减到 floor 0.4、`ratio>2` 标记
   overmatch（FR-057）；四方向防护按方向取 kinetic/chemical（FR-055，上/底暂不可达见 F16）；
   区域命中 N×覆盖率随机舍入 → 逐士兵个人穿深判定 → 未命中不受伤（FR-060/061）；
   弃车双班组、名单清空、悬垂引用已修（FR-062，R3-5 为口径近似）。
4. 宪法合规（✅）：全部新文件有文件级总览注释与"为什么"注释；公开接口逐一注释；
   错误路径显式抛错/事件日志（宪法 17，除 R3-4 泄漏）；语言边界仅新增 sim_core DLL 配置
   无越界共享状态（宪法 14）；无现实时钟参与结算（宪法 16）；数值基线全部配置承载（宪法 12）。
   FR-020 三级超限、FR-034 识别分档在本 PR 任务范围外（未触碰、无回归）；FR-060/074
   间接火力散布仍后置（F16 登记）。

## 未验证猜测

1. CI 双 run（31590597727 push / 31590601698 pull_request，head 43e5fe5）success 为
   用户提供的远端证据，本次审查未在本地重建/重跑（网络受限、构建链依赖 vcpkg）；本地
   仅做了静态核对，路径适配的正确性由 CMake 语义推导 + 与 c666b6c 的逐字节对比佐证。
2. T023/T024 的 RED 证据仍只能从提交顺序（47a2518 RED 先于 b57d727 修复）间接确认，
   未逐提交构建实证。
3. R3-1 压制不一致、R3-2 延迟上界、R3-5 FR-062 近似均为静态推导；因本轮只读且未运行，
   未用临时探针程序做运行时数值复现。
4. 营级规模性能无基准（F14 登记）。
5. 云端 AI 注入（T080 后置）未端到端验证；本 PR 只覆盖脚本/无 AI 路径。

## 整体结论

PASS（置信度 0.88）。

本轮相对上一轮 PASS 无新引入的 🔴；唯一 🟡（R3-1，压制施加值与日志不一致）为
确定性语义口径问题，影响 FR-063 数值一致性但不影响可复现性与内存安全，建议在后续
任务（或同 PR 的顺手修复）中处理。其余为 💭/登记项。rebase 到 native/ 布局的路径适配
正确，测试注册完整，CI 绿（用户提供证据），PR mergeable=CLEAN。

## 收敛与轮次检测

- 收敛检测：正常。前两轮（R1 FAIL 0.82 → R2 PASS 0.86）已修复全部 🔴/🟡；本轮无
  🔴 回归，仅新增 1 条 🟡 + 5 条 💭，问题数单调递减，符合收敛趋势。
- 轮次提醒：本文件为 PR 级第 2 轮（rebased 复审）。若 R3-1 在本 PR 内修复，需再跑
  一轮轻量复审确认压制相关黄金样例/事件语义未变；否则可进入人工 Approve。
