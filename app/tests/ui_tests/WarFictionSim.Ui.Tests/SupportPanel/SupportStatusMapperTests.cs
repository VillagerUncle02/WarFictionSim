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
}
