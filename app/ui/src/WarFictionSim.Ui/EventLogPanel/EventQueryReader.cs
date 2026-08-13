// 文件总览：事件日志 —— wfs_sim_query_events 响应解析（FR-044 数据通道）。
//
// 为什么在事件面板侧手写解析：响应字段必须与 contracts/sim-c-api.md 严格
// 一致（events 按 seq 升序、count 截断前总数、truncated 标记），未知分类/
// 严重级名称与字段类型错误要携带定位信息显式报错（宪法第 17 条），
// 不让核心输出的脏数据静默进入面板。

using System.Text.Json;
using WarFictionSim.Ui.Interop;

namespace WarFictionSim.Ui.EventLogPanel;

/// <summary>事件查询响应（只读）。</summary>
/// <param name="Events">匹配事件（seq 升序）。</param>
/// <param name="Count">过滤后、截断前总条数。</param>
/// <param name="Truncated">是否因 limit 截断。</param>
public sealed record EventQueryResponse(IReadOnlyList<SimEventDto> Events, ulong Count, bool Truncated);

/// <summary>解析 wfs_sim_query_events 返回的只读 JSON 文本。</summary>
public static class EventQueryReader
{
    /// <summary>解析事件查询响应。</summary>
    /// <param name="json">wfs_sim_query_events 输出的 JSON 文本。</param>
    /// <returns>只读响应。</returns>
    /// <exception cref="SnapshotParseException">JSON 非法或字段缺失/类型不符。</exception>
    public static EventQueryResponse Parse(string json)
    {
        ArgumentNullException.ThrowIfNull(json);
        try
        {
            using JsonDocument document = JsonDocument.Parse(json);
            JsonElement root = document.RootElement;
            if (root.ValueKind != JsonValueKind.Object)
            {
                throw new SnapshotParseException("事件查询响应根节点必须是 JSON 对象。");
            }

            var events = new List<SimEventDto>();
            foreach (JsonElement element in RequireArray(root, "events", "$"))
            {
                events.Add(new SimEventDto(
                    RequireUInt64(element, "seq", "$.events[]"),
                    RequireUInt64(element, "tick", "$.events[]"),
                    SimEventCategoryParser.Parse(RequireString(element, "category", "$.events[]")),
                    SimEventSeverityParser.Parse(RequireString(element, "severity", "$.events[]")),
                    RequireString(element, "message", "$.events[]")));
            }

            return new EventQueryResponse(events, RequireUInt64(root, "count", "$"), RequireBoolean(root, "truncated", "$"));
        }
        catch (SnapshotParseException)
        {
            throw;
        }
        catch (JsonException exception)
        {
            throw new SnapshotParseException($"事件查询响应不是合法 JSON：{exception.Message}", exception);
        }
        catch (ArgumentException exception)
        {
            throw new SnapshotParseException($"事件查询响应字段非法：{exception.Message}", exception);
        }
    }

    private static List<JsonElement> RequireArray(JsonElement element, string name, string path)
    {
        if (!element.TryGetProperty(name, out JsonElement value) || value.ValueKind != JsonValueKind.Array)
        {
            throw new SnapshotParseException($"事件查询响应解析失败（{path}.{name}）：缺少数组字段。");
        }

        var items = new List<JsonElement>();
        foreach (JsonElement item in value.EnumerateArray())
        {
            items.Add(item);
        }

        return items;
    }

    private static string RequireString(JsonElement element, string name, string path)
    {
        if (!element.TryGetProperty(name, out JsonElement value) || value.ValueKind != JsonValueKind.String)
        {
            throw new SnapshotParseException($"事件查询响应解析失败（{path}.{name}）：缺少字符串字段。");
        }

        return value.GetString() ?? throw new SnapshotParseException($"事件查询响应解析失败（{path}.{name}）：字符串为空。");
    }

    private static ulong RequireUInt64(JsonElement element, string name, string path)
    {
        if (!element.TryGetProperty(name, out JsonElement value) || value.ValueKind != JsonValueKind.Number ||
            !value.TryGetUInt64(out ulong result))
        {
            throw new SnapshotParseException($"事件查询响应解析失败（{path}.{name}）：缺少非负整数字段。");
        }

        return result;
    }

    private static bool RequireBoolean(JsonElement element, string name, string path)
    {
        if (!element.TryGetProperty(name, out JsonElement value) ||
            (value.ValueKind != JsonValueKind.True && value.ValueKind != JsonValueKind.False))
        {
            throw new SnapshotParseException($"事件查询响应解析失败（{path}.{name}）：缺少布尔字段。");
        }

        return value.GetBoolean();
    }
}
