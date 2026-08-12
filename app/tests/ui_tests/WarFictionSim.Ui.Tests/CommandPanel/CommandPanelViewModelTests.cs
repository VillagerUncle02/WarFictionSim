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
    private static CommandContext Context(ulong tick = 1000) =>
        new()
        {
            CommanderNodeId = "node-player",
            FriendlySide = "side-a",
            CurrentTick = tick,
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
    public void SetExecutors_ReplacesExecutorIds()
    {
        var viewModel = new CommandPanelViewModel();
        viewModel.ApplyContext(Context());

        viewModel.SetExecutors(["squad-a"]);
        Assert.Single(viewModel.Draft.ExecutorIds);

        viewModel.SetExecutors([]);
        Assert.Empty(viewModel.Draft.ExecutorIds);
    }

    [Fact]
    public void ExecutorCollectionChange_TriggersRevalidation()
    {
        var viewModel = new CommandPanelViewModel();
        viewModel.ApplyContext(Context());
        Assert.Contains(viewModel.Issues, issue => issue.Code == "TARGET_REQUIRED");

        viewModel.Draft.ExecutorIds.Add("squad-a");

        Assert.DoesNotContain(viewModel.Issues, issue => issue.Code == "TARGET_REQUIRED");
    }

    [Fact]
    public void ApplyContext_UnchangedContext_DoesNotRebuildOrRevalidate()
    {
        var viewModel = new CommandPanelViewModel();
        viewModel.ApplyContext(Context());
        System.Collections.ObjectModel.ObservableCollection<CommandableUnit> units = viewModel.ContextUnits;
        System.Collections.ObjectModel.ObservableCollection<string> zones = viewModel.ContextZones;
        int issueChanges = 0;
        viewModel.Issues.CollectionChanged += (_, _) => issueChanges++;

        viewModel.ApplyContext(Context()); // 新实例但内容相同：不得重建集合、不得重校验。

        Assert.Same(units, viewModel.ContextUnits);
        Assert.Same(zones, viewModel.ContextZones);
        Assert.Equal(0, issueChanges);
    }

    [Fact]
    public void ApplyContext_AdvancedTick_Revalidates()
    {
        var viewModel = new CommandPanelViewModel();
        viewModel.ApplyContext(Context());
        int issueChanges = 0;
        viewModel.Issues.CollectionChanged += (_, _) => issueChanges++;

        viewModel.ApplyContext(Context(tick: 1001));

        Assert.True(issueChanges > 0);
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
