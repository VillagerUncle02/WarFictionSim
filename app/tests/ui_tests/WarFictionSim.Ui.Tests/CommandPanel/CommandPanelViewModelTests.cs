// 测试：命令面板 —— 上下文应用、执行单位选择、提交与核心拒绝反馈（T041）。
//
// 面板提交分两段：面板三级校验（错误阻断）→ 构造 JSON → 经 ISimClient
// 注入；核心拒绝（最终权威）以错误条目内嵌呈现，不打断操作。

using WarFictionSim.Ui.CommandPanel;
using WarFictionSim.Ui.Interop;
using Xunit;

namespace WarFictionSim.Ui.Tests.CommandPanel;

public class CommandPanelViewModelTests
{
    private static CommandContext Context() =>
        new()
        {
            CommanderNodeId = "node-player",
            FriendlySide = "side-a",
            CurrentTick = 1000,
            Units = [new CommandableUnit("squad-a", "node-player", "side-a", ["ammo-556"], false)],
            ZoneIds = ["zone-hill"],
        };

    [Fact]
    public void TrySubmit_WithErrors_ReturnsFalseAndNoEvent()
    {
        var viewModel = new CommandPanelViewModel();
        viewModel.ApplyContext(Context());
        int raised = 0;
        viewModel.SubmitRequested += (_, _) => raised++;

        bool submitted = viewModel.TrySubmit(out string? json);

        Assert.False(submitted);
        Assert.Null(json);
        Assert.Equal(0, raised);
        Assert.False(viewModel.CanSubmit);
    }

    [Fact]
    public void TrySubmit_WithValidDraft_RaisesJson()
    {
        var viewModel = new CommandPanelViewModel();
        viewModel.ApplyContext(Context());
        viewModel.SetTypeCommand.Execute("SECURE_ZONE");
        viewModel.SelectExecutorCommand.Execute("squad-a");
        viewModel.SetZoneTarget("zone-hill");
        viewModel.Draft.DurationTicks = 2400;
        viewModel.Draft.Intent = "占领高地";
        string? submitted = null;
        viewModel.SubmitRequested += (_, json) => submitted = json;

        bool ok = viewModel.TrySubmit(out string? json);

        Assert.True(ok);
        Assert.Equal(submitted, json);
        Assert.Contains("SECURE_ZONE", json, StringComparison.Ordinal);
        Assert.True(viewModel.CanSubmit);
    }

    [Fact]
    public void SelectExecutor_TogglesSelection()
    {
        var viewModel = new CommandPanelViewModel();

        viewModel.SelectExecutorCommand.Execute("squad-a");
        Assert.Single(viewModel.Draft.ExecutorIds);

        viewModel.SelectExecutorCommand.Execute("squad-a");
        Assert.Empty(viewModel.Draft.ExecutorIds);
    }

    [Fact]
    public void SetType_DefaultsConditionFromCatalog()
    {
        var viewModel = new CommandPanelViewModel();

        viewModel.SetTypeCommand.Execute("MOVE");

        Assert.Equal("MOVE", viewModel.Draft.Type);
        Assert.Equal("reach_point", viewModel.Draft.Condition);
    }

    [Fact]
    public void ShowNativeRejection_AddsBlockingErrorIssue()
    {
        var viewModel = new CommandPanelViewModel();
        viewModel.ApplyContext(Context());

        viewModel.ShowNativeRejection(new SimNativeException(SimResultCode.InvalidData, "核心拒绝测试"));

        Assert.Contains(
            viewModel.Issues,
            issue => issue.Severity == CommandIssueSeverity.Error && issue.Code == "NATIVE_REJECTED");
        Assert.False(viewModel.CanSubmit);
    }
}
