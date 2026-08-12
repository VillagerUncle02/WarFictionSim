// 测试：事件日志 —— 环形保留/关键优先/过滤/搜索/置顶（T042）。
//
// 语义镜像 native event_log.cpp：5000 条环形、驱逐"最旧非关键优先"、
// 过滤（分类/最低严重级/文本子串）按 seq 升序、关键事件可置顶。

using WarFictionSim.Ui.EventLogPanel;
using Xunit;

namespace WarFictionSim.Ui.Tests.EventLogPanel;

public class EventLogViewModelTests
{
    private static SimEventDto Event(
        ulong seq, ulong tick = 100, SimEventCategory category = SimEventCategory.Combat,
        SimEventSeverity severity = SimEventSeverity.Info, string message = "测试事件") =>
        new(seq, tick, category, severity, message);

    [Fact]
    public void Append_ShowsEntriesInSeqOrder()
    {
        var viewModel = new EventLogViewModel();

        viewModel.Append(Event(2));
        viewModel.Append(Event(1));

        Assert.Equal([1uL, 2uL], viewModel.Entries.Select(entry => entry.Seq));
    }

    [Fact]
    public void Overflow_EvictsOldestNonCriticalFirst()
    {
        var viewModel = new EventLogViewModel(capacity: 3);
        viewModel.Append(Event(1, severity: SimEventSeverity.Info));
        viewModel.Append(Event(2, severity: SimEventSeverity.Info));
        viewModel.Append(Event(3, severity: SimEventSeverity.Critical));

        viewModel.Append(Event(4, severity: SimEventSeverity.Info));

        // 满员时驱逐最旧非关键（seq=1），关键事件 seq=3 存活。
        Assert.Equal([2uL, 3uL, 4uL], viewModel.Entries.Select(entry => entry.Seq));
        Assert.Equal(1, viewModel.CriticalCount);
    }

    [Fact]
    public void Overflow_AllCritical_EvictsOldest()
    {
        var viewModel = new EventLogViewModel(capacity: 2);
        viewModel.Append(Event(1, severity: SimEventSeverity.Critical));
        viewModel.Append(Event(2, severity: SimEventSeverity.Critical));

        viewModel.Append(Event(3, severity: SimEventSeverity.Critical));

        Assert.Equal([2uL, 3uL], viewModel.Entries.Select(entry => entry.Seq));
        Assert.Equal(2, viewModel.CriticalCount);
    }

    [Fact]
    public void Filter_ByCategoryMinSeverityAndText()
    {
        var viewModel = new EventLogViewModel();
        viewModel.Append(Event(1, category: SimEventCategory.Intel, severity: SimEventSeverity.Info, message: "发现目标"));
        viewModel.Append(Event(2, category: SimEventCategory.Combat, severity: SimEventSeverity.Critical, message: "接敌"));
        viewModel.Append(Event(3, category: SimEventCategory.Combat, severity: SimEventSeverity.Info, message: "弹药耗尽"));

        viewModel.SelectedCategory = SimEventCategory.Combat;
        viewModel.MinSeverity = SimEventSeverity.Info;
        viewModel.SearchText = "接";

        Assert.Single(viewModel.Entries);
        Assert.Equal("接敌", viewModel.Entries[0].Message);
    }

    [Fact]
    public void PinCritical_PutsCriticalEventsFirst()
    {
        var viewModel = new EventLogViewModel();
        viewModel.Append(Event(1, severity: SimEventSeverity.Info));
        viewModel.Append(Event(2, severity: SimEventSeverity.Critical, message: "关键"));
        viewModel.Append(Event(3, severity: SimEventSeverity.Info));

        viewModel.PinCriticalEvents = true;

        Assert.Equal([2uL, 1uL, 3uL], viewModel.Entries.Select(entry => entry.Seq));
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
    public void Entry_FormatsGameTimeFromTick()
    {
        var entry = new EventLogEntryViewModel(Event(1, tick: 1200));

        Assert.Equal("01:00", entry.GameTimeText); // 1200 tick @20Hz = 60s。
    }

    [Fact]
    public void Constructor_WithInvalidCapacity_Throws()
    {
        Assert.Throws<ArgumentOutOfRangeException>(() => new EventLogViewModel(capacity: 0));
    }
}
