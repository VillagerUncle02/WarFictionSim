// 文件总览：事件日志 —— 单条日志的视图模型（T042）。
//
// 纯展示投影：游戏时间戳统一用游戏 tick（FR-044：加速/暂停不改变日志时序
// 语义，现实时间仅在表现层附加显示）；tick → 现实时间折算使用场景 tick_hz
// （不复硬编码 20Hz），关键事件标记供置顶与样式使用。

using WarFictionSim.Ui.GameControls;

namespace WarFictionSim.Ui.EventLogPanel;

/// <summary>事件日志条目视图模型（只读）。</summary>
public sealed class EventLogEntryViewModel
{
    private const double SecondsPerMinute = 60.0;

    /// <summary>初始化条目。</summary>
    /// <param name="source">事件数据。</param>
    /// <param name="tickHz">场景 tick 频率（Hz）。</param>
    public EventLogEntryViewModel(SimEventDto source, double tickHz)
    {
        Seq = source.Seq;
        Tick = source.Tick;
        Category = source.Category;
        Severity = source.Severity;
        Message = source.Message;
        IsCritical = source.Severity == SimEventSeverity.Critical;
        CategoryText = source.Category.ToDisplayName();
        SeverityText = source.Severity.ToDisplayName();
        GameTimeText = FormatGameTime(source.Tick, tickHz);
    }

    /// <summary>全局单调序号。</summary>
    public ulong Seq { get; }

    /// <summary>游戏 tick。</summary>
    public ulong Tick { get; }

    /// <summary>分类。</summary>
    public SimEventCategory Category { get; }

    /// <summary>严重级。</summary>
    public SimEventSeverity Severity { get; }

    /// <summary>事件正文。</summary>
    public string Message { get; }

    /// <summary>是否为关键事件。</summary>
    public bool IsCritical { get; }

    /// <summary>分类中文名。</summary>
    public string CategoryText { get; }

    /// <summary>严重级中文名。</summary>
    public string SeverityText { get; }

    /// <summary>游戏时间（mm:ss，按场景 tick_hz 折算；纯展示）。</summary>
    public string GameTimeText { get; }

    private static string FormatGameTime(ulong tick, double tickHz)
    {
        double totalSeconds = tick / tickHz;
        int minutes = (int)(totalSeconds / SecondsPerMinute);
        int seconds = (int)(totalSeconds % SecondsPerMinute);
        return $"{minutes:00}:{seconds:00}";
    }
}
