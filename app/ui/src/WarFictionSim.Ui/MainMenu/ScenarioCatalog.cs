// 文件总览：主菜单 —— 场景目录（T039）。
//
// 为什么轻量手写解析：目录只需要场景的静态元数据（id/名称/规模/节点/区域/
// 地图尺寸），完整加载与校验仍由核心 wfs_sim_create 负责（核心是最终权威）；
// 非法文件记录结构化 issue 供主菜单内联提示，绝不静默跳过（宪法第 12/17 条）。

using System.IO;
using System.Text.Json;
using WarFictionSim.Ui.SupportPanel;

namespace WarFictionSim.Ui.MainMenu;

/// <summary>场景目录抽象（主菜单与测试共享）。</summary>
public interface IScenarioCatalog
{
    /// <summary>目录内全部可玩场景。</summary>
    IReadOnlyList<ScenarioCatalogEntry> Entries { get; }

    /// <summary>加载过程中的数据问题（供 UI 展示）。</summary>
    IReadOnlyList<string> LoadIssues { get; }

    /// <summary>按场景 id 查找条目。</summary>
    /// <param name="id">场景 id。</param>
    /// <returns>条目；不存在返回 <see langword="null"/>。</returns>
    ScenarioCatalogEntry? FindById(string id);
}

/// <summary>从 data/scenarios 目录构建的场景目录。</summary>
public sealed class ScenarioCatalog : IScenarioCatalog
{
    private readonly Dictionary<string, ScenarioCatalogEntry> _byId;

    /// <summary>构造目录（供测试注入已解析条目）。</summary>
    /// <param name="entries">场景条目。</param>
    /// <param name="loadIssues">加载问题列表。</param>
    public ScenarioCatalog(IReadOnlyList<ScenarioCatalogEntry> entries, IReadOnlyList<string> loadIssues)
    {
        Entries = entries;
        LoadIssues = loadIssues;
        _byId = entries.ToDictionary(entry => entry.Id, StringComparer.Ordinal);
    }

    /// <inheritdoc />
    public IReadOnlyList<ScenarioCatalogEntry> Entries { get; }

    /// <inheritdoc />
    public IReadOnlyList<string> LoadIssues { get; }

    /// <inheritdoc />
    public ScenarioCatalogEntry? FindById(string id) =>
        _byId.TryGetValue(id, out ScenarioCatalogEntry? entry) ? entry : null;

    /// <summary>加载目录中全部 *.json 场景文件。</summary>
    /// <param name="scenariosDirectory">data/scenarios 目录。</param>
    /// <returns>目录（含问题清单）。</returns>
    public static ScenarioCatalog Load(string scenariosDirectory)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(scenariosDirectory);

        var entries = new List<ScenarioCatalogEntry>();
        var issues = new List<string>();
        if (!Directory.Exists(scenariosDirectory))
        {
            issues.Add($"场景目录不存在：{scenariosDirectory}");
            return new ScenarioCatalog(entries, issues);
        }

        // 固定文件名排序：目录构建结果不依赖文件系统枚举顺序（确定性）。
        foreach (string file in Directory.EnumerateFiles(scenariosDirectory, "*.json").OrderBy(path => path, StringComparer.Ordinal))
        {
            if (TryParseEntry(file, out ScenarioCatalogEntry? entry, out string? warning, out string? fatalIssue))
            {
                entries.Add(entry!);
                if (warning is not null)
                {
                    issues.Add($"{Path.GetFileName(file)}：{warning}");
                }
            }
            else
            {
                issues.Add($"{Path.GetFileName(file)}：{fatalIssue}");
            }
        }

        return new ScenarioCatalog(entries, issues);
    }

    private static bool TryParseEntry(
        string path, out ScenarioCatalogEntry? entry, out string? warning, out string? fatalIssue)
    {
        entry = null;
        warning = null;
        fatalIssue = null;
        try
        {
            using JsonDocument document = JsonDocument.Parse(File.ReadAllBytes(path));
            JsonElement root = document.RootElement;
            string id = RequireString(root, "id");
            string name = RequireString(root, "name");
            string playerNodeId = OptionalString(root, "player_node_id");
            JsonElement map = RequireObject(root, "map");
            double width = RequireDouble(map, "width_km");
            double height = RequireDouble(map, "height_km");
            uint tickHz = OptionalUInt32(root, "tick_hz", 20);
            ulong seed = OptionalUInt64(root, "seed", 0);
            bool tutorial = OptionalBoolean(root, "tutorial", false);
            string? saveSlot = OptionalNullableString(root, "save_slot");

            var zones = new List<string>();
            if (root.TryGetProperty("zones", out JsonElement zonesElement))
            {
                foreach (JsonElement zone in zonesElement.EnumerateArray())
                {
                    zones.Add(RequireString(zone, "id"));
                }
            }

            List<string> nodeIds = DeriveCommandNodes(root, playerNodeId);
            SupportCatalogMetadata support = ReadSupportMetadata(root, path);

            if (!CombatScaleText.TryParse(OptionalNullableString(root, "scale"), out CombatScale scale))
            {
                // 未知规模不是致命错误：条目仍可玩（按连排级），但必须显式
                // 提示数据问题（宪法第 17 条不静默）。
                warning = $"未知作战规模 '{OptionalNullableString(root, "scale")}'，已按连排级处理";
            }

            if (support.Issue is not null)
            {
                warning = warning is null
                    ? $"{Path.GetFileName(path)}：{support.Issue}"
                    : $"{warning}；{Path.GetFileName(path)}：{support.Issue}";
            }

            entry = new ScenarioCatalogEntry
            {
                Id = id,
                Name = name,
                Path = path,
                PlayerNodeId = playerNodeId,
                IsTutorial = tutorial,
                SaveSlot = saveSlot,
                Scale = scale,
                Seed = seed,
                TickHz = tickHz,
                MapWidthKm = width,
                MapHeightKm = height,
                Zones = zones,
                CommandNodeIds = nodeIds,
                SupportConfigured = support.Configured,
                SupportScale = support.Scale,
                SuperiorNodeId = support.SuperiorNodeId,
                SupportKinds = support.Kinds,
            };
            return true;
        }
        catch (Exception exception) when (exception is JsonException or InvalidOperationException or KeyNotFoundException)
        {
            fatalIssue = $"场景数据非法：{exception.Message}";
            return false;
        }
    }

    // 己方阵营 = player_node_id 对应单位的 side（缺省回退 node_id，与核心
    // friendly_side 规则一致）；可扮演节点 = 己方阵营单位的去重 node_id。
    private static List<string> DeriveCommandNodes(JsonElement root, string playerNodeId)
    {
        string friendlySide = playerNodeId;
        var nodeIds = new List<string>();
        if (!root.TryGetProperty("units", out JsonElement unitsElement) || unitsElement.ValueKind != JsonValueKind.Array)
        {
            return nodeIds;
        }

        foreach (JsonElement unit in unitsElement.EnumerateArray())
        {
            string nodeId = RequireString(unit, "node_id");
            string side = OptionalString(unit, "side");
            if (string.IsNullOrEmpty(side))
            {
                side = nodeId;
            }

            if (nodeId == playerNodeId)
            {
                friendlySide = side;
            }
        }

        foreach (JsonElement unit in unitsElement.EnumerateArray())
        {
            string nodeId = RequireString(unit, "node_id");
            string side = OptionalString(unit, "side");
            if (string.IsNullOrEmpty(side))
            {
                side = nodeId;
            }

            if (side == friendlySide && !nodeIds.Contains(nodeId))
            {
                nodeIds.Add(nodeId);
            }
        }

        return nodeIds;
    }

    private static string RequireString(JsonElement element, string name)
    {
        if (!element.TryGetProperty(name, out JsonElement value) || value.ValueKind != JsonValueKind.String)
        {
            throw new KeyNotFoundException($"缺少字符串字段 {name}");
        }

        return value.GetString() ?? throw new KeyNotFoundException($"字段 {name} 为空");
    }

    private static string OptionalString(JsonElement element, string name) =>
        element.TryGetProperty(name, out JsonElement value) && value.ValueKind == JsonValueKind.String
            ? value.GetString() ?? string.Empty
            : string.Empty;

    private static string? OptionalNullableString(JsonElement element, string name) =>
        element.TryGetProperty(name, out JsonElement value) && value.ValueKind == JsonValueKind.String
            ? value.GetString()
            : null;

    private static JsonElement RequireObject(JsonElement element, string name)
    {
        if (!element.TryGetProperty(name, out JsonElement value) || value.ValueKind != JsonValueKind.Object)
        {
            throw new KeyNotFoundException($"缺少对象字段 {name}");
        }

        return value;
    }

    private static double RequireDouble(JsonElement element, string name)
    {
        if (!element.TryGetProperty(name, out JsonElement value) || !value.TryGetDouble(out double result))
        {
            throw new KeyNotFoundException($"缺少数值字段 {name}");
        }

        return result;
    }

    private static uint OptionalUInt32(JsonElement element, string name, uint fallback) =>
        element.TryGetProperty(name, out JsonElement value) && value.TryGetUInt32(out uint result) ? result : fallback;

    private static ulong OptionalUInt64(JsonElement element, string name, ulong fallback) =>
        element.TryGetProperty(name, out JsonElement value) && value.TryGetUInt64(out ulong result) ? result : fallback;

    private static bool OptionalBoolean(JsonElement element, string name, bool fallback) =>
        element.TryGetProperty(name, out JsonElement value) &&
        (value.ValueKind == JsonValueKind.True || value.ValueKind == JsonValueKind.False)
            ? value.GetBoolean()
            : fallback;

    private sealed record SupportCatalogMetadata(
        bool Configured,
        string Scale,
        string SuperiorNodeId,
        IReadOnlyList<SupportKindOption> Kinds,
        string? Issue);

    // 支援静态元数据投影（T053）：support 块 + 派系资源池。可用种类不在
    // 快照内（FR-008：由请求方所属营级编制资源池决定），这里在目录加载时
    // 从 data/factions 投影一次；派系文件缺失/解析失败记录问题（宪法第 17
    // 条不静默），面板以空池给出"池缺失"提示，最终权威仍是核心校验。
    private static SupportCatalogMetadata ReadSupportMetadata(JsonElement root, string scenarioPath)
    {
        if (!root.TryGetProperty("support", out JsonElement supportElement) ||
            supportElement.ValueKind != JsonValueKind.Object)
        {
            return new SupportCatalogMetadata(false, string.Empty, string.Empty, [], null);
        }

        string scale = OptionalString(supportElement, "scale");
        string factionId = OptionalString(supportElement, "faction_id");
        string superiorNodeId = OptionalString(supportElement, "superior_node_id");
        string poolEchelon = OptionalString(supportElement, "pool_echelon");
        if (string.IsNullOrWhiteSpace(poolEchelon))
        {
            poolEchelon = "battalion"; // 与 native initialize_support_state 缺省一致。
        }

        if (string.IsNullOrWhiteSpace(factionId))
        {
            return new SupportCatalogMetadata(true, scale, superiorNodeId, [], "支援配置缺少 faction_id，可用种类池为空");
        }

        string factionPath = Path.GetFullPath(Path.Combine(
            Path.GetDirectoryName(scenarioPath) ?? ".",
            "..",
            "factions",
            $"{factionId}.json"));
        if (!File.Exists(factionPath))
        {
            return new SupportCatalogMetadata(
                true, scale, superiorNodeId, [], $"支援派系模板缺失：{factionPath}（可用种类池为空）");
        }

        try
        {
            using JsonDocument faction = JsonDocument.Parse(File.ReadAllBytes(factionPath));
            JsonElement pools = RequireObject(faction.RootElement, "resource_pools");
            JsonElement pool = RequireObject(pools, poolEchelon);
            var kinds = new List<SupportKindOption>();
            foreach (JsonElement entry in RequireArray(pool, "entries"))
            {
                kinds.Add(new SupportKindOption(
                    RequireString(entry, "id"),
                    RequireUInt64(entry, "cost"),
                    OptionalString(entry, "kind")));
            }

            return new SupportCatalogMetadata(true, scale, superiorNodeId, kinds, null);
        }
        catch (Exception exception) when (exception is JsonException or InvalidOperationException or KeyNotFoundException)
        {
            return new SupportCatalogMetadata(
                true, scale, superiorNodeId, [], $"支援派系资源池解析失败：{exception.Message}");
        }
    }

    private static List<JsonElement> RequireArray(JsonElement element, string name)
    {
        if (!element.TryGetProperty(name, out JsonElement value) || value.ValueKind != JsonValueKind.Array)
        {
            throw new KeyNotFoundException($"缺少数组字段 {name}");
        }

        var items = new List<JsonElement>();
        foreach (JsonElement item in value.EnumerateArray())
        {
            items.Add(item);
        }

        return items;
    }

    private static ulong RequireUInt64(JsonElement element, string name)
    {
        if (!element.TryGetProperty(name, out JsonElement value) || value.ValueKind != JsonValueKind.Number ||
            !value.TryGetUInt64(out ulong result))
        {
            throw new KeyNotFoundException($"缺少非负整数字段 {name}");
        }

        return result;
    }
}
