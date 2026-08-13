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
        ulong attaches = 0,
        string scale = "platoon")
    {
        UnitState friendly = SnapshotFactory.Unit("sp-squad-1", "node-platoon-1", "side-a", 1, 1);
        UnitState otherNode = SnapshotFactory.Unit("sp-squad-2", "node-platoon-2", "side-a", 2, 2);
        UnitState enemy = SnapshotFactory.Unit("enemy-1", "node-enemy", "side-b", 3, 3);
        return SnapshotFactory.Create(
            tick,
            [friendly, otherNode, enemy],
            playerNodeId: "node-platoon-1",
            support: new SupportSummaryState(true, scale, "faction-china", "battalion", pendingRequests, attaches, scoreRemaining));
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

    [Fact]
    public void BattalionOptions_DoNotShowScoreDeductionSemantics()
    {
        var options = new SupportPanelOptions
        {
            Configured = true,
            Scale = "battalion",
            SuperiorNodeId = "node-brigade-1",
            AvailableKinds =
            [
                new SupportKindOption("squad-mortar-team", 30, "unit"),
                new SupportKindOption("artillery-152", 50, "fire_support"),
            ],
        };
        var viewModel = new SupportPanelViewModel(options);
        viewModel.ApplySnapshot(Snapshot(scoreRemaining: 55, scale: "battalion"), friendlySide: "side-a");
        viewModel.ToggleKind("squad-mortar-team");
        viewModel.ToggleKind("artillery-152");
        viewModel.QuantityText = "9"; // 预计 720 > 55：连排级会提示不足，营级不得提示。

        // 复审 R1-2：营级走配属链、native 不扣分；文案与警告按规模区分。
        Assert.False(viewModel.ScoreInsufficient);
        Assert.Contains("无分数扣减", viewModel.EstimatedCostText, StringComparison.Ordinal);
        Assert.Contains("营级", viewModel.ScoreRemainingText, StringComparison.Ordinal);
        Assert.DoesNotContain(viewModel.Issues, issue => issue.Code == "SUPPORT_INSUFFICIENT_SCORE");
    }

    [Fact]
    public void ApplySnapshot_ConsecutiveTickAdvances_AreThrottledByWallClock()
    {
        var client = new FakeSimClient(Snapshot());
        var timeProvider = new MutableTimeProvider();
        var viewModel = new SupportPanelViewModel(Options(), client, timeProvider);

        viewModel.ApplySnapshot(Snapshot(tick: 1), friendlySide: "side-a");
        Assert.Equal(2, client.EventQueries.Count); // 首帧：SUPPORT_ + ATTACH_RETURN 两连查。

        viewModel.ApplySnapshot(Snapshot(tick: 2), friendlySide: "side-a");
        viewModel.ApplySnapshot(Snapshot(tick: 3), friendlySide: "side-a");
        viewModel.ApplySnapshot(Snapshot(tick: 4), friendlySide: "side-a");
        Assert.Equal(2, client.EventQueries.Count); // 复审 R1-3：250ms 墙钟节流窗口内不重复查询。

        timeProvider.Advance(TimeSpan.FromMilliseconds(250));
        viewModel.ApplySnapshot(Snapshot(tick: 5), friendlySide: "side-a");
        Assert.Equal(4, client.EventQueries.Count); // 节流到期后补拉一次（两连查）。
    }

    [Fact]
    public void ApplySnapshot_StatusTracksLatestSubmittedRequest()
    {
        var client = new FakeSimClient(Snapshot());
        client.Events.AddRange(
        [
            new SimEventDto(1, 1, SimEventCategory.Command, SimEventSeverity.Info,
                "SUPPORT_REQUESTED request=req-cmd-1 interaction=SUPPORT_REQUEST from_node=node-platoon-1 to_node=node-battalion-1 request_type=reinforce priority=1 quantity=1"),
            new SimEventDto(2, 1, SimEventCategory.Command, SimEventSeverity.Info,
                "SUPPORT_EVALUATING request=req-cmd-1 interaction=SUPPORT_REQUEST evaluating_tick=1 resolve_tick=41"),
            new SimEventDto(3, 5, SimEventCategory.Command, SimEventSeverity.Info,
                "SUPPORT_REQUESTED request=req-cmd-2 interaction=SUPPORT_REQUEST from_node=node-platoon-1 to_node=node-battalion-1 request_type=medical priority=2 quantity=1"),
            new SimEventDto(4, 41, SimEventCategory.Command, SimEventSeverity.Info,
                "SUPPORT_ASSIGNED request=req-cmd-1 interaction=SUPPORT_REQUEST score_cost=30 score_remaining=30 units=[squad-mortar-team]"),
            new SimEventDto(5, 100, SimEventCategory.Command, SimEventSeverity.Info,
                "ATTACH_RETURNING request=req-cmd-1 unit=squad-mortar-team to=node-platoon-1 tick=100"),
            new SimEventDto(6, 130, SimEventCategory.Command, SimEventSeverity.Info,
                "ATTACH_RETURNED request=req-cmd-1 unit=squad-mortar-team to=node-platoon-1 tick=130"),
            new SimEventDto(7, 140, SimEventCategory.Command, SimEventSeverity.Info,
                "SUPPORT_EVALUATING request=req-cmd-2 interaction=SUPPORT_REQUEST evaluating_tick=140 resolve_tick=180"),
        ]);
        var viewModel = new SupportPanelViewModel(Options(), client);

        viewModel.ApplySnapshot(Snapshot(tick: 150), friendlySide: "side-a");

        // 复审 R1-4：旧请求归建事件不得覆盖新请求状态；文案带 RequestId。
        Assert.Contains("req-cmd-2", viewModel.StatusText, StringComparison.Ordinal);
        Assert.Contains("请求评估中", viewModel.StatusText, StringComparison.Ordinal);
        Assert.DoesNotContain("已归建", viewModel.StatusText, StringComparison.Ordinal);
    }

    [Fact]
    public void ApplySnapshot_StatusShowsLatestRegisterFailed()
    {
        var client = new FakeSimClient(Snapshot());
        client.Events.AddRange(
        [
            new SimEventDto(1, 1, SimEventCategory.Command, SimEventSeverity.Info,
                "SUPPORT_REQUESTED request=req-cmd-1 interaction=SUPPORT_REQUEST from_node=node-platoon-1 to_node=node-battalion-1 request_type=reinforce priority=1 quantity=1"),
            new SimEventDto(2, 1, SimEventCategory.Command, SimEventSeverity.Info,
                "SUPPORT_EVALUATING request=req-cmd-1 interaction=SUPPORT_REQUEST evaluating_tick=1 resolve_tick=41"),
            new SimEventDto(3, 5, SimEventCategory.Command, SimEventSeverity.Info,
                "SUPPORT_REQUEST_REGISTER_FAILED request=req-cmd-2 reason=DUPLICATE_OR_INVALID"),
        ]);
        var viewModel = new SupportPanelViewModel(Options(), client);

        viewModel.ApplySnapshot(Snapshot(tick: 6), friendlySide: "side-a");

        // 复审 R2-1：最新登记失败不得被旧请求评估状态过滤隐藏。
        Assert.Contains("req-cmd-2", viewModel.StatusText, StringComparison.Ordinal);
        Assert.Contains("登记失败", viewModel.StatusText, StringComparison.Ordinal);
        Assert.DoesNotContain("请求评估中", viewModel.StatusText, StringComparison.Ordinal);
    }

    [Fact]
    public void EmptyScaleOptions_DefaultToLimitedScoreSemantics()
    {
        var options = new SupportPanelOptions
        {
            Configured = true,
            Scale = string.Empty, // 场景未声明 scale：与 native 缺省连排级一致。
            SuperiorNodeId = "node-battalion-1",
            AvailableKinds = [new SupportKindOption("squad-mortar-team", 30, "unit")],
        };
        var viewModel = new SupportPanelViewModel(options);
        viewModel.ApplySnapshot(Snapshot(scoreRemaining: 60), friendlySide: "side-a");
        viewModel.ToggleKind("squad-mortar-team");
        viewModel.QuantityText = "3"; // 90 > 60。

        // 复审 R2-2：规模判定与 native 同为白名单（空/platoon → 有限分数）。
        Assert.True(viewModel.IsLimitedScore);
        Assert.True(viewModel.ScoreInsufficient);
        Assert.Contains("剩余分数", viewModel.ScoreRemainingText, StringComparison.Ordinal);
    }

    [Fact]
    public void UnknownScaleOptions_DoNotUseLimitedScoreSemantics()
    {
        var options = new SupportPanelOptions
        {
            Configured = true,
            Scale = "company", // 未来新增规模：native 白名单下不扣分。
            SuperiorNodeId = "node-battalion-1",
            AvailableKinds = [new SupportKindOption("squad-mortar-team", 30, "unit")],
        };
        var viewModel = new SupportPanelViewModel(options);
        viewModel.ApplySnapshot(Snapshot(scoreRemaining: 60), friendlySide: "side-a");
        viewModel.ToggleKind("squad-mortar-team");
        viewModel.QuantityText = "3"; // 90 > 60。

        // 复审 R2-2：黑名单改为白名单后未知规模视为营级语义（不扣分）。
        Assert.False(viewModel.IsLimitedScore);
        Assert.False(viewModel.ScoreInsufficient);
        Assert.Contains("无分数扣减", viewModel.EstimatedCostText, StringComparison.Ordinal);
    }

    private sealed class MutableTimeProvider : TimeProvider
    {
        private DateTimeOffset _now = new(2026, 1, 1, 0, 0, 0, TimeSpan.Zero);

        public override DateTimeOffset GetUtcNow() => _now;

        public void Advance(TimeSpan delta) => _now = _now.Add(delta);
    }
}
