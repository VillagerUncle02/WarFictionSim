// 文件总览：事件日志 —— 环形保留/过滤/搜索/关键置顶（T042）。
//
// 保留与过滤语义镜像 native event_log.cpp：环形容量（默认 5000）、满员驱逐
// "最旧非关键 → 最旧"、查询按 seq 升序、过滤可组合；关键置顶是表现层
// 排序（不动保留集合）。核心快照只给计数摘要（size/capacity/critical_count），
// 事件正文经独立供给通道进入本面板（见最终报告 TODO）。

using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using WarFictionSim.Ui.Interop;

namespace WarFictionSim.Ui.EventLogPanel;

/// <summary>事件日志面板视图模型。</summary>
public sealed partial class EventLogViewModel : ObservableObject
{
    private readonly List<SimEventDto> _retained = [];
    private int _criticalCount;
    private bool _pinCriticalEvents;
    private SimEventCategory? _selectedCategory;
    private SimEventSeverity? _minSeverity;
    private string _searchText = string.Empty;
    private EventLogSummaryState? _nativeSummary;

    /// <summary>初始化日志（默认 5000 条环形，与核心一致）。</summary>
    /// <param name="capacity">保留上限（≥1）。</param>
    public EventLogViewModel(int capacity = 5000)
    {
        if (capacity < 1)
        {
            throw new ArgumentOutOfRangeException(nameof(capacity), "日志容量必须 ≥ 1。");
        }

        Capacity = capacity;
    }

    /// <summary>过滤/置顶后的展示条目。</summary>
    public ObservableCollection<EventLogEntryViewModel> Entries { get; } = [];

    /// <summary>环形保留容量。</summary>
    public int Capacity { get; }

    /// <summary>当前保留集合中的关键事件数。</summary>
    public int CriticalCount => _criticalCount;

    /// <summary>当前保留集合大小。</summary>
    public int RetainedCount => _retained.Count;

    /// <summary>是否把关键事件置顶显示（表现层排序，不改保留集合）。</summary>
    public bool PinCriticalEvents
    {
        get => _pinCriticalEvents;
        set
        {
            if (SetProperty(ref _pinCriticalEvents, value))
            {
                Refresh();
            }
        }
    }

    /// <summary>分类过滤（null = 全部）。</summary>
    public SimEventCategory? SelectedCategory
    {
        get => _selectedCategory;
        set
        {
            if (SetProperty(ref _selectedCategory, value))
            {
                Refresh();
            }
        }
    }

    /// <summary>最低严重级过滤（null = 全部）。</summary>
    public SimEventSeverity? MinSeverity
    {
        get => _minSeverity;
        set
        {
            if (SetProperty(ref _minSeverity, value))
            {
                Refresh();
            }
        }
    }

    /// <summary>文本搜索（子串匹配，区分大小写，与核心一致）。</summary>
    public string SearchText
    {
        get => _searchText;
        set
        {
            if (SetProperty(ref _searchText, value ?? string.Empty))
            {
                Refresh();
            }
        }
    }

    /// <summary>核心日志计数摘要文案（快照提供，面板状态栏显示）。</summary>
    public string NativeSummaryText => _nativeSummary is { } summary
        ? $"核心日志已记录 {summary.Size} 条（关键 {summary.CriticalCount}，容量 {summary.Capacity}）"
        : "核心日志计数尚未同步";

    /// <summary>全部分类（过滤下拉用）。</summary>
    public IReadOnlyList<SimEventCategory> CategoryOptions => SimEventCategoryParser.All;

    /// <summary>全部严重级（过滤下拉用）。</summary>
    public IReadOnlyList<SimEventSeverity> SeverityOptions => SimEventSeverityParser.All;

    /// <summary>追加一条事件（环形保留 + 关键优先驱逐 + 刷新展示）。</summary>
    /// <param name="event">事件。</param>
    public void Append(SimEventDto @event)
    {
        if (_retained.Count >= Capacity)
        {
            // 与核心一致：只要存在非关键事件，新事件就不驱逐关键事件；
            // 全部关键时才驱逐最旧。
            int evictionIndex = _retained.FindIndex(retained => retained.Severity != SimEventSeverity.Critical);
            if (evictionIndex < 0)
            {
                evictionIndex = 0;
            }

            if (_retained[evictionIndex].Severity == SimEventSeverity.Critical)
            {
                _criticalCount--;
            }

            _retained.RemoveAt(evictionIndex);
        }

        _retained.Add(@event);
        if (@event.Severity == SimEventSeverity.Critical)
        {
            _criticalCount++;
        }

        Refresh();
    }

    /// <summary>批量追加事件（按给定顺序，逐条走保留语义）。</summary>
    /// <param name="events">事件序列。</param>
    public void AppendRange(IEnumerable<SimEventDto> events)
    {
        foreach (SimEventDto @event in events)
        {
            Append(@event);
        }
    }

    /// <summary>同步核心快照的日志计数摘要（只更新状态栏，不动本地集合）。</summary>
    /// <param name="summary">快照中的 event_log 摘要。</param>
    public void ApplySummary(EventLogSummaryState summary)
    {
        _nativeSummary = summary;
        OnPropertyChanged(nameof(NativeSummaryText));
    }

    private void Refresh()
    {
        Entries.Clear();
        EventLogFilter filter = new(SelectedCategory, MinSeverity, SearchText);
        IEnumerable<SimEventDto> query = _retained.Where(filter.Matches);
        query = PinCriticalEvents
            ? query.OrderBy(@event => @event.Severity == SimEventSeverity.Critical ? 0 : 1).ThenBy(@event => @event.Seq)
            : query.OrderBy(@event => @event.Seq);
        foreach (SimEventDto @event in query)
        {
            Entries.Add(new EventLogEntryViewModel(@event));
        }
    }
}
