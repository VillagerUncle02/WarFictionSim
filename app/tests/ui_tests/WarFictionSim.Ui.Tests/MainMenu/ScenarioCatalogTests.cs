// 测试：主菜单 —— 场景目录加载与规模/节点派生（T039）。
//
// 场景 JSON 是数据驱动内容（宪法第 12 条）：目录必须把可玩场景解析为
// 强类型条目，非法文件明确记录问题（宪法第 17 条）而不是静默跳过。
// 规模采用可选扩展字段 scale（platoon/battalion），缺失时默认连排级，
// 与场景 Schema 的 additionalProperties 兼容（不改动 data/）。

using WarFictionSim.Ui.MainMenu;
using Xunit;

namespace WarFictionSim.Ui.Tests.MainMenu;

public class ScenarioCatalogTests
{
    private const string TutorialJson = """
        {
          "schema_version": 1,
          "id": "scn-tutorial-platoon",
          "name": "Tutorial: Platoon Attack",
          "player_node_id": "node-tutorial-platoon",
          "map": { "width_km": 1.5, "height_km": 1.5 },
          "tick_hz": 20,
          "seed": 20260812,
          "tutorial": true,
          "save_slot": "tutorial-scn-tutorial-platoon",
          "time_limit_ticks": 18000,
          "zones": [ { "id": "zone-objective-hill" }, { "id": "zone-start" } ],
          "units": [
            { "id": "tutorial-squad-1", "type": "squad-rifle-us", "node_id": "node-tutorial-platoon", "x": 0.2, "y": 0.2, "ammo": ["ammo-556"] },
            { "id": "tutorial-enemy-1", "type": "squad-rifle-opposition", "node_id": "node-tutorial-enemy", "x": 1.1, "y": 1.1, "ammo": ["ammo-762"] }
          ],
          "objectives": [ { "id": "obj-1", "kind": "zone", "target_ref": "zone-objective-hill", "duration_ticks": 2400 } ]
        }
        """;

    [Fact]
    public void Load_ParsesScenarioEntry()
    {
        using var directory = new TempDirectory();
        File.WriteAllText(Path.Combine(directory.Path, "tutorial.json"), TutorialJson);

        ScenarioCatalog catalog = ScenarioCatalog.Load(directory.Path);

        ScenarioCatalogEntry entry = Assert.Single(catalog.Entries);
        Assert.Equal("scn-tutorial-platoon", entry.Id);
        Assert.Equal("Tutorial: Platoon Attack", entry.Name);
        Assert.Equal("node-tutorial-platoon", entry.PlayerNodeId);
        Assert.True(entry.IsTutorial);
        Assert.Equal("tutorial-scn-tutorial-platoon", entry.SaveSlot);
        Assert.Equal(CombatScale.Platoon, entry.Scale);
        Assert.Equal((ulong)20260812, entry.Seed);
        Assert.Equal(20u, entry.TickHz);
        Assert.Equal(1.5, entry.MapWidthKm);
        Assert.Equal(["zone-objective-hill", "zone-start"], entry.Zones);
        Assert.Equal(["node-tutorial-platoon"], entry.CommandNodeIds);
        Assert.Empty(catalog.LoadIssues);
    }

    [Fact]
    public void Load_DerivesScaleFromExplicitField()
    {
        using var directory = new TempDirectory();
        string json = TutorialJson.Replace("\"tutorial\": true,", "\"tutorial\": false,\n  \"scale\": \"battalion\",", StringComparison.Ordinal);
        File.WriteAllText(Path.Combine(directory.Path, "battalion.json"), json);

        ScenarioCatalog catalog = ScenarioCatalog.Load(directory.Path);

        Assert.Equal(CombatScale.Battalion, Assert.Single(catalog.Entries).Scale);
    }

    [Fact]
    public void Load_WithUnknownScale_DefaultsToPlatoonAndRecordsIssue()
    {
        using var directory = new TempDirectory();
        string json = TutorialJson.Replace("\"tutorial\": true,", "\"tutorial\": false,\n  \"scale\": \"brigade\",", StringComparison.Ordinal);
        File.WriteAllText(Path.Combine(directory.Path, "brigade.json"), json);

        ScenarioCatalog catalog = ScenarioCatalog.Load(directory.Path);

        Assert.Equal(CombatScale.Platoon, Assert.Single(catalog.Entries).Scale);
        Assert.Contains(catalog.LoadIssues, issue => issue.Contains("brigade", StringComparison.Ordinal));
    }

    [Fact]
    public void Load_WithInvalidJson_RecordsIssueAndSkipsEntry()
    {
        using var directory = new TempDirectory();
        File.WriteAllText(Path.Combine(directory.Path, "broken.json"), "{ 不是 JSON");
        File.WriteAllText(Path.Combine(directory.Path, "tutorial.json"), TutorialJson);

        ScenarioCatalog catalog = ScenarioCatalog.Load(directory.Path);

        Assert.Single(catalog.Entries);
        Assert.Contains(catalog.LoadIssues, issue => issue.Contains("broken.json", StringComparison.Ordinal));
    }

    [Fact]
    public void FindById_ReturnsEntryOrNull()
    {
        using var directory = new TempDirectory();
        File.WriteAllText(Path.Combine(directory.Path, "tutorial.json"), TutorialJson);
        ScenarioCatalog catalog = ScenarioCatalog.Load(directory.Path);

        Assert.NotNull(catalog.FindById("scn-tutorial-platoon"));
        Assert.Null(catalog.FindById("missing"));
    }

    private sealed class TempDirectory : IDisposable
    {
        public TempDirectory() => Path = System.IO.Directory.CreateTempSubdirectory("wfs-scenario-catalog-").FullName;

        public string Path { get; }

        public void Dispose()
        {
            if (Directory.Exists(Path))
            {
                Directory.Delete(Path, recursive: true);
            }
        }
    }
}
