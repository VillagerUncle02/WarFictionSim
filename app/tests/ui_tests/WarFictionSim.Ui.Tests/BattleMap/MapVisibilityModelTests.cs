// 测试：兵牌地图 —— 迷雾/识别档位/来源标注/最后已知状态解析（T040）。
//
// 断言 FR-033/034/035 与 SC-005/006 的表现层规则：敌方默认不可见；
// 仅玩家节点的有效情报记录点亮敌方；档位/来源/最后动向随记录透出；
// 己方失联单位按最后已知状态显示。

using WarFictionSim.Ui.BattleMap;
using WarFictionSim.Ui.Interop;
using WarFictionSim.Ui.Tests.TestDoubles;
using Xunit;

namespace WarFictionSim.Ui.Tests.BattleMap;

public class MapVisibilityModelTests
{
    [Fact]
    public void FriendlyUnits_AreAlwaysVisible_WithoutIntel()
    {
        UnitState friendly = SnapshotFactory.Unit("friendly-1", "node-player", "side-a", 1, 1);
        SimulationSnapshot snapshot = SnapshotFactory.Create(0, [friendly]);

        IReadOnlyList<UnitMarkerData> markers = MapVisibilityModel.Resolve(snapshot);

        UnitMarkerData marker = Assert.Single(markers);
        Assert.True(marker.IsFriendly);
        Assert.Equal("friendly-1", marker.UnitId);
        Assert.False(marker.IsLastKnown);
    }

    [Fact]
    public void EnemyUnits_WithoutIntel_AreHidden()
    {
        UnitState enemy = SnapshotFactory.Unit("enemy-1", "node-enemy", "side-b", 1, 1);
        SimulationSnapshot snapshot = SnapshotFactory.Create(0, [enemy]);

        Assert.Empty(MapVisibilityModel.Resolve(snapshot));
    }

    [Fact]
    public void EnemyWithValidIntel_IsVisibleWithTierAndSource()
    {
        UnitState enemy = SnapshotFactory.Unit("enemy-1", "node-enemy", "side-b", 1.15, 1.2);
        var intel = new Dictionary<string, IntelRecordState>
        {
            ["node-player:enemy-1"] = SnapshotFactory.Intel("enemy-1", IntelTier.T2, 100, 500, 1.15, 1.2),
        };
        SimulationSnapshot snapshot = SnapshotFactory.Create(150, [enemy], intel);

        UnitMarkerData marker = Assert.Single(MapVisibilityModel.Resolve(snapshot));

        Assert.False(marker.IsFriendly);
        Assert.Equal(IntelTier.T2, marker.Tier);
        Assert.Contains("friendly-1", marker.SourceLabel, StringComparison.Ordinal);
        Assert.Equal(1.15, marker.X);
        Assert.Equal(1.2, marker.Y);
    }

    [Fact]
    public void EnemyIntel_ExpiredByMemory_IsHidden()
    {
        UnitState enemy = SnapshotFactory.Unit("enemy-1", "node-enemy", "side-b", 1, 1);
        var intel = new Dictionary<string, IntelRecordState>
        {
            // memory_until_tick = 200，当前 tick = 200 起记忆删除（核心语义为 >=）。
            ["node-player:enemy-1"] = SnapshotFactory.Intel("enemy-1", IntelTier.T1, 100, 200, 1, 1),
        };
        SimulationSnapshot snapshot = SnapshotFactory.Create(200, [enemy], intel);

        Assert.Empty(MapVisibilityModel.Resolve(snapshot));
    }

    [Fact]
    public void EnemyIntel_WithNoneTier_IsHidden()
    {
        UnitState enemy = SnapshotFactory.Unit("enemy-1", "node-enemy", "side-b", 1, 1);
        var intel = new Dictionary<string, IntelRecordState>
        {
            ["node-player:enemy-1"] = SnapshotFactory.Intel("enemy-1", IntelTier.None, 100, 500, 1, 1),
        };
        SimulationSnapshot snapshot = SnapshotFactory.Create(150, [enemy], intel);

        Assert.Empty(MapVisibilityModel.Resolve(snapshot));
    }

    [Fact]
    public void OutOfContactFriendly_ShowsLastKnownPosition()
    {
        UnitState friendly = SnapshotFactory.Unit(
            "friendly-1", "node-player", "side-a", 1, 1, outOfContact: true, hasLastKnown: true, lastKnownX: 0.8, lastKnownY: 0.9);
        SimulationSnapshot snapshot = SnapshotFactory.Create(0, [friendly]);

        UnitMarkerData marker = Assert.Single(MapVisibilityModel.Resolve(snapshot));

        Assert.True(marker.IsLastKnown);
        Assert.Equal(0.8, marker.X);
        Assert.Equal(0.9, marker.Y);
        Assert.True(marker.OutOfContact);
    }

    [Fact]
    public void StaleEnemyObservation_IsMarkedLastKnownWithMotion()
    {
        UnitState enemy = SnapshotFactory.Unit("enemy-1", "node-enemy", "side-b", 2, 2);
        var intel = new Dictionary<string, IntelRecordState>
        {
            // 最后目视 tick=100 < 当前 tick=150：仍保留记忆，但已非实时目视。
            ["node-player:enemy-1"] = SnapshotFactory.Intel(
                "enemy-1", IntelTier.T3, 100, 500, 1.1, 1.2, motionDx: 0.6, motionDy: 0.8),
        };
        SimulationSnapshot snapshot = SnapshotFactory.Create(150, [enemy], intel);

        UnitMarkerData marker = Assert.Single(MapVisibilityModel.Resolve(snapshot));

        Assert.True(marker.IsLastKnown);
        Assert.Equal(0.6, marker.MotionDx);
        Assert.Equal(0.8, marker.MotionDy);
    }

    [Fact]
    public void ExpiredSource_KeepsMemoryButShowsExpiredLabel()
    {
        UnitState enemy = SnapshotFactory.Unit("enemy-1", "node-enemy", "side-b", 1, 1);
        var intel = new Dictionary<string, IntelRecordState>
        {
            ["node-player:enemy-1"] = SnapshotFactory.Intel(
                "enemy-1", IntelTier.T1, 100, 500, 1, 1, sourceKind: "expired", sourceUnitId: ""),
        };
        SimulationSnapshot snapshot = SnapshotFactory.Create(150, [enemy], intel);

        UnitMarkerData marker = Assert.Single(MapVisibilityModel.Resolve(snapshot));

        Assert.Contains("过期", marker.SourceLabel, StringComparison.Ordinal);
    }

    [Fact]
    public void EchelonSource_ShowsEchelonLabel()
    {
        UnitState enemy = SnapshotFactory.Unit("enemy-1", "node-enemy", "side-b", 1, 1);
        var intel = new Dictionary<string, IntelRecordState>
        {
            ["node-player:enemy-1"] = new IntelRecordState(
                "node-player", "enemy-1", IntelTier.T1, 100, 500, 1000,
                new IntelSourceState("echelon", "", "brigade-intel", 100), 1, 1, 0, 0),
        };
        SimulationSnapshot snapshot = SnapshotFactory.Create(150, [enemy], intel);

        UnitMarkerData marker = Assert.Single(MapVisibilityModel.Resolve(snapshot));

        Assert.Contains("brigade-intel", marker.SourceLabel, StringComparison.Ordinal);
    }
}
