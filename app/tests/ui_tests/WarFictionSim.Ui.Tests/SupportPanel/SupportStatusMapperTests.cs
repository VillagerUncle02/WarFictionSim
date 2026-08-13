// 测试：支援请求 UI —— 事件 → 中文状态映射（T053）。
//
// 事件名与 native support_runtime.cpp/attach.cpp 的稳定文本对齐
// （SUPPORT_REQUESTED/SUPPORT_EVALUATING/SUPPORT_ASSIGNED/SUPPORT_REJECTED/
// ATTACH_RETURNING/ATTACH_RETURNED/SUPPORT_ESCALATED 等），拒绝原因映射
// INSUFFICIENT_SCORE/SCOPE_VIOLATION/NO_SUPERIOR_ESCALATION 等稳定错误码。

using WarFictionSim.Ui.EventLogPanel;
using WarFictionSim.Ui.SupportPanel;
using Xunit;

namespace WarFictionSim.Ui.Tests.SupportPanel;

public class SupportStatusMapperTests
{
    private static SimEventDto Event(ulong seq, ulong tick, string message) =>
        new(seq, tick, SimEventCategory.Command, SimEventSeverity.Info, message);

    [Fact]
    public void Map_NoEvents_ReturnsNone()
    {
        SupportStatusInfo info = SupportStatusMapper.Map([]);

        Assert.Equal(SupportRequestStatus.None, info.Status);
        Assert.Equal("暂无支援请求状态", info.StatusText);
    }

    [Fact]
    public void Map_RequestedThenEvaluating_ShowsEvaluating()
    {
        SupportStatusInfo info = SupportStatusMapper.Map(
        [
            Event(1, 1, "SUPPORT_REQUESTED request=req-cmd-1 interaction=SUPPORT_REQUEST from_node=node-platoon-1 to_node=node-battalion-1 request_type=reinforce priority=1 quantity=2"),
            Event(2, 1, "SUPPORT_EVALUATING request=req-cmd-1 interaction=SUPPORT_REQUEST evaluating_tick=1 resolve_tick=41"),
        ]);

        Assert.Equal(SupportRequestStatus.Evaluating, info.Status);
        Assert.Equal("请求评估中", info.StatusText);
        Assert.Equal("req-cmd-1", info.RequestId);
    }

    [Fact]
    public void Map_Assigned_ShowsScoreDeduction()
    {
        SupportStatusInfo info = SupportStatusMapper.Map(
        [
            Event(1, 1, "SUPPORT_REQUESTED request=req-cmd-1 interaction=SUPPORT_REQUEST from_node=node-platoon-1 to_node=node-battalion-1 request_type=reinforce priority=1 quantity=2"),
            Event(2, 1, "SUPPORT_EVALUATING request=req-cmd-1 interaction=SUPPORT_REQUEST evaluating_tick=1 resolve_tick=41"),
            Event(3, 41, "SUPPORT_ASSIGNED request=req-cmd-1 interaction=SUPPORT_REQUEST score_cost=30 score_remaining=30 units=[squad-mortar-team]"),
        ]);

        Assert.Equal(SupportRequestStatus.Assigned, info.Status);
        Assert.Contains("支援已配属", info.StatusText, StringComparison.Ordinal);
        Assert.Contains("扣分 30", info.StatusText, StringComparison.Ordinal);
        Assert.Contains("剩余分数 30", info.StatusText, StringComparison.Ordinal);
    }

    [Fact]
    public void Map_RejectedInsufficientScore_ShowsChineseReason()
    {
        SupportStatusInfo info = SupportStatusMapper.Map(
        [
            Event(1, 1, "SUPPORT_REQUESTED request=req-cmd-1 interaction=SUPPORT_REQUEST from_node=node-platoon-1 to_node=node-battalion-1 request_type=reinforce priority=1 quantity=2"),
            Event(2, 41, "SUPPORT_REJECTED request=req-cmd-1 interaction=SUPPORT_REQUEST reason=INSUFFICIENT_SCORE"),
        ]);

        Assert.Equal(SupportRequestStatus.Rejected, info.Status);
        Assert.Equal("INSUFFICIENT_SCORE", info.ReasonCode);
        Assert.Contains("剩余分数不足", info.ReasonText, StringComparison.Ordinal);
        Assert.Contains("请求被拒绝", info.StatusText, StringComparison.Ordinal);
    }

    [Fact]
    public void Map_RejectedScopeViolation_ShowsChineseReason()
    {
        SupportStatusInfo info = SupportStatusMapper.Map(
        [
            Event(1, 1, "SUPPORT_REQUESTED request=req-cmd-1 interaction=SUPPORT_REQUEST from_node=node-platoon-1 to_node=node-battalion-1 request_type=fire_support priority=1 quantity=1"),
            Event(2, 41, "SUPPORT_REJECTED request=req-cmd-1 interaction=SUPPORT_REQUEST reason=SCOPE_VIOLATION 支援种类不在所属编制资源池内: artillery-999"),
        ]);

        Assert.Equal(SupportRequestStatus.Rejected, info.Status);
        Assert.Equal("SCOPE_VIOLATION", info.ReasonCode);
        Assert.Contains("支援种类超出所属编制资源池", info.ReasonText, StringComparison.Ordinal);
    }

    [Fact]
    public void Map_AttachReturningThenReturned_ShowsReturned()
    {
        SupportStatusInfo info = SupportStatusMapper.Map(
        [
            Event(1, 100, "ATTACH_RETURNING request=req-cmd-1 unit=squad-mortar-team to=node-platoon-1 tick=100"),
            Event(2, 130, "ATTACH_RETURNED request=req-cmd-1 unit=squad-mortar-team to=node-platoon-1 tick=130"),
        ]);

        Assert.Equal(SupportRequestStatus.Returned, info.Status);
        Assert.Equal("支援单位已归建", info.StatusText);
    }

    [Fact]
    public void Map_EscalatedThenRejectedNoSuperior_FinalStatusIsRejected()
    {
        SupportStatusInfo info = SupportStatusMapper.Map(
        [
            Event(1, 1, "SUPPORT_REQUESTED request=req-cmd-1 interaction=SUPPORT_REQUEST from_node=node-platoon-1 to_node=node-battalion-1 request_type=reinforce priority=1 quantity=1"),
            Event(2, 41, "SUPPORT_ESCALATED request=req-cmd-1 interaction=SUPPORT_REQUEST to_node=node-brigade-1 reason=INSUFFICIENT_AVAILABLE kind=squad-atgm-team"),
            Event(3, 41, "SUPPORT_REJECTED request=req-cmd-1 interaction=SUPPORT_REQUEST reason=NO_SUPERIOR_ESCALATION escalated=true"),
        ]);

        Assert.Equal(SupportRequestStatus.Rejected, info.Status);
        Assert.Equal("NO_SUPERIOR_ESCALATION", info.ReasonCode);
        Assert.Contains("无更上级可转请", info.ReasonText, StringComparison.Ordinal);
    }

    [Fact]
    public void Map_RegisterFailed_ShowsFailure()
    {
        SupportStatusInfo info = SupportStatusMapper.Map(
        [
            Event(1, 1, "SUPPORT_REQUEST_REGISTER_FAILED request=req-cmd-1 reason=DUPLICATE_OR_INVALID"),
        ]);

        Assert.Equal(SupportRequestStatus.RegisterFailed, info.Status);
        Assert.Contains("登记失败", info.StatusText, StringComparison.Ordinal);
    }

    [Fact]
    public void MapForRequest_InterleavedRequests_FiltersEachChainAndPrefixesRequestId()
    {
        var events = new List<SimEventDto>
        {
            Event(1, 1, "SUPPORT_REQUESTED request=req-cmd-1 interaction=SUPPORT_REQUEST from_node=node-platoon-1 to_node=node-battalion-1 request_type=reinforce priority=1 quantity=1"),
            Event(2, 5, "SUPPORT_REQUESTED request=req-cmd-2 interaction=SUPPORT_REQUEST from_node=node-platoon-1 to_node=node-battalion-1 request_type=medical priority=2 quantity=1"),
            Event(3, 41, "SUPPORT_EVALUATING request=req-cmd-1 interaction=SUPPORT_REQUEST evaluating_tick=41 resolve_tick=81"),
            Event(4, 41, "SUPPORT_ASSIGNED request=req-cmd-1 interaction=SUPPORT_REQUEST score_cost=30 score_remaining=30 units=[squad-mortar-team]"),
            Event(5, 100, "ATTACH_RETURNING request=req-cmd-1 unit=squad-mortar-team to=node-platoon-1 tick=100"),
            Event(6, 130, "ATTACH_RETURNED request=req-cmd-1 unit=squad-mortar-team to=node-platoon-1 tick=130"),
            Event(7, 140, "SUPPORT_EVALUATING request=req-cmd-2 interaction=SUPPORT_REQUEST evaluating_tick=140 resolve_tick=180"),
        };

        SupportStatusInfo newest = SupportStatusMapper.MapForRequest(events, "req-cmd-2");
        SupportStatusInfo oldest = SupportStatusMapper.MapForRequest(events, "req-cmd-1");

        // 复审 R1-4：两个请求交错事件时按请求过滤，各自状态正确且文案含 RequestId。
        Assert.Equal(SupportRequestStatus.Evaluating, newest.Status);
        Assert.Contains("req-cmd-2", newest.StatusText, StringComparison.Ordinal);
        Assert.Equal("req-cmd-2", newest.RequestId);
        Assert.Equal(SupportRequestStatus.Returned, oldest.Status);
        Assert.Contains("req-cmd-1", oldest.StatusText, StringComparison.Ordinal);
    }

    [Fact]
    public void MapForRequest_WithoutRequestId_FallsBackToGlobalLatest()
    {
        var events = new List<SimEventDto>
        {
            Event(1, 1, "SUPPORT_REQUESTED request=req-cmd-1 interaction=SUPPORT_REQUEST from_node=node-platoon-1 to_node=node-battalion-1 request_type=reinforce priority=1 quantity=1"),
            Event(2, 130, "ATTACH_RETURNED request=req-cmd-1 unit=squad-mortar-team to=node-platoon-1 tick=130"),
        };

        SupportStatusInfo info = SupportStatusMapper.MapForRequest(events, requestId: null);

        // 无请求 id（尚未提交/无法解析）时回退全局最新事件（与 Map 一致）。
        Assert.Equal(SupportRequestStatus.Returned, info.Status);
        Assert.Equal(info, SupportStatusMapper.Map(events));
    }

    [Fact]
    public void FindLatestRequestId_ReturnsLastSubmittedRequest()
    {
        var events = new List<SimEventDto>
        {
            Event(1, 1, "SUPPORT_REQUESTED request=req-cmd-1 interaction=SUPPORT_REQUEST from_node=node-platoon-1 to_node=node-battalion-1 request_type=reinforce priority=1 quantity=1"),
            Event(2, 5, "SUPPORT_REQUESTED request=req-cmd-2 interaction=SUPPORT_REQUEST from_node=node-platoon-1 to_node=node-battalion-1 request_type=medical priority=2 quantity=1"),
            Event(3, 130, "ATTACH_RETURNED request=req-cmd-1 unit=squad-mortar-team to=node-platoon-1 tick=130"),
        };

        // 复审 R1-4：追踪目标取最近一次 SUPPORT_REQUESTED 的请求，而非全局最后事件。
        Assert.Equal("req-cmd-2", SupportStatusMapper.FindLatestRequestId(events));
        Assert.Null(SupportStatusMapper.FindLatestRequestId([]));
    }

    [Fact]
    public void FindLatestRequestId_RegisterFailedAfterRequested_TracksFailedRegistration()
    {
        var events = new List<SimEventDto>
        {
            Event(1, 1, "SUPPORT_REQUESTED request=req-cmd-1 interaction=SUPPORT_REQUEST from_node=node-platoon-1 to_node=node-battalion-1 request_type=reinforce priority=1 quantity=1"),
            Event(2, 1, "SUPPORT_EVALUATING request=req-cmd-1 interaction=SUPPORT_REQUEST evaluating_tick=1 resolve_tick=41"),
            Event(3, 5, "SUPPORT_REQUEST_REGISTER_FAILED request=req-cmd-2 reason=DUPLICATE_OR_INVALID"),
        };

        // 复审 R2-1：登记失败也是"最新提交结果"来源，不得被旧请求 REQUESTED 覆盖。
        Assert.Equal("req-cmd-2", SupportStatusMapper.FindLatestRequestId(events));
        SupportStatusInfo info = SupportStatusMapper.MapForRequest(events, "req-cmd-2");
        Assert.Equal(SupportRequestStatus.RegisterFailed, info.Status);
        Assert.Contains("req-cmd-2", info.StatusText, StringComparison.Ordinal);
        Assert.Contains("登记失败", info.StatusText, StringComparison.Ordinal);
    }

    [Fact]
    public void FindLatestRequestId_LaterRequestedWinsOverRegisterFailed()
    {
        var events = new List<SimEventDto>
        {
            Event(1, 1, "SUPPORT_REQUESTED request=req-cmd-1 interaction=SUPPORT_REQUEST from_node=node-platoon-1 to_node=node-battalion-1 request_type=reinforce priority=1 quantity=1"),
            Event(2, 5, "SUPPORT_REQUEST_REGISTER_FAILED request=req-cmd-2 reason=DUPLICATE_OR_INVALID"),
            Event(3, 9, "SUPPORT_REQUESTED request=req-cmd-3 interaction=SUPPORT_REQUEST from_node=node-platoon-1 to_node=node-battalion-1 request_type=medical priority=2 quantity=1"),
        };

        // 取 REQUESTED 与 REGISTER_FAILED 两者中最新者（按事件流顺序）。
        Assert.Equal("req-cmd-3", SupportStatusMapper.FindLatestRequestId(events));
    }
}
