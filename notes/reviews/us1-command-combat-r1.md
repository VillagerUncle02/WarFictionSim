# Review us1-command-combat - 第 1 轮

- 审查范围：T023/T024/T029–T031（提交 `15b1402` + `fc62e4b`，`git diff fa08bc8..HEAD`，27 文件 +3435/−23）
- 审查方式：Code Reviewer 子代理（只读）+ 主循环逐条核实
- 时间：2026-08-12

## 对比上一轮

- 上一逻辑组（us1-models）遗留建议已在 T025–T028 落地；并行 ctest 临时路径修复在本轮 `-j4` 下依然有效。

## Findings

| # | 级别 | file:line | 问题 | 修复方向 | 状态 |
|---|---|---|---|---|---|
| F1 | 🔴 | combat.cpp:825–827, 905 | 脱靶不消耗开火冷却：`COMBAT_MISS` 分支 `break` 先于 `last_fire_tick` 落账，脱靶单位每 tick 连射 | 开火即落冷却（提前到命中判定前），补命中率 0 测试 | 已确认 → 回传实现代理 |
| F2 | 🔴 | combat.cpp:643–646 | 弃车路径悬垂引用（UB）：先 `push_back(dismounted)` 使 `vehicle` 引用失效，再 `LogCombat` 读 `vehicle.id` | push_back 前复制 id/人数到局部变量；补弃车测试 | 已确认 → 回传实现代理 |
| F3 | 🔴 | combat.cpp:615–646 | 非摧毁弃车后 `vehicle.soldiers` 保留 moved-from 残留（人员重复）；FR-062 要求乘员/载员生成两个徒步班组，实现合并为一个 | 弃车成功清空 `vehicle.soldiers`；区分乘员/载员生成两个班组；补测试 | 已确认 → 回传实现代理 |
| F4 | 🔴 | command_chain.cpp:315–331, 494–530 | 撤回/修改元命令不校验目标命令归属单位，可跨单位取消/改写任务 | Issue 阶段校验 `target_command_id.unit_id == 本命令 unit_id`（MODIFY 同时校验 replace_with ref）；补拒绝测试 | 已确认 → 回传实现代理 |
| F5 | 🟡 | sim_runtime.cpp:89–162 | 运行期不支持车辆单位（只查 squads、`is_vehicle` 恒 false），FR-062/064 路径不可达且零运行期战斗测试 | 接入 vehicles 加载；补步兵/载具运行期战斗测试（先 RED） | 已确认 → 回传实现代理 |
| F6 | 🟡 | combat.cpp:496–530 | 自动目标选择未纳入弹药适配（`ammo_fit_weight` 未参与评分） | 评分纳入选弹适配度；补多目标+弹药适配黄金样例 | 已确认 → 回传实现代理 |
| F7 | 🟡 | run_command_chain_tests.ps1:132–141；save_test.cpp:86–101 | 批量 ACK 用子命令 id，runner 精确匹配静默跳过；save 测试未覆盖战斗中期状态 | 按子命令匹配/断言父命令延迟；补中期状态存档往返用例 | 已确认 → 回传实现代理 |
| F8 | 🟡 | sim_runtime.cpp:124 | 两栖能力硬编码全员 true，与 FR-022 不符，深水通行限制形同虚设 | 从数据读 amphibious/重装备标志；补拒绝用例 | 已确认 → 回传实现代理 |
| F9 | 🟡 | combat.cpp:221–280 | `CombatConfig::FromScenario` 无范围校验，max<min 可致无符号回绕 | 与 MovementConfig 同级校验；补非法配置测试 | 已确认 → 回传实现代理 |
| F10 | 🟡 | 提交历史 | T024 RED 证据不可审计（黄金测试随实现同提交） | 本次新增测试按 RED 提交；审计登记 T024 RED 来源（实现代理编译期 C1083 记录） | 登记处理 |
| F11 | 💭 | command_chain.cpp:398, 592 | 时限判定 `>` 差 1 tick | 改 `>=` 并补边界测试 | 回传实现代理（低风险顺手修） |
| F12 | 💭 | combat.h:153；combat.cpp:334 | `smoke_concealment` 死参数，烟幕不影响区域覆盖/伤亡 | 删除或定义口径 | 登记 |
| F13 | 💭 | command.schema.json:33–54 | meta 命令仍强制 completion/intent/behavior/deadline；MODIFY 递归校验无深度上限 | schema if-then 放宽；递归深度上限 | 登记 |
| F14 | 💭 | combat.cpp:693–810；sim_runtime.cpp:96–98 | 每 tick 整体拷贝 units + O(N²) 指针查找，营级规模可优化 | T093 性能预算前暂缓 | 登记 |
| F15 | 💭 | ai_inject.cpp:190–195；decision_log.h:29 | 注释“拒绝决策 arrival_seq=0”与实现记录非零未消费序列号不一致 | 统一语义 | 回传实现代理（低风险顺手修） |
| F16 | 💭 | combat.cpp:553–570, 809 | 载具选弹只用侧面动能评估；上/底防护不可达（间接火力未接入） | 在 data-model/任务登记显式记录 | 登记 |

## 未验证猜测

1. T024 先写测试的 RED 证据无法从提交历史验证（实现代理报告中留存编译期 C1083 记录）；
2. golden-run.hash 更新前的人工语义确认无法核实（已确认与当前代码运行结果一致）；
3. GitHub Actions CI 与远端 clang-tidy 尚未运行（本地 ctest/format 已验证）；
4. 营级 300–800 实体量级性能未做基准（登记 F14）；
5. 云端 AI 注入路径（T080 后置）未端到端验证。

## 运行时自适应

- 审查代理使用 `%TEMP%` 临时探针实证 F1–F3（已清理）；主循环以代码核实确认。

## 整体结论

FAIL（置信度 0.82）：F1–F4 为确定性状态机/内存正确性缺陷，合入前必须修复；F5–F9 建议合入前处理。

## 收敛检测

正常（首轮，无历史对比）。

## 轮次提醒

正常（第 1 轮）。
