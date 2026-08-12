// 测试：兵牌地图 —— 兵牌集合更新、选择与屏幕坐标刷新（T040）。
//
// 渲染独立于模拟 tick：ApplySnapshot 只把快照投影为兵牌（FR-025），
// 视口变化只重算屏幕坐标、不改动任何模拟数据。

using WarFictionSim.Ui.BattleMap;
using WarFictionSim.Ui.Interop;
using WarFictionSim.Ui.Tests.TestDoubles;
using Xunit;

namespace WarFictionSim.Ui.Tests.BattleMap;

public class BattleMapViewModelTests
{
    [Fact]
    public void ApplySnapshot_BuildsVisibleMarkersOnly()
    {
        UnitState friendly = SnapshotFactory.Unit("friendly-1", "node-player", "side-a", 1, 1);
        UnitState hiddenEnemy = SnapshotFactory.Unit("enemy-hidden", "node-enemy", "side-b", 2, 2);
        UnitState spottedEnemy = SnapshotFactory.Unit("enemy-seen", "node-enemy", "side-b", 3, 3);
        var intel = new Dictionary<string, IntelRecordState>
        {
            ["node-player:enemy-seen"] = SnapshotFactory.Intel("enemy-seen", IntelTier.T1, 0, 500, 3, 3),
        };
        var viewModel = new BattleMapViewModel(5, 5);

        viewModel.ApplySnapshot(SnapshotFactory.Create(0, [friendly, hiddenEnemy, spottedEnemy], intel));

        Assert.Equal(2, viewModel.Markers.Count);
        Assert.Contains(viewModel.Markers, marker => marker.UnitId == "friendly-1");
        Assert.Contains(viewModel.Markers, marker => marker.UnitId == "enemy-seen");
    }

    [Fact]
    public void ApplySnapshot_SecondTime_UpdatesExistingMarkers()
    {
        UnitState friendly = SnapshotFactory.Unit("friendly-1", "node-player", "side-a", 1, 1);
        var viewModel = new BattleMapViewModel(5, 5);
        viewModel.ApplySnapshot(SnapshotFactory.Create(0, [friendly]));
        UnitState moved = SnapshotFactory.Unit("friendly-1", "node-player", "side-a", 2.5, 3.5);

        viewModel.ApplySnapshot(SnapshotFactory.Create(1, [moved]));

        UnitMarkerViewModel marker = Assert.Single(viewModel.Markers);
        Assert.Equal(2.5, marker.WorldX);
        Assert.Equal(3.5, marker.WorldY);
    }

    [Fact]
    public void ApplySnapshot_WhenIntelLost_RemovesMarker()
    {
        UnitState enemy = SnapshotFactory.Unit("enemy-1", "node-enemy", "side-b", 1, 1);
        var intel = new Dictionary<string, IntelRecordState>
        {
            ["node-player:enemy-1"] = SnapshotFactory.Intel("enemy-1", IntelTier.T1, 0, 500, 1, 1),
        };
        var viewModel = new BattleMapViewModel(5, 5);
        viewModel.ApplySnapshot(SnapshotFactory.Create(0, [enemy], intel));

        viewModel.ApplySnapshot(SnapshotFactory.Create(600, [enemy]));

        Assert.Empty(viewModel.Markers);
    }

    [Fact]
    public void SelectUnit_RaisesEventAndFlagsMarker()
    {
        UnitState friendly = SnapshotFactory.Unit("friendly-1", "node-player", "side-a", 1, 1);
        var viewModel = new BattleMapViewModel(5, 5);
        viewModel.ApplySnapshot(SnapshotFactory.Create(0, [friendly]));
        string? selected = null;
        viewModel.UnitSelected += (_, unitId) => selected = unitId;

        viewModel.SelectUnit("friendly-1");

        Assert.Equal("friendly-1", selected);
        Assert.Equal("friendly-1", viewModel.SelectedUnitId);
        Assert.True(Assert.Single(viewModel.Markers).IsSelected);
    }

    [Fact]
    public void SelectUnit_Again_ClearsSelection()
    {
        UnitState friendly = SnapshotFactory.Unit("friendly-1", "node-player", "side-a", 1, 1);
        var viewModel = new BattleMapViewModel(5, 5);
        viewModel.ApplySnapshot(SnapshotFactory.Create(0, [friendly]));
        var events = new List<string?>();
        viewModel.UnitSelected += (_, unitId) => events.Add(unitId);

        viewModel.SelectUnit("friendly-1");
        viewModel.SelectUnit("friendly-1");

        Assert.Equal(["friendly-1", null], events);
        Assert.Null(viewModel.SelectedUnitId);
        Assert.False(Assert.Single(viewModel.Markers).IsSelected);
    }

    [Fact]
    public void BoxSelect_SelectsUnitsInsideScreenRect_FriendlyOnlyByDefault()
    {
        UnitState friendlyA = SnapshotFactory.Unit("friendly-a", "node-player", "side-a", 1, 1);
        UnitState friendlyB = SnapshotFactory.Unit("friendly-b", "node-player", "side-a", 2, 2);
        UnitState enemy = SnapshotFactory.Unit("enemy-1", "node-enemy", "side-b", 3, 3);
        var intel = new Dictionary<string, IntelRecordState>
        {
            ["node-player:enemy-1"] = SnapshotFactory.Intel("enemy-1", IntelTier.T1, 0, 500, 3, 3),
        };
        var viewModel = new BattleMapViewModel(5, 5);
        viewModel.ApplySnapshot(SnapshotFactory.Create(0, [friendlyA, friendlyB, enemy], intel));
        foreach (UnitMarkerViewModel marker in viewModel.Markers)
        {
            marker.SetScreenPosition(
                marker.UnitId switch
                {
                    "friendly-a" => 10,
                    "enemy-1" => 30,
                    _ => 100,
                },
                marker.UnitId == "friendly-a" ? 10 : 100);
        }

        IReadOnlyList<string>? selected = null;
        viewModel.UnitsSelected += (_, unitIds) => selected = unitIds;

        viewModel.SelectUnitsInScreenRect(0, 0, 60, 60);

        Assert.Equal(["friendly-a"], selected);
        Assert.True(viewModel.Markers.Single(marker => marker.UnitId == "friendly-a").IsSelected);
        Assert.False(viewModel.Markers.Single(marker => marker.UnitId == "enemy-1").IsSelected);
        Assert.False(viewModel.Markers.Single(marker => marker.UnitId == "friendly-b").IsSelected);
    }

    [Fact]
    public void RefreshPositions_ProjectsWorldToScreen()
    {
        UnitState friendly = SnapshotFactory.Unit("friendly-1", "node-player", "side-a", 2, 1);
        var viewModel = new BattleMapViewModel(5, 5);
        viewModel.ApplySnapshot(SnapshotFactory.Create(0, [friendly]));
        viewModel.Viewport.SetViewportSize(1000, 1000);
        viewModel.Viewport.ResetToFit();
        viewModel.Viewport.PanBy(0, 0);

        viewModel.RefreshPositions();

        UnitMarkerViewModel marker = Assert.Single(viewModel.Markers);
        (double expectedX, double expectedY) = viewModel.Viewport.WorldToScreen(2, 1);
        Assert.Equal(expectedX, marker.ScreenX, 3);
        Assert.Equal(expectedY, marker.ScreenY, 3);
    }
}
