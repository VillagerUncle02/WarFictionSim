// 测试：支援请求 UI —— 视图模型：快照驱动的分数显示与扣减反馈、目标池
// 过滤、数量输入内联错误、状态事件订阅与提交（T053）。
//
// 分数与请求计数全部来自快照 support 摘要（只读消费，宪法第 14 条）；
// 提交序列化走 CommandJsonBuilder 统一路径，注入由应用壳负责（本测试只
// 断言事件载荷，注入路径在 GameScreenViewModelTests 验证 FakeSimClient）。

using WarFictionSim.Ui.CommandPanel;
using WarFictionSim.Ui.EventLogPanel;
using WarFictionSim.Ui.Interop;
using WarFictionSim.Ui.SupportPanel;
using WarFictionSim.Ui.Tests.TestDoubles;
using Xunit;

namespace WarFictionSim.Ui.Tests.SupportPanel;

public class SupportPanelViewModelTests
{
    private static SupportPanelOptions Options() =>
        new()
        {
            Configured = true,
            Scale = "platoon",
            SuperiorNodeId = "node-battalion-1",
            AvailableKinds =
            [
                new SupportKindOption("squad-mortar-team", 30, "unit"),
                new SupportKindOption("squad-hmg-team", 15, "unit"),
            ],
        };

    private static SimulationSnapshot Snapshot(
        ulong tick = 0,
        ulong scoreRemaining = 60,
        ulong pendingRequests = 0,
        ulong attaches = 0)
    {
        UnitState friendly = SnapshotFactory.Unit("sp-squad-1", "node-platoon-1", "side-a", 1, 1);
        UnitState otherNode = SnapshotFactory.Unit("sp-squad-2", "node-platoon-2", "side-a", 2, 2);
        UnitState enemy = SnapshotFactory.Unit("enemy-1", "node-enemy", "side-b", 3, 3);
        return SnapshotFactory.Create(
            tick,
            [friendly, otherNode, enemy],
            playerNodeId: "node-platoon-1",
            support: new SupportSummaryState(true, "platoon", "faction-china", "battalion", pendingRequests, attaches, scoreRemaining));
    }

    private static void SelectValidRequest(SupportPanelViewModel viewModel)
    {
        viewModel.SetTarget("sp-squad-1");
        viewModel.SelectedRequestType = "reinforce";
        viewModel.ToggleKind("squad-mortar-team");
    }

    [Fact]
    public void ApplySnapshot_OnlyFriendlyCommandableUnits_AreTargets()
    {
        var viewModel = new SupportPanelViewModel(Options());
        viewModel.ApplySnapshot(Snapshot(), friendlySide: "side-a");

        Assert.Equal(["sp-squad-1"], viewModel.TargetUnits.Select(unit => unit.Id));
    }

    [Fact]
    public void ApplySnapshot_DrivesScoreRemainingDisplay()
    {
        var viewModel = new SupportPanelViewModel(Options());

        viewModel.ApplySnapshot(Snapshot(scoreRemaining: 60), friendlySide: "side-a");

        Assert.Equal((ulong)60, viewModel.ScoreRemaining);
        Assert.Equal("剩余分数：60", viewModel.ScoreRemainingText);
    }

    [Fact]
    public void KindToggleAndQuantity_RecomputeEstimatedCostAndRemainder()
    {
        var viewModel = new SupportPanelViewModel(Options());
        viewModel.ApplySnapshot(Snapshot(), friendlySide: "side-a");
        viewModel.ToggleKind("squad-mortar-team");
        viewModel.ToggleKind("squad-hmg-team");
        viewModel.QuantityText = "2";

        Assert.Equal((ulong)90, viewModel.EstimatedCost);
        Assert.True(viewModel.ScoreInsufficient);
        Assert.Contains("不足", viewModel.ScoreAfterText, StringComparison.Ordinal);

        viewModel.ToggleKind("squad-hmg-team");
        Assert.Equal((ulong)60, viewModel.EstimatedCost);
        Assert.False(viewModel.ScoreInsufficient);
        Assert.Equal("扣减后剩余：0", viewModel.ScoreAfterText);
    }

    [Fact]
    public void QuantityText_Invalid_ShowsInlineErrorAndBlocksSubmit()
    {
        var viewModel = new SupportPanelViewModel(Options());
        viewModel.ApplySnapshot(Snapshot(), friendlySide: "side-a");

        viewModel.QuantityText = "abc";

        Assert.Contains(
            viewModel.Issues,
            issue => issue.Code == "NUMERIC_INPUT_INVALID" && issue.Severity == CommandIssueSeverity.Error);
        Assert.False(viewModel.CanSubmit);
    }

    [Fact]
    public void QuantityZero_IsValidationError()
    {
        var viewModel = new SupportPanelViewModel(Options());
        viewModel.ApplySnapshot(Snapshot(), friendlySide: "side-a");
        SelectValidRequest(viewModel);

        viewModel.QuantityText = "0";

        Assert.Contains(
            viewModel.Issues,
            issue => issue.Code == "SUPPORT_QUANTITY_INVALID" && issue.Severity == CommandIssueSeverity.Error);
        Assert.False(viewModel.CanSubmit);
    }

    [Fact]
    public void TrySubmit_ValidSelection_RaisesSupportJson()
    {
        var viewModel = new SupportPanelViewModel(Options());
        viewModel.ApplySnapshot(Snapshot(), friendlySide: "side-a");
        SelectValidRequest(viewModel);
        string? raised = null;
        viewModel.SubmitRequested += (_, json) => raised = json;

        bool ok = viewModel.TrySubmit(out string? json);

        Assert.True(ok);
        Assert.Equal(raised, json);
        Assert.Contains("\"type\":\"SUPPORT_REQUEST\"", json, StringComparison.Ordinal);
        Assert.Contains("\"request_type\":\"reinforce\"", json, StringComparison.Ordinal);
        Assert.Contains("\"kinds\":[\"squad-mortar-team\"]", json, StringComparison.Ordinal);
        Assert.Contains("\"to_node\":\"node-battalion-1\"", json, StringComparison.Ordinal);
        Assert.True(viewModel.CanSubmit);
    }

    [Fact]
    public void TrySubmit_WithErrors_ReturnsFalseWithoutEvent()
    {
        var viewModel = new SupportPanelViewModel(Options());
        viewModel.ApplySnapshot(Snapshot(), friendlySide: "side-a");
        int raised = 0;
        viewModel.SubmitRequested += (_, _) => raised++;

        bool ok = viewModel.TrySubmit(out string? json);

        Assert.False(ok);
        Assert.Null(json);
        Assert.Equal(0, raised);
    }

    [Fact]
    public void ApplySnapshot_ScoreRemaining_ReflectsPostAssignmentDeduction()
    {
        var viewModel = new SupportPanelViewModel(Options());
        viewModel.ApplySnapshot(Snapshot(scoreRemaining: 60), friendlySide: "side-a");

        viewModel.ApplySnapshot(Snapshot(tick: 41, scoreRemaining: 30), friendlySide: "side-a");

        Assert.Equal("剩余分数：30", viewModel.ScoreRemainingText);
    }

    [Fact]
    public void ApplySnapshot_QueriesSupportEventsAndShowsStatus()
    {
        var client = new FakeSimClient(Snapshot());
        client.Events.Add(new SimEventDto(
            1,
            1,
            SimEventCategory.Command,
            SimEventSeverity.Info,
            "SUPPORT_REQUESTED request=req-cmd-1 interaction=SUPPORT_REQUEST from_node=node-platoon-1 to_node=node-battalion-1 request_type=reinforce priority=1 quantity=1"));
        var viewModel = new SupportPanelViewModel(Options(), client);

        viewModel.ApplySnapshot(Snapshot(tick: 1), friendlySide: "side-a");

        Assert.Contains("已提交支援请求", viewModel.StatusText, StringComparison.Ordinal);
        Assert.True(client.EventQueries.Count > 0);
    }

    [Fact]
    public void UnconfiguredOptions_ShowHintAndEmptyPool()
    {
        var options = new SupportPanelOptions { Configured = false };
        var viewModel = new SupportPanelViewModel(options);
        viewModel.ApplySnapshot(Snapshot(), friendlySide: "side-a");

        Assert.False(viewModel.IsSupportConfigured);
        Assert.Empty(viewModel.KindOptions);
        Assert.Contains("未配置支援", viewModel.SupportModeHint, StringComparison.Ordinal);
    }
}
