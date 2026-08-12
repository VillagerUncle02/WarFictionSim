// 文件总览：命令面板 —— 三级校验规则（T041）。
//
// 规则与 native command_validation.cpp 语义对齐（顺序固定：类型 → 目标 →
// 完成条件 → 弹药覆盖），错误项会阻断提交；面板额外提供 native 没有的
// 表现层警告/建议（时限风险、取代现有任务、可选参数提示）。关键边界：
// 面板校验只是预检，最终权威是注入时核心的双重校验（本文件头声明即契约）。

namespace WarFictionSim.Ui.CommandPanel;

/// <summary>命令草稿三级校验（面板预检，核心为最终权威）。</summary>
public static class CommandValidationRules
{
    private static readonly string[] RegisteredConditions =
    [
        "secure_zone", "destroy_unit", "drive_out", "clear", "hold", "reach_point",
        "patrol", "fortify", "recon",
    ];

    /// <summary>按固定顺序校验草稿并产出错误/警告/建议。</summary>
    /// <param name="draft">命令草稿。</param>
    /// <param name="context">校验上下文（快照 + 场景元数据合成）。</param>
    /// <returns>校验结果。</returns>
    public static CommandValidationResult Validate(CommandDraft draft, CommandContext context)
    {
        var issues = new List<CommandValidationIssue>();

        // ---- 语义检查 1：类型与完成条件必须选择（对应 native UNREGISTERED_TYPE）。 ----
        if (string.IsNullOrWhiteSpace(draft.Type))
        {
            issues.Add(Error("TYPE_REQUIRED", "请选择命令类型。"));
        }

        if (string.IsNullOrWhiteSpace(draft.Condition))
        {
            issues.Add(Error("CONDITION_REQUIRED", "请选择完成条件。"));
        }

        // ---- 语义检查 2：执行单位必须存在且属于指挥范围。 ----
        if (draft.ExecutorIds.Count == 0)
        {
            issues.Add(Error("TARGET_REQUIRED", "请点选/框选至少一个执行单位。"));
        }
        else
        {
            var unitsById = context.Units.ToDictionary(unit => unit.Id);
            foreach (string executorId in draft.ExecutorIds)
            {
                if (!unitsById.TryGetValue(executorId, out CommandableUnit? unit))
                {
                    issues.Add(Error("TARGET_NOT_FOUND", $"目标单位不存在：{executorId}。"));
                    continue;
                }

                if (!string.IsNullOrEmpty(context.CommanderNodeId) && unit.NodeId != context.CommanderNodeId)
                {
                    issues.Add(Error(
                        "UNAUTHORIZED_TARGET",
                        $"目标单位不属于指挥范围：{executorId}（所属节点 {unit.NodeId}，指挥节点 {context.CommanderNodeId}）。"));
                }
            }
        }

        // ---- 语义检查 3：完成条件必须可求值（对应 native CONDITION_NOT_EVALUABLE）。 ----
        if (!string.IsNullOrWhiteSpace(draft.Condition))
        {
            ValidateCondition(draft, context, issues);
        }

        // ---- 语义检查 4：弹药覆盖必须存在于单位装备（对应 native AMMO_NOT_FOUND）。 ----
        if (!string.IsNullOrWhiteSpace(draft.AmmoOverride) && draft.ExecutorIds.Count > 0)
        {
            if (draft.ExecutorIds.Count > 1)
            {
                // 与 native 一致：批量命令的弹药覆盖延迟到命令链到达时按单位判定。
                issues.Add(Warning(
                    "BATCH_AMMO_PER_UNIT",
                    "批量命令的弹药覆盖将按单位分别校验，不匹配的单位可能拒绝该覆盖。"));
            }
            else
            {
                CommandableUnit? executor = context.Units.FirstOrDefault(unit => unit.Id == draft.ExecutorIds[0]);
                if (executor is not null && !executor.AmmoIds.Contains(draft.AmmoOverride))
                {
                    issues.Add(Error(
                        "AMMO_NOT_FOUND",
                        $"目标单位装备中不存在弹药 {draft.AmmoOverride}（单位 {executor.Id}）。"));
                }
            }
        }

        // ---- 表现层警告：时限风险（native Schema 只要求 ≥0，不判语义）。 ----
        if (draft.DeadlineTick == 0)
        {
            issues.Add(Warning("DEADLINE_PAST_OR_TIGHT", "未设置时限，命令可能被立即判定超时，建议填写游戏时间。"));
        }
        else if (draft.DeadlineTick <= context.CurrentTick)
        {
            issues.Add(Warning("DEADLINE_PAST_OR_TIGHT", $"时限 {draft.DeadlineTick} 不晚于当前游戏时间 {context.CurrentTick}，命令生效后可能立即超时。"));
        }

        // ---- 表现层警告：新命令将取代单位现有任务（FR-041 可见提示）。 ----
        foreach (string executorId in draft.ExecutorIds)
        {
            if (context.Units.Any(unit => unit.Id == executorId && unit.MissionActive))
            {
                issues.Add(Warning("SUPERSEDES_EXISTING", $"单位 {executorId} 已有执行中任务，新命令将取代旧命令。"));
            }
        }

        // ---- 建议：可选行为参数与意图的上下文提示（不阻断）。 ----
        if (string.IsNullOrWhiteSpace(draft.Intent))
        {
            issues.Add(Suggestion("INTENT_MISSING", "建议补充意图/附加说明，便于 AI 理解与战后复盘。"));
        }

        if (draft.Engagement == "balanced")
        {
            issues.Add(Suggestion("ENGAGEMENT_DEFAULT", "未指定接敌策略，将采用默认“balanced（伺机）”。"));
        }

        if (draft.FailureAction == "report")
        {
            issues.Add(Suggestion("FAILURE_ACTION_DEFAULT", "未指定失败后处置，将采用默认“report（上报）”。"));
        }

        return new CommandValidationResult(issues);
    }

    private static void ValidateCondition(CommandDraft draft, CommandContext context, List<CommandValidationIssue> issues)
    {
        if (!RegisteredConditions.Contains(draft.Condition))
        {
            issues.Add(Error("CONDITION_NOT_EVALUABLE", $"完成条件未注册：{draft.Condition}。"));
            return;
        }

        switch (draft.Condition)
        {
            case "secure_zone":
            case "drive_out":
            case "clear":
                RequireZone(draft.Condition, draft.ZoneId, context, issues);
                break;
            case "destroy_unit":
                if (string.IsNullOrWhiteSpace(draft.TargetUnitId))
                {
                    issues.Add(Error("CONDITION_NOT_EVALUABLE", "完成条件 destroy_unit 缺少目标单位（target_unit）。"));
                }
                else if (context.Units.All(unit => unit.Id != draft.TargetUnitId))
                {
                    issues.Add(Error("CONDITION_NOT_EVALUABLE", $"条件引用的单位不存在：{draft.TargetUnitId}。"));
                }

                break;
            case "hold":
                if (draft.DurationTicks is null or <= 0)
                {
                    issues.Add(Error("CONDITION_NOT_EVALUABLE", "完成条件 hold 缺少正整数驻留时长（duration_ticks）。"));
                }

                break;
            case "reach_point":
                if (draft.PointX is null || draft.PointY is null)
                {
                    issues.Add(Error("CONDITION_NOT_EVALUABLE", "完成条件 reach_point 缺少目标坐标（point）。"));
                }

                break;
            case "patrol":
                if (draft.CycleTicks is null or <= 0)
                {
                    issues.Add(Error("CONDITION_NOT_EVALUABLE", "完成条件 patrol 缺少正整数巡逻周期（cycle_ticks）。"));
                }

                break;
            case "fortify":
                if (draft.ConstructionTicks is null or <= 0)
                {
                    issues.Add(Error("CONDITION_NOT_EVALUABLE", "完成条件 fortify 缺少正整数构筑时长（construction_ticks）。"));
                }

                break;
            case "recon":
                if (draft.PointX is null || draft.PointY is null)
                {
                    issues.Add(Error("CONDITION_NOT_EVALUABLE", "完成条件 recon 缺少侦察目标点（point）。"));
                }

                if (draft.ExitPointX.HasValue != draft.ExitPointY.HasValue)
                {
                    issues.Add(Error("CONDITION_NOT_EVALUABLE", "撤离点（exit_point）必须同时提供 x 与 y。"));
                }

                break;
        }
    }

    private static void RequireZone(
        string condition, string? zoneId, CommandContext context, List<CommandValidationIssue> issues)
    {
        if (string.IsNullOrWhiteSpace(zoneId))
        {
            issues.Add(Error("CONDITION_NOT_EVALUABLE", $"完成条件 {condition} 缺少区域目标（zone）。"));
            return;
        }

        if (!context.ZoneIds.Contains(zoneId))
        {
            issues.Add(Error("CONDITION_NOT_EVALUABLE", $"条件引用的区域不存在：{zoneId}。"));
        }
    }

    private static CommandValidationIssue Error(string code, string message) =>
        new(CommandIssueSeverity.Error, code, message);

    private static CommandValidationIssue Warning(string code, string message) =>
        new(CommandIssueSeverity.Warning, code, message);

    private static CommandValidationIssue Suggestion(string code, string message) =>
        new(CommandIssueSeverity.Suggestion, code, message);
}
