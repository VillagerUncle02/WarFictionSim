// 测试：主菜单 —— 规模选择、扮演节点、新游戏/读档流程（T039）。
//
// 断言要点：规模过滤、节点派生（仅己方阵营节点）、启动参数完整性、
// 读档前置校验（存档头 → ABI 版本，任一失败不进战斗且内联提示错误）。

using WarFictionSim.Ui.MainMenu;
using Xunit;

namespace WarFictionSim.Ui.Tests.MainMenu;

public class MainMenuViewModelTests
{
    private static ScenarioCatalogEntry Scenario(
        string id,
        CombatScale scale = CombatScale.Platoon,
        bool tutorial = false,
        string? saveSlot = null,
        string playerNodeId = "node-player",
        IReadOnlyList<string>? commandNodeIds = null) =>
        new()
        {
            Id = id,
            Name = id,
            Path = $"{id}.json",
            PlayerNodeId = playerNodeId,
            IsTutorial = tutorial,
            SaveSlot = saveSlot,
            Scale = scale,
            Seed = 42,
            TickHz = 20,
            MapWidthKm = 5.0,
            MapHeightKm = 5.0,
            Zones = ["zone-a"],
            CommandNodeIds = commandNodeIds ?? [playerNodeId],
        };

    private static ScenarioCatalog Catalog(params ScenarioCatalogEntry[] entries) =>
        new(entries.ToList(), []);

    [Fact]
    public void Construction_FiltersScenariosBySelectedScale()
    {
        var platoon = Scenario("platoon-a");
        var battalion = Scenario("battalion-a", CombatScale.Battalion);
        var viewModel = new MainMenuViewModel(Catalog(platoon, battalion));

        Assert.Single(viewModel.VisibleScenarios);
        Assert.Equal("platoon-a", viewModel.VisibleScenarios[0].Id);
    }

    [Fact]
    public void SelectScale_RefreshesVisibleScenarios()
    {
        var platoon = Scenario("platoon-a");
        var battalion = Scenario("battalion-a", CombatScale.Battalion);
        var viewModel = new MainMenuViewModel(Catalog(platoon, battalion));

        viewModel.SelectScaleCommand.Execute(CombatScale.Battalion);

        Assert.Single(viewModel.VisibleScenarios);
        Assert.Equal("battalion-a", viewModel.VisibleScenarios[0].Id);
    }

    [Fact]
    public void SelectScenario_PopulatesAvailableNodes()
    {
        var entry = Scenario("platoon-a");
        var viewModel = new MainMenuViewModel(Catalog(entry));

        viewModel.SelectedScenario = entry;

        Assert.Equal(["node-player"], viewModel.AvailableNodes);
    }

    [Fact]
    public void StartNewGame_WithoutSelection_SetsError()
    {
        // 空目录：没有可自动选中的场景，必须走"请先选择"错误路径。
        var viewModel = new MainMenuViewModel(Catalog());

        viewModel.StartNewGameCommand.Execute(null);

        Assert.True(viewModel.HasError);
        Assert.Contains("场景", viewModel.ErrorMessage, StringComparison.Ordinal);
    }

    [Fact]
    public void StartNewGame_RaisesRequestWithResolvedParameters()
    {
        var entry = Scenario("platoon-a");
        var viewModel = new MainMenuViewModel(Catalog(entry));
        GameStartRequest? request = null;
        viewModel.GameStartRequested += (_, args) => request = args.Request;
        viewModel.SelectedScenario = entry;
        viewModel.SelectedNodeId = "node-player";

        viewModel.StartNewGameCommand.Execute(null);

        Assert.NotNull(request);
        Assert.Equal(entry, request!.Scenario);
        Assert.Equal((ulong)42, request.Seed);
        Assert.Null(request.SavePath);
        Assert.False(viewModel.HasError);
    }

    [Fact]
    public void FixedPlayerNode_IsSelectedFromScenario_AndSelectionIsLocked()
    {
        var entry = Scenario("platoon-a", commandNodeIds: ["node-player", "node-b"]);
        var viewModel = new MainMenuViewModel(Catalog(entry));

        Assert.True(viewModel.IsPlayerNodeFixed);
        Assert.False(viewModel.CanChooseNode);
        Assert.Equal("node-player", viewModel.SelectedNodeId);
        Assert.Contains("node-player", viewModel.PlayerNodeNote, StringComparison.Ordinal);
    }

    [Fact]
    public void NoPlayerNode_AllowsListingNodes_WithV1Note()
    {
        var entry = Scenario("platoon-a", playerNodeId: "", commandNodeIds: ["node-a", "node-b"]);
        var viewModel = new MainMenuViewModel(Catalog(entry));

        Assert.False(viewModel.IsPlayerNodeFixed);
        Assert.True(viewModel.CanChooseNode);
        Assert.Equal(2, viewModel.AvailableNodes.Count);
        Assert.Contains("v1", viewModel.PlayerNodeNote, StringComparison.Ordinal);
    }

    [Fact]
    public void StartNewGame_WithFixedNodeMismatch_SetsError()
    {
        var entry = Scenario("platoon-a", commandNodeIds: ["node-player", "node-b"]);
        var viewModel = new MainMenuViewModel(Catalog(entry));
        viewModel.SelectedNodeId = "node-b";

        viewModel.StartNewGameCommand.Execute(null);

        Assert.True(viewModel.HasError);
        Assert.Contains("场景数据指定", viewModel.ErrorMessage, StringComparison.Ordinal);
    }

    [Fact]
    public void StartNewGame_WithInvalidThreads_SetsError()
    {
        var entry = Scenario("platoon-a");
        var viewModel = new MainMenuViewModel(Catalog(entry)) { Threads = 0 };
        viewModel.SelectedScenario = entry;

        viewModel.StartNewGameCommand.Execute(null);

        Assert.True(viewModel.HasError);
        Assert.Contains("并行度", viewModel.ErrorMessage, StringComparison.Ordinal);
    }

    [Fact]
    public void StartTutorial_StartsTutorialScenario()
    {
        var entry = Scenario("tutorial", tutorial: true, saveSlot: "tutorial-slot");
        var viewModel = new MainMenuViewModel(Catalog(entry));
        GameStartRequest? request = null;
        viewModel.GameStartRequested += (_, args) => request = args.Request;

        viewModel.StartTutorialCommand.Execute(null);

        Assert.NotNull(request);
        Assert.Equal("tutorial", request!.Scenario.Id);
        Assert.Null(request.SavePath);
    }

    [Fact]
    public void LoadSave_WithCorruptSave_SetsErrorAndDoesNotStart()
    {
        var viewModel = new MainMenuViewModel(Catalog(Scenario("platoon-a")));
        int raised = 0;
        viewModel.GameStartRequested += (_, _) => raised++;

        viewModel.LoadSaveCommand.Execute("missing-save.wfs");

        Assert.True(viewModel.HasError);
        Assert.Equal(0, raised);
    }

    [Fact]
    public void LoadSave_WithAbiMismatch_SetsErrorAndDoesNotStart()
    {
        string savePath = WriteSaveFile(abiVersion: "9.9.9");
        var viewModel = new MainMenuViewModel(Catalog(Scenario("platoon-a")));
        int raised = 0;
        viewModel.GameStartRequested += (_, _) => raised++;

        viewModel.LoadSaveCommand.Execute(savePath);

        Assert.True(viewModel.HasError);
        Assert.Contains("ABI", viewModel.ErrorMessage, StringComparison.Ordinal);
        Assert.Equal(0, raised);
    }

    [Fact]
    public void LoadSave_WithUnknownScenario_SetsErrorAndDoesNotStart()
    {
        string savePath = WriteSaveFile(abiVersion: "0.2.0", scenarioId: "scn-not-installed");
        var viewModel = new MainMenuViewModel(Catalog(Scenario("platoon-a")));
        int raised = 0;
        viewModel.GameStartRequested += (_, _) => raised++;

        viewModel.LoadSaveCommand.Execute(savePath);

        Assert.True(viewModel.HasError);
        Assert.Contains("场景", viewModel.ErrorMessage, StringComparison.Ordinal);
        Assert.Equal(0, raised);
    }

    [Fact]
    public void LoadSave_WithValidSave_RaisesRequestWithSavePathAndHeaderSeed()
    {
        string savePath = WriteSaveFile(abiVersion: "0.2.0", seed: 777);
        var entry = Scenario("platoon-a");
        var viewModel = new MainMenuViewModel(Catalog(entry));
        GameStartRequest? request = null;
        viewModel.GameStartRequested += (_, args) => request = args.Request;

        viewModel.LoadSaveCommand.Execute(savePath);

        Assert.NotNull(request);
        Assert.Equal(entry, request!.Scenario);
        Assert.Equal((ulong)777, request.Seed);
        Assert.Equal(savePath, request.SavePath);
        Assert.False(viewModel.HasError);
    }

    private static string WriteSaveFile(string abiVersion, string scenarioId = "platoon-a", ulong seed = 42)
    {
        string headerJson =
            $"{{\"abi_version\":\"{abiVersion}\",\"scenario_id\":\"{scenarioId}\",\"scenario_name\":\"{scenarioId}\"," +
            $"\"tick\":100,\"seed\":{seed},\"threads\":4,\"schema_version\":1,\"state_hash_alg\":\"SHA-256\",\"state_size_bytes\":512}}";
        byte[] header = System.Text.Encoding.UTF8.GetBytes(headerJson);
        using var stream = new MemoryStream();
        stream.Write(System.Text.Encoding.ASCII.GetBytes("WFS-SAVE"));
        WriteUInt32LittleEndian(stream, 1);
        WriteUInt32LittleEndian(stream, (uint)header.Length);
        stream.Write(header);
        stream.Write(new byte[8]);
        stream.Write(new byte[32]);
        string path = Path.Combine(Path.GetTempPath(), $"wfs-menu-{Guid.NewGuid():N}.wfs");
        File.WriteAllBytes(path, stream.ToArray());
        return path;
    }

    private static void WriteUInt32LittleEndian(Stream stream, uint value)
    {
        stream.WriteByte((byte)(value & 0xFF));
        stream.WriteByte((byte)((value >> 8) & 0xFF));
        stream.WriteByte((byte)((value >> 16) & 0xFF));
        stream.WriteByte((byte)((value >> 24) & 0xFF));
    }
}
