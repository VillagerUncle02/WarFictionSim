// 测试：主菜单 —— 场景目录加载与规模/节点派生（T039）。
//
// 场景 JSON 是数据驱动内容（宪法第 12 条）：目录必须把可玩场景解析为
// 强类型条目，非法文件明确记录问题（宪法第 17 条）而不是静默跳过。
// 规模采用可选扩展字段 scale（platoon/battalion），缺失时默认连排级，
// 与场景 Schema 的 additionalProperties 兼容（不改动 data/）。

using WarFictionSim.Ui.MainMenu;
using WarFictionSim.Ui.SupportPanel;
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

    private const string FactionJson = """
        {
          "schema_version": 1,
          "id": "faction-china",
          "name": "中国",
          "approval_level": 0,
          "command_style": { "org_style": "builtin_combined" },
          "resource_pools": {
            "battalion": {
              "support_score": 60,
              "support_kinds": [ "artillery-152" ],
              "entries": [
                { "id": "squad-mortar-team", "kind": "unit", "quantity": 2, "cost": 30 },
                { "id": "artillery-152", "kind": "fire_support", "quantity": 1, "cost": 50 }
              ]
            }
          }
        }
        """;

    private const string SupportScenarioJson = """
        {
          "schema_version": 1,
          "id": "scn-support-platoon",
          "name": "Support Chain: Platoon Limited Score",
          "player_node_id": "node-platoon-1",
          "map": { "width_km": 1.5, "height_km": 1.5 },
          "tick_hz": 20,
          "seed": 20260813,
          "time_limit_ticks": 18000,
          "zones": [ { "id": "zone-support-obj" } ],
          "units": [
            { "id": "sp-squad-1", "type": "squad-rifle-us", "node_id": "node-platoon-1", "x": 0.3, "y": 0.3, "ammo": [ "ammo-556" ] }
          ],
          "support": {
            "scale": "platoon",
            "faction_id": "faction-china",
            "player_node_id": "node-platoon-1",
            "superior_node_id": "node-battalion-1",
            "evaluation_delay_ticks": 20,
            "return_delay_ticks": 30
          }
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
    public void Load_WithSupportConfig_LoadsFactionPoolKinds()
    {
        using var root = new TempDirectory();
        string scenariosDir = Path.Combine(root.Path, "scenarios");
        string factionsDir = Path.Combine(root.Path, "factions");
        Directory.CreateDirectory(scenariosDir);
        Directory.CreateDirectory(factionsDir);
        File.WriteAllText(Path.Combine(scenariosDir, "support.json"), SupportScenarioJson);
        File.WriteAllText(Path.Combine(factionsDir, "faction-china.json"), FactionJson);

        ScenarioCatalog catalog = ScenarioCatalog.Load(scenariosDir);

        ScenarioCatalogEntry entry = Assert.Single(catalog.Entries);
        Assert.True(entry.SupportConfigured);
        Assert.Equal("platoon", entry.SupportScale);
        Assert.Equal("node-battalion-1", entry.SuperiorNodeId);
        Assert.Equal(["squad-mortar-team", "artillery-152"], entry.SupportKinds.Select(kind => kind.Id));
        Assert.Equal((ulong)30, entry.SupportKinds[0].Cost);
        Assert.Equal("unit", entry.SupportKinds[0].Kind);
        Assert.Empty(catalog.LoadIssues);
    }

    [Fact]
    public void Load_WithoutSupportConfig_DefaultsToUnconfigured()
    {
        using var directory = new TempDirectory();
        File.WriteAllText(Path.Combine(directory.Path, "tutorial.json"), TutorialJson);

        ScenarioCatalogEntry entry = Assert.Single(ScenarioCatalog.Load(directory.Path).Entries);

        Assert.False(entry.SupportConfigured);
        Assert.Equal(string.Empty, entry.SuperiorNodeId);
        Assert.Empty(entry.SupportKinds);
    }

    [Fact]
    public void Load_WithSupportConfigButMissingFaction_RecordsIssue()
    {
        using var directory = new TempDirectory();
        File.WriteAllText(Path.Combine(directory.Path, "support.json"), SupportScenarioJson);

        ScenarioCatalog catalog = ScenarioCatalog.Load(directory.Path);

        ScenarioCatalogEntry entry = Assert.Single(catalog.Entries);
        Assert.True(entry.SupportConfigured);
        Assert.Empty(entry.SupportKinds);
        Assert.Contains(catalog.LoadIssues, issue => issue.Contains("faction", StringComparison.OrdinalIgnoreCase));
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
