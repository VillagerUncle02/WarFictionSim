// 文件总览：事件日志 —— 过滤条件（T042）。
//
// 过滤语义镜像 native EventLog.query：分类精确匹配、最低严重级（枚举升序
// 比较）、文本子串匹配（区分大小写，与核心一致）；三条件可组合，空条件
// 表示不过滤。

namespace WarFictionSim.Ui.EventLogPanel;

/// <summary>事件日志过滤条件（只读）。</summary>
/// <param name="Category">分类过滤（null = 全部）。</param>
/// <param name="MinSeverity">最低严重级（null = 全部）。</param>
/// <param name="Text">文本子串（空 = 全部，区分大小写）。</param>
public sealed record EventLogFilter(SimEventCategory? Category, SimEventSeverity? MinSeverity, string? Text)
{
    /// <summary>不过滤的默认条件。</summary>
    public static readonly EventLogFilter Empty = new(null, null, null);

    /// <summary>判断事件是否满足本过滤条件。</summary>
    /// <param name="event">事件。</param>
    /// <returns>满足返回 <see langword="true"/>。</returns>
    public bool Matches(SimEventDto @event) =>
        (Category is null || @event.Category == Category) &&
        (MinSeverity is null || @event.Severity >= MinSeverity) &&
        (string.IsNullOrEmpty(Text) || @event.Message.Contains(Text, StringComparison.Ordinal));
}
