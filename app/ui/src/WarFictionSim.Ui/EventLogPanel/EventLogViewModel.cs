// 文件总览：事件日志 —— 真实事件查询消费、过滤/搜索/关键置顶（T042）。
//
// 单一事实源是核心 EventLog：面板不再镜像环形驱逐，事件正文经
// ISimClient.QueryEvents（wfs_sim_query_events）在快照更新时按需拉取，
// 默认取最近 500 条窗口；分类/严重级/文本/单位过滤下推给 native 查询
// （text 与 unit_id 同时给定取交集），游戏时间过滤在窗口内本地执行
// （native 查询契约无 tick 字段）；关键事件=severity critical 置顶；
// 状态栏计数仍来自快照 summary。
// TODO(F1 后续)：native limit 语义为"取最旧前缀"，"最近 N"目前靠本地
// 对未受限查询取尾实现；核心事件日志容量有界（默认 5000），传输可接受。

using System.Collections.ObjectModel;
using System.Globalization;
using System.IO;
using System.Text;
using System.Text.Encodings.Web;
using System.Text.Json;
using CommunityToolkit.Mvvm.ComponentModel;
using WarFictionSim.Ui.GameControls;
using WarFictionSim.Ui.Interop;

namespace WarFictionSim.Ui.EventLogPanel;

/// <summary>事件日志面板视图模型。</summary>
public sealed partial class EventLogViewModel : ObservableObject
{
    /// <summary>默认展示窗口：最近 500 条（与"回看"用途匹配，避免每帧全量渲染）。</summary>
    public const int DefaultWindowSize = 500;

    private readonly ISimClient? _client;
    private readonly double _tickHz;
    private IReadOnlyList<SimEventDto> _window = [];
    private EventLogSummaryState? _nativeSummary;
    private ulong? _lastSyncedTick;
    private bool _pinCriticalEvents;
    private SimEventCategory? _selectedCategory;
    private SimEventSeverity? _minSeverity;
    private string _searchText = string.Empty;
    private string _unitIdText = string.Empty;
    private string _sinceTickText = string.Empty;
    private ulong? _sinceTick;
    private string? _gameTimeFilterError;

    /// <summary>初始化日志面板。</summary>
    /// <param name="client">模拟客户端（生产注入；null 时只更新状态栏计数）。</param>
    /// <param name="tickHz">场景 tick 频率（游戏时间折算用，不复硬编码 20Hz）。</param>
    public EventLogViewModel(ISimClient? client = null, double tickHz = TimeScales.DefaultTickHz)
    {
        if (tickHz < 1)
        {
            throw new ArgumentOutOfRangeException(nameof(tickHz), "tick 频率必须 ≥ 1。");
        }

        _client = client;
        _tickHz = tickHz;
    }

    /// <summary>过滤/置顶后的展示条目。</summary>
    public ObservableCollection<EventLogEntryViewModel> Entries { get; } = [];

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

    /// <summary>分类过滤（null = 全部；下推给 native 查询）。</summary>
    public SimEventCategory? SelectedCategory
    {
        get => _selectedCategory;
        set
        {
            if (SetProperty(ref _selectedCategory, value))
            {
                InvalidateAndPull();
            }
        }
    }

    /// <summary>最低严重级过滤（null = 全部；下推给 native 查询）。</summary>
    public SimEventSeverity? MinSeverity
    {
        get => _minSeverity;
        set
        {
            if (SetProperty(ref _minSeverity, value))
            {
                InvalidateAndPull();
            }
        }
    }

    /// <summary>文本搜索（子串匹配，区分大小写；下推给 native 查询）。</summary>
    public string SearchText
    {
        get => _searchText;
        set
        {
            if (SetProperty(ref _searchText, value ?? string.Empty))
            {
                InvalidateAndPull();
            }
        }
    }

    /// <summary>单位筛选输入（消息子串匹配，区分大小写；与文本搜索取交集；下推为 unit_id）。</summary>
    public string UnitIdText
    {
        get => _unitIdText;
        set
        {
            if (SetProperty(ref _unitIdText, value ?? string.Empty))
            {
                InvalidateAndPull();
            }
        }
    }

    /// <summary>起始游戏时间筛选输入文本（tick，空 = 全部；非法输入内联提示）。</summary>
    public string SinceTickText
    {
        get => _sinceTickText;
        set
        {
            if (SetProperty(ref _sinceTickText, value ?? string.Empty))
            {
                ParseSinceTick();
            }
        }
    }

    /// <summary>已解析的起始游戏时间（tick；null = 不过滤）。</summary>
    public ulong? SinceTick => _sinceTick;

    /// <summary>游戏时间输入的内联错误提示（为空表示输入合法）。</summary>
    public string? GameTimeFilterError
    {
        get => _gameTimeFilterError;
        private set => SetProperty(ref _gameTimeFilterError, value);
    }

    /// <summary>核心日志计数摘要文案（快照提供，面板状态栏显示）。</summary>
    public string NativeSummaryText => _nativeSummary is { } summary
        ? $"核心日志已记录 {summary.Size} 条（关键 {summary.CriticalCount}，容量 {summary.Capacity}）"
        : "核心日志计数尚未同步";

    /// <summary>全部分类（过滤下拉用）。</summary>
    public IReadOnlyList<SimEventCategory> CategoryOptions => SimEventCategoryParser.All;

    /// <summary>全部严重级（过滤下拉用）。</summary>
    public IReadOnlyList<SimEventSeverity> SeverityOptions => SimEventSeverityParser.All;

    /// <summary>同步快照日志摘要；快照 tick 前进时按需从核心拉取事件正文。</summary>
    /// <param name="summary">快照中的 event_log 摘要（状态栏计数）。</param>
    /// <param name="snapshotTick">快照所属游戏 tick（用于按需拉取）。</param>
    public void ApplySummary(EventLogSummaryState summary, ulong snapshotTick)
    {
        _nativeSummary = summary;
        OnPropertyChanged(nameof(NativeSummaryText));
        if (_client is not null && _lastSyncedTick != snapshotTick)
        {
            PullEvents();
            _lastSyncedTick = snapshotTick;
        }
    }

    private void InvalidateAndPull()
    {
        // 过滤条件变化必须立即重查（即使快照 tick 未前进）。
        _lastSyncedTick = null;
        PullEvents();
    }

    private void PullEvents()
    {
        if (_client is null)
        {
            _window = [];
            Refresh();
            return;
        }

        EventQueryResponse response = EventQueryReader.Parse(_client.QueryEvents(BuildNativeQuery()));
        IReadOnlyList<SimEventDto> events = response.Events;
        _window = events.Count > DefaultWindowSize
            ? events.TakeLast(DefaultWindowSize).ToList()
            : events;
        Refresh();
    }

    private string BuildNativeQuery()
    {
        using var stream = new MemoryStream();
        // 中文按 UTF-8 原样输出（不转义为 \uXXXX）：与 CommandJsonBuilder 及
        // 核心 nlohmann::json dump 行为一致，便于测试与排查。
        using (var writer = new Utf8JsonWriter(stream, new JsonWriterOptions
        {
            Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
        }))
        {
            writer.WriteStartObject();
            if (SelectedCategory is { } category)
            {
                writer.WriteString("category", SimEventCategoryParser.ToNativeName(category));
            }

            if (MinSeverity is { } severity)
            {
                writer.WriteString("min_severity", SimEventSeverityParser.ToNativeName(severity));
            }

            if (!string.IsNullOrEmpty(SearchText))
            {
                writer.WriteString("text", SearchText);
            }

            if (!string.IsNullOrEmpty(UnitIdText))
            {
                writer.WriteString("unit_id", UnitIdText);
            }

            writer.WriteEndObject();
        }

        return Encoding.UTF8.GetString(stream.ToArray());
    }

    private void Refresh()
    {
        Entries.Clear();
        IEnumerable<SimEventDto> query = _window;
        if (SinceTick is { } since)
        {
            query = query.Where(item => item.Tick >= since);
        }

        query = PinCriticalEvents
            ? query.OrderBy(item => item.Severity == SimEventSeverity.Critical ? 0 : 1).ThenBy(item => item.Seq)
            : query.OrderBy(item => item.Seq);
        foreach (SimEventDto item in query)
        {
            Entries.Add(new EventLogEntryViewModel(item, _tickHz));
        }
    }

    private void ParseSinceTick()
    {
        if (string.IsNullOrWhiteSpace(_sinceTickText))
        {
            _sinceTick = null;
            GameTimeFilterError = null;
            Refresh();
            return;
        }

        if (ulong.TryParse(_sinceTickText.Trim(), NumberStyles.None, CultureInfo.InvariantCulture, out ulong value))
        {
            _sinceTick = value;
            GameTimeFilterError = null;
            Refresh();
            return;
        }

        GameTimeFilterError = $"“{_sinceTickText}”不是有效游戏时间（tick 须为非负整数）。";
        Refresh(); // 保留上一次有效的本地过滤。
    }
}
