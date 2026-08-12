// 测试：命令面板 —— 三级校验判定表（错误/警告/建议，T041）。
//
// 面板侧校验规则与 native command_validation.cpp 语义对齐（类型注册、目标
// 存在/越权、完成条件可求值、弹药覆盖存在），错误必须阻断提交；警告
// （超时风险/取代现有任务）可提交但提示风险；建议只是上下文提示。
// 最终权威仍是注入时的核心校验（面板不替代核心）。

using WarFictionSim.Ui.CommandPanel;
using Xunit;

namespace WarFictionSim.Ui.Tests.CommandPanel;

public class CommandValidationRulesTests
{
    private static CommandContext Context(ulong tick = 1000) =>
        new()
        {
            CommanderNodeId = "node-player",
            FriendlySide = "side-a",
            CurrentTick = tick,
            Units =
            [
                new CommandableUnit("squad-a", "node-player", "side-a", ["ammo-556"], MissionActive: false),
                new CommandableUnit("squad-b", "node-player", "side-a", ["ammo-762"], MissionActive: true),
                new CommandableUnit("enemy-1", "node-enemy", "side-b", ["ammo-762"], MissionActive: false),
            ],
            ZoneIds = ["zone-hill"],
        };

    private static CommandDraft ValidDraft() =>
        new()
        {
            Type = "SECURE_ZONE",
            Condition = "secure_zone",
            Intent = "占领高地并坚守",
            Engagement = "aggressive",
            FailureAction = "hold",
            Priority = 1,
            DeadlineTick = 3600,
        };

    [Fact]
    public void Validate_EmptyDraft_ReportsBlockingErrors()
    {
        var draft = new CommandDraft();

        CommandValidationResult result = CommandValidationRules.Validate(draft, Context());

        Assert.True(result.HasErrors);
        Assert.Contains(result.Issues, issue => issue.Code == "TYPE_REQUIRED" && issue.Severity == CommandIssueSeverity.Error);
        Assert.Contains(result.Issues, issue => issue.Code == "TARGET_REQUIRED" && issue.Severity == CommandIssueSeverity.Error);
    }

    [Fact]
    public void Validate_ValidZoneCommand_NoErrors()
    {
        CommandDraft draft = ValidDraft();
        draft.ExecutorIds.Add("squad-a");
        draft.ZoneId = "zone-hill";
        draft.DurationTicks = 2400;

        CommandValidationResult result = CommandValidationRules.Validate(draft, Context());

        Assert.False(result.HasErrors);
    }

    [Fact]
    public void Validate_ZoneConditionWithoutZone_IsBlockingError()
    {
        CommandDraft draft = ValidDraft();
        draft.ExecutorIds.Add("squad-a");

        CommandValidationResult result = CommandValidationRules.Validate(draft, Context());

        Assert.Contains(
            result.Issues,
            issue => issue.Code == "CONDITION_NOT_EVALUABLE" && issue.Severity == CommandIssueSeverity.Error);
    }

    [Fact]
    public void Validate_DestroyUnitWithUnknownTarget_IsBlockingError()
    {
        CommandDraft draft = ValidDraft();
        draft.Type = "ATTACK";
        draft.Condition = "destroy_unit";
        draft.ExecutorIds.Add("squad-a");
        draft.TargetUnitId = "ghost-unit";

        CommandValidationResult result = CommandValidationRules.Validate(draft, Context());

        Assert.Contains(
            result.Issues,
            issue => issue.Code == "CONDITION_NOT_EVALUABLE" && issue.Severity == CommandIssueSeverity.Error);
    }

    [Fact]
    public void Validate_ExecutorOutsideCommand_IsBlockingError()
    {
        CommandDraft draft = ValidDraft();
        draft.ExecutorIds.Add("enemy-1");
        draft.ZoneId = "zone-hill";

        CommandValidationResult result = CommandValidationRules.Validate(draft, Context());

        Assert.Contains(
            result.Issues,
            issue => issue.Code == "UNAUTHORIZED_TARGET" && issue.Severity == CommandIssueSeverity.Error);
    }

    [Fact]
    public void Validate_UnknownExecutor_IsBlockingError()
    {
        CommandDraft draft = ValidDraft();
        draft.ExecutorIds.Add("ghost-unit");
        draft.ZoneId = "zone-hill";

        CommandValidationResult result = CommandValidationRules.Validate(draft, Context());

        Assert.Contains(
            result.Issues,
            issue => issue.Code == "TARGET_NOT_FOUND" && issue.Severity == CommandIssueSeverity.Error);
    }

    [Fact]
    public void Validate_AmmoOverrideMissingFromExecutor_IsBlockingError()
    {
        CommandDraft draft = ValidDraft();
        draft.ExecutorIds.Add("squad-a");
        draft.ZoneId = "zone-hill";
        draft.AmmoOverride = "ammo-999";

        CommandValidationResult result = CommandValidationRules.Validate(draft, Context());

        Assert.Contains(
            result.Issues,
            issue => issue.Code == "AMMO_NOT_FOUND" && issue.Severity == CommandIssueSeverity.Error);
    }

    [Fact]
    public void Validate_DeadlineInPast_IsWarningNotError()
    {
        CommandDraft draft = ValidDraft();
        draft.ExecutorIds.Add("squad-a");
        draft.ZoneId = "zone-hill";
        draft.DeadlineTick = 500; // 当前 tick=1000：已过期。

        CommandValidationResult result = CommandValidationRules.Validate(draft, Context());

        Assert.False(result.HasErrors);
        Assert.Contains(
            result.Issues,
            issue => issue.Code == "DEADLINE_PAST_OR_TIGHT" && issue.Severity == CommandIssueSeverity.Warning);
    }

    [Fact]
    public void Validate_ExecutorWithActiveMission_IsWarningAboutSupersede()
    {
        CommandDraft draft = ValidDraft();
        draft.ExecutorIds.Add("squad-b"); // 该单位已有执行中任务。
        draft.ZoneId = "zone-hill";

        CommandValidationResult result = CommandValidationRules.Validate(draft, Context());

        Assert.Contains(
            result.Issues,
            issue => issue.Code == "SUPERSEDES_EXISTING" && issue.Severity == CommandIssueSeverity.Warning);
    }

    [Fact]
    public void Validate_BatchWithAmmoOverride_IsWarningAboutPerUnitCheck()
    {
        CommandDraft draft = ValidDraft();
        draft.ExecutorIds.Add("squad-a");
        draft.ExecutorIds.Add("squad-b");
        draft.ZoneId = "zone-hill";
        draft.AmmoOverride = "ammo-556";

        CommandValidationResult result = CommandValidationRules.Validate(draft, Context());

        Assert.False(result.HasErrors);
        Assert.Contains(
            result.Issues,
            issue => issue.Code == "BATCH_AMMO_PER_UNIT" && issue.Severity == CommandIssueSeverity.Warning);
    }

    [Fact]
    public void Validate_EmptyIntent_IsSuggestion()
    {
        CommandDraft draft = ValidDraft();
        draft.ExecutorIds.Add("squad-a");
        draft.ZoneId = "zone-hill";
        draft.Intent = string.Empty;

        CommandValidationResult result = CommandValidationRules.Validate(draft, Context());

        Assert.Contains(
            result.Issues,
            issue => issue.Code == "INTENT_MISSING" && issue.Severity == CommandIssueSeverity.Suggestion);
    }

    [Fact]
    public void Validate_UnknownCondition_IsBlockingError()
    {
        CommandDraft draft = ValidDraft();
        draft.ExecutorIds.Add("squad-a");
        draft.Condition = "teleport";

        CommandValidationResult result = CommandValidationRules.Validate(draft, Context());

        Assert.Contains(
            result.Issues,
            issue => issue.Code == "CONDITION_NOT_EVALUABLE" && issue.Severity == CommandIssueSeverity.Error);
    }
}
