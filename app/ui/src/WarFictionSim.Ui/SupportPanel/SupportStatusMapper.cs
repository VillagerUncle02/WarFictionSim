// 文件总览：支援请求 UI —— 事件 → 中文状态映射（T053）。
//
// 事件文本与 native/sim/src/support_runtime.cpp、attach.cpp 的稳定日志
// 对齐（SUPPORT_REQUESTED→EVALUATING→ASSIGNED/REJECTED，归建 ATTACH_
// RETURNING/RETURNED，转请 ESCALATED），拒绝原因映射稳定错误码
// （INSUFFICIENT_SCORE/SCOPE_VIOLATION/NO_SUPERIOR_ESCALATION 等）。
// 状态是表现层派生视图：不写回核心，事件日志仍是唯一事实源。

using WarFictionSim.Ui.EventLogPanel;

namespace WarFictionSim.Ui.SupportPanel;

/// <summary>支援请求状态（事件派生的只读展示状态）。</summary>
public enum SupportRequestStatus
{
    /// <summary>尚无支援事件。</summary>
    None,

    /// <summary>已提交请求（SUPPORT_REQUESTED）。</summary>
    Submitted,

    /// <summary>评估中（SUPPORT_EVALUATING）。</summary>
    Evaluating,

    /// <summary>已配属（SUPPORT_ASSIGNED）。</summary>
    Assigned,

    /// <summary>已拒绝（SUPPORT_REJECTED）。</summary>
    Rejected,

    /// <summary>归建途中（ATTACH_RETURNING）。</summary>
    Returning,

    /// <summary>已归建（ATTACH_RETURNED）。</summary>
    Returned,

    /// <summary>已转请上级（SUPPORT_ESCALATED）。</summary>
    Escalated,

    /// <summary>登记失败（SUPPORT_REQUEST_REGISTER_FAILED）。</summary>
    RegisterFailed,

    /// <summary>支援配置无效（SUPPORT_CONFIG_INVALID）。</summary>
    ConfigInvalid,
}

/// <summary>映射结果（只读）。</summary>
/// <param name="Status">最新状态。</param>
/// <param name="StatusText">中文状态文案。</param>
/// <param name="RequestId">关联请求 id（req-*）。</param>
/// <param name="ReasonCode">拒绝原因稳定错误码（无则 null）。</param>
/// <param name="ReasonText">拒绝原因中文说明。</param>
/// <param name="Tick">最新相关事件的游戏 tick。</param>
public sealed record SupportStatusInfo(
    SupportRequestStatus Status,
    string StatusText,
    string? RequestId,
    string? ReasonCode,
    string? ReasonText,
    ulong? Tick);

/// <summary>把核心事件文本映射为中文状态。</summary>
public static class SupportStatusMapper
{
    /// <summary>按事件顺序取最新一条相关事件映射（事件已按 seq 升序）。</summary>
    /// <param name="events">支援相关事件（SUPPORT_* / ATTACH_* 子串过滤后的全集）。</param>
    /// <returns>最新状态；无事件返回 None。</returns>
    public static SupportStatusInfo Map(IReadOnlyList<SimEventDto> events)
    {
        SupportStatusInfo? latest = null;
        foreach (SimEventDto entry in events)
        {
            SupportStatusInfo? mapped = TryMapEvent(entry);
            if (mapped is not null)
            {
                latest = mapped;
            }
        }

        return latest ?? new SupportStatusInfo(SupportRequestStatus.None, "暂无支援请求状态", null, null, null, null);
    }

    /// <summary>映射单条事件；无关事件返回 <see langword="null"/>。</summary>
    public static SupportStatusInfo? TryMapEvent(SimEventDto entry)
    {
        string message = entry.Message;
        if (message.StartsWith("SUPPORT_REQUESTED ", StringComparison.Ordinal))
        {
            return new SupportStatusInfo(
                SupportRequestStatus.Submitted, "已提交支援请求（等待受理）", ExtractRequestId(message), null, null, entry.Tick);
        }

        if (message.StartsWith("SUPPORT_EVALUATING ", StringComparison.Ordinal))
        {
            return new SupportStatusInfo(
                SupportRequestStatus.Evaluating, "请求评估中", ExtractRequestId(message), null, null, entry.Tick);
        }

        if (message.StartsWith("SUPPORT_ASSIGNED ", StringComparison.Ordinal))
        {
            string? scoreCost = ExtractField(message, "score_cost");
            string? scoreRemaining = ExtractField(message, "score_remaining");
            string text = scoreCost is null || scoreRemaining is null
                ? "支援已配属"
                : $"支援已配属（扣分 {scoreCost}，剩余分数 {scoreRemaining}）";
            return new SupportStatusInfo(
                SupportRequestStatus.Assigned, text, ExtractRequestId(message), null, null, entry.Tick);
        }

        if (message.StartsWith("SUPPORT_REJECTED ", StringComparison.Ordinal))
        {
            return CreateRejected(message, entry.Tick);
        }

        if (message.StartsWith("SUPPORT_ESCALATED ", StringComparison.Ordinal))
        {
            return new SupportStatusInfo(
                SupportRequestStatus.Escalated, "请求已转请上级（等待进一步处置）", ExtractRequestId(message), null, null, entry.Tick);
        }

        if (message.StartsWith("ATTACH_RETURNING ", StringComparison.Ordinal))
        {
            return new SupportStatusInfo(
                SupportRequestStatus.Returning, "支援单位归建途中", ExtractRequestId(message), null, null, entry.Tick);
        }

        if (message.StartsWith("ATTACH_RETURNED ", StringComparison.Ordinal))
        {
            return new SupportStatusInfo(
                SupportRequestStatus.Returned, "支援单位已归建", ExtractRequestId(message), null, null, entry.Tick);
        }

        if (message.StartsWith("SUPPORT_REQUEST_REGISTER_FAILED ", StringComparison.Ordinal))
        {
            return new SupportStatusInfo(
                SupportRequestStatus.RegisterFailed, "支援请求登记失败（重复或非法）", ExtractRequestId(message), null, null, entry.Tick);
        }

        if (message.StartsWith("SUPPORT_CONFIG_INVALID ", StringComparison.Ordinal))
        {
            return new SupportStatusInfo(SupportRequestStatus.ConfigInvalid, "支援配置无效", null, null, null, entry.Tick);
        }

        return null;
    }

    /// <summary>把稳定错误码映射为中文原因（未知码原样返回，不静默）。</summary>
    /// <param name="code">稳定错误码。</param>
    /// <returns>中文原因。</returns>
    public static string ToChineseReason(string code) => code switch
    {
        "INSUFFICIENT_SCORE" => "剩余分数不足（有限分数用尽即止）",
        "POOL_NOT_FOUND" => "编制资源池不可用",
        "NO_SUPERIOR_ESCALATION" => "无更上级可转请（请求被拒绝）",
        "DUPLICATE_OR_INVALID" => "请求重复或非法",
        _ when code.StartsWith("SCOPE_VIOLATION", StringComparison.Ordinal) =>
            "支援种类超出所属编制资源池范围",
        _ when code.StartsWith("INSUFFICIENT_AVAILABLE", StringComparison.Ordinal) =>
            "可用支援力量不足",
        _ => code,
    };

    private static SupportStatusInfo CreateRejected(string message, ulong tick)
    {
        string? reasonCode = ExtractField(message, "reason");
        string code = reasonCode ?? "UNKNOWN";
        string reasonText = ToChineseReason(code);
        return new SupportStatusInfo(
            SupportRequestStatus.Rejected,
            $"请求被拒绝：{reasonText}（{code}）",
            ExtractRequestId(message),
            code,
            reasonText,
            tick);
    }

    private static string? ExtractRequestId(string message)
    {
        const string marker = "request=";
        int start = message.IndexOf(marker, StringComparison.Ordinal);
        if (start < 0)
        {
            return null;
        }

        start += marker.Length;
        int end = message.IndexOf(' ', start);
        return end < 0 ? message[start..] : message[start..end];
    }

    private static string? ExtractField(string message, string field)
    {
        string marker = field + "=";
        int start = message.IndexOf(marker, StringComparison.Ordinal);
        if (start < 0)
        {
            return null;
        }

        start += marker.Length;
        int end = message.IndexOf(' ', start);
        return end < 0 ? message[start..] : message[start..end];
    }
}
