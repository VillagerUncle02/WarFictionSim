// 测试替身：实现 ISimClient 的可配置假实现（各面板 ViewModel 单元测试用）。
//
// 为什么存在：ViewModel 只依赖 ISimClient 抽象，测试不需要真实 sim_core.dll；
// 假实现记录注入/步进/存档/事件查询调用，并镜像 native wfs_sim_query_events
// 的过滤语义（category/min_severity/text/unit_id/limit，text 与 unit_id 取
// 交集），让断言聚焦表现层逻辑
// 而非原生边界。

using System.Text.Json;
using WarFictionSim.Ui.EventLogPanel;
using WarFictionSim.Ui.Interop;

namespace WarFictionSim.Ui.Tests.TestDoubles;

public sealed class FakeSimClient : ISimClient
{
    public FakeSimClient(SimulationSnapshot snapshot)
    {
        Snapshot = snapshot;
        AbiVersion = SimAbiVersion.Expected;
        StateHash = new string('0', 64);
    }

    public string AbiVersion { get; set; }

    public SimulationSnapshot Snapshot { get; set; }

    public string StateHash { get; set; }

    public List<string> InjectedCommands { get; } = [];

    public int StepCount { get; private set; }

    public List<string> SavedPaths { get; } = [];

    public List<string> LoadedPaths { get; } = [];

    public List<SimEventDto> Events { get; } = [];

    public List<string> EventQueries { get; } = [];

    public Exception? NextGetSnapshotError { get; set; }

    public SimNativeException? NextInjectError { get; set; }

    public Exception? NextStepError { get; set; }

    public Exception? NextQueryEventsError { get; set; }

    public bool Disposed { get; private set; }

    public int StateHashCallCount { get; private set; }

    public SimulationSnapshot GetSnapshot()
    {
        if (NextGetSnapshotError is not null)
        {
            throw NextGetSnapshotError;
        }

        return Snapshot;
    }

    public string GetStateHash()
    {
        StateHashCallCount++;
        return StateHash;
    }

    public void Step()
    {
        if (NextStepError is not null)
        {
            throw NextStepError;
        }

        StepCount++;
    }

    public void InjectCommand(string commandJson)
    {
        if (NextInjectError is not null)
        {
            throw NextInjectError;
        }

        InjectedCommands.Add(commandJson);
    }

    public string QueryEvents(string queryJson)
    {
        EventQueries.Add(queryJson);
        if (NextQueryEventsError is not null)
        {
            throw NextQueryEventsError;
        }

        // 镜像 native：category/min_severity/text/unit_id 精确过滤（text 与
        // unit_id 同时给定取交集），limit 截断最旧前缀，count 为截断前总数。
        // 事件按 seq 升序返回（追加顺序）。
        using JsonDocument document = JsonDocument.Parse(queryJson);
        JsonElement root = document.RootElement;
        IEnumerable<SimEventDto> matches = Events;
        if (TryGetString(root, "category", out string? category))
        {
            SimEventCategory parsed = SimEventCategoryParser.Parse(category!);
            matches = matches.Where(item => item.Category == parsed);
        }

        if (TryGetString(root, "min_severity", out string? minSeverity))
        {
            SimEventSeverity parsed = SimEventSeverityParser.Parse(minSeverity!);
            matches = matches.Where(item => item.Severity >= parsed);
        }

        if (TryGetString(root, "text", out string? text) && !string.IsNullOrEmpty(text))
        {
            matches = matches.Where(item => item.Message.Contains(text!, StringComparison.Ordinal));
        }

        if (TryGetString(root, "unit_id", out string? unitId) && !string.IsNullOrEmpty(unitId))
        {
            matches = matches.Where(item => item.Message.Contains(unitId!, StringComparison.Ordinal));
        }

        List<SimEventDto> result = matches.OrderBy(item => item.Seq).ToList();
        ulong count = (ulong)result.Count;
        bool truncated = false;
        if (root.TryGetProperty("limit", out JsonElement limitElement) &&
            limitElement.ValueKind == JsonValueKind.Number &&
            limitElement.TryGetUInt64(out ulong limit) &&
            limit > 0 &&
            count > limit)
        {
            truncated = true;
            result = result.Take((int)limit).ToList();
        }

        var response = new
        {
            events = result.Select(item => new
            {
                seq = item.Seq,
                tick = item.Tick,
                category = SimEventCategoryParser.ToNativeName(item.Category),
                severity = SimEventSeverityParser.ToNativeName(item.Severity),
                message = item.Message,
            }),
            count,
            truncated,
        };
        return JsonSerializer.Serialize(response);
    }

    public void Save(string path) => SavedPaths.Add(path);

    public void LoadSave(string path) => LoadedPaths.Add(path);

    public void Dispose() => Disposed = true;

    private static bool TryGetString(JsonElement element, string name, out string? value)
    {
        if (element.TryGetProperty(name, out JsonElement property) && property.ValueKind == JsonValueKind.String)
        {
            value = property.GetString();
            return true;
        }

        value = null;
        return false;
    }
}
