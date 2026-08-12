# Command Schema 契约

玩家与 AI 共用同一命令结构（FR-045）。命令经 JSON Schema + 语义双重校验后进入确定性命令队列（按 `(game_tick, monotonic_seq)` 排序）。

## 1. 顶层结构

```json
{
  "type": "task_type",
  "target": {"kind": "unit|zone|point", "ref": "..."},
  "completion": {
    "condition": "secure_zone|destroy_unit|drive_out|clear|hold|reach_point|patrol|fortify|recon",
    "params": {"zone": "...", "duration_ticks": 1200, "target_unit": "...",
               "cycle_ticks": 1200, "construction_ticks": 1200,
               "point": {"x": 1.0, "y": 1.0}, "exit_point": {"x": 0.5, "y": 0.5}}
  },
  "intent": "自由文本，供展示与 AI 理解背景",
  "behavior": {
    "engagement": "aggressive|balanced|cautious",
    "ammo_override": "optional",
    "failure_action": "withdraw_to|hold|report"
  },
  "priority": 0,
  "deadline": {"game_time": "tick_count"}
}
```

完成条件参数（语义校验基线，M1）：

- `secure_zone`/`drive_out`/`clear`：`zone`（区域 id）；
- `destroy_unit`：`target_unit`（单位 id）；
- `hold`：`duration_ticks`（驻留 tick）；
- `reach_point`：`point`（坐标对象）；
- `patrol`：`cycle_ticks`（巡逻周期 tick）；
- `fortify`：`construction_ticks`（构筑时长 tick）；
- `recon`：`point`（侦察目标点，必填），`exit_point`（渗透撤离点，可选）。

## 2. 语义校验规则（双重校验管道）

- 类型必须已注册（未注册类型拒绝）；
- 目标必须存在且属于可指挥范围（越权命令拒绝）；
- 完成条件必须可求值（区域、单位、时长参数完整）；
- 弹药覆盖指定必须存在于单位装备中；
- 同优先级冲突命令按到达顺序，新命令取代旧命令并产生可见事件。

## 3. 确定性仲裁

- 支援请求竞争：按 `(priority, arrival_seq)` 排序，高优先级/先到者优先，其余拒绝或排队并明确反馈；
- 分支条件同时满足：按优先级，未设置时按命令内配置顺序，先满足者生效。

## 4. 简报结构（周期/紧急简报与 AI 决策输入）

- 周期简报：按配置的游戏时间间隔发送固定战况汇总（FR-037）；
- 紧急简报：下级完成重大任务或汇报重要情况时，上级可要求立即进行局势判断与敌方意图分析（FR-037）；
- 决策输入：简报与 AI 决策输入共用统一裁剪规则——只含本节点权限内可见信息（变化 ≤50 条 / 历史 ≤20 条 / 总 token ≤4000，T082 校准），摘要以生成时刻快照语义为准（FR-039/051）。

## 5. 四种交互类型（上下级交互契约）

上下级交互强制限定为四种（FR-050），契约字段统一带 `interaction_type`：

- `TASK_DISPATCH`：上级向下级下发任务（含完整指令结构）；
- `EXECUTION`：下级执行状态与结果上报（任务状态变化事件）；
- `SUMMARY_REPORT`：下级向上级提交摘要汇报（仅权限内信息）；
- `SUPPORT_REQUEST`：支援请求与配属处置（RequestSupport / AssignTo，仲裁见 §3）。

## 6. 契约镜像

`contracts/schemas/command.schema.json` 为运行时校验文件，与本文档保持同步（T007/T014）。
