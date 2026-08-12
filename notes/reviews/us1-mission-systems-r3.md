# US1 Mission Systems 第 3 轮只读复审记录

- 分支：`feature/us1-mission-systems`
- tip：`4c067b84cc874d0d431fd3514ec3932a0db2951f`（本地，未推送）
- 仓库：`VillagerUncle02/WarFictionSim`
- 审查方式：全程只读（`git log` / `git show` / `git diff`，未读取工作区文件，未 checkout/push/merge/approve）
- 复核范围：链式 rebase 结构 + 第 2 轮遗留 N1/N2 的修复闭环，以及由此引入的测试期望一致性
- 结论：**PASS（置信度：高，约 95%）**。无新 🔴 blocker，无新 🟡 suggestion；仅 3 条 💭 nit（均为既有信任边界或文档措辞，不阻塞）

## 1. 历史与变基结构 —— 通过

- `git log --oneline feature/us1-command-combat..feature/us1-mission-systems` 共 8 个提交（`ec3f905`..`4c067b8`），tip 为 `4c067b8`，与任务描述一致。
- 图形化日志确认自 `43e5fe5` 起至 tip 完全线性（单链无分叉）；`git merge-base` 两分支汇合点 = `ac6dbb8`（command-combat 的 tip），rebase 精确落在 onto 分支顶端，无杂散提交。
- 祖先性核对（`git merge-base --is-ancestor`，退出码均为 0）：`e45a9d5`（FR-063 压制缩放）、`028d833`（CMake 守卫）、`43e5fe5` 均为 tip 祖先。
- `6d20915` 非 tip 祖先（退出码 1），且已无任何分支引用。补丁等价性：`git patch-id --stable` 显示 `43e5fe5` 与 `6d20915` 均为 `b3b25aafd33149794204eb279194fe3603f5282e`，即同一补丁；被丢弃副本与保留副本内容完全一致，**无信息损失**。
- `4c067b8` 为纯文档提交（仅新增 `notes/reviews/us1-mission-systems-r2.md`，+76 行），代码与 `9c5b79d` 完全一致。

## 2. N1：sim_core POST_BUILD 守卫 —— 通过

- tip 的 `native/sim/CMakeLists.txt` 中 `sim_core` 段（第 106–125 行）已带 `_sim_core_need_dll_copy` 守卫：`IMPORTED_LOCATION_RELEASE` 读取失败时回退 `IMPORTED_LOCATION`，`TOLOWER` 后按 `\.dll$` 判断动态库，仅当为 TRUE 时才注册 POST_BUILD 拷贝命令，随后 `unset` 清理三个临时变量。
- 该段与 `028d833` 的补丁逐字一致，并与 `feature/foundation-core-abi` 分支同名段 `git diff` 零差异（两分支 diff 只体现在更早历史的其他源码清单，守卫段本身无差异）。

## 3. N2：恢复时长 64 位闭区间采样 —— 通过

- `next_bounded64`（`native/sim/src/rng.cpp:67`）与既有 `next_bounded` 结构同构：`threshold = (0U - bound) % bound` 是无偏 threshold 拒绝采样（`0U` 经常规算术转换提升为 `uint64_t`，回绕后取模即 `-bound mod bound`）；候选值由两次连续 `next()` 拼成 64 位（高 32 位在前），`value % bound` 无模偏差。`bound == 0` 返回 0 且不推进 RNG，契约与 `next_bounded` 一致并在头文件注释中说明。
- 闭区间语义正确：`contact.cpp:126-127` 取 `span = max - min`，抽样 `min + next_bounded64(span + 1)`，覆盖 `[min, max]` 全部 `span+1` 个取值；`min == max` 时 `next_bounded64(1) = 0` 恒返回 `min`。
- uint32 截断彻底消失：唯一调用点全程 `uint64_t`，旧 `kMaxBoundedSpan` 的 `++span` 特判与 `static_cast<uint32_t>` 截断均已删除。
- 上界合理：`kMaxContactTicks = 2^53-1`（`contact.h:43`）同时满足 IEEE-754 double 可无损表达的最大整数、以及 `span+1 ≤ 2^53` 不溢出 `uint64_t`；与 64 位采样运算一致（bound 为 2 的幂时 threshold=0，模运算仍严格均匀）。
- 校验真实挂接：`ContactConfig::FromScenario` 解析后统一调用 `is_valid()`（含 `max_ticks <= kMaxContactTicks`）并抛 `std::invalid_argument`（`contact.cpp:97-101`）；新式 `contact` 段读的正是 `min_ticks` / `max_ticks` 键，因此新增测试中 `{"contact":{"max_ticks":2^53}}` 的 `EXPECT_THROW` 不是空转。
- 新增 4 个测试齐全：span=2^32 边界（5 个种子）、大 span 超 uint32（2 个种子）、min==max（32 个种子）、is_valid 上界拒绝 + FromScenario 抛异常。
- 数值可信性：用 PCG32 参考实现（含 `pcg32_srandom_r` 初始化语义）独立复算，全部测试期望值逐位命中——5 个边界值、2 个大 span 值、32 个 min==max 值均精确匹配。

## 4. golden 变更与 golden-run.hash —— 通过

- `git show 9c5b79d -- native/tests/sim_tests/golden/`：仅 `combat_golden.cpp` 的 `contact_loss` 样例由 `1388` 改为 `1830`，`probability` 保持 `0.24000000000000002` 不变（概率判定路径未被 N2 触碰），其余样例（hit/area/suppression/target/ammo）零变更。
- 三代 golden 值全部用参考实现反推命中：`041eea0` 的 `2267`（旧半开区间 `next_bounded(2400)`）、`41f4e4a` 的 `1388`（M7 闭区间 `next_bounded(2401)`）、`9c5b79d` 的 `1830`（N2 `next_bounded64(2401)`），三者概率 roll 恒为 99，仅时长随算法切换而变——**确系按实际代码确定性重生成，非手改数字骗过**。
- `golden-run.hash` 在 `9c5b79d` 中未改动，且这是正确的：权威哈希门禁跑 `scn-smoke-test.json`（seed 42、600 tick、script AI），玩家班组（约 1.0,1.0）与敌方班组（4.0,4.0）相距约 4.24 km，远超步枪 500 m 有效射程，30 秒内不可能接战；`step_combat` 找不到射程内目标即提前返回，`resolve_contact_loss` 与 `next_bounded64` 在整个运行中从不执行，RNG 流与状态哈希不受 N2 影响。`41f4e4a` 那次 hash 变化可归因于同提交的其它 M 修复，而非 M7 采样改动。

## 5. RNG 流影响与测试期望一致性 —— 通过

- `next_bounded64` 全仓仅 `contact.cpp:127` 一处调用；`next()` / `next_bounded()` 实现未动，其余所有 RNG 消费路径的流完全不变。游戏未发布，失联时长数值流的变更属已知且可接受的有意语义修正。
- 全仓测试中除 `combat_golden.cpp` 的 contact_loss 样例与 `contact_test.cpp` 新增断言外，无任何其它硬编码失联时长期望；`combat_golden` 各样例各自独立构造 Rng，互不串流。
- 场景 schema 根节点 `additionalProperties: true`，contact 配置本就不在 schema 约束内、由 `FromScenario` 装载期校验——新上界落在正确执行点，无 schema/sim 两套约束冲突。

## Findings 汇总

无 🔴 blocker，无 🟡 suggestion。

💭 nit 1（既有信任边界，非本次引入）：`resolve_contact_loss`（`contact.cpp:109`）对直接构造的 `ContactConfig` 不做防御性校验，若调用方绕过 `FromScenario` 传入 `max < min`，`span` 下溢导致 `span+1` 回绕。生产入口 `initialize_runtime_state` 与 `CombatConfig` 适配重载均来自已校验配置，实际不可达；可选加固：函数入口加 `is_valid()` 检查或断言。

💭 nit 2（文档措辞）：`contact_test.cpp` 的 `ContactRecoveryMinEqualsMaxIsStable` 注释“不得因 64 位采样引入除零/推进 RNG 的差异”表述不精确——`next_bounded64(1)` 实际会消耗 2 次 `next()`（推进 RNG），断言锁定的只是返回值恒为 `min`。建议改为“返回值恒为唯一值且无除零”，避免误导读者。

💭 nit 3（覆盖增强，可选）：权威状态哈希门禁（smoke 场景）不经过失联采样路径，N2 的回归保护目前依赖单测 + `combat_golden` 单点黄金样例，已足够；后续可考虑新增一个能实际接战的黄金场景，让全量哈希门禁覆盖该路径。

## 备注

- 本分支未推送、无远端 CI；本地全量门禁（fix 代理报告 326/326）与 CI 将在开 PR 后执行。本轮为静态审查，未运行构建/测试（工作区由另一修复代理占用，且遵守只读约定）。
- 所有 git 操作只读；本记录为唯一落盘产出。
