# Review us1-command-combat - 第 2 轮

- 审查范围：T023/T024/T029–T031 全部提交（`git diff fa08bc8..HEAD`：15b1402、fc62e4b、836eefc、3227c2b、7729027）
- 对比上一轮：已修复 16 / 回归 0 / 新增 2（N1/N2/N3 计 3）
- 审查方式：Code Reviewer 子代理（只读）+ 主循环核实
- 时间：2026-08-12

## 逐项核对

| # | 结论 | 证据 |
|---|---|---|
| F1 脱靶冷却 | ✅ | `last_fire_tick` 提前到 resolve_hit 前；`HitConsumesFireCooldown` 通过 |
| F2 弃车悬垂引用 | ✅ | push_back 前拷贝 id/人数；日志输出 `unit=vehicle-friend crew=2 passengers=5` |
| F3 弃车空壳/双班组 | ✅ | 两种路径 soldiers.clear()；按 crew_count 生成两个徒步班组（FR-062） |
| F4 撤回/修改归属 | ✅ | Issue 阶段 unit_id 一致校验；跨单位拒绝测试通过 |
| F5 车辆加载 | ✅ | DataLibrary 七类目录含 vehicles；MakeRuntimeUnit 车辆初始化；运行期步兵/载具战斗测试 |
| F6 选弹适配 | ✅ | select_target 纳入 select_ammo 有效性；两个黄金样例 |
| F7 测试盲区 | ✅ | 批量 ACK 子命令匹配+显式 Fail；SaveLoadMidBattleRestoresCombatState |
| F8 两栖数据驱动 | ✅ | 重装备派生/数据字段；深水 UNIT_STUCK 测试 |
| F9 配置范围校验 | ✅ | isfinite/非负/max≥min；三类非法配置 EXPECT_THROW |
| F10 T024 RED | ✅ 登记 | 836eefc 独立 RED 提交可审计；T024 原始 RED 按 R1 登记（编译期 C1083） |
| F11 deadline | ✅ | `>` 改 `>=`；边界测试通过 |
| F12/F13/F14/F16 | ✅ 登记 | 未误实现，R1 登记开放项 |
| F15 arrival_seq | ✅ | CHAIN_REJECTED/INVALID_JSON 置 0，与 decision_log.h 注释一致 |
| N1 存档兼容 | ✅ 修复 | `json.value("crew_count", soldiers.size())`；LegacyV1SaveWithoutCrewCountLoads 通过（278/278） |

## Findings（第 2 轮新增）

| # | 级别 | file:line | 问题 | 修复方向 | 状态 |
|---|---|---|---|---|---|
| N1 | 🟡 | sim_state.h:180 | crew_count 必填破坏 v1 存档兼容（宪法第 13 条） | value() 默认 soldiers.size() | 已修复 7729027 |
| N2 | 💭 | command_chain.cpp:286–317 | 批量 meta 命令绕过 F4 校验但最终被 TARGET_COMMAND_NOT_FOUND 拒绝，事件名误导 | 明确拒绝或实现批量撤回 | 登记 |
| N3 | 💭 | combat.cpp:494–512 | 弹药适配因子仅正向加成，全劣配时退化纯威胁/距离排序 | 可选降权/提示 | 登记 |

## 未验证猜测

1. GitHub Actions CI 与远端 clang-tidy 未运行（本地 278/278、format 通过）；
2. 836eefc RED 具体失败形态未逐提交构建实证（提交顺序满足测试先于修复）；
3. 旧版 v1 存档加载失败为静态确认后已修复并补测试；
4. 云端 AI 注入（T080 后置）未端到端验证；
5. 营级性能无基准（F14）。

## 整体结论

PASS（置信度 0.86）；N1 已在复审后修复（278/278 全绿）。

## 收敛检测

正常。

## 轮次提醒

正常（第 2 轮）。
