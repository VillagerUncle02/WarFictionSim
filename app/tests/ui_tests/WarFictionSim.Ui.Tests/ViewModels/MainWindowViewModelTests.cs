// 测试：应用壳 —— 主窗口视图模型的泵生命周期（N5）。
//
// 返回主菜单必须先停步进泵（等待在途 step）再释放客户端句柄：泵随战斗
// 主屏启停，而不是在主菜单空转至关窗；Dispose 按同一顺序释放泵与客户端，
// 保证没有线程池回调在句柄释放后继续触碰原生句柄（F3）。

using WarFictionSim.Ui.Interop;
using WarFictionSim.Ui.MainMenu;
using WarFictionSim.Ui.Tests.TestDoubles;
using WarFictionSim.Ui.ViewModels;
using Xunit;

namespace WarFictionSim.Ui.Tests.ViewModels;

public class MainWindowViewModelTests
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

    [Fact]
    public async Task BackToMenu_StopsPumpAndDisposesClient()
    {
        var client = new FakeSimClient(SnapshotFactory.Create(0, []));
        var factory = new FakeSimClientFactory((_, _, _) => client);
        var viewModel = new MainWindowViewModel(new ScenarioCatalog([Scenario()], []), factory);
        viewModel.Menu.StartNewGameCommand.Execute(null);
        Assert.NotNull(viewModel.Game);
        await WaitUntilAsync(() => client.StepCount > 0);

        viewModel.BackToMenu();

        int stepsAtMenu = client.StepCount;
        await Task.Delay(150);
        Assert.Equal(stepsAtMenu, client.StepCount); // 返回主菜单后泵已停止，不再空转。
        Assert.True(client.Disposed);
        Assert.Null(viewModel.Game);
        viewModel.Dispose();
    }

    [Fact]
    public async Task Dispose_StopsPumpBeforeDisposingClient()
    {
        var client = new FakeSimClient(SnapshotFactory.Create(0, []));
        var factory = new FakeSimClientFactory((_, _, _) => client);
        var viewModel = new MainWindowViewModel(new ScenarioCatalog([Scenario()], []), factory);
        viewModel.Menu.StartNewGameCommand.Execute(null);
        await WaitUntilAsync(() => client.StepCount > 0);

        viewModel.Dispose();

        int stepsAtDispose = client.StepCount;
        await Task.Delay(150);
        Assert.Equal(stepsAtDispose, client.StepCount);
        Assert.True(client.Disposed);
        Assert.Null(viewModel.Game);
    }

    private static async Task WaitUntilAsync(Func<bool> condition)
    {
        DateTimeOffset deadline = DateTimeOffset.UtcNow.AddSeconds(2);
        while (!condition() && DateTimeOffset.UtcNow < deadline)
        {
            await Task.Delay(10);
        }

        Assert.True(condition());
    }
}
