# Review us1-mission-systems - 第 1 轮

- 审查范围：T032–T036（提交 `3ac3cf9` RED + `310acb2` 实现，`git diff b248b36..HEAD`，28 文件 +3231/−80）
- 审查方式：Code Reviewer 子代理（只读）+ 主循环核实
- 时间：2026-08-12

## 对比上一轮

- 上轮（us1-command-combat）开放项 F12/F13/F14/F16 未被误实现；F14 性能问题本轮 intel 新增同类 O(N²)（M6 关联）。

## Findings

| # | 级别 | file:line | 问题 | 修复方向 | 状态 |
|---|---|---|---|---|---|
| M1 | 🔴 | command_validation.cpp:41–42 | BuiltInConditions 未注册 patrol/fortify/recon，三组命令被 CONDITION_NOT_EVALUABLE 拒绝，FR-042 任务类型不可用 | 注册条件+参数，同步 schema/契约，补端到端测试 | 已确认 → 回传实现代理 |
| M2 | 🟡 | command_chain.cpp:536–543；mission_exec.cpp:319–327 | 持续任务循环重启后命令变 kCompleted，撤回无法终止（FR-044） | 循环保持命令有效或撤回匹配活动任务 | 已确认 → 回传实现代理 |
| M3 | 🟡 | mission_exec/recon_tasks/outcome/intel | 敌我判定仅 node_id 不等，营级同阵营节点误判敌方 | 增加 side/faction 字段统一判定 | 已确认 → 回传实现代理 |
| M4 | 🟡 | outcome.cpp:134–137, 273–278 | deployment_enabled 漏配 deadline（默认 0）→ tick 0 即失败 | 校验 deadline>0 且 zone 非空 | 已确认 → 回传实现代理 |
| M5 | 🟡 | intel.cpp:69–74, 306–313 | 观察能力未联动观瞄模块/压制（effective_observation_effect） | 评分按模块/压制衰减 | 已确认 → 回传实现代理 |
| M6 | 🟡 | intel.cpp:308–313；movement.cpp:107–129 | step_intel O(N²) + terrain_sample_at 每 tick 重建 map，营级超预算风险 | 缓存索引 + 距离粗筛 | 已确认 → 回传实现代理 |
| M7 | 💭 | contact.cpp:120–126 | 恢复区间 [min, max-1] 与契约差 1 tick | max-min+1 或改注释 | 回传实现代理（低成本） |
| M8 | 💭 | combat.cpp:955–966 | 最后已知快照在模块结算前捕获 | 移到模块结算后（注意引用安全） | 登记 |
| M9 | 💭 | contact.cpp:33, 206–227 | 压制降级阈值 0.5 硬编码 | 并入 ContactConfig | 回传实现代理（低成本） |
| M10 | 💭 | intel_test.cpp:124–125 | RED→GREEN 来源单位断言弱化 | 保留强断言或注释放宽理由 | 登记 |
| M11 | 💭 | outcome.cpp:259–262 | 读档 objective_states 与场景目标数量不一致只刷新 id | 加载时对齐或拒绝 | 登记 |

## 未验证猜测

1. RED 提交具体编译失败形态未逐提交构建实证（结构性证实：测试引用头文件在该提交不存在）；
2. GitHub Actions CI 与远端 clang-tidy 未运行（本地 305/305、format 通过）；
3. 营级 intel/outcome 实际 tick 耗时未基准（M6）；
4. 云 AI 注入（T080 后置）未端到端验证；
5. 新增字段的旧存档默认值语义仅静态确认。

## 整体结论

FAIL（置信度 0.84）：核心确定性设计扎实、305/305 通过；M1 使巡逻/构筑/侦察类任务在真实命令路径不可用，M2 破坏持续任务终止语义，合入前至少修复 M1/M2，M3–M6 建议同轮处理。

## 收敛检测

正常（首轮）。

## 轮次提醒

正常（第 1 轮）。
