// 测试：应用壳 —— 战斗主屏的选择接线与命令闭环（F2）。
//
// 断言地图点选/框选事件经 GameScreenViewModel 转换为命令面板执行单位
// （只取己方单位），空选择给出"未选择执行单位"错误——US1 指挥闭环
// 不再断裂（FR-045/SC-011）。

using WarFictionSim.Ui.BattleMap;
using WarFictionSim.Ui.Interop;
using WarFictionSim.Ui.MainMenu;
using WarFictionSim.Ui.Tests.TestDoubles;
using WarFictionSim.Ui.ViewModels;
using Xunit;

namespace WarFictionSim.Ui.Tests.ViewModels;

public class GameScreenViewModelTests
{
    private static ScenarioCatalogEntry Scenario() =>
        new()
        {
            Id = "scn-test",
            Name = "测试场景",
            Path = "scn-test.json",
            PlayerNodeId = "node-player",
            IsTutorial = false,
            SaveSlot = null,
            Scale = CombatScale.Platoon,
            Seed = 42,
            TickHz = 20,
            MapWidthKm = 5,
            MapHeightKm = 5,
            Zones = ["zone-a"],
            CommandNodeIds = ["node-player"],
        };

    private static (FakeSimClient Client, SimulationSnapshot Snapshot) SnapshotWithUnits()
    {
        UnitState friendly = SnapshotFactory.Unit("friendly-1", "node-player", "side-a", 1, 1);
        UnitState enemy = SnapshotFactory.Unit("enemy-1", "node-enemy", "side-b", 2, 2);
        var intel = new Dictionary<string, IntelRecordState>
        {
            ["node-player:enemy-1"] = SnapshotFactory.Intel("enemy-1", IntelTier.T1, 0, 500, 2, 2),
        };
        var client = new FakeSimClient(SnapshotFactory.Create(0, [friendly, enemy], intel));
        return (client, client.Snapshot);
    }

    [Fact]
    public void UnitSelected_ForwardsFriendlyUnitToCommandPanel()
    {
        (FakeSimClient client, SimulationSnapshot snapshot) = SnapshotWithUnits();
        var viewModel = new GameScreenViewModel(client, Scenario());
        viewModel.ApplySnapshot(snapshot);

        viewModel.BattleMap.SelectUnit("friendly-1");

        Assert.Equal(["friendly-1"], viewModel.CommandPanel.Draft.ExecutorIds);
    }

    [Fact]
    public void UnitSelected_EnemyUnit_IsNotForwardedAsExecutor()
    {
        (FakeSimClient client, SimulationSnapshot snapshot) = SnapshotWithUnits();
        var viewModel = new GameScreenViewModel(client, Scenario());
        viewModel.ApplySnapshot(snapshot);

        viewModel.BattleMap.SelectUnit("enemy-1");

        Assert.Empty(viewModel.CommandPanel.Draft.ExecutorIds);
    }

    [Fact]
    public void BoxSelection_ForwardsFriendlyUnitsToCommandPanel()
    {
        (FakeSimClient client, SimulationSnapshot snapshot) = SnapshotWithUnits();
        var viewModel = new GameScreenViewModel(client, Scenario());
        viewModel.ApplySnapshot(snapshot);
        UnitMarkerViewModel friendlyMarker =
            viewModel.BattleMap.Markers.Single(marker => marker.UnitId == "friendly-1");
        UnitMarkerViewModel enemyMarker =
            viewModel.BattleMap.Markers.Single(marker => marker.UnitId == "enemy-1");
        friendlyMarker.SetScreenPosition(10, 10);
        enemyMarker.SetScreenPosition(100, 100);

        viewModel.BattleMap.SelectUnitsInScreenRect(0, 0, 60, 60);

        Assert.Equal(["friendly-1"], viewModel.CommandPanel.Draft.ExecutorIds);
    }

    [Fact]
    public void EmptySelection_YieldsUnselectedExecutorError()
    {
        (FakeSimClient client, SimulationSnapshot snapshot) = SnapshotWithUnits();
        var viewModel = new GameScreenViewModel(client, Scenario());
        viewModel.ApplySnapshot(snapshot);
        viewModel.BattleMap.SelectUnit("friendly-1");
        Assert.DoesNotContain(viewModel.CommandPanel.Issues, issue => issue.Code == "TARGET_REQUIRED");

        viewModel.BattleMap.SelectUnit("friendly-1"); // 重复点选取消。

        Assert.Contains(
            viewModel.CommandPanel.Issues,
            issue => issue.Code == "TARGET_REQUIRED" && issue.Message.Contains("未选择执行单位", StringComparison.Ordinal));
        Assert.False(viewModel.CommandPanel.CanSubmit);
    }

    [Fact]
    public void OnPresentationFrame_CatchesSnapshotParseException()
    {
        (FakeSimClient client, _) = SnapshotWithUnits();
        client.NextGetSnapshotError = new SnapshotParseException("字段漂移");
        var viewModel = new GameScreenViewModel(client, Scenario());

        viewModel.OnPresentationFrame();

        Assert.Contains("快照解析失败", viewModel.StatusError, StringComparison.Ordinal);
    }

    [Fact]
    public void OnPresentationFrame_CatchesObjectDisposedException()
    {
        (FakeSimClient client, _) = SnapshotWithUnits();
        client.NextGetSnapshotError = new ObjectDisposedException("sim");
        var viewModel = new GameScreenViewModel(client, Scenario());

        viewModel.OnPresentationFrame();

        Assert.Contains("已释放", viewModel.StatusError, StringComparison.Ordinal);
    }

    [Fact]
    public void StepOneTick_CatchesObjectDisposedException()
    {
        (FakeSimClient client, _) = SnapshotWithUnits();
        client.NextStepError = new ObjectDisposedException("sim");
        var viewModel = new GameScreenViewModel(client, Scenario());

        viewModel.StepOneTick();
        // 返回主菜单/关窗竞态：不得逃逸为线程池未处理异常（本测试通过即证明）。
    }

    [Fact]
    public void Dispose_DisposesClient()
    {
        (FakeSimClient client, _) = SnapshotWithUnits();
        var viewModel = new GameScreenViewModel(client, Scenario());

        viewModel.Dispose();

        Assert.True(client.Disposed);
    }
}
