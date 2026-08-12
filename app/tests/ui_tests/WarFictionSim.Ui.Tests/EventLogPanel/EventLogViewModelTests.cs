// 测试：事件日志 —— 真实事件查询消费、过滤/搜索/置顶（T042 + FR-044 数据通道）。
//
// 面板不再镜像核心的环形驱逐：事件正文经 ISimClient.QueryEvents 从
// wfs_sim_query_events 拉取（快照更新时按需、默认最近 500 条窗口），
// 分类/严重级/文本/单位过滤下推给 native 查询（text 与 unit_id 取交集），
// 游戏时间过滤在窗口内本地执行，关键事件=severity critical 置顶；状态栏
// 计数仍来自快照 summary。刷新策略（N1）：tick 前进按 250ms 节流、按 seq
// 增量合并去重、全量重建外发滚动锚点、查询失败重试一次后显示中文错误。

using WarFictionSim.Ui.EventLogPanel;
using WarFictionSim.Ui.Interop;
using WarFictionSim.Ui.Tests.TestDoubles;
using Xunit;

namespace WarFictionSim.Ui.Tests.EventLogPanel;

public class EventLogViewModelTests
{
    private static SimEventDto Event(
        ulong seq, ulong tick = 100, SimEventCategory category = SimEventCategory.Combat,
        SimEventSeverity severity = SimEventSeverity.Info, string message = "测试事件") =>
        new(seq, tick, category, severity, message);

    private static FakeSimClient Client(params SimEventDto[] events)
    {
        var client = new FakeSimClient(SnapshotFactory.Create(0, []));
        client.Events.AddRange(events);
        return client;
    }

    [Fact]
    public void ApplySummary_PullsLatestWindowFromClient()
    {
        List<SimEventDto> events = Enumerable.Range(0, 600).Select(seq => Event((ulong)seq)).ToList();
        var client = Client([.. events]);
        var viewModel = new EventLogViewModel(client);

        viewModel.ApplySummary(new EventLogSummaryState(600, 5000, 0), snapshotTick: 10);

        // 最近 500 条（seq 100..599），按 seq 升序展示。
        Assert.Equal(Enumerable.Range(100, 500).Select(seq => (ulong)seq), viewModel.Entries.Select(entry => entry.Seq));
        Assert.Single(client.EventQueries);
    }

    [Fact]
    public void ApplySummary_SameSnapshotTick_DoesNotRepull()
    {
        var client = Client(Event(1));
        var viewModel = new EventLogViewModel(client);

        viewModel.ApplySummary(new EventLogSummaryState(1, 5000, 0), snapshotTick: 5);
        viewModel.ApplySummary(new EventLogSummaryState(1, 5000, 0), snapshotTick: 5);

        Assert.Single(client.EventQueries);
    }

    [Fact]
    public void ApplySummary_AdvancedTick_AfterThrottleInterval_RepullsEvents()
    {
        var client = Client(Event(1));
        var timeProvider = new MutableTimeProvider();
        var viewModel = new EventLogViewModel(client, timeProvider: timeProvider);

        viewModel.ApplySummary(new EventLogSummaryState(1, 5000, 0), snapshotTick: 5);
        timeProvider.Advance(EventLogViewModel.RefreshThrottle);
        viewModel.ApplySummary(new EventLogSummaryState(1, 5000, 0), snapshotTick: 6);

        Assert.Equal(2, client.EventQueries.Count);
    }

    [Fact]
    public void ApplySummary_RapidTickAdvance_IsThrottledAndDrainsPendingPull()
    {
        var client = Client(Event(1));
        var timeProvider = new MutableTimeProvider();
        var viewModel = new EventLogViewModel(client, timeProvider: timeProvider);

        viewModel.ApplySummary(new EventLogSummaryState(1, 5000, 0), snapshotTick: 1);
        viewModel.ApplySummary(new EventLogSummaryState(1, 5000, 0), snapshotTick: 2);
        viewModel.ApplySummary(new EventLogSummaryState(1, 5000, 0), snapshotTick: 3);

        // 节流窗口内不再每帧全量拉取（N1）。
        Assert.Single(client.EventQueries);

        // 被节流挂起的 tick 前进到期补拉（即使 tick 未再前进）。
        timeProvider.Advance(EventLogViewModel.RefreshThrottle);
        viewModel.ApplySummary(new EventLogSummaryState(1, 5000, 0), snapshotTick: 3);

        Assert.Equal(2, client.EventQueries.Count);
    }

    [Fact]
    public void ApplySummary_MergesNewEventsIncrementallyWithoutDuplicates()
    {
        var client = Client(Event(1), Event(2), Event(3));
        var timeProvider = new MutableTimeProvider();
        var viewModel = new EventLogViewModel(client, timeProvider: timeProvider);
        viewModel.ApplySummary(new EventLogSummaryState(3, 5000, 0), snapshotTick: 1);
        EventLogEntryViewModel first = viewModel.Entries[0];
        client.Events.AddRange([Event(4), Event(5)]);

        timeProvider.Advance(EventLogViewModel.RefreshThrottle);
        viewModel.ApplySummary(new EventLogSummaryState(5, 5000, 0), snapshotTick: 2);

        Assert.Equal([1uL, 2uL, 3uL, 4uL, 5uL], viewModel.Entries.Select(entry => entry.Seq));
        Assert.Same(first, viewModel.Entries[0]); // 增量合并：既有条目实例复用，不清空重建。
    }

    [Fact]
    public void ApplySummary_EvictsOldestEntriesFromWindow_WithoutDuplicates()
    {
        List<SimEventDto> events = Enumerable.Range(0, 500).Select(seq => Event((ulong)seq)).ToList();
        var client = Client([.. events]);
        var timeProvider = new MutableTimeProvider();
        var viewModel = new EventLogViewModel(client, timeProvider: timeProvider);
        viewModel.ApplySummary(new EventLogSummaryState(500, 5000, 0), snapshotTick: 1);
        Assert.Equal(500, viewModel.Entries.Count);

        client.Events.AddRange([Event(500), Event(501)]);
        timeProvider.Advance(EventLogViewModel.RefreshThrottle);
        viewModel.ApplySummary(new EventLogSummaryState(502, 5000, 0), snapshotTick: 2);

        // 窗口右移：头部驱逐 seq 0/1，尾部追加 500/501，总数恒定且无重复。
        Assert.Equal(500, viewModel.Entries.Count);
        Assert.Equal(Enumerable.Range(2, 500).Select(seq => (ulong)seq), viewModel.Entries.Select(entry => entry.Seq));
        Assert.Equal(500, viewModel.Entries.Select(entry => entry.Seq).Distinct().Count());
    }

    [Fact]
    public void CategorySeverityTextFilters_AreSentToNativeQuery()
    {
        var client = Client(Event(1));
        var viewModel = new EventLogViewModel(client);

        viewModel.SelectedCategory = SimEventCategory.Combat;
        viewModel.MinSeverity = SimEventSeverity.Info;
        viewModel.SearchText = "接敌";

        string query = Assert.Single(client.EventQueries, JsonContainsAllFilters);
        Assert.Contains("\"category\":\"combat\"", query, StringComparison.Ordinal);
        Assert.Contains("\"min_severity\":\"info\"", query, StringComparison.Ordinal);
        Assert.Contains("\"text\":\"接敌\"", query, StringComparison.Ordinal);
    }

    [Fact]
    public void UnitIdFilter_IsSentToNativeQuery()
    {
        var client = Client(Event(1));
        var viewModel = new EventLogViewModel(client);

        viewModel.UnitIdText = "platoon-1";

        string query = Assert.Single(client.EventQueries);
        Assert.Contains("\"unit_id\":\"platoon-1\"", query, StringComparison.Ordinal);
    }

    [Fact]
    public void UnitIdAndTextFilters_AreCombinedInOneQuery()
    {
        var client = Client(Event(1));
        var viewModel = new EventLogViewModel(client);

        viewModel.UnitIdText = "platoon-1";
        viewModel.SearchText = "接敌";

        // 最后一次查询同时携带 unit_id 与 text（组合下推，交集由核心执行）。
        string query = client.EventQueries[^1];
        Assert.Contains("\"unit_id\":\"platoon-1\"", query, StringComparison.Ordinal);
        Assert.Contains("\"text\":\"接敌\"", query, StringComparison.Ordinal);
    }

    [Fact]
    public void UnitIdAndTextFilters_IntersectNativeResults()
    {
        var client = Client(
            Event(1, message: "unit-a 接敌"),
            Event(2, message: "unit-b 接敌"),
            Event(3, message: "unit-a 撤退"));
        var viewModel = new EventLogViewModel(client);

        viewModel.UnitIdText = "unit-a";
        viewModel.SearchText = "接敌";
        viewModel.ApplySummary(new EventLogSummaryState(3, 5000, 0), snapshotTick: 1);

        // unit_id 与 text 取交集（镜像 native 语义）：只剩同时命中的一条。
        Assert.Equal([1uL], viewModel.Entries.Select(entry => entry.Seq));
    }

    [Fact]
    public void NativeFilteredEvents_AreDisplayedAscending()
    {
        var client = Client(
            Event(1, category: SimEventCategory.Intel, severity: SimEventSeverity.Info, message: "发现目标"),
            Event(2, category: SimEventCategory.Combat, severity: SimEventSeverity.Critical, message: "接敌"),
            Event(3, category: SimEventCategory.Combat, severity: SimEventSeverity.Info, message: "弹药耗尽"));
        var viewModel = new EventLogViewModel(client);
        viewModel.SelectedCategory = SimEventCategory.Combat;
        viewModel.MinSeverity = SimEventSeverity.Info;

        viewModel.ApplySummary(new EventLogSummaryState(3, 5000, 1), snapshotTick: 1);

        Assert.Equal([2uL, 3uL], viewModel.Entries.Select(entry => entry.Seq));
    }

    [Fact]
    public void GameTimeFilter_IsAppliedLocallyWithinPullWindow()
    {
        var client = Client(
            Event(1, tick: 10),
            Event(2, tick: 20),
            Event(3, tick: 30));
        var viewModel = new EventLogViewModel(client);
        viewModel.ApplySummary(new EventLogSummaryState(3, 5000, 0), snapshotTick: 1);

        viewModel.SinceTickText = "20";

        Assert.Equal([2uL, 3uL], viewModel.Entries.Select(entry => entry.Seq));
        // 游戏时间过滤是本地语义：不得下推成 native 查询字段（契约无 tick 过滤）。
        Assert.DoesNotContain(client.EventQueries, query => query.Contains("tick", StringComparison.Ordinal));
    }

    [Fact]
    public void PinCritical_PutsCriticalEventsFirst()
    {
        var client = Client(
            Event(1, severity: SimEventSeverity.Info),
            Event(2, severity: SimEventSeverity.Critical, message: "关键"),
            Event(3, severity: SimEventSeverity.Info));
        var viewModel = new EventLogViewModel(client);
        viewModel.ApplySummary(new EventLogSummaryState(3, 5000, 1), snapshotTick: 1);

        viewModel.PinCriticalEvents = true;

        Assert.Equal([2uL, 1uL, 3uL], viewModel.Entries.Select(entry => entry.Seq));
    }

    [Fact]
    public void FilterChange_RebuildsEntriesAndRaisesScrollAnchor()
    {
        var client = Client(
            Event(1, message: "接敌"),
            Event(2, message: "移动"),
            Event(3, message: "接敌"));
        var viewModel = new EventLogViewModel(client);
        viewModel.ApplySummary(new EventLogSummaryState(3, 5000, 0), snapshotTick: 1);
        ulong? anchor = null;
        viewModel.ScrollAnchorChanged += (_, seq) => anchor = seq;

        viewModel.SearchText = "接敌";

        // 过滤变化触发带滚动锚点的全量重建：锚点 = 重建前最后一个展示条目。
        Assert.Equal((ulong)3, anchor);
        Assert.Equal([1uL, 3uL], viewModel.Entries.Select(entry => entry.Seq));
    }

    [Fact]
    public void QueryFailure_RetriesOnce_ThenRecovers()
    {
        var client = Client(Event(1));
        client.QueryEventsErrors.Enqueue(new InvalidOperationException("瞬断"));
        var viewModel = new EventLogViewModel(client);

        viewModel.ApplySummary(new EventLogSummaryState(1, 5000, 0), snapshotTick: 1);

        Assert.Equal(2, client.EventQueries.Count); // 第一次失败 + 重试成功。
        Assert.Single(viewModel.Entries);
        Assert.Null(viewModel.QueryErrorText);
    }

    [Fact]
    public void QueryFailure_AfterRetry_ShowsChineseErrorAndKeepsLastWindow()
    {
        var client = Client(Event(1));
        var timeProvider = new MutableTimeProvider();
        var viewModel = new EventLogViewModel(client, timeProvider: timeProvider);
        viewModel.ApplySummary(new EventLogSummaryState(1, 5000, 0), snapshotTick: 1);
        Assert.Single(viewModel.Entries);
        client.QueryEventsErrors.Enqueue(new InvalidOperationException("boom"));
        client.QueryEventsErrors.Enqueue(new InvalidOperationException("boom"));

        timeProvider.Advance(EventLogViewModel.RefreshThrottle);
        viewModel.ApplySummary(new EventLogSummaryState(1, 5000, 0), snapshotTick: 2);

        // 重试一次后仍失败：保留上次成功窗口、显示中文错误、不抛向渲染循环。
        Assert.Single(viewModel.Entries);
        Assert.Contains("查询事件日志失败", viewModel.QueryErrorText, StringComparison.Ordinal);
        Assert.Contains("boom", viewModel.QueryErrorText, StringComparison.Ordinal);
    }

    [Fact]
    public void NativeSummaryText_UsesSnapshotCounts()
    {
        var viewModel = new EventLogViewModel(Client(Event(1)));

        viewModel.ApplySummary(new EventLogSummaryState(600, 5000, 7), snapshotTick: 1);

        Assert.Contains("600", viewModel.NativeSummaryText, StringComparison.Ordinal);
        Assert.Contains("7", viewModel.NativeSummaryText, StringComparison.Ordinal);
    }

    [Fact]
    public void InvalidGameTimeInput_ShowsInlineErrorAndKeepsLastFilter()
    {
        var client = Client(
            Event(1, tick: 10),
            Event(2, tick: 30));
        var viewModel = new EventLogViewModel(client);
        viewModel.ApplySummary(new EventLogSummaryState(2, 5000, 0), snapshotTick: 1);
        viewModel.SinceTickText = "10";

        viewModel.SinceTickText = "abc";

        Assert.False(string.IsNullOrEmpty(viewModel.GameTimeFilterError));
        Assert.Equal((ulong)10, viewModel.SinceTick);
        Assert.Equal([1uL, 2uL], viewModel.Entries.Select(entry => entry.Seq));
    }

    [Fact]
    public void CategoryParse_AcceptsNativeNames()
    {
        Assert.Equal(SimEventCategory.Command, SimEventCategoryParser.Parse("command"));
        Assert.Equal(SimEventCategory.System, SimEventCategoryParser.Parse("system"));
        Assert.Throws<ArgumentException>(() => SimEventCategoryParser.Parse("unknown"));
    }

    [Fact]
    public void SeverityParse_AcceptsNativeNames()
    {
        Assert.Equal(SimEventSeverity.Critical, SimEventSeverityParser.Parse("critical"));
        Assert.Throws<ArgumentException>(() => SimEventSeverityParser.Parse("fatal"));
    }

    [Fact]
    public void Entry_FormatsGameTimeFromTickHz()
    {
        var entry20 = new EventLogEntryViewModel(Event(1, tick: 1200), tickHz: 20);
        var entry40 = new EventLogEntryViewModel(Event(1, tick: 1200), tickHz: 40);

        Assert.Equal("01:00", entry20.GameTimeText); // 1200 tick @20Hz = 60s。
        Assert.Equal("00:30", entry40.GameTimeText); // 1200 tick @40Hz = 30s（不复硬编码 20Hz）。
    }

    [Fact]
    public void Constructor_WithInvalidTickHz_Throws()
    {
        Assert.Throws<ArgumentOutOfRangeException>(() => new EventLogViewModel(tickHz: 0));
    }

    private static bool JsonContainsAllFilters(string query) =>
        query.Contains("\"category\"", StringComparison.Ordinal) &&
        query.Contains("\"min_severity\"", StringComparison.Ordinal) &&
        query.Contains("\"text\"", StringComparison.Ordinal);

    private sealed class MutableTimeProvider : TimeProvider
    {
        private DateTimeOffset _now = new(2026, 1, 1, 0, 0, 0, TimeSpan.Zero);

        public override DateTimeOffset GetUtcNow() => _now;

        public void Advance(TimeSpan delta) => _now = _now.Add(delta);
    }
}
