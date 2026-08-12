// 文件总览：事件日志 —— 真实事件查询消费、过滤/搜索/关键置顶（T042）。
//
// 单一事实源是核心 EventLog：面板不再镜像环形驱逐，事件正文经
// ISimClient.QueryEvents（wfs_sim_query_events）在快照更新时按需拉取，
// 默认取最近 500 条窗口；分类/严重级/文本/单位过滤下推给 native 查询
// （text 与 unit_id 同时给定取交集），游戏时间过滤在窗口内本地执行
// （native 查询契约无 tick 字段）；关键事件=severity critical 置顶；
// 状态栏计数仍来自快照 summary。
//
// 刷新策略（N1）：不再每 50ms 全量拉取+全量重建。运行中快照 tick 前进
// 时按 RefreshThrottle（250ms）节流查询，被节流的 tick 前进记挂起、到期
// 补拉；过滤条件变化立即重查。查询契约无 seq 范围，因此按"最近窗口 +
// 按 seq 本地去重"增量合并：新事件尾部追加、头部驱逐、既有条目实例复用
// （不 Clear+Add，滚动不复位）；必须全量重建（过滤/置顶变化、放宽本地
// 时间过滤）时先外发滚动锚点（重建前最后一个展示条目的 seq），视图据此
// 恢复滚动位置。查询失败重试一次，仍失败显示中文错误并保留上次成功窗口，
// 不向渲染循环抛异常。
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

    /// <summary>运行中快照 tick 前进时的刷新节流间隔（≥ 该间隔才重新查询，N1）。</summary>
    public static readonly TimeSpan RefreshThrottle = TimeSpan.FromMilliseconds(250);

    private readonly ISimClient? _client;
    private readonly double _tickHz;
    private readonly TimeProvider _timeProvider;
    private IReadOnlyList<SimEventDto> _window = [];
    private EventLogSummaryState? _nativeSummary;
    private ulong? _lastSyncedTick;
    private ulong _lastSnapshotTick;
    private DateTimeOffset _lastPullAt = DateTimeOffset.MinValue;
    private bool _pullPending;
    private bool _fullRebuild = true;
    private bool _pinCriticalEvents;
    private SimEventCategory? _selectedCategory;
    private SimEventSeverity? _minSeverity;
    private string _searchText = string.Empty;
    private string _unitIdText = string.Empty;
    private string _sinceTickText = string.Empty;
    private ulong? _sinceTick;
    private string? _gameTimeFilterError;
    private string? _queryErrorText;

    /// <summary>初始化日志面板。</summary>
    /// <param name="client">模拟客户端（生产注入；null 时只更新状态栏计数）。</param>
    /// <param name="tickHz">场景 tick 频率（游戏时间折算用，不复硬编码 20Hz）。</param>
    /// <param name="timeProvider">现实时间源（节流测试注入；默认系统时钟）。</param>
    public EventLogViewModel(
        ISimClient? client = null,
        double tickHz = TimeScales.DefaultTickHz,
        TimeProvider? timeProvider = null)
    {
        if (tickHz < 1)
        {
            throw new ArgumentOutOfRangeException(nameof(tickHz), "tick 频率必须 ≥ 1。");
        }

        _client = client;
        _tickHz = tickHz;
        _timeProvider = timeProvider ?? TimeProvider.System;
    }

    /// <summary>过滤/置顶后的展示条目。</summary>
    public ObservableCollection<EventLogEntryViewModel> Entries { get; } = [];

    /// <summary>全量重建开始前触发；参数为重建前最后一个展示条目的 seq（视图恢复滚动锚点）。</summary>
    public event EventHandler<ulong?>? ScrollAnchorChanged;

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

    /// <summary>查询失败后的中文错误提示（为空表示最近一次查询成功）。</summary>
    public string? QueryErrorText
    {
        get => _queryErrorText;
        private set => SetProperty(ref _queryErrorText, value);
    }

    /// <summary>核心日志计数摘要文案（快照提供，面板状态栏显示）。</summary>
    public string NativeSummaryText => _nativeSummary is { } summary
        ? $"核心日志已记录 {summary.Size} 条（关键 {summary.CriticalCount}，容量 {summary.Capacity}）"
        : "核心日志计数尚未同步";

    /// <summary>全部分类（过滤下拉用）。</summary>
    public IReadOnlyList<SimEventCategory> CategoryOptions => SimEventCategoryParser.All;

    /// <summary>全部严重级（过滤下拉用）。</summary>
    public IReadOnlyList<SimEventSeverity> SeverityOptions => SimEventSeverityParser.All;

    /// <summary>同步快照日志摘要；快照 tick 前进时按节流间隔从核心拉取事件正文。</summary>
    /// <param name="summary">快照中的 event_log 摘要（状态栏计数）。</param>
    /// <param name="snapshotTick">快照所属游戏 tick（用于按需拉取与节流）。</param>
    public void ApplySummary(EventLogSummaryState summary, ulong snapshotTick)
    {
        _nativeSummary = summary;
        OnPropertyChanged(nameof(NativeSummaryText));
        if (_client is null)
        {
            return;
        }

        _lastSnapshotTick = snapshotTick;
        if (_lastSyncedTick == snapshotTick)
        {
            // 同一 tick：仅当上一帧 tick 前进被节流挂起时，到期补拉一次（N1）。
            if (_pullPending && _timeProvider.GetUtcNow() - _lastPullAt >= RefreshThrottle)
            {
                PullEvents();
                _lastSyncedTick = snapshotTick;
                _pullPending = false;
            }

            return;
        }

        // tick 前进：距上次拉取不足节流间隔则挂起，到期再拉（不再每帧全量拉取）。
        if (_timeProvider.GetUtcNow() - _lastPullAt < RefreshThrottle)
        {
            _pullPending = true;
            return;
        }

        PullEvents();
        _lastSyncedTick = snapshotTick;
        _pullPending = false;
    }

    private void InvalidateAndPull()
    {
        // 过滤条件变化必须立即重查（即使快照 tick 未前进），并标记全量重建。
        _fullRebuild = true;
        _lastSyncedTick = _lastSnapshotTick;
        _pullPending = false;
        PullEvents();
    }

    private void PullEvents()
    {
        _lastPullAt = _timeProvider.GetUtcNow();
        if (_client is null)
        {
            _window = [];
            RebuildEntries([]);
            return;
        }

        try
        {
            IReadOnlyList<SimEventDto> events = QueryWindowWithRetry();
            QueryErrorText = null;
            _window = events.Count > DefaultWindowSize
                ? events.TakeLast(DefaultWindowSize).ToList()
                : events;
            UpdateEntries();
        }
        catch (ObjectDisposedException)
        {
            // 返回主菜单/关窗竞态：保留上次窗口，不向渲染循环抛异常（N1）。
            QueryErrorText = "模拟核心已释放：事件日志查询已停止。";
        }
        catch (Exception exception)
        {
            // 重试一次后仍失败：保留上次成功窗口并给出中文错误，绝不崩溃（N1）。
            QueryErrorText = $"查询事件日志失败：{exception.Message}（已重试一次）";
        }
    }

    /// <summary>查询并解析窗口；失败自动重试一次，两次都失败则抛出最后一次异常。</summary>
    private IReadOnlyList<SimEventDto> QueryWindowWithRetry()
    {
        Exception? lastError = null;
        for (int attempt = 0; attempt < 2; attempt++)
        {
            try
            {
                string json = _client!.QueryEvents(BuildNativeQuery());
                return EventQueryReader.Parse(json).Events;
            }
            catch (Exception exception)
            {
                lastError = exception;
            }
        }

        throw lastError ?? new InvalidOperationException("事件查询失败且无异常详情。");
    }

    /// <summary>把新拉取窗口并入展示集合：增量合并，或（过滤/置顶变化时）带锚点全量重建。</summary>
    private void UpdateEntries()
    {
        IEnumerable<SimEventDto> ordered = BuildDisplayOrder(_window);
        if (_fullRebuild || PinCriticalEvents)
        {
            RebuildEntries(ordered);
            _fullRebuild = false;
            return;
        }

        MergeIncrementally(ordered);
    }

    private IEnumerable<SimEventDto> BuildDisplayOrder(IReadOnlyList<SimEventDto> window)
    {
        IEnumerable<SimEventDto> query = SinceTick is { } since
            ? window.Where(item => item.Tick >= since)
            : window;
        return PinCriticalEvents
            ? query.OrderBy(item => item.Severity == SimEventSeverity.Critical ? 0 : 1).ThenBy(item => item.Seq)
            : query.OrderBy(item => item.Seq);
    }

    /// <summary>按 seq 增量合并（无 seq 范围的查询用最近窗口本地去重，N1）。</summary>
    private void MergeIncrementally(IEnumerable<SimEventDto> ordered)
    {
        List<SimEventDto> target = ordered.ToList();
        HashSet<ulong> targetSeqs = target.Select(item => item.Seq).ToHashSet();
        for (int index = Entries.Count - 1; index >= 0; index--)
        {
            if (!targetSeqs.Contains(Entries[index].Seq))
            {
                Entries.RemoveAt(index);
            }
        }

        HashSet<ulong> existingSeqs = Entries.Select(item => item.Seq).ToHashSet();
        List<SimEventDto> missing = target.Where(item => !existingSeqs.Contains(item.Seq)).ToList();
        ulong? maxExistingSeq = Entries.Count > 0 ? Entries[^1].Seq : null;
        if (missing.Any(item => maxExistingSeq is not null && item.Seq <= maxExistingSeq.Value))
        {
            // 放宽本地时间过滤使历史条目回归中间位置：退化为带滚动锚点的全量重建。
            RebuildEntries(target);
            return;
        }

        foreach (SimEventDto item in missing)
        {
            Entries.Add(new EventLogEntryViewModel(item, _tickHz));
        }
    }

    /// <summary>清空并重建展示集合；重建前外发滚动锚点（重建前最后一个展示条目）。</summary>
    private void RebuildEntries(IEnumerable<SimEventDto> ordered)
    {
        ulong? anchor = Entries.LastOrDefault()?.Seq;
        ScrollAnchorChanged?.Invoke(this, anchor);
        Entries.Clear();
        foreach (SimEventDto item in ordered)
        {
            Entries.Add(new EventLogEntryViewModel(item, _tickHz));
        }
    }

    /// <summary>本地重新过滤/排序当前窗口（不重新查询核心）。</summary>
    private void Refresh()
    {
        RebuildEntries(BuildDisplayOrder(_window));
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
