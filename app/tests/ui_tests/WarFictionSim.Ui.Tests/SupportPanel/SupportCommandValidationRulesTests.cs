// 测试：支援请求 UI —— 支援负载三级校验判定表（T053）。
//
// 判定表覆盖 FR-008/FR-050 的面板侧预检：未选目标/需求类型/支援种类、
// 非法数量、错误完成条件、池外种类 = 错误（阻断提交）；分数余量不足与
// 默认优先级 = 警告（可提交但提示风险，核心仍是最终权威）；正常负载
// = 可提交。规则与 command-schema.md §1.1/§5 及 command.schema.json 对齐。

using WarFictionSim.Ui.CommandPanel;
using WarFictionSim.Ui.SupportPanel;
using Xunit;

namespace WarFictionSim.Ui.Tests.SupportPanel;

public class SupportCommandValidationRulesTests
{
    private static readonly SupportKindOption[] Pool =
    [
        new("squad-mortar-team", 30, "unit"),
        new("squad-hmg-team", 15, "unit"),
        new("artillery-152", 50, "fire_support"),
    ];

    private static CommandContext Context(ulong scoreRemaining = 60) =>
        new()
        {
            CommanderNodeId = "node-platoon-1",
            FriendlySide = "side-a",
            CurrentTick = 1000,
            Units = [new CommandableUnit("sp-squad-1", "node-platoon-1", "side-a", [], false)],
            ZoneIds = [],
            SupportPool = Pool,
            SupportScoreRemaining = scoreRemaining,
        };

    private static CommandDraft ValidDraft()
    {
        var draft = new CommandDraft
        {
            Type = "SUPPORT_REQUEST",
            Condition = "support",
            SupportRequestType = "reinforce",
            SupportQuantity = 1,
            SupportToNode = "node-battalion-1",
            Priority = 1,
            DeadlineTick = 3600,
        };
        draft.ExecutorIds.Add("sp-squad-1");
        draft.SupportKinds.Add("squad-mortar-team");
        return draft;
    }

    [Fact]
    public void Validate_SupportWithoutTarget_IsBlockingError()
    {
        CommandDraft draft = ValidDraft();
        draft.ExecutorIds.Clear();

        CommandValidationResult result = CommandValidationRules.Validate(draft, Context());

        Assert.Contains(
            result.Issues,
            issue => issue.Code == "TARGET_REQUIRED" && issue.Severity == CommandIssueSeverity.Error);
    }

    [Fact]
    public void Validate_SupportWithoutRequestType_IsBlockingError()
    {
        CommandDraft draft = ValidDraft();
        draft.SupportRequestType = null;

        CommandValidationResult result = CommandValidationRules.Validate(draft, Context());

        Assert.Contains(
            result.Issues,
            issue => issue.Code == "SUPPORT_REQUEST_TYPE_REQUIRED" && issue.Severity == CommandIssueSeverity.Error);
    }

    [Fact]
    public void Validate_SupportWithoutKinds_IsBlockingError()
    {
        CommandDraft draft = ValidDraft();
        draft.SupportKinds.Clear();

        CommandValidationResult result = CommandValidationRules.Validate(draft, Context());

        Assert.Contains(
            result.Issues,
            issue => issue.Code == "SUPPORT_KINDS_REQUIRED" && issue.Severity == CommandIssueSeverity.Error);
    }

    [Fact]
    public void Validate_SupportQuantityZero_IsBlockingError()
    {
        CommandDraft draft = ValidDraft();
        draft.SupportQuantity = 0;

        CommandValidationResult result = CommandValidationRules.Validate(draft, Context());

        Assert.Contains(
            result.Issues,
            issue => issue.Code == "SUPPORT_QUANTITY_INVALID" && issue.Severity == CommandIssueSeverity.Error);
    }

    [Fact]
    public void Validate_SupportQuantityNegative_IsBlockingError()
    {
        CommandDraft draft = ValidDraft();
        draft.SupportQuantity = -1;

        CommandValidationResult result = CommandValidationRules.Validate(draft, Context());

        Assert.Contains(
            result.Issues,
            issue => issue.Code == "SUPPORT_QUANTITY_INVALID" && issue.Severity == CommandIssueSeverity.Error);
    }

    [Fact]
    public void Validate_SupportQuantityNull_DefaultsToOneWithoutError()
    {
        CommandDraft draft = ValidDraft();
        draft.SupportQuantity = null;

        CommandValidationResult result = CommandValidationRules.Validate(draft, Context());

        Assert.DoesNotContain(result.Issues, issue => issue.Code == "SUPPORT_QUANTITY_INVALID");
    }

    [Fact]
    public void Validate_SupportWithWrongCondition_IsBlockingError()
    {
        CommandDraft draft = ValidDraft();
        draft.Condition = "reach_point";

        CommandValidationResult result = CommandValidationRules.Validate(draft, Context());

        Assert.Contains(
            result.Issues,
            issue => issue.Code == "SUPPORT_CONDITION_FIXED" && issue.Severity == CommandIssueSeverity.Error);
    }

    [Fact]
    public void Validate_SupportKindOutsidePool_IsBlockingError()
    {
        CommandDraft draft = ValidDraft();
        draft.SupportKinds.Add("squad-atgm-team");

        CommandValidationResult result = CommandValidationRules.Validate(draft, Context());

        Assert.Contains(
            result.Issues,
            issue => issue.Code == "SCOPE_VIOLATION" && issue.Severity == CommandIssueSeverity.Error);
    }

    [Fact]
    public void Validate_SupportInsufficientScore_IsWarningNotError()
    {
        CommandDraft draft = ValidDraft();
        draft.SupportKinds.Add("artillery-152"); // 30 + 50 = 80 > 60。

        CommandValidationResult result = CommandValidationRules.Validate(draft, Context(scoreRemaining: 60));

        Assert.False(result.HasErrors);
        Assert.Contains(
            result.Issues,
            issue => issue.Code == "SUPPORT_INSUFFICIENT_SCORE" && issue.Severity == CommandIssueSeverity.Warning);
    }

    [Fact]
    public void Validate_SupportDefaultPriority_IsWarning()
    {
        CommandDraft draft = ValidDraft();
        draft.Priority = 0;

        CommandValidationResult result = CommandValidationRules.Validate(draft, Context());

        Assert.False(result.HasErrors);
        Assert.Contains(
            result.Issues,
            issue => issue.Code == "SUPPORT_LOW_PRIORITY" && issue.Severity == CommandIssueSeverity.Warning);
    }

    [Fact]
    public void Validate_CompleteSupportDraft_IsSubmittable()
    {
        CommandDraft draft = ValidDraft();

        CommandValidationResult result = CommandValidationRules.Validate(draft, Context());

        Assert.False(result.HasErrors);
        Assert.DoesNotContain(result.Issues, issue => issue.Code == "SUPPORT_INSUFFICIENT_SCORE");
    }

    [Fact]
    public void Validate_SupportHugeQuantity_SaturatesToInsufficientWarningWithoutThrowing()
    {
        CommandDraft draft = ValidDraft();
        draft.SupportQuantity = long.MaxValue; // cost × quantity 超出 ulong：不得抛异常。

        CommandValidationResult result = CommandValidationRules.Validate(draft, Context());

        Assert.False(result.HasErrors);
        Assert.Contains(
            result.Issues,
            issue => issue.Code == "SUPPORT_INSUFFICIENT_SCORE" && issue.Severity == CommandIssueSeverity.Warning);
    }

    [Fact]
    public void Validate_NonSupportCommandWithSupportCondition_IsBlockingError()
    {
        CommandDraft draft = ValidDraft();
        draft.Type = "MOVE";

        CommandValidationResult result = CommandValidationRules.Validate(draft, Context());

        Assert.Contains(
            result.Issues,
            issue => issue.Code == "CONDITION_NOT_EVALUABLE" && issue.Severity == CommandIssueSeverity.Error);
    }
}
