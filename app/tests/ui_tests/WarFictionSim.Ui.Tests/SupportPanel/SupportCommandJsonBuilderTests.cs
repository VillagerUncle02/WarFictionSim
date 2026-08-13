// 测试：支援请求 UI —— SUPPORT_REQUEST 命令 JSON 黄金字符串（T053）。
//
// 输出必须与 command-schema.md §1.1/§5 及 contracts/schemas/command.schema.json
// 逐字段一致（schema_version/type/target/completion/support/priority），否则
// 核心会以 SCHEMA_INVALID 拒绝；这里用黄金字符串锁定键序与可选字段省略规则。

using WarFictionSim.Ui.CommandPanel;
using Xunit;

namespace WarFictionSim.Ui.Tests.SupportPanel;

public class SupportCommandJsonBuilderTests
{
    [Fact]
    public void Build_SupportRequest_MatchesContractShape()
    {
        var draft = new CommandDraft
        {
            Type = "SUPPORT_REQUEST",
            Condition = "support",
            Intent = "请求迫击炮与重机枪加强",
            Engagement = "aggressive",
            FailureAction = "hold",
            Priority = 1,
            DeadlineTick = 3600,
            SupportRequestType = "reinforce",
            SupportQuantity = 2,
            SupportToNode = "node-battalion-1",
            SupportForCommandId = "cmd-7",
        };
        draft.ExecutorIds.Add("sp-squad-1");
        draft.SupportKinds.Add("squad-mortar-team");
        draft.SupportKinds.Add("squad-hmg-team");

        string json = CommandJsonBuilder.Build(draft);

        const string expected =
            "{\"schema_version\":1,\"type\":\"SUPPORT_REQUEST\"," +
            "\"target\":{\"kind\":\"unit\",\"ref\":\"sp-squad-1\"}," +
            "\"completion\":{\"condition\":\"support\",\"params\":{}}," +
            "\"support\":{\"request_type\":\"reinforce\"," +
            "\"kinds\":[\"squad-mortar-team\",\"squad-hmg-team\"],\"quantity\":2," +
            "\"to_node\":\"node-battalion-1\",\"for_command_id\":\"cmd-7\"}," +
            "\"intent\":\"请求迫击炮与重机枪加强\"," +
            "\"behavior\":{\"engagement\":\"aggressive\",\"failure_action\":\"hold\"}," +
            "\"priority\":1,\"deadline\":{\"game_time\":3600}}";
        Assert.Equal(expected, json);
    }

    [Fact]
    public void Build_SupportRequest_OmitsOptionalFieldsAndDefaultsQuantity()
    {
        var draft = new CommandDraft
        {
            Type = "SUPPORT_REQUEST",
            Condition = "support",
            Intent = "医疗后送",
            Engagement = "balanced",
            FailureAction = "report",
            Priority = 0,
            DeadlineTick = 1800,
            SupportRequestType = "medical",
        };
        draft.ExecutorIds.Add("sp-squad-1");
        draft.SupportKinds.Add("squad-mortar-team");

        string json = CommandJsonBuilder.Build(draft);

        Assert.Contains(
            "\"support\":{\"request_type\":\"medical\",\"kinds\":[\"squad-mortar-team\"],\"quantity\":1}",
            json,
            StringComparison.Ordinal);
        Assert.DoesNotContain("to_node", json, StringComparison.Ordinal);
        Assert.DoesNotContain("for_command_id", json, StringComparison.Ordinal);
    }

    [Fact]
    public void CommandTypeCatalog_SupportRequest_DefaultsToSupportCondition()
    {
        Assert.Equal("support", CommandTypeCatalog.DefaultConditionFor("SUPPORT_REQUEST"));
    }
}
