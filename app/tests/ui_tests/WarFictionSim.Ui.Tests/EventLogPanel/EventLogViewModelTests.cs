// 测试：事件日志 —— 真实事件查询消费、过滤/搜索/置顶（T042 + FR-044 数据通道）。
//
// 面板不再镜像核心的环形驱逐：事件正文经 ISimClient.QueryEvents 从
// wfs_sim_query_events 拉取（快照更新时按需、默认最近 500 条窗口），
// 分类/严重级/文本过滤下推给 native 查询，游戏时间过滤在窗口内本地执行，
// 关键事件=severity critical 置顶；状态栏计数仍来自快照 summary。

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
    public void ApplySummary_AdvancedTick_RepullsEvents()
    {
        var client = Client(Event(1));
        var viewModel = new EventLogViewModel(client);

        viewModel.ApplySummary(new EventLogSummaryState(1, 5000, 0), snapshotTick: 5);
        viewModel.ApplySummary(new EventLogSummaryState(1, 5000, 0), snapshotTick: 6);

        Assert.Equal(2, client.EventQueries.Count);
    }

    [Fact]
    public void CategorySeverityTextFilters_AreSentToNativeQuery()
    {
        var client = Client(Event(1));
        var viewModel = new EventLogViewModel(client);

        viewModel.SelectedCategory = SimEventCategory.Combat;
        viewModel.MinSeverity = SimEventSeverity.Info;
        viewModel.SearchText = "接敌";

        string query = Assert.Single(client.EventQueries.Where(JsonContainsAllFilters));
        Assert.Contains("\"category\":\"combat\"", query, StringComparison.Ordinal);
        Assert.Contains("\"min_severity\":\"info\"", query, StringComparison.Ordinal);
        Assert.Contains("\"text\":\"接敌\"", query, StringComparison.Ordinal);
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
}
