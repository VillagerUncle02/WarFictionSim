// 测试：命令面板 —— 命令 JSON 构造（T041）。
//
// 输出必须与 contracts/schemas/command.schema.json 及 command-schema.md §1
// 逐字段一致（schema_version/type/target/completion/intent/behavior/priority/
// deadline），否则核心会以 SCHEMA_INVALID 拒绝——这里用黄金字符串锁定格式。

using WarFictionSim.Ui.CommandPanel;
using Xunit;

namespace WarFictionSim.Ui.Tests.CommandPanel;

public class CommandJsonBuilderTests
{
    [Fact]
    public void Build_SecureZoneCommand_MatchesContractShape()
    {
        var draft = new CommandDraft
        {
            Type = "SECURE_ZONE",
            Condition = "secure_zone",
            Intent = "占领高地并坚守",
            Engagement = "aggressive",
            FailureAction = "hold",
            Priority = 1,
            DeadlineTick = 3600,
            ZoneId = "zone-hill",
            DurationTicks = 2400,
        };
        draft.ExecutorIds.Add("squad-a");

        string json = CommandJsonBuilder.Build(draft);

        const string expected =
            "{\"schema_version\":1,\"type\":\"SECURE_ZONE\",\"target\":{\"kind\":\"unit\",\"ref\":\"squad-a\"}," +
            "\"completion\":{\"condition\":\"secure_zone\",\"params\":{\"zone\":\"zone-hill\",\"duration_ticks\":2400}}," +
            "\"intent\":\"占领高地并坚守\",\"behavior\":{\"engagement\":\"aggressive\",\"failure_action\":\"hold\"}," +
            "\"priority\":1,\"deadline\":{\"game_time\":3600}}";
        Assert.Equal(expected, json);
    }

    [Fact]
    public void Build_BatchTarget_UsesRefsArray()
    {
        var draft = new CommandDraft
        {
            Type = "PATROL",
            Condition = "patrol",
            Intent = "巡逻",
            Engagement = "balanced",
            FailureAction = "report",
            Priority = 0,
            DeadlineTick = 7200,
            CycleTicks = 3600,
        };
        draft.ExecutorIds.Add("squad-a");
        draft.ExecutorIds.Add("squad-b");

        string json = CommandJsonBuilder.Build(draft);

        Assert.Contains(
            "\"target\":{\"kind\":\"units\",\"refs\":[\"squad-a\",\"squad-b\"]}", json, StringComparison.Ordinal);
        Assert.Contains(
            "\"condition\":\"patrol\",\"params\":{\"cycle_ticks\":3600}", json, StringComparison.Ordinal);
    }

    [Fact]
    public void Build_ReachPointCommand_EmitsPointParams()
    {
        var draft = new CommandDraft
        {
            Type = "MOVE",
            Condition = "reach_point",
            Intent = "机动至目标点",
            Engagement = "balanced",
            FailureAction = "report",
            Priority = 0,
            DeadlineTick = 1800,
            PointX = 2.5,
            PointY = 3.5,
        };
        draft.ExecutorIds.Add("squad-a");

        string json = CommandJsonBuilder.Build(draft);

        Assert.Contains("\"point\":{\"x\":2.5,\"y\":3.5}", json, StringComparison.Ordinal);
    }

    [Fact]
    public void Build_DestroyUnitCommand_EmitsTargetUnit()
    {
        var draft = new CommandDraft
        {
            Type = "ATTACK",
            Condition = "destroy_unit",
            Intent = "消灭目标",
            Engagement = "aggressive",
            FailureAction = "report",
            Priority = 2,
            DeadlineTick = 2400,
            TargetUnitId = "enemy-1",
        };
        draft.ExecutorIds.Add("squad-a");

        string json = CommandJsonBuilder.Build(draft);

        Assert.Contains(
            "\"condition\":\"destroy_unit\",\"params\":{\"target_unit\":\"enemy-1\"}", json, StringComparison.Ordinal);
    }

    [Fact]
    public void Build_BehaviorExtras_AreIncludedOnlyWhenSet()
    {
        var draft = new CommandDraft
        {
            Type = "SECURE_ZONE",
            Condition = "secure_zone",
            Intent = "占领",
            Engagement = "cautious",
            Formation = "combat",
            AmmoOverride = "ammo-556",
            FailureAction = "withdraw_to",
            FailureTarget = "1.0,1.0",
            Priority = 0,
            DeadlineTick = 1000,
            ZoneId = "zone-hill",
        };
        draft.ExecutorIds.Add("squad-a");

        string json = CommandJsonBuilder.Build(draft);

        Assert.Contains(
            "\"engagement\":\"cautious\",\"formation\":\"combat\",\"ammo_override\":\"ammo-556\"," +
            "\"failure_action\":\"withdraw_to\",\"failure_target\":\"1.0,1.0\"}",
            json,
            StringComparison.Ordinal);
    }

    [Fact]
    public void Build_NegativePriority_Throws()
    {
        var draft = new CommandDraft
        {
            Type = "MOVE",
            Condition = "reach_point",
            Priority = -1,
            DeadlineTick = 1000,
        };
        draft.ExecutorIds.Add("squad-a");

        ArgumentException exception = Assert.Throws<ArgumentException>(() => CommandJsonBuilder.Build(draft));

        Assert.Contains("优先级", exception.Message, StringComparison.Ordinal);
    }
}
