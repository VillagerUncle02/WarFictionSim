// 文件总览：事件日志 —— 事件 DTO 与枚举解析（T042）。
//
// 镜像 native event_log.h 的 SimEvent 结构与稳定名称（command/combat/intel/
// mission/logistics/system、debug/info/warning/critical）；未知名称显式报错
// （宪法第 17 条），与核心的 event_*_from_string 行为一致。

namespace WarFictionSim.Ui.EventLogPanel;

/// <summary>事件分类（与核心 EventCategory 一致）。</summary>
public enum SimEventCategory
{
    /// <summary>命令链路（下达/确认/执行）。</summary>
    Command,

    /// <summary>战斗结算。</summary>
    Combat,

    /// <summary>情报（发现/识别/同步）。</summary>
    Intel,

    /// <summary>任务（完成/失败/超时/取消）。</summary>
    Mission,

    /// <summary>后勤。</summary>
    Logistics,

    /// <summary>系统。</summary>
    System,
}

/// <summary>事件严重级（升序，用于最低严重级过滤）。</summary>
public enum SimEventSeverity
{
    /// <summary>调试。</summary>
    Debug,

    /// <summary>信息。</summary>
    Info,

    /// <summary>警告。</summary>
    Warning,

    /// <summary>关键（置顶与保留优先）。</summary>
    Critical,
}

/// <summary>一条模拟事件（只读）。</summary>
/// <param name="Seq">全局单调序号。</param>
/// <param name="Tick">事件发生的游戏 tick。</param>
/// <param name="Category">分类。</param>
/// <param name="Severity">严重级。</param>
/// <param name="Message">事件正文。</param>
public sealed record SimEventDto(ulong Seq, ulong Tick, SimEventCategory Category, SimEventSeverity Severity, string Message);

/// <summary>事件分类名称解析与中文显示。</summary>
public static class SimEventCategoryParser
{
    /// <summary>全部分类（过滤下拉用）。</summary>
    public static readonly IReadOnlyList<SimEventCategory> All =
        Enum.GetValues<SimEventCategory>();

    /// <summary>解析核心稳定名称；未知名称抛错（宪法第 17 条）。</summary>
    /// <param name="name">command/combat/intel/mission/logistics/system。</param>
    /// <returns>分类枚举。</returns>
    public static SimEventCategory Parse(string name) => name switch
    {
        "command" => SimEventCategory.Command,
        "combat" => SimEventCategory.Combat,
        "intel" => SimEventCategory.Intel,
        "mission" => SimEventCategory.Mission,
        "logistics" => SimEventCategory.Logistics,
        "system" => SimEventCategory.System,
        _ => throw new ArgumentException($"未知事件分类：{name}", nameof(name)),
    };

    /// <summary>返回分类的中文显示名。</summary>
    /// <param name="category">分类。</param>
    /// <returns>中文名。</returns>
    public static string ToDisplayName(this SimEventCategory category) => category switch
    {
        SimEventCategory.Command => "命令",
        SimEventCategory.Combat => "战斗",
        SimEventCategory.Intel => "情报",
        SimEventCategory.Mission => "任务",
        SimEventCategory.Logistics => "后勤",
        SimEventCategory.System => "系统",
        _ => "未知",
    };

    /// <summary>返回分类的核心稳定名称（构造 wfs_sim_query_events 查询用）。</summary>
    /// <param name="category">分类。</param>
    /// <returns>稳定名称。</returns>
    public static string ToNativeName(this SimEventCategory category) => category switch
    {
        SimEventCategory.Command => "command",
        SimEventCategory.Combat => "combat",
        SimEventCategory.Intel => "intel",
        SimEventCategory.Mission => "mission",
        SimEventCategory.Logistics => "logistics",
        SimEventCategory.System => "system",
        _ => throw new ArgumentOutOfRangeException(nameof(category), category, null),
    };
}

/// <summary>事件严重级名称解析与中文显示。</summary>
public static class SimEventSeverityParser
{
    /// <summary>全部严重级（过滤下拉用）。</summary>
    public static readonly IReadOnlyList<SimEventSeverity> All =
        Enum.GetValues<SimEventSeverity>();

    /// <summary>解析核心稳定名称；未知名称抛错（宪法第 17 条）。</summary>
    /// <param name="name">debug/info/warning/critical。</param>
    /// <returns>严重级枚举。</returns>
    public static SimEventSeverity Parse(string name) => name switch
    {
        "debug" => SimEventSeverity.Debug,
        "info" => SimEventSeverity.Info,
        "warning" => SimEventSeverity.Warning,
        "critical" => SimEventSeverity.Critical,
        _ => throw new ArgumentException($"未知事件严重级：{name}", nameof(name)),
    };

    /// <summary>返回严重级的中文显示名。</summary>
    /// <param name="severity">严重级。</param>
    /// <returns>中文名。</returns>
    public static string ToDisplayName(this SimEventSeverity severity) => severity switch
    {
        SimEventSeverity.Debug => "调试",
        SimEventSeverity.Info => "信息",
        SimEventSeverity.Warning => "警告",
        SimEventSeverity.Critical => "关键",
        _ => "未知",
    };

    /// <summary>返回严重级的核心稳定名称（构造 wfs_sim_query_events 查询用）。</summary>
    /// <param name="severity">严重级。</param>
    /// <returns>稳定名称。</returns>
    public static string ToNativeName(this SimEventSeverity severity) => severity switch
    {
        SimEventSeverity.Debug => "debug",
        SimEventSeverity.Info => "info",
        SimEventSeverity.Warning => "warning",
        SimEventSeverity.Critical => "critical",
        _ => throw new ArgumentOutOfRangeException(nameof(severity), severity, null),
    };
}
